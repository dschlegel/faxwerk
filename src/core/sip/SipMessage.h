#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <optional>

namespace faxwerk {

// A SIP request or response (RFC 3261, section 7).
//
// Header names are matched case-insensitively and compact forms
// (v f t i m l c k s ...) are expanded to their full names when parsing.
// Headers keep their order; multiple instances of the same header are allowed.
class SipMessage
{
public:
    static SipMessage request(const QString &method, const QString &requestUri);
    static SipMessage response(int statusCode, const QString &reasonPhrase);

    // Parses one datagram. On failure returns std::nullopt and, if given,
    // writes a human readable reason to *error.
    static std::optional<SipMessage> parse(const QByteArray &data, QString *error = nullptr);

    // Serializes the message. Content-Length is always (re)computed from the body.
    QByteArray serialize() const;

    bool isRequest() const { return m_isRequest; }
    bool isResponse() const { return !m_isRequest; }

    QString method() const { return m_method; }
    QString requestUri() const { return m_requestUri; }
    void setRequestUri(const QString &uri) { m_requestUri = uri; }
    int statusCode() const { return m_statusCode; }
    QString reasonPhrase() const { return m_reasonPhrase; }

    // First value of the header, or an empty string.
    QString header(const QString &name) const;
    bool hasHeader(const QString &name) const;
    // All values of the header in order. Comma separated lists are split
    // (respecting quotes and <...>), except for headers that are known not
    // to be lists (e.g. WWW-Authenticate).
    QStringList headerValues(const QString &name) const;

    // Replaces all instances of the header with one value.
    void setHeader(const QString &name, const QString &value);
    void addHeader(const QString &name, const QString &value);
    // Inserts before the first instance of the header, or as the first header.
    void prependHeader(const QString &name, const QString &value);
    void removeHeader(const QString &name);
    const QList<QPair<QString, QString>> &headers() const { return m_headers; }

    QByteArray body() const { return m_body; }
    void setBody(const QByteArray &body, const QString &contentType = QString());

    // Convenience accessors.
    QString callId() const { return header(QStringLiteral("Call-ID")); }
    int cseqNumber() const;
    QString cseqMethod() const;
    QString fromTag() const;
    QString toTag() const;
    QString topViaBranch() const;
    // Expires header in seconds, or -1 if absent/invalid.
    int expires() const;

    // Canonical full header name for any spelling or compact form,
    // e.g. "v" -> "Via", "call-id" -> "Call-ID".
    static QString canonicalHeaderName(const QString &name);

private:
    bool m_isRequest = true;
    QString m_method;
    QString m_requestUri;
    int m_statusCode = 0;
    QString m_reasonPhrase;
    QList<QPair<QString, QString>> m_headers;
    QByteArray m_body;
};

// "Display Name" <sip:user@host;uri-params>;header-params
struct SipNameAddr
{
    QString displayName;
    QString uri;
    QString params; // header parameters including the leading ';', e.g. ";tag=abc"

    static SipNameAddr parse(const QString &value);
    QString toString() const;
    QString param(const QString &name) const;
    void setParam(const QString &name, const QString &value);
};

// Parsed SIP URI: sip:user@host:port;params
struct SipUri
{
    QString scheme = QStringLiteral("sip");
    QString user;
    QString host;
    quint16 port = 0; // 0 = not specified
    QString params;   // including leading ';'

    static std::optional<SipUri> parse(const QString &uri);
    QString toString() const;
    bool hasParam(const QString &name) const;
};

namespace sip {

// Value of a ';name=value' parameter in a header value, empty if missing.
// Returns an empty string for a valueless parameter; use hasParam() to test for those.
QString param(const QString &headerValue, const QString &name);
bool hasParam(const QString &headerValue, const QString &name);

// Splits a comma separated header value, ignoring commas inside quotes or <...>.
QStringList splitList(const QString &value);

// Removes surrounding double quotes and unescapes quoted-pairs.
QString unquote(const QString &value);
QString quote(const QString &value);

QString generateTag();
QString generateBranch(); // with the RFC 3261 magic cookie "z9hG4bK"
QString generateCallId(const QString &host);

QString defaultReasonPhrase(int statusCode);

} // namespace sip
} // namespace faxwerk
