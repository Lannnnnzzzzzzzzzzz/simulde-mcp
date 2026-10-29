/***************************************************************************
 *   SimulIDE 2 remote-control addon.                                      *
 *                                                                         *
 *   MCP (Model Context Protocol) server over HTTP (Streamable HTTP        *
 *   transport, single-endpoint JSON mode). Lets AI clients drive           *
 *   SimulIDE through the RemoteServer command dispatcher.                  *
 *                                                                         *
 *   - Binds to 127.0.0.1 only.                                            *
 *   - Stateless: no session management; every request is independent.     *
 *   - Endpoint: POST /mcp  (JSON-RPC 2.0 body, JSON response)             *
 ***************************************************************************/

#pragma once

#include <QObject>
#include <QJsonObject>

class QTcpServer;
class QTcpSocket;

class McpServer : public QObject
{
    Q_OBJECT

    public:
        McpServer( QObject* parent=nullptr );
        ~McpServer();

        bool start( quint16 port );
        void stop();

        bool isActive();
        quint16 port() { return m_port; }

    private slots:
        void onConnection();
        void onSocketRead();

    private:
        void handleRequest( QTcpSocket* sock, const QByteArray& raw );
        QByteArray handleRpc( const QJsonValue& msg, bool& isNotification );
        QJsonObject callTool( const QString& name, const QJsonObject& args );
        void sendResponse( QTcpSocket* sock, int status, const QByteArray& body,
                           const QByteArray& contentType="application/json" );

        QTcpServer* m_server;
        quint16 m_port;
};
