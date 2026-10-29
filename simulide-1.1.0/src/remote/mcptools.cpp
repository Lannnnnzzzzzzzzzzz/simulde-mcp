/***************************************************************************
 *   SimulIDE 1 remote-control addon — MCP tool registry (ported subset).  *
 *                                                                         *
 *   Each tool maps to a RemoteServer command ("cmd"). The arguments are   *
 *   passed through unchanged. Only the tools implemented by the           *
 *   SimulIDE 1 RemoteServer port are listed here.                         *
 ***************************************************************************/

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

// name, cmd, description, inputSchema (JSON Schema for the arguments object)
static const char* TOOLS_JSON = R"TOOLS([
{"name":"simulide_status","cmd":"ping",
 "description":"SimulIDE version, pid and currently open circuit.",
 "inputSchema":{"type":"object","properties":{}}},
{"name":"new_circuit","cmd":"circuit.new",
 "description":"Close the current circuit and start a new empty one. Unsaved changes are discarded unless save_first is true.",
 "inputSchema":{"type":"object","properties":{"save_first":{"type":"boolean"}}}},
{"name":"open_circuit","cmd":"circuit.open",
 "description":"Open a .sim1 circuit file (replaces the current circuit). Unsaved changes are discarded unless save_first is true.",
 "inputSchema":{"type":"object","properties":{"path":{"type":"string","description":"Absolute path to the .sim1 file"},"save_first":{"type":"boolean"}},"required":["path"]}},
{"name":"save_circuit","cmd":"circuit.save",
 "description":"Save the current circuit. Uses the current file path if none is given (.sim1 appended if missing).",
 "inputSchema":{"type":"object","properties":{"path":{"type":"string"}}}},
{"name":"get_circuit_xml","cmd":"circuit.xml",
 "description":"Return the whole circuit serialized as .sim1 XML text.",
 "inputSchema":{"type":"object","properties":{}}},
{"name":"list_components","cmd":"circuit.components",
 "description":"List all components in the current circuit (id, type, label, position, rotation, flips).",
 "inputSchema":{"type":"object","properties":{}}},
{"name":"list_wires","cmd":"circuit.wires",
 "description":"List all wires/connectors (id, start pin id, end pin id, points).",
 "inputSchema":{"type":"object","properties":{}}},
{"name":"list_library","cmd":"library.list",
 "description":"List all component types available in SimulIDE's library (name, category, type for add_component).",
 "inputSchema":{"type":"object","properties":{}}},
{"name":"add_component","cmd":"comp.add",
 "description":"Place a new component on the circuit (simulation must be off). The resulting CircId '<name>-<n>' is returned as 'id'. Type 'Node' creates a junction node (3 pins '<id>-0/1/2') for branching a net - in SimulIDE a pin accepts only one wire, so branches must go through a Node.",
 "inputSchema":{"type":"object","properties":{
   "type":{"type":"string","description":"Library type, e.g. 'Resistor', 'Led', 'Battery', or 'Node'"},
   "x":{"type":"integer"},"y":{"type":"integer"},
   "name":{"type":"string","description":"Base for the component id; defaults to type"},
   "props":{"type":"object","description":"Optional {property: value} map, e.g. {\"Resistance\": \"1 k\"}"}},
  "required":["type"]}},
{"name":"remove_component","cmd":"comp.remove",
 "description":"Remove a component (and its connections) from the circuit (simulation must be off).",
 "inputSchema":{"type":"object","properties":{"id":{"type":"string","description":"Component CircId, e.g. 'Resistor-1'"}},"required":["id"]}},
{"name":"set_property","cmd":"comp.set_prop",
 "description":"Set a component property. Values accept units as shown in the GUI ('1 k', '100 n', '5 V', '16 MHz').",
 "inputSchema":{"type":"object","properties":{
   "id":{"type":"string"},"prop":{"type":"string"},"value":{"type":"string"}},
  "required":["id","prop","value"]}},
{"name":"get_property","cmd":"comp.get_prop",
 "description":"Get one property value of a component.",
 "inputSchema":{"type":"object","properties":{"id":{"type":"string"},"prop":{"type":"string"}},"required":["id","prop"]}},
{"name":"get_properties","cmd":"comp.props",
 "description":"Get all properties (name: value) of a component - use to discover settable property names.",
 "inputSchema":{"type":"object","properties":{"id":{"type":"string"}},"required":["id"]}},
{"name":"get_pins","cmd":"comp.pins",
 "description":"List a component's pins with full pin ids (for connect_pins), voltage and connection state.",
 "inputSchema":{"type":"object","properties":{"id":{"type":"string"}},"required":["id"]}},
{"name":"connect_pins","cmd":"wire.add",
 "description":"Connect two pins with a wire, e.g. connect_pins('Resistor-1-rPin', 'Battery-2-lPin') (simulation must be off). Pin ids come from get_pins.",
 "inputSchema":{"type":"object","properties":{"start":{"type":"string"},"end":{"type":"string"}},"required":["start","end"]}},
{"name":"remove_wire","cmd":"wire.remove",
 "description":"Remove a wire by its connector id (from list_wires; simulation must be off).",
 "inputSchema":{"type":"object","properties":{"id":{"type":"string"}},"required":["id"]}},
{"name":"set_power","cmd":"sim.power",
 "description":"Power the simulation on (start) or off (stop/reset) - the toolbar power button.",
 "inputSchema":{"type":"object","properties":{"on":{"type":"boolean"}},"required":["on"]}},
{"name":"set_pause","cmd":"sim.pause",
 "description":"Pause or resume the running simulation.",
 "inputSchema":{"type":"object","properties":{"on":{"type":"boolean"}},"required":["on"]}},
{"name":"get_sim_state","cmd":"sim.state",
 "description":"Simulation state: running/paused flags, state name, circuit time (ps), speed, real speed (%).",
 "inputSchema":{"type":"object","properties":{}}},
{"name":"read_voltage","cmd":"pin.voltage",
 "description":"Read the voltage at a pin (any pin id from get_pins). Pause the simulation first for a consistent value.",
 "inputSchema":{"type":"object","properties":{"pin":{"type":"string"}},"required":["pin"]}},
{"name":"read_nets","cmd":"circuit.nets",
 "description":"Dump every wire/net with its voltage and logic state (high/low, only while running). SimulIDE 1 connectors do not track current, so no current values are reported.",
 "inputSchema":{"type":"object","properties":{}}},
{"name":"screenshot","cmd":"view.screenshot",
 "description":"Render the circuit canvas to a PNG image. Whole circuit by default; pass 'component' to crop to one component or 'area' for a scene rectangle.",
 "inputSchema":{"type":"object","properties":{
   "scale":{"type":"number"},
   "component":{"type":"string","description":"Crop to this component's bounding box"},
   "area":{"type":"object","description":"Crop to scene rect {x,y,w,h}"}}}}
])TOOLS";

QJsonArray mcpToolList()
{
    static QJsonArray tools = []()
    {
        QJsonArray out;
        QJsonDocument doc = QJsonDocument::fromJson( TOOLS_JSON );
        for( const QJsonValue& v : doc.array() )
        {
            QJsonObject t = v.toObject();
            t.remove( "cmd" );
            out.append( t );
        }
        return out;
    }();
    return tools;
}

QString mcpToolCommand( const QString& name )
{
    static QJsonDocument doc = QJsonDocument::fromJson( TOOLS_JSON );
    for( const QJsonValue& v : doc.array() )
    {
        QJsonObject t = v.toObject();
        if( t.value( "name" ).toString() == name )
            return t.value( "cmd" ).toString();
    }
    return QString();
}
