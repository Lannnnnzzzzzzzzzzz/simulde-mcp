/***************************************************************************
 *   SimulIDE 1 remote-control addon (port of the SimulIDE 2 MCP addon).   *
 *                                                                         *
 *   Newline-delimited JSON API over a QLocalServer. Each request is one   *
 *   JSON object per line:  {"id": 1, "cmd": "sim.state", "args": {...}}   *
 *   Each response is one JSON object per line:                            *
 *   {"id": 1, "ok": true, "result": {...}}  or  {"ok": false, "error":}   *
 *                                                                         *
 *   Everything runs in the GUI thread, so all calls are safe to make      *
 *   on the Circuit/Simulator singletons.                                  *
 *                                                                         *
 *   Differences from the SimulIDE 2 version:                              *
 *   - Components are created with Circuit::createItem()+addComponent()    *
 *     (there is no createComponent() in SimulIDE 1).                      *
 *   - There is no Circuit::getPin(id) registry getter: pins are found     *
 *     by scanning the component list.                                     *
 *   - Circuit::circuitToString() is private: get_circuit_xml saves to     *
 *     a temporary file and reads it back.                                 *
 *   - Connectors have no getCurrent(): read_nets reports voltage only.    *
 *   - Circuit files are .sim1.                                            *
 ***************************************************************************/

#include "remoteserver.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCoreApplication>
#include <QApplication>
#include <QTimer>
#include <QImage>
#include <QPainter>
#include <QBuffer>
#include <QFile>
#include <QDir>
#include <cstdio>

#include "circuitwidget.h"
#include "circuit.h"
#include "mainwindow.h"
#include "simulator.h"
#include "component.h"
#include "connector.h"
#include "node.h"
#include "pin.h"
#include "comproperty.h"
#include "itemlibrary.h"

RemoteServer* RemoteServer::m_pSelf = nullptr;

RemoteServer::RemoteServer( QObject* parent )
    : QObject( parent )
    , m_server( nullptr )
{
    m_pSelf = this;
}

RemoteServer::~RemoteServer()
{
    if( m_server ){
        m_server->close();
        QLocalServer::removeServer( m_sockPath );
    }
}

bool RemoteServer::start()
{
    QString path = qEnvironmentVariable( "SIMULIDE_REMOTE_SOCKET" );
    QString ena  = qEnvironmentVariable( "SIMULIDE_REMOTE" );

    if( path.isEmpty() )
    {
        if( ena.isEmpty() || ena == "0" || ena == "false" ) return false;
        if( ena == "1" || ena == "true" || ena == "yes" )
            path = QString( "/tmp/simulide1-remote-%1.sock" ).arg( qEnvironmentVariable( "USER", "user" ) );
        else
            path = ena;   // SIMULIDE_REMOTE=<path> also works
    }

    QLocalServer::removeServer( path );   // clean up stale socket

    m_server = new QLocalServer( this );
    if( !m_server->listen( path ) )
    {
        fprintf( stderr, "RemoteServer: failed to listen on %s: %s\n"
               , path.toUtf8().constData(), m_server->errorString().toUtf8().constData() );
        delete m_server;
        m_server = nullptr;
        return false;
    }
    m_sockPath = path;
    connect( m_server, &QLocalServer::newConnection, this, &RemoteServer::onNewConnection );
    fprintf( stderr, "RemoteServer: listening on %s\n", path.toUtf8().constData() );
    return true;
}

void RemoteServer::onNewConnection()
{
    while( QLocalSocket* client = m_server->nextPendingConnection() )
    {
        connect( client, &QLocalSocket::readyRead,    this, &RemoteServer::onReadyRead );
        connect( client, &QLocalSocket::disconnected, this, &RemoteServer::onDisconnected );
        m_clients.append( client );
        client->setProperty( "buffer", QByteArray() );
    }
}

void RemoteServer::onDisconnected()
{
    QLocalSocket* client = qobject_cast<QLocalSocket*>( sender() );
    if( !client ) return;
    m_clients.removeAll( client );
    client->deleteLater();
}

void RemoteServer::onReadyRead()
{
    QLocalSocket* client = qobject_cast<QLocalSocket*>( sender() );
    if( !client ) return;

    QByteArray buf = client->property( "buffer" ).toByteArray() + client->readAll();

    int nl;
    while( (nl = buf.indexOf( '\n' )) >= 0 )
    {
        QByteArray line = buf.left( nl ).trimmed();
        buf.remove( 0, nl+1 );
        if( line.isEmpty() ) continue;

        QJsonParseError perr;
        QJsonDocument doc = QJsonDocument::fromJson( line, &perr );
        QJsonObject resp;
        if( perr.error != QJsonParseError::NoError || !doc.isObject() )
        {
            resp["ok"]    = false;
            resp["error"] = "invalid JSON: " + perr.errorString();
        }
        else{
            QJsonObject req = doc.object();
            resp = dispatch( req );
            resp["id"] = req.value( "id" );
        }
        client->write( QJsonDocument( resp ).toJson( QJsonDocument::Compact ) + "\n" );
        client->flush();
    }
    client->setProperty( "buffer", buf );
}

QJsonObject RemoteServer::dispatch( const QJsonObject& req )
{
    static const QMap<QString, QJsonObject(RemoteServer::*)( const QJsonObject& )> handlers = {
        { "ping",               &RemoteServer::cmdPing },
        { "circuit.new",        &RemoteServer::cmdCircuitNew },
        { "circuit.open",       &RemoteServer::cmdCircuitOpen },
        { "circuit.save",       &RemoteServer::cmdCircuitSave },
        { "circuit.xml",        &RemoteServer::cmdCircuitXml },
        { "circuit.components", &RemoteServer::cmdCircuitComponents },
        { "circuit.wires",      &RemoteServer::cmdCircuitWires },
        { "library.list",       &RemoteServer::cmdLibraryList },
        { "comp.add",           &RemoteServer::cmdCompAdd },
        { "comp.remove",        &RemoteServer::cmdCompRemove },
        { "comp.set_prop",      &RemoteServer::cmdCompSetProp },
        { "comp.get_prop",      &RemoteServer::cmdCompGetProp },
        { "comp.props",         &RemoteServer::cmdCompProps },
        { "comp.pins",          &RemoteServer::cmdCompPins },
        { "wire.add",           &RemoteServer::cmdWireAdd },
        { "wire.remove",        &RemoteServer::cmdWireRemove },
        { "sim.power",          &RemoteServer::cmdSimPower },
        { "sim.pause",          &RemoteServer::cmdSimPause },
        { "sim.state",          &RemoteServer::cmdSimState },
        { "pin.voltage",        &RemoteServer::cmdPinVoltage },
        { "circuit.nets",       &RemoteServer::cmdCircuitNets },
        { "view.screenshot",    &RemoteServer::cmdViewScreenshot },
    };

    QJsonObject result;
    QString cmd = req.value( "cmd" ).toString();
    auto it = handlers.constFind( cmd );

    try {
        if( it == handlers.constEnd() ) throw QString( "unknown command: '%1'" ).arg( cmd );
        result = (this->*(it.value()))( req.value( "args" ).toObject() );
    }
    catch( const QString& err )
    {
        return QJsonObject{ { "ok", false }, { "error", err } };
    }
    catch( ... )
    {
        return QJsonObject{ { "ok", false }, { "error", "internal error in " + cmd } };
    }
    return QJsonObject{ { "ok", true }, { "result", result } };
}

QJsonObject RemoteServer::call( const QString& cmd, const QJsonObject& args )
{
    QJsonObject resp = dispatch( QJsonObject{ { "cmd", cmd }, { "args", args } } );
    if( !resp.value( "ok" ).toBool() )
        throw resp.value( "error" ).toString( "unknown error" );
    return resp.value( "result" ).toObject();
}

// ---------------------------------------------------------------------------
// Helpers

Component* RemoteServer::getComp( const QJsonObject& args )
{
    QString id = args.value( "id" ).toString();
    if( id.isEmpty() ) throw QString( "missing 'id' argument" );
    Component* comp = Circuit::self()->getCompById( id );
    if( !comp )   // Junction nodes loaded from a file may not be in the comp map
        for( Node* node : *Circuit::self()->nodeList() )
            if( node->getUid() == id ) { comp = node; break; }
    if( !comp ) throw QString( "component not found: '%1'" ).arg( id );
    return comp;
}

Pin* RemoteServer::getPinArg( const QJsonObject& args, const char* key )
{
    QString id = args.value( key ).toString();
    if( id.isEmpty() ) throw QString( "missing '%1' argument" ).arg( key );
    // SimulIDE 1 has no pin registry getter: scan components, then nodes
    // (junction Nodes are Components but live in their own list).
    for( Component* comp : *Circuit::self()->compList() )
        for( Pin* pin : comp->getPins() )
            if( pin && pin->pinId() == id ) return pin;
    for( Node* node : *Circuit::self()->nodeList() )
        for( Pin* pin : node->getPins() )
            if( pin && pin->pinId() == id ) return pin;
    throw QString( "pin not found: '%1'" ).arg( id );
}

void RemoteServer::requireStopped()
{
    if( Simulator::self() && Simulator::self()->isRunning() )
        throw QString( "simulation is running; power it off first (sim.power off)" );
}

// Clearing the circuit pops a modal "save changes?" dialog when the title has
// the modified marker '*'; that would hang the remote call. Clear the marker
// first so the GUI proceeds without user interaction.
void RemoteServer::suppressModifiedDialog()
{
    QString title = MainWindow::self()->windowTitle();
    if( title.endsWith( "*" ) ) MainWindow::self()->setWindowTitle( title.chopped( 1 ) );
}

QJsonObject RemoteServer::compToJson( Component* comp )
{
    return QJsonObject{
        { "id",    comp->getUid() },
        { "type",  comp->itemType() },
        { "label", comp->idLabel() },
        { "x",     comp->pos().x() },
        { "y",     comp->pos().y() },
        { "rotation", comp->getAngle() },
        { "hflip", comp->hflip() },
        { "vflip", comp->vflip() },
    };
}

// ---------------------------------------------------------------------------
// Commands

QJsonObject RemoteServer::cmdPing( const QJsonObject& )
{
    return QJsonObject{
        { "app",     "SimulIDE" },
        { "version", QString( APP_VERSION ) },
        { "pid",     QCoreApplication::applicationPid() },
        { "socket",  m_sockPath },
        { "circuit", Circuit::self() ? Circuit::self()->getFilePath() : "" },
    };
}

QJsonObject RemoteServer::cmdCircuitNew( const QJsonObject& args )
{
    // newCircuit() powers off itself; no requireStopped here.
    if( args.value( "save_first" ).toBool( false ) ) cmdCircuitSave( args );
    suppressModifiedDialog();
    CircuitWidget::self()->newCircuit();
    return QJsonObject{ { "done", true } };
}

QJsonObject RemoteServer::cmdCircuitOpen( const QJsonObject& args )
{
    QString path = args.value( "path" ).toString();
    if( path.isEmpty() ) throw QString( "missing 'path' argument" );
    if( !QFile::exists( path ) ) throw QString( "file not found: " + path );
    if( args.value( "save_first" ).toBool( false ) ) cmdCircuitSave( args );
    suppressModifiedDialog();
    CircuitWidget::self()->loadCirc( path );
    return QJsonObject{ { "path", Circuit::self()->getFilePath() } };
}

QJsonObject RemoteServer::cmdCircuitSave( const QJsonObject& args )
{
    QString path = args.value( "path" ).toString();
    if( path.isEmpty() ) path = Circuit::self()->getFilePath();
    if( path.isEmpty() ) throw QString( "no file path; pass 'path' (circuit was never saved)" );
    if( !path.endsWith( ".sim1" ) ) path += ".sim1";
    if( !Circuit::self()->saveCircuit( path ) ) throw QString( "failed to save: " + path );
    Circuit::self()->setFilePath( path );
    return QJsonObject{ { "path", path } };
}

QJsonObject RemoteServer::cmdCircuitXml( const QJsonObject& )
{
    // circuitToString() is private in SimulIDE 1: serialize through a
    // temporary file instead, then restore the circuit's own file path.
    QString oldPath = Circuit::self()->getFilePath();
    QString tmpPath = QDir::temp().filePath( "simulide1-remote-circuit.sim1" );
    if( !Circuit::self()->saveCircuit( tmpPath ) )
        throw QString( "failed to serialize circuit" );
    Circuit::self()->setFilePath( oldPath );

    QFile file( tmpPath );
    if( !file.open( QIODevice::ReadOnly ) )
        throw QString( "failed to read serialized circuit" );
    QString xml = QString::fromUtf8( file.readAll() );
    file.close();
    QFile::remove( tmpPath );
    return QJsonObject{ { "xml", xml } };
}

QJsonObject RemoteServer::cmdCircuitComponents( const QJsonObject& )
{
    QJsonArray list;
    for( Component* comp : *Circuit::self()->compList() )
        list.append( compToJson( comp ) );
    for( Node* node : *Circuit::self()->nodeList() )   // .sim1 files list Nodes as items too
        list.append( compToJson( node ) );
    return QJsonObject{ { "components", list } };
}

QJsonObject RemoteServer::cmdCircuitWires( const QJsonObject& )
{
    QJsonArray list;
    for( Connector* conn : *Circuit::self()->conList() )
    {
        list.append( QJsonObject{
            { "id",     conn->getUid() },
            { "start",  conn->startPinId() },
            { "end",    conn->endPinId() },
            { "points", QJsonArray::fromStringList( conn->pointList() ) },
        } );
    }
    return QJsonObject{ { "wires", list } };
}

QJsonObject RemoteServer::cmdLibraryList( const QJsonObject& )
{
    QJsonArray list;
    for( LibraryItem* item : ItemLibrary::self()->items() )
    {
        list.append( QJsonObject{
            { "name",     item->name() },
            { "category", item->category() },
            { "type",     item->type() },
        } );
    }
    return QJsonObject{ { "library", list } };
}

QJsonObject RemoteServer::cmdCompAdd( const QJsonObject& args )
{
    requireStopped();
    QString type = args.value( "type" ).toString();
    if( type.isEmpty() ) throw QString( "missing 'type' argument" );
    QString name = args.value( "name" ).toString();
    if( name.isEmpty() ) name = type;
    int x = args.value( "x" ).toInt( 0 );
    int y = args.value( "y" ).toInt( 0 );

    // SimulIDE 1 creation flow (mirrors CircuitView's drop handling):
    // the caller composes the CircId as "<name>-<seq>".
    QString id = name + "-" + Circuit::self()->newSceneId();
    Component* comp = nullptr;
    if( type == "Node" )   // Junction nodes are not in the item library:
    {                      // the GUI/loader constructs them directly.
        Node* joint = new Node( type, id );
        Circuit::self()->addNode( joint );
        comp = joint;
    }
    else{
        comp = Circuit::self()->createItem( type, id );
        if( !comp ) throw QString( "failed to create component of type '%1'" ).arg( type );
        Circuit::self()->addComponent( comp );
        Circuit::self()->saveCompChange( comp->getUid(), COMP_STATE_NEW, "" );
    }
    comp->setPos( x, y );

    QJsonArray propErrors;
    QJsonObject props = args.value( "props" ).toObject();
    for( auto it = props.constBegin(); it != props.constEnd(); ++it )
        if( !comp->setPropStr( it.key(), it.value().toString() ) )
            propErrors.append( it.key() );

    QJsonObject res = compToJson( comp );
    if( !propErrors.isEmpty() ) res["prop_errors"] = propErrors;
    return res;
}

QJsonObject RemoteServer::cmdCompRemove( const QJsonObject& args )
{
    requireStopped();
    Component* comp = getComp( args );
    Circuit::self()->beginUndoStep();
    Circuit::self()->removeComp( comp );
    Circuit::self()->endUndoStep();
    return QJsonObject{ { "done", true } };
}

QJsonObject RemoteServer::cmdCompSetProp( const QJsonObject& args )
{
    Component* comp = getComp( args );
    QString prop = args.value( "prop" ).toString();
    if( prop.isEmpty() ) throw QString( "missing 'prop' argument" );
    QString val = args.value( "value" ).toString();

    if( !comp->setPropStr( prop, val ) )
        throw QString( "property '%1' rejected by %2 (check name with comp.props)" )
                .arg( prop, comp->getUid() );
    return QJsonObject{ { "id", comp->getUid() }, { "prop", prop },
                        { "value", comp->getPropStr( prop ) } };
}

QJsonObject RemoteServer::cmdCompGetProp( const QJsonObject& args )
{
    Component* comp = getComp( args );
    QString prop = args.value( "prop" ).toString();
    if( prop.isEmpty() ) throw QString( "missing 'prop' argument" );
    return QJsonObject{ { "id", comp->getUid() }, { "prop", prop },
                        { "value", comp->getPropStr( prop ) } };
}

QJsonObject RemoteServer::cmdCompProps( const QJsonObject& args )
{
    Component* comp = getComp( args );
    QJsonObject props;
    for( const propGroup& group : *comp->properties() )
        for( ComProperty* p : group.propList )
            props[p->name()] = p->getValStr();
    return QJsonObject{ { "id", comp->getUid() }, { "props", props } };
}

QJsonObject RemoteServer::cmdCompPins( const QJsonObject& args )
{
    Component* comp = getComp( args );
    QJsonArray pins;
    for( Pin* pin : comp->getPins() )
    {
        if( !pin ) continue;
        pins.append( QJsonObject{
            { "pin",       pin->pinId() },
            { "voltage",   pin->getVoltage() },
            { "connected", pin->isConnected() },
        } );
    }
    return QJsonObject{ { "id", comp->getUid() }, { "pins", pins } };
}

QJsonObject RemoteServer::cmdWireAdd( const QJsonObject& args )
{
    requireStopped();
    Pin* start = getPinArg( args, "start" );
    Pin* end   = getPinArg( args, "end" );

    Circuit::self()->newconnector( start, true );
    Circuit::self()->closeconnector( end, true );

    Connector* conn = start->connector();
    if( !conn ) throw QString( "connection failed" );
    return QJsonObject{ { "id", conn->getUid() },
                        { "start", conn->startPinId() },
                        { "end",   conn->endPinId() } };
}

QJsonObject RemoteServer::cmdWireRemove( const QJsonObject& args )
{
    requireStopped();
    QString id = args.value( "id" ).toString();
    for( Connector* conn : *Circuit::self()->conList() )
        if( conn->getUid() == id )
        {
            Circuit::self()->beginUndoStep();
            Circuit::self()->removeConnector( conn );
            Circuit::self()->endUndoStep();
            return QJsonObject{ { "done", true } };
        }
    throw QString( "wire not found: '%1'" ).arg( id );
}

QJsonObject RemoteServer::cmdSimPower( const QJsonObject& args )
{
    bool on = args.value( "on" ).toBool( true );
    if( on ) CircuitWidget::self()->powerCircOn();
    else     CircuitWidget::self()->powerCircOff();
    return cmdSimState( QJsonObject() );
}

QJsonObject RemoteServer::cmdSimPause( const QJsonObject& args )
{
    bool pause = args.value( "on" ).toBool( true );
    if( pause ) Simulator::self()->pauseSim();
    else        Simulator::self()->resumeSim();
    return cmdSimState( QJsonObject() );
}

QJsonObject RemoteServer::cmdSimState( const QJsonObject& )
{
    Simulator* sim = Simulator::self();
    static const QMap<int, QString> stateNames = {
        { SIM_STOPPED, "stopped" }, { SIM_ERROR,   "error" },
        { SIM_STARTING,"starting" },{ SIM_PAUSED,  "paused" },
        { SIM_WAITING, "waiting" }, { SIM_RUNNING, "running" },
        { SIM_DEBUG,   "debug" },
    };
    return QJsonObject{
        { "running",    sim->isRunning() },
        { "paused",     sim->isPaused() },
        { "state",      stateNames.value( (int)sim->simState(), "unknown" ) },
        { "time_ps",    QString::number( sim->circTime() ) },   // u64 as string: JSON doubles lose precision
        { "ps_per_sec", QString::number( sim->psPerSec() ) },
        { "real_speed", sim->realSpeed() },
    };
}

QJsonObject RemoteServer::cmdPinVoltage( const QJsonObject& args )
{
    Pin* pin = getPinArg( args, "pin" );
    return QJsonObject{ { "pin", pin->pinId() }, { "voltage", pin->getVoltage() } };
}

QJsonObject RemoteServer::cmdCircuitNets( const QJsonObject& )
{
    Circuit* circ = Circuit::self();
    bool running = Simulator::self()->isRunning();

    QJsonArray nets;
    for( Connector* conn : *circ->conList() )
    {
        double volt = conn->getVoltage();
        QJsonObject net{
            { "id",      conn->getUid() },
            { "start",   conn->startPinId() },
            { "end",     conn->endPinId() },
            { "points",  QJsonArray::fromStringList( conn->pointList() ) },
            { "bus",     conn->isBus() },
            { "voltage", volt },
            // SimulIDE 1 connectors do not track current: no "current" field.
        };
        if( running ) net["logic"] = volt > 2.5 ? "high" : "low";
        nets.append( net );
    }
    return QJsonObject{
        { "nets",    nets },
        { "running", running },
    };
}

QJsonObject RemoteServer::cmdViewScreenshot( const QJsonObject& args )
{
    Circuit* circ = Circuit::self();
    QRectF rect;
    if( args.contains( "component" ) )          // crop to one component
    {
        QString cid = args.value( "component" ).toString();
        Component* comp = Circuit::self()->getCompById( cid );
        if( !comp ) throw QString( "component not found: '%1'" ).arg( cid );
        rect = comp->sceneBoundingRect().adjusted( -40, -40, 40, 40 );
    }
    else if( args.contains( "area" ) )          // crop to scene rect {x,y,w,h}
    {
        QJsonObject a = args.value( "area" ).toObject();
        rect = QRectF( a.value("x").toDouble(), a.value("y").toDouble(),
                       a.value("w").toDouble( 100 ), a.value("h").toDouble( 100 ) );
    }
    else{
        rect = circ->itemsBoundingRect().adjusted( -60, -60, 60, 60 );
        if( rect.width() < 100 || rect.height() < 100 ) rect = QRectF( -400, -300, 800, 600 );
    }

    double scale = args.value( "scale" ).toDouble( 2.0 );
    double maxDim = 4096.0;
    if( rect.width() * scale > maxDim ) scale = maxDim / rect.width();
    if( rect.height() * scale > maxDim ) scale = maxDim / rect.height();

    QImage img( ( rect.size() * scale ).toSize(), QImage::Format_ARGB32 );
    img.fill( QColor( "#202020" ) );
    QPainter painter( &img );
    painter.setRenderHint( QPainter::Antialiasing );
    circ->render( &painter, QRectF( QPointF( 0, 0 ), rect.size() * scale ), rect );
    painter.end();

    QByteArray bytes;
    QBuffer buffer( &bytes );
    buffer.open( QIODevice::WriteOnly );
    img.save( &buffer, "PNG" );

    return QJsonObject{ { "png_base64", QString::fromLatin1( bytes.toBase64() ) },
                        { "width", img.width() }, { "height", img.height() } };
}
