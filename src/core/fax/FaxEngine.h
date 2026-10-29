#pragma once

#include "rtp/AudioPort.h"

#include <QObject>
#include <QString>

struct fax_state_s; // spandsp, kept out of the public header

namespace faxwerk {

struct FaxOptions
{
    QString stationId;   // T.30 TSI/CSI, max. 20 characters, e.g. "+49 30 1234567"
    QString headerInfo;  // page header line text, max. 50 characters
    int maxBitRate = 9600; // 9600 = V.29/V.27ter, 14400 adds V.17
    bool ecm = true;
    bool protocolLog = false; // forward spandsp T.30 logging via logMessage()
};

struct FaxResult
{
    bool success = false;
    int completionCode = -1;   // T30_ERR_* code
    QString completionText;    // t30_completion_code_to_str()
    int pagesTransferred = 0;
    int pagesInFile = 0;
    int bitRate = 0;
    bool ecm = false;
    QString remoteIdent;
};

// T.30 fax over audio (G.711) using spandsp's fax_state in audio mode.
// Implements AudioPort, so it can be attached directly to an RtpSession.
class FaxEngine : public QObject, public AudioPort
{
    Q_OBJECT

public:
    enum class Role { Send, Receive };

    // callingParty: true for the side that dialed (sends CNG), false for the
    // answering side (sends CED).
    FaxEngine(Role role, bool callingParty, const FaxOptions &options, QObject *parent = nullptr);
    ~FaxEngine() override;

    // Send: multi-page TIFF to transmit. Receive: TIFF file to write.
    bool start(const QString &tiffPath, QString *error = nullptr);
    // Tells T.30 that the call is gone; results in finished() if a session was active.
    void terminate();

    Role role() const { return m_role; }
    bool isStarted() const { return m_fax != nullptr; }
    bool isFinished() const { return m_finished; }
    FaxResult result() const { return m_result; }

    // AudioPort
    void receiveAudio(const std::int16_t *samples, int count) override;
    void receiveGap(int count) override;
    void produceAudio(std::int16_t *samples, int count) override;

    // Dispatch targets for the spandsp callbacks (internal use).
    void handlePhaseB(int result);
    void handlePhaseD(int result);
    void handlePhaseE(int completionCode);
    void handleLog(int level, const char *text);

signals:
    void negotiated(int bitRate, bool ecm, const QString &remoteIdent);
    void pageTransferred(int pages, int pagesInFile);
    void finished(const faxwerk::FaxResult &result);
    void logMessage(const QString &message);

private:
    void fillStatistics(FaxResult &result) const;

    Role m_role;
    bool m_callingParty;
    FaxOptions m_options;
    fax_state_s *m_fax = nullptr;
    bool m_finished = false;
    FaxResult m_result;
};

} // namespace faxwerk

Q_DECLARE_METATYPE(faxwerk::FaxResult)
