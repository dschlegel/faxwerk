// Command line front end.
//
//   faxwerk send --to <number> --pdf <file> [--pdf ...] [options]
//   faxwerk register-test [options]
//   faxwerk render --pdf <file> --out <file.tif> [--dither]

#include "ExitCodes.h"
#include "ProductInfo.h"
#include "Terminal.h"

#include "FaxJob.h"
#include "config/PasswordStore.h"
#include "document/FaxTiff.h"
#include "document/PdfRasterizer.h"
#include "sip/SipUserAgent.h"

#include <QCommandLineParser>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>
#include <memory>

using namespace faxwerk;
using namespace faxwerk::cli;

namespace {

void printErr(const QString &text)
{
    std::fprintf(stderr, "%s\n", text.toLocal8Bit().constData());
    std::fflush(stderr);
}

void printOut(const QString &text)
{
    std::fprintf(stdout, "%s\n", text.toLocal8Bit().constData());
    std::fflush(stdout);
}

QString timestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
}

QString usageText()
{
    return QStringLiteral(
               "Usage: %1 <command> [options]\n"
               "\n"
               "Commands:\n"
               "  send           Send PDF documents as fax\n"
               "  register-test  Register at the SIP server, report the result and unregister\n"
               "  render         Rasterize PDFs into a fax TIFF (debugging)\n"
               "\n"
               "Run '%1 <command> --help' for the options of a command.")
        .arg(QCoreApplication::applicationName().toLower());
}

// --- Options shared by the SIP commands ---------------------------------------

struct SipOptionSet
{
    QCommandLineOption config{QStringList{QStringLiteral("config")},
                              QStringLiteral("INI configuration file."), QStringLiteral("file")};
    QCommandLineOption server{QStringList{QStringLiteral("server")},
                              QStringLiteral("SIP registrar (default: fritz.box)."), QStringLiteral("host")};
    QCommandLineOption port{QStringList{QStringLiteral("port")},
                            QStringLiteral("SIP server port (default: 5060)."), QStringLiteral("port")};
    QCommandLineOption domain{QStringList{QStringLiteral("domain")},
                              QStringLiteral("SIP domain (default: server)."), QStringLiteral("domain")};
    QCommandLineOption user{QStringList{QStringLiteral("user")},
                            QStringLiteral("SIP user name of the IP phone."), QStringLiteral("name")};
    QCommandLineOption authUser{QStringList{QStringLiteral("auth-user")},
                                QStringLiteral("Digest user name if different from --user."), QStringLiteral("name")};
    QCommandLineOption password{QStringList{QStringLiteral("password")},
                                QStringLiteral("SIP password (prefer the FAXWERK_PASSWORD environment variable)."),
                                QStringLiteral("secret")};
    QCommandLineOption localPort{QStringList{QStringLiteral("local-port")},
                                 QStringLiteral("Local SIP UDP port (default: any)."), QStringLiteral("port")};
    QCommandLineOption sipTrace{QStringList{QStringLiteral("sip-trace")},
                                QStringLiteral("Append all SIP datagrams to this file."), QStringLiteral("file")};
    QCommandLineOption verbose{QStringList{QStringLiteral("verbose")},
                               QStringLiteral("Print protocol details (SIP and T.30) to stderr.")};

    void addTo(QCommandLineParser &parser)
    {
        parser.addOptions({config, server, port, domain, user, authUser, password, localPort, sipTrace, verbose});
    }
};

std::unique_ptr<QSettings> openSettings(const QCommandLineParser &parser, const SipOptionSet &opts)
{
    if (parser.isSet(opts.config))
        return std::make_unique<QSettings>(parser.value(opts.config), QSettings::IniFormat);
    return std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope,
                                       QStringLiteral(FAXWERK_PRODUCT_NAME), QStringLiteral("faxwerk"));
}

// The password the desktop app keeps in the keychain for the location that
// [sip] mirrors.
QString keychainPassword(QSettings &ini)
{
    const QString location = ini.value(QStringLiteral("sip/location")).toString();
    const std::unique_ptr<PasswordStore> store = PasswordStore::createSystem();
    if (location.isEmpty() || !store)
        return QString();
    QString password;
    QString error;
    const PasswordStore::Result result = store->read(PasswordStore::sipKey(location), &password, &error);
    if (result == PasswordStore::Result::Failed || result == PasswordStore::Result::Unavailable)
        printErr(QStringLiteral("warning: cannot read the password from the keychain: %1").arg(error));
    return password;
}

// Command line > environment > INI file > keychain > defaults.
bool buildAccount(const QCommandLineParser &parser, const SipOptionSet &opts, QSettings &ini,
                  SipAccount *account, QString *error)
{
    auto pick = [&](const QCommandLineOption &option, const QString &key, const QString &fallback) {
        if (parser.isSet(option))
            return parser.value(option);
        return ini.value(key, fallback).toString();
    };
    auto pickPort = [&](const QCommandLineOption &option, const QString &key, quint16 fallback, bool *ok) {
        const QString text = pick(option, key, QString::number(fallback));
        const uint value = text.toUInt(ok);
        *ok = *ok && value <= 65535;
        return quint16(value);
    };

    account->server = pick(opts.server, QStringLiteral("sip/server"), QStringLiteral("fritz.box"));
    account->domain = pick(opts.domain, QStringLiteral("sip/domain"), QString());
    account->user = pick(opts.user, QStringLiteral("sip/user"), QString());
    account->authUser = pick(opts.authUser, QStringLiteral("sip/auth_user"), QString());
    account->displayName = ini.value(QStringLiteral("sip/display_name")).toString();
    account->registerExpires = ini.value(QStringLiteral("sip/expires"), 600).toInt();
    account->userAgent = QStringLiteral("%1/%2").arg(QStringLiteral(FAXWERK_PRODUCT_NAME), QStringLiteral(FAXWERK_VERSION));

    bool ok = false;
    account->serverPort = pickPort(opts.port, QStringLiteral("sip/port"), 5060, &ok);
    if (!ok || account->serverPort == 0) {
        *error = QStringLiteral("invalid server port");
        return false;
    }
    account->localPort = pickPort(opts.localPort, QStringLiteral("sip/local_port"), 0, &ok);
    if (!ok) {
        *error = QStringLiteral("invalid local port");
        return false;
    }

    if (parser.isSet(opts.password))
        account->password = parser.value(opts.password);
    else if (qEnvironmentVariableIsSet("FAXWERK_PASSWORD"))
        account->password = qEnvironmentVariable("FAXWERK_PASSWORD");
    else
        account->password = ini.value(QStringLiteral("sip/password")).toString();
    if (account->password.isEmpty())
        account->password = keychainPassword(ini);

    if (account->user.isEmpty()) {
        *error = QStringLiteral("no SIP user name given (--user or [sip] user=)");
        return false;
    }
    if (account->password.isEmpty() && stdinIsTerminal())
        account->password = readPassword(QStringLiteral("SIP password for %1: ").arg(account->user));
    if (account->password.isEmpty()) {
        *error = QStringLiteral("no SIP password given (FAXWERK_PASSWORD, --password, [sip] password= or the keychain)");
        return false;
    }
    return true;
}

// Writes SIP datagrams to a trace file. Passwords never appear on the wire
// (only digest hashes), so the trace can be shared for debugging.
class TraceWriter
{
public:
    bool open(const QString &path, QString *error)
    {
        m_file.setFileName(path);
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            *error = QStringLiteral("cannot open trace file %1: %2").arg(path, m_file.errorString());
            return false;
        }
        return true;
    }

    void write(bool outgoing, const QString &peer, const QByteArray &data)
    {
        if (!m_file.isOpen())
            return;
        const QString head = QStringLiteral("---- %1 %2 %3 (%4 bytes)\n")
                                 .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                      outgoing ? QStringLiteral("SEND to") : QStringLiteral("RECV from"), peer)
                                 .arg(data.size());
        m_file.write(head.toUtf8());
        QByteArray body = data;
        body.replace("\r\n", "\n");
        m_file.write(body);
        if (!body.endsWith('\n'))
            m_file.write("\n");
        m_file.flush();
    }

private:
    QFile m_file;
};

// Polls the Ctrl+C flag from the event loop.
void watchInterrupt(QObject *context, const std::function<void()> &onInterrupt)
{
    installInterruptHandler();
    auto *timer = new QTimer(context);
    QObject::connect(timer, &QTimer::timeout, context, [timer, onInterrupt] {
        if (interruptRequested()) {
            timer->stop();
            onInterrupt();
        }
    });
    timer->start(100);
}

// --- send -------------------------------------------------------------------

int runSend(const QStringList &arguments)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Send PDF documents as fax."));
    parser.addHelpOption();
    SipOptionSet sipOpts;
    const QCommandLineOption to{QStringList{QStringLiteral("to")}, QStringLiteral("Destination fax number."),
                                QStringLiteral("number")};
    const QCommandLineOption pdf{QStringList{QStringLiteral("pdf")},
                                 QStringLiteral("PDF document (repeat for several documents)."), QStringLiteral("file")};
    const QCommandLineOption ident{QStringList{QStringLiteral("ident")},
                                   QStringLiteral("Station identifier (TSI), max. 20 characters."), QStringLiteral("id")};
    const QCommandLineOption header{QStringList{QStringLiteral("header")},
                                    QStringLiteral("Page header text, max. 50 characters."), QStringLiteral("text")};
    const QCommandLineOption maxRate{QStringList{QStringLiteral("max-rate")},
                                     QStringLiteral("Maximum bit rate: 9600 (V.29, default) or 14400 (V.17)."),
                                     QStringLiteral("bps")};
    const QCommandLineOption noEcm{QStringList{QStringLiteral("no-ecm")},
                                   QStringLiteral("Disable error correction mode.")};
    const QCommandLineOption dither{QStringList{QStringLiteral("dither")},
                                    QStringLiteral("Floyd-Steinberg dithering instead of a threshold (photos).")};
    const QCommandLineOption timeout{QStringList{QStringLiteral("timeout")},
                                     QStringLiteral("Overall timeout in seconds (default: 900)."),
                                     QStringLiteral("seconds")};
    sipOpts.addTo(parser);
    parser.addOptions({to, pdf, ident, header, maxRate, noEcm, dither, timeout});
    parser.process(arguments);

    auto ini = openSettings(parser, sipOpts);
    FaxJobConfig config;
    config.productName = QStringLiteral(FAXWERK_PRODUCT_NAME);
    QString error;
    if (!buildAccount(parser, sipOpts, *ini, &config.sip.account, &error)) {
        printErr(QStringLiteral("error: %1").arg(error));
        return kExitUsage;
    }

    config.destination = parser.value(to).trimmed();
    config.pdfFiles = parser.values(pdf);
    if (config.destination.isEmpty() || config.pdfFiles.isEmpty()) {
        printErr(QStringLiteral("error: --to and at least one --pdf are required"));
        return kExitUsage;
    }
    for (const QString &file : std::as_const(config.pdfFiles)) {
        if (!QFileInfo(file).isReadable()) {
            printErr(QStringLiteral("error: cannot read %1").arg(file));
            return kExitDocument;
        }
    }

    config.fax.stationId = parser.isSet(ident) ? parser.value(ident) : ini->value(QStringLiteral("fax/ident")).toString();
    config.fax.headerInfo = parser.isSet(header) ? parser.value(header) : ini->value(QStringLiteral("fax/header")).toString();
    if (config.fax.stationId.size() > 20 || config.fax.headerInfo.size() > 50) {
        printErr(QStringLiteral("error: --ident is limited to 20 and --header to 50 characters"));
        return kExitUsage;
    }
    const QString rate = parser.isSet(maxRate) ? parser.value(maxRate)
                                               : ini->value(QStringLiteral("fax/max_rate"), 9600).toString();
    if (rate != QLatin1String("9600") && rate != QLatin1String("14400")) {
        printErr(QStringLiteral("error: --max-rate must be 9600 or 14400"));
        return kExitUsage;
    }
    config.fax.maxBitRate = rate.toInt();
    config.fax.ecm = parser.isSet(noEcm) ? false : ini->value(QStringLiteral("fax/ecm"), true).toBool();
    const bool verbose = parser.isSet(sipOpts.verbose);
    config.fax.protocolLog = verbose;
    config.raster.mode = (parser.isSet(dither) || ini->value(QStringLiteral("fax/dither"), false).toBool())
                             ? PdfRasterizer::Mode::Dither
                             : PdfRasterizer::Mode::Threshold;
    bool ok = true;
    config.timeoutSeconds = parser.isSet(timeout) ? parser.value(timeout).toInt(&ok)
                                                  : ini->value(QStringLiteral("job/timeout"), 900).toInt(&ok);
    if (!ok || config.timeoutSeconds <= 0) {
        printErr(QStringLiteral("error: invalid --timeout"));
        return kExitUsage;
    }

    TraceWriter trace;
    if (parser.isSet(sipOpts.sipTrace) && !trace.open(parser.value(sipOpts.sipTrace), &error)) {
        printErr(QStringLiteral("error: %1").arg(error));
        return kExitUsage;
    }

    FaxJob job(config);
    QObject::connect(&job, &FaxJob::statusChanged, [](const QString &status) {
        printErr(QStringLiteral("[%1] %2").arg(timestamp(), status));
    });
    QObject::connect(&job, &FaxJob::pageTransferred, [](int pages, int total) {
        printErr(QStringLiteral("[%1] Page %2 of %3 transferred").arg(timestamp()).arg(pages).arg(total));
    });
    if (verbose) {
        QObject::connect(&job, &FaxJob::logMessage, [](const QString &message) {
            printErr(QStringLiteral("[%1]   %2").arg(timestamp(), message));
        });
    }
    QObject::connect(&job, &FaxJob::protocolTrace,
                     [&trace](bool outgoing, const QString &peer, const QByteArray &data) {
                         trace.write(outgoing, peer, data);
                     });
    QObject::connect(&job, &FaxJob::finished, qApp, [](const FaxOutcome &) { QCoreApplication::quit(); },
                     Qt::QueuedConnection);
    watchInterrupt(&job, [&job] {
        printErr(QStringLiteral("Interrupted, hanging up..."));
        job.abort(QStringLiteral("interrupted by user"));
    });

    printErr(QStringLiteral("[%1] Sending %2 document(s) to %3 via %4@%5")
                 .arg(timestamp())
                 .arg(config.pdfFiles.size())
                 .arg(config.destination, config.sip.account.user, config.sip.account.server));
    job.start();
    QCoreApplication::exec();

    const FaxOutcome outcome = job.outcome();
    if (outcome.status == FaxStatus::Success) {
        printOut(QStringLiteral("OK: %1").arg(outcome.message));
    } else {
        printErr(QStringLiteral("FAILED (%1): %2").arg(faxStatusName(outcome.status), outcome.message));
    }
    return exitCodeFor(outcome.status);
}

// --- register-test ----------------------------------------------------------------

int runRegisterTest(const QStringList &arguments)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Register at the SIP server, print the result and unregister again."));
    parser.addHelpOption();
    SipOptionSet sipOpts;
    sipOpts.addTo(parser);
    parser.process(arguments);

    auto ini = openSettings(parser, sipOpts);
    SipAccount account;
    QString error;
    if (!buildAccount(parser, sipOpts, *ini, &account, &error)) {
        printErr(QStringLiteral("error: %1").arg(error));
        return kExitUsage;
    }
    TraceWriter trace;
    if (parser.isSet(sipOpts.sipTrace) && !trace.open(parser.value(sipOpts.sipTrace), &error)) {
        printErr(QStringLiteral("error: %1").arg(error));
        return kExitUsage;
    }
    const bool verbose = parser.isSet(sipOpts.verbose);

    SipUserAgent ua(account);
    int exitCode = kExitInternal;
    QObject::connect(&ua, &SipUserAgent::trace,
                     [&trace, verbose](bool outgoing, const QHostAddress &peer, quint16 port, const QByteArray &data) {
                         const QString peerText = QStringLiteral("%1:%2").arg(peer.toString()).arg(port);
                         trace.write(outgoing, peerText, data);
                         if (verbose)
                             printErr(QStringLiteral("[%1] %2 %3\n%4")
                                          .arg(timestamp(), outgoing ? QStringLiteral(">>") : QStringLiteral("<<"),
                                               peerText, QString::fromUtf8(data).trimmed()));
                     });
    if (verbose) {
        QObject::connect(&ua, &SipUserAgent::logMessage, [](const QString &message) {
            printErr(QStringLiteral("[%1]   %2").arg(timestamp(), message));
        });
    }
    QObject::connect(&ua, &SipUserAgent::resolved, [&account](const QHostAddress &server, const QHostAddress &local) {
        printErr(QStringLiteral("Server %1 resolved to %2, local address %3")
                     .arg(account.server, server.toString(), local.toString()));
    });
    QObject::connect(&ua, &SipUserAgent::registered, [&](int expires) {
        printOut(QStringLiteral("OK: registered as %1@%2 (expires %3 s, contact %4:%5)")
                     .arg(account.user, account.effectiveDomain())
                     .arg(expires)
                     .arg(ua.localAddress().toString())
                     .arg(ua.localPort()));
        exitCode = kExitOk;
        ua.unregister();
    });
    QObject::connect(&ua, &SipUserAgent::registrationFailed,
                     [&](SipUserAgent::Failure failure, int, const QString &reason) {
                         printErr(QStringLiteral("FAILED: %1").arg(reason));
                         switch (failure) {
                         case SipUserAgent::Failure::ResolveFailed:
                         case SipUserAgent::Failure::NetworkError: exitCode = kExitNetwork; break;
                         case SipUserAgent::Failure::AuthenticationFailed: exitCode = kExitAuth; break;
                         default: exitCode = kExitRegistration; break;
                         }
                         QCoreApplication::quit();
                     });
    QObject::connect(&ua, &SipUserAgent::unregistered, [] {
        printErr(QStringLiteral("Unregistered."));
        QCoreApplication::quit();
    });
    QTimer::singleShot(45000, &ua, [&] {
        printErr(QStringLiteral("FAILED: no result within 45 s"));
        exitCode = kExitTimeout;
        QCoreApplication::quit();
    });
    watchInterrupt(&ua, [&] {
        exitCode = kExitInterrupted;
        QCoreApplication::quit();
    });

    ua.start();
    QCoreApplication::exec();
    return exitCode;
}

// --- render ---------------------------------------------------------------------

int runRender(const QStringList &arguments)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Rasterize PDFs into a fax TIFF (204x196 dpi, CCITT G3)."));
    parser.addHelpOption();
    const QCommandLineOption pdf{QStringList{QStringLiteral("pdf")}, QStringLiteral("PDF document (repeatable)."),
                                 QStringLiteral("file")};
    const QCommandLineOption out{QStringList{QStringLiteral("out")}, QStringLiteral("Output TIFF file."),
                                 QStringLiteral("file")};
    const QCommandLineOption dither{QStringList{QStringLiteral("dither")},
                                    QStringLiteral("Floyd-Steinberg dithering instead of a threshold.")};
    parser.addOptions({pdf, out, dither});
    parser.process(arguments);

    if (!parser.isSet(pdf) || !parser.isSet(out)) {
        printErr(QStringLiteral("error: --pdf and --out are required"));
        return kExitUsage;
    }
    PdfRasterizer::Options options;
    options.mode = parser.isSet(dither) ? PdfRasterizer::Mode::Dither : PdfRasterizer::Mode::Threshold;
    QString error;
    const int pages = PdfRasterizer(options).rasterizeToTiff(parser.values(pdf), parser.value(out),
                                                             QStringLiteral(FAXWERK_PRODUCT_NAME), &error);
    if (pages < 0) {
        printErr(QStringLiteral("FAILED: %1").arg(error));
        return kExitDocument;
    }
    FaxTiffInfo info;
    readFaxTiffInfo(parser.value(out), &info);
    for (int i = 0; i < info.pages; ++i) {
        printOut(QStringLiteral("page %1: %2 x %3 px, %4 x %5 dpi")
                     .arg(i + 1)
                     .arg(info.widths.at(i))
                     .arg(info.heights.at(i))
                     .arg(info.xResolutions.at(i))
                     .arg(info.yResolutions.at(i)));
    }
    printOut(QStringLiteral("OK: %1 page(s) written to %2").arg(pages).arg(parser.value(out)));
    return kExitOk;
}

} // namespace

int main(int argc, char **argv)
{
    // QtPdf needs a QGuiApplication. On Linux there may be no display at all
    // (container, ssh), so default to the offscreen platform there.
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral(FAXWERK_PRODUCT_NAME));
    QCoreApplication::setApplicationVersion(QStringLiteral(FAXWERK_VERSION));

    QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        printErr(usageText());
        return kExitUsage;
    }
    const QString command = args.takeAt(1);
    if (command == QLatin1String("--help") || command == QLatin1String("-h") || command == QLatin1String("help")) {
        printOut(usageText());
        return kExitOk;
    }
    if (command == QLatin1String("--version") || command == QLatin1String("-v")) {
        printOut(QStringLiteral("%1 %2").arg(QStringLiteral(FAXWERK_PRODUCT_NAME), QStringLiteral(FAXWERK_VERSION)));
        return kExitOk;
    }
    // Make "--help" output show "faxwerk send" etc.
    args[0] = QStringLiteral("%1 %2").arg(QFileInfo(args.at(0)).fileName(), command);

    if (command == QLatin1String("send"))
        return runSend(args);
    if (command == QLatin1String("register-test"))
        return runRegisterTest(args);
    if (command == QLatin1String("render"))
        return runRender(args);

    printErr(QStringLiteral("error: unknown command '%1'\n\n%2").arg(command, usageText()));
    return kExitUsage;
}
