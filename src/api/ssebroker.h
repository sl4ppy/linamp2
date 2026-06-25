#ifndef SSEBROKER_H
#define SSEBROKER_H

#include <QObject>
#include <QSet>
#include <QByteArray>

class QTcpSocket;
class QTimer;
class WebStateHub;

// Fans WebStateHub updates out to connected SSE clients. ApiServer hands a
// freshly-accepted /api/events socket to addClient() (which writes the SSE
// response headers + an initial status event) and notifies removeClient() when
// the socket disconnects. The broker never owns socket lifetime — it only holds
// pointers and writes to them.
class SseBroker : public QObject
{
    Q_OBJECT
public:
    explicit SseBroker(WebStateHub *hub, QObject *parent = nullptr);

    void addClient(QTcpSocket *socket);
    void removeClient(QTcpSocket *socket);
    int clientCount() const { return m_clients.size(); }

private slots:
    void onStateChanged();
    void onPositionChanged(qint64 ms);
    void sendHeartbeat();

private:
    void writeEvent(QTcpSocket *s, const char *event, const QByteArray &data);
    void broadcast(const char *event, const QByteArray &data);

    WebStateHub *m_hub;
    QSet<QTcpSocket *> m_clients;
    QTimer *m_heartbeat;
    qint64 m_lastPosSec = -1;
};

#endif // SSEBROKER_H
