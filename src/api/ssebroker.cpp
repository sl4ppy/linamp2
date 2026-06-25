#include "ssebroker.h"
#include "webstatehub.h"

#include <QTcpSocket>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>

SseBroker::SseBroker(WebStateHub *hub, QObject *parent)
    : QObject(parent), m_hub(hub)
{
    connect(hub, &WebStateHub::stateChanged,    this, &SseBroker::onStateChanged);
    connect(hub, &WebStateHub::positionChanged, this, &SseBroker::onPositionChanged);

    m_heartbeat = new QTimer(this);
    m_heartbeat->setInterval(15000);
    connect(m_heartbeat, &QTimer::timeout, this, &SseBroker::sendHeartbeat);
    m_heartbeat->start();
}

void SseBroker::addClient(QTcpSocket *socket)
{
    static const QByteArray headers =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    socket->write(headers);
    socket->flush();
    m_clients.insert(socket);

    const QByteArray data = QJsonDocument(m_hub->snapshot()).toJson(QJsonDocument::Compact);
    writeEvent(socket, "status", data);
}

void SseBroker::removeClient(QTcpSocket *socket)
{
    m_clients.remove(socket);
}

void SseBroker::writeEvent(QTcpSocket *s, const char *event, const QByteArray &data)
{
    if (s->state() != QAbstractSocket::ConnectedState)
        return;
    QByteArray frame;
    frame += "event: ";
    frame += event;
    frame += "\r\ndata: ";
    frame += data;
    frame += "\r\n\r\n";
    s->write(frame);
    s->flush();
}

void SseBroker::broadcast(const char *event, const QByteArray &data)
{
    // Iterate a copy: a failed write can disconnect a socket, which removes it
    // from m_clients mid-iteration.
    const auto clients = m_clients;
    for (QTcpSocket *s : clients)
        writeEvent(s, event, data);
}

void SseBroker::onStateChanged()
{
    const QByteArray data = QJsonDocument(m_hub->snapshot()).toJson(QJsonDocument::Compact);
    broadcast("status", data);
}

void SseBroker::onPositionChanged(qint64 ms)
{
    const qint64 sec = ms / 1000;
    if (sec == m_lastPosSec)
        return; // throttle to ~1 Hz
    m_lastPosSec = sec;
    const QByteArray data = "{\"positionMs\":" + QByteArray::number(ms) + "}";
    broadcast("position", data);
}

void SseBroker::sendHeartbeat()
{
    const auto clients = m_clients;
    for (QTcpSocket *s : clients) {
        if (s->state() == QAbstractSocket::ConnectedState) {
            s->write(": ping\r\n\r\n");
            s->flush();
        }
    }
}
