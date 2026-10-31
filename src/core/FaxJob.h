#pragma once

#include "document/PdfRasterizer.h"
#include "transport/FaxTransport.h"
#include "transport/SipG711Transport.h"

#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

namespace faxwerk {

enum class TransportKind {
    SipG711,
    // Planned: T38 (SIP + UDPTL), CapiOverTcp
};

struct FaxJobConfig
{
    TransportKind transport = TransportKind::SipG711;
    SipG711Transport::Options sip;
    QString destination;
    QStringList pdfFiles;
    FaxOptions fax;
    PdfRasterizer::Options raster;
    int timeoutSeconds = 900;
    QString productName = QStringLiteral("Faxwerk");
};

// Sends one fax: rasterize PDFs -> transport (register, call, T.30, hang up)
// -> outcome. Enforces an overall timeout and produces readable errors.
class FaxJob : public QObject
{
    Q_OBJECT

public:
    explicit FaxJob(const FaxJobConfig &config, QObject *parent = nullptr);
    ~FaxJob() override;

    void start();
    void abort(const QString &reason = QStringLiteral("aborted by user"));

    bool isFinished() const { return m_finished; }
    FaxOutcome outcome() const { return m_outcome; }
    FaxTransport *transport() const { return m_transport.get(); }

    // Creates the backend for the configured transport kind.
    static std::unique_ptr<FaxTransport> createTransport(const FaxJobConfig &config);

signals:
    void statusChanged(const QString &status);
    void phaseChanged(faxwerk::FaxPhase phase);
    void negotiated(int bitRate, bool ecm, const QString &remoteIdent);
    void pageTransferred(int pages, int total);
    void logMessage(const QString &message);
    void protocolTrace(bool outgoing, const QString &peer, const QByteArray &data);
    void finished(const faxwerk::FaxOutcome &outcome);

private:
    void finish(const FaxOutcome &outcome);

    FaxJobConfig m_config;
    QTemporaryDir m_tempDir;
    std::unique_ptr<FaxTransport> m_transport;
    QTimer m_timeout;
    bool m_started = false;
    bool m_timedOut = false;
    bool m_finished = false;
    FaxOutcome m_outcome;
};

} // namespace faxwerk
