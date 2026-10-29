#include "SipMessage.h"

#include <QHash>
#include <QRandomGenerator>

namespace faxwerk {

namespace {

const QHash<QString, QString> &compactForms()
{
    static const QHash<QString, QString> forms = {
        {QStringLiteral("a"), QStringLiteral("Accept-Contact")},
        {QStringLiteral("b"), QStringLiteral("Referred-By")},
        {QStringLiteral("c"), QStringLiteral("Content-Type")},
        {QStringLiteral("d"), QStringLiteral("Request-Disposition")},
        {QStringLiteral("e"), QStringLiteral("Content-Encoding")},
        {QStringLiteral("f"), QStringLiteral("From")},
        {QStringLiteral("i"), QStringLiteral("Call-ID")},
        {QStringLiteral("j"), QStringLiteral("Reject-Contact")},
        {QStringLiteral("k"), QStringLiteral("Supported")},
        {QStringLiteral("l"), QStringLiteral("Content-Length")},
        {QStringLiteral("m"), QStringLiteral("Contact")},
        {QStringLiteral("o"), QStringLiteral("Event")},
        {QStringLiteral("r"), QStringLiteral("Refer-To")},
        {QStringLiteral("s"), QStringLiteral("Subject")},
        {QStringLiteral("t"), QStringLiteral("To")},
        {QStringLiteral("u"), QStringLiteral("Allow-Events")},
        {QStringLiteral("v"), QStringLiteral("Via")},
        {QStringLiteral("x"), QStringLiteral("Session-Expires")},
    };
    return forms;
}

// Names whose canonical spelling is not plain "Word-Word" capitalization.
const QHash<QString, QString> &specialNames()
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("call-id"), QStringLiteral("Call-ID")},
        {QStringLiteral("cseq"), QStringLiteral("CSeq")},
        {QStringLiteral("www-authenticate"), QStringLiteral("WWW-Authenticate")},
        {QStringLiteral("mime-version"), QStringLiteral("MIME-Version")},
        {QStringLiteral("rack"), QStringLiteral("RAck")},
        {QStringLiteral("rseq"), QStringLiteral("RSeq")},
        {QStringLiteral("sip-etag"), QStringLiteral("SIP-ETag")},
        {QStringLiteral("sip-if-match"), QStringLiteral("SIP-If-Match")},
    };
    return names;
}

// Headers whose values are comma separated lists (RFC 3261 section 7.3.1).
bool isListHeader(const QString &canonicalName)
{
    static const QStringList lists = {
        QStringLiteral("Via"),         QStringLiteral("Route"),
        QStringLiteral("Record-Route"), QStringLiteral("Contact"),
        QStringLiteral("Allow"),       QStringLiteral("Supported"),
        QStringLiteral("Require"),     QStringLiteral("Proxy-Require"),
        QStringLiteral("Unsupported"), QStringLiteral("Accept"),
        QStringLiteral("Allow-Events"), QStringLiteral("Path"),
        QStringLiteral("Service-Route"),
    };
    return lists.contains(canonicalName);
}

QString randomHex(int bytes)
{
    QByteArray raw(bytes, Qt::Uninitialized);
    for (int i = 0; i < bytes; ++i)
        raw[i] = char(QRandomGenerator::global()->bounded(256));
    return QString::fromLatin1(raw.toHex());
}

// Splits ";a=b;c" style parameter lists (outside quotes and <...>).
QList<QPair<QString, QString>> parseParams(const QString &value)
{
    QList<QPair<QString, QString>> result;
    bool inQuotes = false;
    int angle = 0;
    int start = -1;
    auto flush = [&](int end) {
        if (start < 0)
            return;
        const QString part = value.mid(start, end - start).trimmed();
        if (!part.isEmpty()) {
            const int eq = part.indexOf(QLatin1Char('='));
            if (eq < 0)
                result.append({part, QString()});
            else
                result.append({part.left(eq).trimmed(), part.mid(eq + 1).trimmed()});
        }
    };
    for (int i = 0; i < value.size(); ++i) {
        const QChar ch = value.at(i);
        if (inQuotes) {
            if (ch == QLatin1Char('\\'))
                ++i;
            else if (ch == QLatin1Char('"'))
                inQuotes = false;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            inQuotes = true;
        } else if (ch == QLatin1Char('<')) {
            ++angle;
        } else if (ch == QLatin1Char('>')) {
            angle = qMax(0, angle - 1);
        } else if (ch == QLatin1Char(';') && angle == 0) {
            flush(i);
            start = i + 1;
        }
    }
    flush(value.size());
    return result;
}

} // namespace

SipMessage SipMessage::request(const QString &method, const QString &requestUri)
{
    SipMessage msg;
    msg.m_isRequest = true;
    msg.m_method = method;
    msg.m_requestUri = requestUri;
    return msg;
}

SipMessage SipMessage::response(int statusCode, const QString &reasonPhrase)
{
    SipMessage msg;
    msg.m_isRequest = false;
    msg.m_statusCode = statusCode;
    msg.m_reasonPhrase = reasonPhrase.isEmpty() ? sip::defaultReasonPhrase(statusCode) : reasonPhrase;
    return msg;
}

std::optional<SipMessage> SipMessage::parse(const QByteArray &data, QString *error)
{
    auto fail = [error](const QString &reason) -> std::optional<SipMessage> {
        if (error)
            *error = reason;
        return std::nullopt;
    };

    // Locate the end of the header section; tolerate bare LF line endings.
    int headerEnd = data.indexOf("\r\n\r\n");
    int bodyStart = headerEnd + 4;
    const int lfEnd = data.indexOf("\n\n");
    if (headerEnd < 0 || (lfEnd >= 0 && lfEnd < headerEnd)) {
        headerEnd = lfEnd;
        bodyStart = lfEnd + 2;
    }
    if (headerEnd < 0) {
        // A message without body may end directly after the last header.
        headerEnd = data.size();
        bodyStart = data.size();
    }

    const QString head = QString::fromUtf8(data.constData(), headerEnd);
    QStringList lines = head.split(QLatin1Char('\n'));
    for (QString &line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
    }
    // Skip leading empty lines (keep-alive CRLFs).
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
        lines.removeFirst();
    if (lines.isEmpty())
        return fail(QStringLiteral("empty message"));

    SipMessage msg;
    const QString startLine = lines.takeFirst();
    if (startLine.startsWith(QLatin1String("SIP/2.0 "), Qt::CaseInsensitive)) {
        msg.m_isRequest = false;
        const QString rest = startLine.mid(8).trimmed();
        const int sp = rest.indexOf(QLatin1Char(' '));
        bool ok = false;
        msg.m_statusCode = rest.left(sp < 0 ? rest.size() : sp).toInt(&ok);
        if (!ok || msg.m_statusCode < 100 || msg.m_statusCode > 699)
            return fail(QStringLiteral("invalid status line: %1").arg(startLine));
        msg.m_reasonPhrase = sp < 0 ? QString() : rest.mid(sp + 1).trimmed();
    } else {
        const QStringList parts = startLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() != 3 || parts.at(2).compare(QLatin1String("SIP/2.0"), Qt::CaseInsensitive) != 0)
            return fail(QStringLiteral("invalid request line: %1").arg(startLine));
        msg.m_isRequest = true;
        msg.m_method = parts.at(0).toUpper();
        msg.m_requestUri = parts.at(1);
    }

    for (const QString &line : std::as_const(lines)) {
        if (line.isEmpty())
            continue;
        if (line.at(0) == QLatin1Char(' ') || line.at(0) == QLatin1Char('\t')) {
            // Header folding: continuation of the previous header value.
            if (msg.m_headers.isEmpty())
                return fail(QStringLiteral("continuation line without header"));
            QString &value = msg.m_headers.last().second;
            value = (value + QLatin1Char(' ') + line.trimmed()).trimmed();
            continue;
        }
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            return fail(QStringLiteral("malformed header line: %1").arg(line));
        msg.m_headers.append({canonicalHeaderName(line.left(colon).trimmed()), line.mid(colon + 1).trimmed()});
    }

    QByteArray body = data.mid(bodyStart);
    if (msg.hasHeader(QStringLiteral("Content-Length"))) {
        bool ok = false;
        const int length = msg.header(QStringLiteral("Content-Length")).toInt(&ok);
        if (!ok || length < 0)
            return fail(QStringLiteral("invalid Content-Length"));
        if (body.size() < length)
            return fail(QStringLiteral("truncated body (%1 of %2 bytes)").arg(body.size()).arg(length));
        body.truncate(length);
    }
    msg.m_body = body;

    for (const char *required : {"Via", "From", "To", "Call-ID", "CSeq"}) {
        if (!msg.hasHeader(QLatin1String(required)))
            return fail(QStringLiteral("missing mandatory header %1").arg(QLatin1String(required)));
    }
    return msg;
}

QByteArray SipMessage::serialize() const
{
    QByteArray out;
    out.reserve(512 + m_body.size());
    if (m_isRequest)
        out += m_method.toUtf8() + ' ' + m_requestUri.toUtf8() + " SIP/2.0\r\n";
    else
        out += "SIP/2.0 " + QByteArray::number(m_statusCode) + ' ' + m_reasonPhrase.toUtf8() + "\r\n";
    for (const auto &h : m_headers) {
        if (h.first == QLatin1String("Content-Length"))
            continue;
        out += h.first.toUtf8() + ": " + h.second.toUtf8() + "\r\n";
    }
    out += "Content-Length: " + QByteArray::number(m_body.size()) + "\r\n\r\n";
    out += m_body;
    return out;
}

QString SipMessage::header(const QString &name) const
{
    const QString canonical = canonicalHeaderName(name);
    for (const auto &h : m_headers) {
        if (h.first == canonical)
            return h.second;
    }
    return QString();
}

bool SipMessage::hasHeader(const QString &name) const
{
    const QString canonical = canonicalHeaderName(name);
    for (const auto &h : m_headers) {
        if (h.first == canonical)
            return true;
    }
    return false;
}

QStringList SipMessage::headerValues(const QString &name) const
{
    const QString canonical = canonicalHeaderName(name);
    const bool split = isListHeader(canonical);
    QStringList values;
    for (const auto &h : m_headers) {
        if (h.first != canonical)
            continue;
        if (split)
            values += sip::splitList(h.second);
        else
            values.append(h.second);
    }
    return values;
}

void SipMessage::setHeader(const QString &name, const QString &value)
{
    const QString canonical = canonicalHeaderName(name);
    for (int i = 0; i < m_headers.size(); ++i) {
        if (m_headers.at(i).first == canonical) {
            m_headers[i].second = value;
            for (int j = m_headers.size() - 1; j > i; --j) {
                if (m_headers.at(j).first == canonical)
                    m_headers.removeAt(j);
            }
            return;
        }
    }
    m_headers.append({canonical, value});
}

void SipMessage::addHeader(const QString &name, const QString &value)
{
    m_headers.append({canonicalHeaderName(name), value});
}

void SipMessage::prependHeader(const QString &name, const QString &value)
{
    // Insert before the first instance of the same header, or at the very top.
    const QString canonical = canonicalHeaderName(name);
    for (int i = 0; i < m_headers.size(); ++i) {
        if (m_headers.at(i).first == canonical) {
            m_headers.insert(i, {canonical, value});
            return;
        }
    }
    m_headers.prepend({canonical, value});
}

void SipMessage::removeHeader(const QString &name)
{
    const QString canonical = canonicalHeaderName(name);
    for (int i = m_headers.size() - 1; i >= 0; --i) {
        if (m_headers.at(i).first == canonical)
            m_headers.removeAt(i);
    }
}

void SipMessage::setBody(const QByteArray &body, const QString &contentType)
{
    m_body = body;
    if (!contentType.isEmpty())
        setHeader(QStringLiteral("Content-Type"), contentType);
    else if (body.isEmpty())
        removeHeader(QStringLiteral("Content-Type"));
}

int SipMessage::cseqNumber() const
{
    const QString cseq = header(QStringLiteral("CSeq")).trimmed();
    return cseq.section(QLatin1Char(' '), 0, 0, QString::SectionSkipEmpty).toInt();
}

QString SipMessage::cseqMethod() const
{
    const QString cseq = header(QStringLiteral("CSeq")).trimmed();
    return cseq.section(QLatin1Char(' '), 1, 1, QString::SectionSkipEmpty).toUpper();
}

QString SipMessage::fromTag() const
{
    return SipNameAddr::parse(header(QStringLiteral("From"))).param(QStringLiteral("tag"));
}

QString SipMessage::toTag() const
{
    return SipNameAddr::parse(header(QStringLiteral("To"))).param(QStringLiteral("tag"));
}

QString SipMessage::topViaBranch() const
{
    const QStringList vias = headerValues(QStringLiteral("Via"));
    return vias.isEmpty() ? QString() : sip::param(vias.first(), QStringLiteral("branch"));
}

int SipMessage::expires() const
{
    if (!hasHeader(QStringLiteral("Expires")))
        return -1;
    bool ok = false;
    const int value = header(QStringLiteral("Expires")).trimmed().toInt(&ok);
    return ok && value >= 0 ? value : -1;
}

QString SipMessage::canonicalHeaderName(const QString &name)
{
    const QString lower = name.trimmed().toLower();
    if (lower.size() == 1) {
        const auto it = compactForms().constFind(lower);
        if (it != compactForms().constEnd())
            return it.value();
    }
    const auto special = specialNames().constFind(lower);
    if (special != specialNames().constEnd())
        return special.value();

    QString result = lower;
    bool upperNext = true;
    for (QChar &ch : result) {
        if (upperNext)
            ch = ch.toUpper();
        upperNext = (ch == QLatin1Char('-'));
    }
    return result;
}

// ---------------------------------------------------------------------------

SipNameAddr SipNameAddr::parse(const QString &value)
{
    SipNameAddr result;
    const QString v = value.trimmed();

    // Find '<' outside of a quoted display name.
    int lt = -1;
    bool inQuotes = false;
    for (int i = 0; i < v.size(); ++i) {
        const QChar ch = v.at(i);
        if (inQuotes) {
            if (ch == QLatin1Char('\\'))
                ++i;
            else if (ch == QLatin1Char('"'))
                inQuotes = false;
        } else if (ch == QLatin1Char('"')) {
            inQuotes = true;
        } else if (ch == QLatin1Char('<')) {
            lt = i;
            break;
        }
    }

    if (lt >= 0) {
        const int gt = v.indexOf(QLatin1Char('>'), lt);
        result.displayName = sip::unquote(v.left(lt).trimmed());
        result.uri = v.mid(lt + 1, (gt < 0 ? v.size() : gt) - lt - 1).trimmed();
        result.params = gt < 0 ? QString() : v.mid(gt + 1).trimmed();
    } else {
        // addr-spec form: parameters after the URI belong to the header.
        const int semi = v.indexOf(QLatin1Char(';'));
        result.uri = semi < 0 ? v : v.left(semi).trimmed();
        result.params = semi < 0 ? QString() : v.mid(semi).trimmed();
    }
    return result;
}

QString SipNameAddr::toString() const
{
    QString out;
    if (!displayName.isEmpty())
        out = sip::quote(displayName) + QLatin1Char(' ');
    out += QLatin1Char('<') + uri + QLatin1Char('>') + params;
    return out;
}

QString SipNameAddr::param(const QString &name) const
{
    return sip::param(params, name);
}

void SipNameAddr::setParam(const QString &name, const QString &value)
{
    QString rebuilt;
    bool replaced = false;
    for (const auto &p : parseParams(params)) {
        if (p.first.compare(name, Qt::CaseInsensitive) == 0) {
            rebuilt += QLatin1Char(';') + name + QLatin1Char('=') + value;
            replaced = true;
        } else {
            rebuilt += QLatin1Char(';') + p.first;
            if (!p.second.isNull())
                rebuilt += QLatin1Char('=') + p.second;
        }
    }
    if (!replaced)
        rebuilt += QLatin1Char(';') + name + QLatin1Char('=') + value;
    params = rebuilt;
}

// ---------------------------------------------------------------------------

std::optional<SipUri> SipUri::parse(const QString &uriText)
{
    QString uri = uriText.trimmed();
    if (uri.startsWith(QLatin1Char('<')) && uri.endsWith(QLatin1Char('>')))
        uri = uri.mid(1, uri.size() - 2);
    const int colon = uri.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return std::nullopt;

    SipUri result;
    result.scheme = uri.left(colon).toLower();
    if (result.scheme != QLatin1String("sip") && result.scheme != QLatin1String("sips"))
        return std::nullopt;

    QString rest = uri.mid(colon + 1);
    const int question = rest.indexOf(QLatin1Char('?'));
    if (question >= 0)
        rest.truncate(question);
    const int at = rest.lastIndexOf(QLatin1Char('@'));
    if (at >= 0) {
        result.user = rest.left(at);
        const int userParam = result.user.indexOf(QLatin1Char(':'));
        if (userParam >= 0)
            result.user.truncate(userParam); // drop password
        rest = rest.mid(at + 1);
    }
    const int semi = rest.indexOf(QLatin1Char(';'));
    if (semi >= 0) {
        result.params = rest.mid(semi);
        rest.truncate(semi);
    }

    QString hostPort = rest;
    if (hostPort.startsWith(QLatin1Char('['))) {
        const int close = hostPort.indexOf(QLatin1Char(']'));
        if (close < 0)
            return std::nullopt;
        result.host = hostPort.mid(1, close - 1);
        hostPort = hostPort.mid(close + 1);
        if (hostPort.startsWith(QLatin1Char(':')))
            result.port = quint16(hostPort.mid(1).toUInt());
    } else {
        const int portSep = hostPort.indexOf(QLatin1Char(':'));
        result.host = portSep < 0 ? hostPort : hostPort.left(portSep);
        if (portSep >= 0)
            result.port = quint16(hostPort.mid(portSep + 1).toUInt());
    }
    if (result.host.isEmpty())
        return std::nullopt;
    return result;
}

QString SipUri::toString() const
{
    QString out = scheme + QLatin1Char(':');
    if (!user.isEmpty())
        out += user + QLatin1Char('@');
    out += host.contains(QLatin1Char(':')) ? QLatin1Char('[') + host + QLatin1Char(']') : host;
    if (port != 0)
        out += QLatin1Char(':') + QString::number(port);
    out += params;
    return out;
}

bool SipUri::hasParam(const QString &name) const
{
    return sip::hasParam(params, name);
}

// ---------------------------------------------------------------------------

namespace sip {

QString param(const QString &headerValue, const QString &name)
{
    for (const auto &p : parseParams(headerValue)) {
        if (p.first.compare(name, Qt::CaseInsensitive) == 0)
            return unquote(p.second);
    }
    return QString();
}

bool hasParam(const QString &headerValue, const QString &name)
{
    for (const auto &p : parseParams(headerValue)) {
        if (p.first.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QStringList splitList(const QString &value)
{
    QStringList parts;
    bool inQuotes = false;
    int angle = 0;
    int start = 0;
    for (int i = 0; i < value.size(); ++i) {
        const QChar ch = value.at(i);
        if (inQuotes) {
            if (ch == QLatin1Char('\\'))
                ++i;
            else if (ch == QLatin1Char('"'))
                inQuotes = false;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            inQuotes = true;
        } else if (ch == QLatin1Char('<')) {
            ++angle;
        } else if (ch == QLatin1Char('>')) {
            angle = qMax(0, angle - 1);
        } else if (ch == QLatin1Char(',') && angle == 0) {
            const QString part = value.mid(start, i - start).trimmed();
            if (!part.isEmpty())
                parts.append(part);
            start = i + 1;
        }
    }
    const QString last = value.mid(start).trimmed();
    if (!last.isEmpty())
        parts.append(last);
    return parts;
}

QString unquote(const QString &value)
{
    const QString v = value.trimmed();
    if (v.size() < 2 || !v.startsWith(QLatin1Char('"')) || !v.endsWith(QLatin1Char('"')))
        return v;
    QString out;
    out.reserve(v.size());
    for (int i = 1; i < v.size() - 1; ++i) {
        if (v.at(i) == QLatin1Char('\\') && i + 1 < v.size() - 1)
            ++i;
        out += v.at(i);
    }
    return out;
}

QString quote(const QString &value)
{
    QString out = QStringLiteral("\"");
    for (const QChar ch : value) {
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\\'))
            out += QLatin1Char('\\');
        out += ch;
    }
    out += QLatin1Char('"');
    return out;
}

QString generateTag()
{
    return randomHex(8);
}

QString generateBranch()
{
    return QStringLiteral("z9hG4bK") + randomHex(10);
}

QString generateCallId(const QString &host)
{
    return randomHex(12) + QLatin1Char('@') + host;
}

QString defaultReasonPhrase(int statusCode)
{
    switch (statusCode) {
    case 100: return QStringLiteral("Trying");
    case 180: return QStringLiteral("Ringing");
    case 183: return QStringLiteral("Session Progress");
    case 200: return QStringLiteral("OK");
    case 400: return QStringLiteral("Bad Request");
    case 401: return QStringLiteral("Unauthorized");
    case 403: return QStringLiteral("Forbidden");
    case 404: return QStringLiteral("Not Found");
    case 407: return QStringLiteral("Proxy Authentication Required");
    case 408: return QStringLiteral("Request Timeout");
    case 480: return QStringLiteral("Temporarily Unavailable");
    case 481: return QStringLiteral("Call/Transaction Does Not Exist");
    case 486: return QStringLiteral("Busy Here");
    case 487: return QStringLiteral("Request Terminated");
    case 488: return QStringLiteral("Not Acceptable Here");
    case 491: return QStringLiteral("Request Pending");
    case 500: return QStringLiteral("Server Internal Error");
    case 501: return QStringLiteral("Not Implemented");
    case 503: return QStringLiteral("Service Unavailable");
    case 603: return QStringLiteral("Decline");
    default: break;
    }
    if (statusCode < 200)
        return QStringLiteral("Provisional");
    if (statusCode < 300)
        return QStringLiteral("OK");
    return QStringLiteral("Error");
}

} // namespace sip
} // namespace faxwerk
