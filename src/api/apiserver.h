#ifndef APISERVER_H
#define APISERVER_H

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QString>

class AudioSourceCoordinator;
class MainWindow;
class WebStateHub;
class SseBroker;
class QTcpServer;
class QTcpSocket;

// Parsed HTTP request (request line + headers; body is ignored).
struct HttpRequest {
    QString method;                   // "GET", "POST", "OPTIONS", ...
    QString path;                     // "/api/play"
    QHash<QString, QString> query;    // decoded query parameters
    QHash<QString, QString> headers;  // lowercased header name -> value
    bool valid = false;
};

// Parse a raw HTTP request buffer. Free function so it is testable in isolation.
HttpRequest parseRequest(const QByteArray &raw);

class ApiServer : public QObject
{
    Q_OBJECT
public:
    explicit ApiServer(AudioSourceCoordinator *coordinator,
                       MainWindow *window,
                       WebStateHub *webState,
                       SseBroker *sseBroker,
                       QObject *parent = nullptr);

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    struct Response {
        int status;
        QByteArray body;
        QByteArray contentType = "application/json";
    };

    Response route(const HttpRequest &req);
    bool handleMeta(const QString &path, const HttpRequest &req, Response &out);
    bool handleStatic(const QString &path, Response &out);
    bool handlePlaylist(const QString &path, const HttpRequest &req, Response &out);
    bool handleSources(const QString &path, const HttpRequest &req, Response &out);
    bool handleTransport(const QString &path, const HttpRequest &req, Response &out);   // Task 5
    bool handleScreensaver(const QString &path, const HttpRequest &req, Response &out); // Task 6
    bool authorized(const HttpRequest &req) const;
    void sendResponse(QTcpSocket *socket, const Response &resp);

    QTcpServer *m_server = nullptr;
    AudioSourceCoordinator *m_coordinator = nullptr;
    MainWindow *m_window = nullptr;
    WebStateHub *m_webState = nullptr;
    SseBroker *m_sseBroker = nullptr;
    QHash<QTcpSocket *, QByteArray> m_buffers;

    bool m_enabled = true;
    quint16 m_port = 8080;
    QString m_bindAddress = "0.0.0.0";
    QString m_token;
    int m_maxSseClients = 8;
};

#endif // APISERVER_H
