#include "apiserver.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QSettings>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <QDebug>

#include "audiosourcecoordinator.h"
#include "mainwindow.h"
#include "screensaverview.h"

// --- small helpers ---

static QByteArray okJson()
{
    return QByteArrayLiteral("{\"ok\":true}");
}

static QByteArray errJson(const QString &msg)
{
    QJsonObject o;
    o["ok"] = false;
    o["error"] = msg;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

static QByteArray reasonPhrase(int status)
{
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    default:  return "OK";
    }
}

static bool parseIntParam(const QString &s, int &out)
{
    bool ok = false;
    int v = s.toInt(&ok);
    if (ok) out = v;
    return ok;
}

// --- request parsing ---

HttpRequest parseRequest(const QByteArray &raw)
{
    HttpRequest req;
    int headerEnd = raw.indexOf("\r\n\r\n");
    QByteArray head = headerEnd >= 0 ? raw.left(headerEnd) : raw;

    QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty())
        return req;

    QByteArray reqLine = lines.first().trimmed();
    QList<QByteArray> parts = reqLine.split(' ');
    if (parts.size() < 2)
        return req;

    req.method = QString::fromLatin1(parts[0]).toUpper();

    QByteArray target = parts[1];
    int q = target.indexOf('?');
    QByteArray pathPart  = q >= 0 ? target.left(q)   : target;
    QByteArray queryPart = q >= 0 ? target.mid(q + 1) : QByteArray();

    req.path = QString::fromUtf8(QByteArray::fromPercentEncoding(pathPart));

    const QList<QByteArray> pairs = queryPart.split('&');
    for (QByteArray pair : pairs) {
        if (pair.isEmpty())
            continue;
        int eq = pair.indexOf('=');
        QByteArray k = eq >= 0 ? pair.left(eq)   : pair;
        QByteArray v = eq >= 0 ? pair.mid(eq + 1) : QByteArray();
        k.replace('+', ' ');
        v.replace('+', ' ');
        QString key = QString::fromUtf8(QByteArray::fromPercentEncoding(k));
        QString val = QString::fromUtf8(QByteArray::fromPercentEncoding(v));
        req.query.insert(key, val);
    }

    for (int i = 1; i < lines.size(); ++i) {
        QByteArray line = lines[i].trimmed();
        if (line.isEmpty())
            continue;
        int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        QString name  = QString::fromLatin1(line.left(colon)).trimmed().toLower();
        QString value = QString::fromLatin1(line.mid(colon + 1)).trimmed();
        req.headers.insert(name, value);
    }

    req.valid = true;
    return req;
}

// --- ApiServer ---

ApiServer::ApiServer(AudioSourceCoordinator *coordinator, MainWindow *window, QObject *parent)
    : QObject(parent), m_coordinator(coordinator), m_window(window)
{
    QSettings settings;
    m_enabled     = settings.value("api/enabled", true).toBool();
    m_port        = static_cast<quint16>(settings.value("api/port", 8080).toInt());
    m_bindAddress = settings.value("api/bindAddress", "0.0.0.0").toString();
    m_token       = settings.value("api/token", "").toString();

    if (!m_enabled) {
        qInfo() << "[api] disabled via config";
        return;
    }

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &ApiServer::onNewConnection);

    QHostAddress addr = (m_bindAddress == "0.0.0.0")
                            ? QHostAddress(QHostAddress::Any)
                            : QHostAddress(m_bindAddress);

    if (!m_server->listen(addr, m_port))
        qWarning() << "[api] failed to listen on" << m_bindAddress << m_port
                   << ":" << m_server->errorString();
    else
        qInfo() << "[api] listening on" << m_bindAddress << m_port;
}

void ApiServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, &ApiServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void ApiServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;

    QByteArray &buf = m_buffers[socket];
    buf += socket->readAll();

    if (buf.size() > 64 * 1024) {
        sendResponse(socket, {400, errJson("request too large")});
        socket->disconnectFromHost();
        return;
    }
    if (!buf.contains("\r\n\r\n"))
        return; // wait for the full header block

    HttpRequest req = parseRequest(buf);
    m_buffers.remove(socket);

    Response resp;
    if (!req.valid)
        resp = {400, errJson("bad request")};
    else if (!authorized(req))
        resp = {401, errJson("unauthorized")};
    else
        resp = route(req);

    sendResponse(socket, resp);
    socket->disconnectFromHost();
}

bool ApiServer::authorized(const HttpRequest &req) const
{
    if (m_token.isEmpty())
        return true;
    if (req.query.value("token") == m_token)
        return true;
    if (req.headers.value("authorization") == "Bearer " + m_token)
        return true;
    return false;
}

ApiServer::Response ApiServer::route(const HttpRequest &req)
{
    if (req.method == "OPTIONS")
        return {200, okJson()};
    if (req.method != "GET" && req.method != "POST")
        return {405, errJson("method not allowed")};

    QString path = req.path;
    if (path.size() > 1 && path.endsWith('/'))
        path.chop(1);

    Response out;
    if (handleMeta(path, req, out))        return out;
    if (handleTransport(path, req, out))   return out;
    if (handleScreensaver(path, req, out)) return out;
    return {404, errJson("unknown endpoint")};
}

bool ApiServer::handleMeta(const QString &path, const HttpRequest &req, Response &out)
{
    Q_UNUSED(req);
    if (path == "/" || path == "/api/health") {
        out = {200, QByteArrayLiteral("{\"ok\":true,\"service\":\"linamp\"}")};
        return true;
    }
    if (path == "/api/clock/list") {
        QJsonObject o;
        o["ok"] = true;
        o["faces"] = QJsonArray::fromStringList(ScreenSaverView::faceNames());
        out = {200, QJsonDocument(o).toJson(QJsonDocument::Compact)};
        return true;
    }
    return false;
}

// Stubs filled in by later tasks.
bool ApiServer::handleTransport(const QString &, const HttpRequest &, Response &)
{
    return false;
}

bool ApiServer::handleScreensaver(const QString &, const HttpRequest &, Response &)
{
    return false;
}

void ApiServer::sendResponse(QTcpSocket *socket, const Response &resp)
{
    QByteArray body = resp.json;
    QByteArray out;
    out += "HTTP/1.1 " + QByteArray::number(resp.status) + " " + reasonPhrase(resp.status) + "\r\n";
    out += "Content-Type: application/json\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Access-Control-Allow-Origin: *\r\n";
    out += "Connection: close\r\n\r\n";
    out += body;
    socket->write(out);
    socket->flush();
}
