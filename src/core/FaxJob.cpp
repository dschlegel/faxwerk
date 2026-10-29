#include "FaxJob.h"

#include <QFileInfo>

namespace faxwerk {

FaxJob::FaxJob(const FaxJobConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
    qRegisterMetaType<faxwerk::FaxOutcome>();
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_timedOut = true;
        if (m_transport)
            m_transport->abort(QStringLiteral("overall timeout of %1 s exceeded").arg(m_config.timeoutSeconds));
        else
            finish({FaxStatus::Timeout, QStringLiteral("timeout"), {}, 0});
    });
}

FaxJob::~FaxJob() = default;

std::unique_ptr<FaxTransport> FaxJob::createTransport(const FaxJobConfig &config)
{
    switch (config.transport) {
    case TransportKind::SipG711:
        return std::make_unique<SipG711Transport>(config.sip);
    }
    return nullptr;
}

void FaxJob::start()
{
    if (m_started)
        return;
    m_started = true;

    // Defer so that callers can connect to signals after start() returns
    // and results are always delivered from the event loop.
    QMetaObject::invokeMethod(this, [this] {
        if (m_finished)
            return;
        if (m_config.destination.trimmed().isEmpty()) {
            finish({FaxStatus::InvalidRequest, QStringLiteral("no destination number given"), {}, 0});
            return;
        }
        if (m_config.pdfFiles.isEmpty()) {
            finish({FaxStatus::InvalidRequest, QStringLiteral("no PDF file given"), {}, 0});
            return;
        }
        if (!m_tempDir.isValid()) {
            finish({FaxStatus::InternalError, QStringLiteral("cannot create temporary directory"), {}, 0});
            return;
        }

        emit statusChanged(QStringLiteral("Rendering %1 document(s)").arg(m_config.pdfFiles.size()));
        const QString tiffPath = m_tempDir.filePath(QStringLiteral("fax.tif"));
        QString error;
        const int pages = PdfRasterizer(m_config.raster)
                              .rasterizeToTiff(m_config.pdfFiles, tiffPath, m_config.productName, &error);
        if (pages <= 0) {
            finish({FaxStatus::DocumentError, error, {}, 0});
            return;
        }
        emit statusChanged(QStringLiteral("Rendered %1 page(s)").arg(pages));

        m_transport = createTransport(m_config);
        if (!m_transport) {
            finish({FaxStatus::InternalError, QStringLiteral("transport not available"), {}, 0});
            return;
        }
        connect(m_transport.get(), &FaxTransport::statusChanged, this, &FaxJob::statusChanged);
        connect(m_transport.get(), &FaxTransport::pageTransferred, this, &FaxJob::pageTransferred);
        connect(m_transport.get(), &FaxTransport::logMessage, this, &FaxJob::logMessage);
        connect(m_transport.get(), &FaxTransport::protocolTrace, this, &FaxJob::protocolTrace);
        connect(m_transport.get(), &FaxTransport::finished, this, [this](const FaxOutcome &outcome) {
            FaxOutcome result = outcome;
            if (m_timedOut && result.status == FaxStatus::Aborted)
                result.status = FaxStatus::Timeout;
            finish(result);
        });

        if (m_config.timeoutSeconds > 0)
            m_timeout.start(m_config.timeoutSeconds * 1000);

        FaxSendRequest request;
        request.destination = m_config.destination.trimmed();
        request.tiffPath = tiffPath;
        request.pageCount = pages;
        request.fax = m_config.fax;
        m_transport->send(request);
    }, Qt::QueuedConnection);
}

void FaxJob::abort(const QString &reason)
{
    if (m_finished)
        return;
    if (m_transport)
        m_transport->abort(reason);
    else
        finish({FaxStatus::Aborted, reason, {}, 0});
}

void FaxJob::finish(const FaxOutcome &outcome)
{
    if (m_finished)
        return;
    m_finished = true;
    m_timeout.stop();
    m_outcome = outcome;
    emit finished(outcome);
}

} // namespace faxwerk
