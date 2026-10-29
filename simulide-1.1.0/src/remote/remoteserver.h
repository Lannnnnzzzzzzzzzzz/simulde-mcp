/***************************************************************************
 *   SimulIDE 1 remote-control addon (port of the SimulIDE 2 MCP addon).   *
 *                                                                         *
 *   Exposes a newline-delimited JSON API over a local socket so that      *
 *   external tools (e.g. an MCP server) can drive SimulIDE the same way   *
 *   a user would: build circuits, set properties, wire pins, power the    *
 *   simulation and read back voltages.                                    *
 *                                                                         *
 *   Activated only when the environment variable SIMULIDE_REMOTE or       *
 *   SIMULIDE_REMOTE_SOCKET is set. Otherwise start() is a no-op.          *
 *                                                                         *
 *   Ported subset: circuit new/open/save/xml/components/wires, library,   *
 *   component add/remove/properties/pins, wiring, power/pause/state,      *
 *   pin voltage, circuit nets, canvas screenshot. MCU debug, waveform,    *
 *   serial and input tools of the v2 addon are NOT ported (yet).          *
 ***************************************************************************/

#pragma once

#include <QObject>
#include <QJsonObject>

class QLocalServer;
class QLocalSocket;
class Component;
class Pin;
class Connector;

class RemoteServer : public QObject
{
    Q_OBJECT

    public:
        RemoteServer( QObject* parent=nullptr );
        ~RemoteServer();

        // Starts listening if enabled via env vars. Safe to call always.
        bool start();

        bool isActive() { return m_server != nullptr; }
        QString socketPath() { return m_sockPath; }

        // Execute a command; returns the result object or throws a QString
        // error message. Used by both the socket path and the MCP HTTP server.
        QJsonObject call( const QString& cmd, const QJsonObject& args );

 static RemoteServer* self() { return m_pSelf; }

    private slots:
        void onNewConnection();
        void onReadyRead();
        void onDisconnected();

    private:
        QJsonObject dispatch( const QJsonObject& req );

        // Command handlers. Throw a QString with the error message on failure.
        QJsonObject cmdPing( const QJsonObject& args );
        QJsonObject cmdCircuitNew( const QJsonObject& args );
        QJsonObject cmdCircuitOpen( const QJsonObject& args );
        QJsonObject cmdCircuitSave( const QJsonObject& args );
        QJsonObject cmdCircuitXml( const QJsonObject& args );
        QJsonObject cmdCircuitComponents( const QJsonObject& args );
        QJsonObject cmdCircuitWires( const QJsonObject& args );
        QJsonObject cmdLibraryList( const QJsonObject& args );
        QJsonObject cmdCompAdd( const QJsonObject& args );
        QJsonObject cmdCompRemove( const QJsonObject& args );
        QJsonObject cmdCompSetProp( const QJsonObject& args );
        QJsonObject cmdCompGetProp( const QJsonObject& args );
        QJsonObject cmdCompProps( const QJsonObject& args );
        QJsonObject cmdCompPins( const QJsonObject& args );
        QJsonObject cmdWireAdd( const QJsonObject& args );
        QJsonObject cmdWireRemove( const QJsonObject& args );
        QJsonObject cmdSimPower( const QJsonObject& args );
        QJsonObject cmdSimPause( const QJsonObject& args );
        QJsonObject cmdSimState( const QJsonObject& args );
        QJsonObject cmdPinVoltage( const QJsonObject& args );
        QJsonObject cmdCircuitNets( const QJsonObject& args );
        QJsonObject cmdViewScreenshot( const QJsonObject& args );

        // Helpers
        Component* getComp( const QJsonObject& args );          // throws if missing
        Pin*       getPinArg( const QJsonObject& args, const char* key );
        void       requireStopped();                            // throws if sim running
        void       suppressModifiedDialog();                    // avoid modal save dialog
        QJsonObject compToJson( Component* comp );

        QLocalServer* m_server;
        QList<QLocalSocket*> m_clients;
        QString m_sockPath;

 static RemoteServer* m_pSelf;
};
