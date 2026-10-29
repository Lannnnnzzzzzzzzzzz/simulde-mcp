/***************************************************************************
 *   SimulIDE 1 remote-control addon — MCP over HTTP (port of the SimulIDE 2 addon).                      *
 ***************************************************************************/

#include "mcpserver.h"
#include "remoteserver.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QHostAddress>
#include <QDateTime>
#include <cstdio>

McpServer::McpServer( QObject* parent )
    : QObject( parent )
    , m_server( nullptr )
    , m_port( 0 )
{
}

McpServer::~McpServer() { stop(); }

bool McpServer::start( quint16 port )
{
    if( m_server ) return true;
    m_server = new QTcpServer( this );
    if( !m_server->listen( QHostAddress::LocalHost, port ) )
    {
        fprintf( stderr, "McpServer: failed to listen on 127.0.0.1:%u: %s\n"
               , port, m_server->errorString().toUtf8().constData() );
        delete m_server;
        m_server = nullptr;
        return false;
    }
    m_port = port;
    connect( m_server, &QTcpServer::newConnection, this, &McpServer::onConnection );
    fprintf( stderr, "McpServer: MCP endpoint at http://127.0.0.1:%u/mcp\n", port );
    return true;
}

void McpServer::stop()
{
    if( !m_server ) return;
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
    fprintf( stderr, "McpServer: stopped\n" );
}

bool McpServer::isActive() { return m_server && m_server->isListening(); }

// ---------------------------------------------------------------------------
// Minimal HTTP/1.1 handling (one request per connection)

void McpServer::onConnection()
{
    while( QTcpSocket* sock = m_server->nextPendingConnection() )
    {
        sock->setProperty( "buffer", QByteArray() );
        connect( sock, &QTcpSocket::readyRead, this, &McpServer::onSocketRead );
        connect( sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater );
    }
}

void McpServer::onSocketRead()
{
    QTcpSocket* sock = qobject_cast<QTcpSocket*>( sender() );
    if( !sock ) return;

    QByteArray buf = sock->property( "buffer" ).toByteArray() + sock->readAll();

    int headerEnd = buf.indexOf( "\r\n\r\n" );
    if( headerEnd < 0 ){ sock->setProperty( "buffer", buf ); return; }

    // Content-Length (case-insensitive scan of the header block)
    int contentLength = 0;
    QByteArray head = buf.left( headerEnd );
    for( const QByteArray& line : head.split( '\n' ) )
    {
        QByteArray l = line.trimmed();
        if( l.toLower().startsWith( "content-length:" ) )
            contentLength = l.mid( 15 ).trimmed().toInt();
    }
    if( buf.size() < headerEnd + 4 + contentLength )
    {
        sock->setProperty( "buffer", buf );   // wait for the rest of the body
        return;
    }
    handleRequest( sock, buf.left( headerEnd + 4 + contentLength ) );
}

void McpServer::sendResponse( QTcpSocket* sock, int status, const QByteArray& body,
                              const QByteArray& contentType )
{
    static const QMap<int, QByteArray> reasons = {
        { 200, "OK" }, { 202, "Accepted" }, { 400, "Bad Request" },
        { 404, "Not Found" }, { 405, "Method Not Allowed" },
    };
    QByteArray resp = "HTTP/1.1 " + QByteArray::number( status ) + " "
                    + reasons.value( status, "OK" ) + "\r\n"
                    + "Content-Type: " + contentType + "\r\n"
                    + "Content-Length: " + QByteArray::number( body.size() ) + "\r\n"
                    + "Access-Control-Allow-Origin: *\r\n"
                    + "Connection: close\r\n\r\n" + body;
    sock->write( resp );
    sock->flush();
    sock->disconnectFromHost();
}

void McpServer::handleRequest( QTcpSocket* sock, const QByteArray& raw )
{
    int headerEnd = raw.indexOf( "\r\n\r\n" );
    QByteArray head = raw.left( headerEnd );
    QByteArray body = raw.mid( headerEnd + 4 );

    QByteArray requestLine = head.left( head.indexOf( '\n' ) ).trimmed();
    QList<QByteArray> parts = requestLine.split( ' ' );
    QByteArray method = parts.value( 0 ).toUpper();
    QByteArray path   = parts.value( 1 );

    if( method == "OPTIONS" ){ sendResponse( sock, 202, "" ); return; }

    if( method == "GET" )   // No SSE stream: inform the client politely
    {
        sendResponse( sock, 405, R"({"jsonrpc":"2.0","error":{"code":-32000,"message":"SSE not supported; use POST with JSON responses"}})" );
        return;
    }
    if( method != "POST" || (path != "/mcp" && path != "/mcp/" && path != "/") )
    {
        sendResponse( sock, 404, R"({"error":"not found"})" );
        return;
    }

    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson( body, &perr );
    if( perr.error != QJsonParseError::NoError )
    {
        QJsonObject err{ { "jsonrpc", "2.0" }, { "id", QJsonValue() },
                         { "error", QJsonObject{ { "code", -32700 },
                                                 { "message", "Parse error" } } } };
        sendResponse( sock, 400, QJsonDocument( err ).toJson( QJsonDocument::Compact ) );
        return;
    }

    if( doc.isArray() )   // JSON-RPC batch
    {
        QJsonArray out;
        for( const QJsonValue& v : doc.array() )
        {
            bool isNotif = false;
            QByteArray r = handleRpc( v, isNotif );
            if( !isNotif ) out.append( QJsonDocument::fromJson( r ).object() );
        }
        if( out.isEmpty() ){ sendResponse( sock, 202, "" ); return; }
        sendResponse( sock, 200, QJsonDocument( out ).toJson( QJsonDocument::Compact ) );
        return;
    }

    bool isNotif = false;
    QByteArray resp = handleRpc( doc.object(), isNotif );
    if( isNotif ){ sendResponse( sock, 202, "" ); return; }
    sendResponse( sock, 200, resp );
}

// ---------------------------------------------------------------------------
// MCP protocol

QByteArray McpServer::handleRpc( const QJsonValue& msg, bool& isNotification )
{
    isNotification = false;
    QJsonObject req = msg.toObject();
    QJsonValue id = req.value( "id" );
    QString method  = req.value( "method" ).toString();

    if( id.isUndefined() ){ isNotification = true; return {}; }   // notification: no reply

    QJsonObject result;
    QJsonObject error;

    if( method == "initialize" )
    {
        QString clientVer = req.value( "params" ).toObject()
                            .value( "protocolVersion" ).toString();
        if( clientVer.isEmpty() ) clientVer = "2025-06-18";
        result = QJsonObject{
            { "protocolVersion", clientVer },
            { "capabilities", QJsonObject{ { "tools", QJsonObject{ { "listChanged", false } } } } },
            { "serverInfo", QJsonObject{ { "name", "simulide1" }, { "version", "1.1.0" } } },
            { "instructions",
              "Remote control for the SimulIDE 1 circuit simulator (.sim1 files). "
              "Build circuits (add_component, connect_pins, set_property), power "
              "the simulation (set_power), read voltages (read_voltage, read_nets) "
              "and take screenshots. Components are addressed by CircId "
              "(e.g. 'Resistor-1'); pins by full pin id ('Resistor-1-rPin') from "
              "get_pins. Structural edits require the simulation to be powered "
              "off (set_power on=false). MCU debug, waveform and serial tools of "
              "the SimulIDE 2 addon are not ported yet." },
        };
    }
    else if( method == "ping" ) result = QJsonObject();
    else if( method == "tools/list" )
    {
        extern QJsonArray mcpToolList();              // mcptools.cpp
        result = QJsonObject{ { "tools", mcpToolList() } };
    }
    else if( method == "tools/call" )
    {
        QJsonObject params = req.value( "params" ).toObject();
        QString name = params.value( "name" ).toString();
        QJsonObject args = params.value( "arguments" ).toObject();
        try {
            result = callTool( name, args );
        }
        catch( const QString& err ) {
            result = QJsonObject{
                { "isError", true },
                { "content", QJsonArray{ QJsonObject{
                    { "type", "text" }, { "text", err } } } },
            };
        }
    }
    else{
        error = QJsonObject{ { "code", -32601 },
                             { "message", "Method not found: " + method } };
    }

    QJsonObject resp{ { "jsonrpc", "2.0" }, { "id", id } };
    if( error.isEmpty() ) resp["result"] = result;
    else                  resp["error"]  = error;
    return QJsonDocument( resp ).toJson( QJsonDocument::Compact );
}

// ---------------------------------------------------------------------------
// Tool execution: map MCP tool names to RemoteServer commands

QJsonObject McpServer::callTool( const QString& name, const QJsonObject& args )
{
    extern QString mcpToolCommand( const QString& name );   // mcptools.cpp
    QString cmd = mcpToolCommand( name );
    if( cmd.isEmpty() ) throw QString( "unknown tool: '%1'" ).arg( name );
    if( !RemoteServer::self() ) throw QString( "remote server not initialized" );

    QJsonObject res = RemoteServer::self()->call( cmd, args );

    QJsonArray content;
    if( res.contains( "png_base64" ) )   // screenshot: return as image block
    {
        content.append( QJsonObject{ { "type", "image" },
                                     { "data", res.value( "png_base64" ).toString() },
                                     { "mimeType", "image/png" } } );
    }
    else if( res.contains( "xml" ) )     // circuit.xml: return bare XML text
    {
        content.append( QJsonObject{ { "type", "text" },
                                     { "text", res.value( "xml" ).toString() } } );
    }
    else{
        content.append( QJsonObject{ { "type", "text" },
            { "text", QString::fromUtf8( QJsonDocument( res ).toJson( QJsonDocument::Compact ) ) } } );
    }
    return QJsonObject{ { "content", content }, { "isError", false } };
}
