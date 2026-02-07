/**************************************************************************/
/*  godot_mcp_server.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "godot_mcp_server.h"

#include "../util/mcp_scene_serializer.h"
#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/ip_address.h"
#include "core/io/resource_loader.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "main/main.h"
#include "scene/main/node.h"
#include "servers/display/display_server.h"

#ifdef TOOLS_ENABLED
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_log.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/run/game_view_plugin.h"
#include "scene/debugger/scene_debugger.h"
#endif

const char *GodotMCPServer::PROTOCOL_VERSION = "2024-11-05";

// Resource types safe for instantiation via MCP
const HashSet<String> GodotMCPServer::ALLOWED_RESOURCE_TYPES = {
	// Primitive meshes
	"BoxMesh", "SphereMesh", "CylinderMesh", "CapsuleMesh", "PlaneMesh",
	"PrismMesh", "TorusMesh", "PointMesh", "QuadMesh", "TextMesh",
	// Materials
	"StandardMaterial3D", "ORMMaterial3D", "ShaderMaterial",
	// Shapes (3D)
	"BoxShape3D", "SphereShape3D", "CapsuleShape3D", "CylinderShape3D",
	"ConvexPolygonShape3D", "ConcavePolygonShape3D", "WorldBoundaryShape3D",
	"HeightMapShape3D", "SeparationRayShape3D",
	// Shapes (2D)
	"RectangleShape2D", "CircleShape2D", "CapsuleShape2D",
	"ConvexPolygonShape2D", "ConcavePolygonShape2D", "SegmentShape2D",
	"SeparationRayShape2D", "WorldBoundaryShape2D",
	// Other safe resources
	"Gradient", "Curve", "Curve2D", "Curve3D",
	"Environment", "Sky", "PhysicsMaterial",
	"ProceduralSkyMaterial", "PanoramaSkyMaterial", "PhysicalSkyMaterial",
	"StyleBoxFlat", "StyleBoxLine", "StyleBoxEmpty",
	"LabelSettings", "FontVariation",
};

void GodotMCPServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start", "port", "host"), &GodotMCPServer::start, DEFVAL(DEFAULT_PORT), DEFVAL("127.0.0.1"));
	ClassDB::bind_method(D_METHOD("stop"), &GodotMCPServer::stop);
	ClassDB::bind_method(D_METHOD("poll"), &GodotMCPServer::poll);
	ClassDB::bind_method(D_METHOD("is_running"), &GodotMCPServer::is_running);
	ClassDB::bind_method(D_METHOD("get_port"), &GodotMCPServer::get_port);
	ClassDB::bind_method(D_METHOD("get_client_count"), &GodotMCPServer::get_client_count);

	ClassDB::bind_method(D_METHOD("_write_script_file", "path", "content"), &GodotMCPServer::_write_script_file);
	ClassDB::bind_method(D_METHOD("_delete_script_file", "path"), &GodotMCPServer::_delete_script_file);
	ClassDB::bind_method(D_METHOD("_attach_script_to_node", "node_path", "script_path"), &GodotMCPServer::_attach_script_to_node);
	ClassDB::bind_method(D_METHOD("_detach_script_from_node", "node_path"), &GodotMCPServer::_detach_script_from_node);
	ClassDB::bind_method(D_METHOD("_on_screenshot_captured", "width", "height", "path", "rect"), &GodotMCPServer::_on_screenshot_captured);
	ClassDB::bind_method(D_METHOD("_on_debugger_output", "msg", "level"), &GodotMCPServer::_on_debugger_output);

	ADD_SIGNAL(MethodInfo("tool_called", PropertyInfo(Variant::STRING, "tool_name"), PropertyInfo(Variant::DICTIONARY, "args")));
	ADD_SIGNAL(MethodInfo("client_connected", PropertyInfo(Variant::INT, "client_id")));
	ADD_SIGNAL(MethodInfo("client_disconnected", PropertyInfo(Variant::INT, "client_id")));
}

void GodotMCPServer::_on_screenshot_captured(int p_width, int p_height, const String &p_path, const Rect2i &p_rect) {
	pending_screenshot.completed = true;
	pending_screenshot.width = p_width;
	pending_screenshot.height = p_height;
	pending_screenshot.file_path = p_path;
}

void GodotMCPServer::_on_debugger_output(const String &p_msg, int p_level) {
#ifdef TOOLS_ENABLED
	OutputMessage msg;
	msg.text = p_msg;
	msg.timestamp = Time::get_singleton()->get_unix_time_from_system();

	// Map EditorLog::MessageType to type strings.
	if (p_level == EditorLog::MSG_TYPE_ERROR) {
		msg.type = "error";
	} else if (p_level == EditorLog::MSG_TYPE_WARNING) {
		msg.type = "warning";
	} else {
		msg.type = "log";
	}

	output_buffer.push_back(msg);

	// Trim buffer to max size.
	while (output_buffer.size() > MAX_OUTPUT_BUFFER) {
		output_buffer.remove_at(0);
	}
#endif
}

void GodotMCPServer::_connect_debugger_signals() {
#ifdef TOOLS_ENABLED
	if (debugger_connected) {
		return;
	}

	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	if (!debugger_node) {
		return;
	}

	ScriptEditorDebugger *debugger = debugger_node->get_current_debugger();
	if (debugger && debugger->is_session_active()) {
		Callable callback = callable_mp(this, &GodotMCPServer::_on_debugger_output);
		if (!debugger->is_connected("output", callback)) {
			debugger->connect("output", callback);
			debugger_connected = true;
			output_buffer.clear(); // Fresh start for new session.
		}
	}
#endif
}

void GodotMCPServer::_disconnect_debugger_signals() {
#ifdef TOOLS_ENABLED
	if (!debugger_connected) {
		return;
	}

	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	if (debugger_node) {
		ScriptEditorDebugger *debugger = debugger_node->get_current_debugger();
		if (debugger) {
			Callable callback = callable_mp(this, &GodotMCPServer::_on_debugger_output);
			if (debugger->is_connected("output", callback)) {
				debugger->disconnect("output", callback);
			}
		}
	}
	debugger_connected = false;
#endif
}

GodotMCPServer::GodotMCPServer() {
	server.instantiate();

	// Initialize tool dispatch map.
	tool_handlers["godot_get_scene_tree"] = &GodotMCPServer::_tool_get_scene_tree;
	tool_handlers["godot_add_node"] = &GodotMCPServer::_tool_add_node;
	tool_handlers["godot_remove_node"] = &GodotMCPServer::_tool_remove_node;
	tool_handlers["godot_set_property"] = &GodotMCPServer::_tool_set_property;
	tool_handlers["godot_get_property"] = &GodotMCPServer::_tool_get_property;
	tool_handlers["godot_create_script"] = &GodotMCPServer::_tool_create_script;
	tool_handlers["godot_read_script"] = &GodotMCPServer::_tool_read_script;
	tool_handlers["godot_modify_script"] = &GodotMCPServer::_tool_modify_script;
	tool_handlers["godot_get_selected_nodes"] = &GodotMCPServer::_tool_get_selected_nodes;
	tool_handlers["godot_select_nodes"] = &GodotMCPServer::_tool_select_nodes;
	tool_handlers["godot_run_scene"] = &GodotMCPServer::_tool_run_scene;
	tool_handlers["godot_stop_scene"] = &GodotMCPServer::_tool_stop_scene;
	tool_handlers["godot_get_runtime_scene_tree"] = &GodotMCPServer::_tool_get_runtime_scene_tree;
	tool_handlers["godot_get_runtime_output"] = &GodotMCPServer::_tool_get_runtime_output;
	tool_handlers["godot_capture_screenshot"] = &GodotMCPServer::_tool_capture_screenshot;
	tool_handlers["godot_runtime_camera_control"] = &GodotMCPServer::_tool_runtime_camera_control;
	tool_handlers["godot_get_runtime_camera_info"] = &GodotMCPServer::_tool_get_runtime_camera_info;
}

GodotMCPServer::~GodotMCPServer() {
	stop();
}

Error GodotMCPServer::start(int p_port, const String &p_host) {
	if (running) {
		return ERR_ALREADY_IN_USE;
	}

	port = p_port;
	host = p_host;

	IPAddress ip = (host == "localhost") ? IPAddress("127.0.0.1") : IPAddress(host);

	Error err = server->listen(port, ip);
	if (err != OK) {
		ERR_PRINT(vformat("Claude MCP: Failed to listen on %s:%d", host, port));
		return err;
	}

	running = true;
	print_verbose(vformat("Claude MCP: Server listening on %s:%d", host, port));
	return OK;
}

void GodotMCPServer::stop() {
	if (!running) {
		return;
	}

	_disconnect_debugger_signals();

	// Disconnect all clients.
	for (KeyValue<int, Peer> &E : clients) {
		E.value.connection->disconnect_from_host();
	}
	clients.clear();

	server->stop();
	running = false;
	active_client_id = -1;
	print_verbose("Claude MCP: Server stopped");
}

void GodotMCPServer::poll() {
	if (!running) {
		return;
	}

#ifdef TOOLS_ENABLED
	// Manage debugger signal connections based on session state.
	bool is_game_running = EditorInterface::get_singleton()->is_playing_scene();
	if (is_game_running) {
		_connect_debugger_signals();
	} else {
		_disconnect_debugger_signals();
	}
#endif

	// Accept new connections.
	while (server->is_connection_available()) {
		if (clients.size() >= MAX_CLIENTS) {
			// Reject - too many clients.
			Ref<StreamPeerTCP> reject = server->take_connection();
			reject->disconnect_from_host();
			break;
		}
		_on_client_connected();
	}

	// Process each client.
	Vector<int> to_remove;
	for (KeyValue<int, Peer> &E : clients) {
		int id = E.key;
		Peer &peer = E.value;

		peer.connection->poll();
		StreamPeerTCP::Status status = peer.connection->get_status();

		if (status == StreamPeerTCP::STATUS_NONE || status == StreamPeerTCP::STATUS_ERROR) {
			to_remove.push_back(id);
			continue;
		}

		if (status != StreamPeerTCP::STATUS_CONNECTED) {
			continue;
		}

		_process_client(id, peer);
	}

	// Remove disconnected clients.
	for (int id : to_remove) {
		clients.erase(id);
		emit_signal("client_disconnected", id);
	}
}

void GodotMCPServer::_on_client_connected() {
	Ref<StreamPeerTCP> connection = server->take_connection();
	if (!connection.is_valid()) {
		return;
	}

	int id = next_client_id++;
	Peer peer;
	peer.connection = connection;
	clients[id] = peer;

	emit_signal("client_connected", id);
	print_verbose(vformat("Claude MCP: Client %d connected", id));
}

void GodotMCPServer::_process_client(int p_id, Peer &p_peer) {
	// Read available data.
	int available = p_peer.connection->get_available_bytes();
	if (available > 0) {
		if (p_peer.read_buf.size() + available > MAX_BUFFER_SIZE) {
			// Buffer overflow - disconnect client.
			p_peer.connection->disconnect_from_host();
			return;
		}

		int offset = p_peer.read_buf.size();
		p_peer.read_buf.resize(offset + available);
		int read = 0;
		Error err = p_peer.connection->get_partial_data(p_peer.read_buf.ptrw() + offset, available, read);
		if (err != OK) {
			p_peer.connection->disconnect_from_host();
			return;
		}
		p_peer.read_buf.resize(offset + read);
	}

	// Process complete lines (newline-delimited JSON).
	while (true) {
		int newline_pos = -1;
		for (int i = 0; i < p_peer.read_buf.size(); i++) {
			if (p_peer.read_buf[i] == '\n') {
				newline_pos = i;
				break;
			}
		}
		if (newline_pos < 0) {
			break;
		}

		// Extract the line.
		String line;
		if (newline_pos > 0) {
			Vector<uint8_t> line_buf;
			line_buf.resize(newline_pos);
			memcpy(line_buf.ptrw(), p_peer.read_buf.ptr(), newline_pos);
			line = String::utf8((const char *)line_buf.ptr(), newline_pos).strip_edges();
		}

		// Remove processed bytes from buffer.
		int remaining = p_peer.read_buf.size() - newline_pos - 1;
		if (remaining > 0) {
			memmove(p_peer.read_buf.ptrw(), p_peer.read_buf.ptr() + newline_pos + 1, remaining);
		}
		p_peer.read_buf.resize(remaining);

		// Process the line.
		if (!line.is_empty()) {
			active_client_id = p_id;

			JSON json;
			Error err = json.parse(line);
			if (err != OK) {
				_send_error(-32700, "Parse error: " + json.get_error_message(), Variant());
				continue;
			}

			Variant data = json.get_data();
			if (data.get_type() != Variant::DICTIONARY) {
				_send_error(-32600, "Invalid request: expected object", Variant());
				continue;
			}

			Dictionary request = data;
			_handle_request(request);
		}
	}

	// Send queued responses.
	while (p_peer.response_queue.size() > 0) {
		const CharString &response = p_peer.response_queue[0];
		int to_send = response.length() - p_peer.response_sent;
		int sent = 0;
		Error err = p_peer.connection->put_partial_data((const uint8_t *)response.ptr() + p_peer.response_sent, to_send, sent);
		if (err != OK) {
			p_peer.connection->disconnect_from_host();
			return;
		}
		p_peer.response_sent += sent;
		if (p_peer.response_sent >= response.length()) {
			p_peer.response_queue.remove_at(0);
			p_peer.response_sent = 0;
		} else {
			break; // Partial send, try again next poll.
		}
	}
}

void GodotMCPServer::_send_to_client(int p_id, const String &p_data) {
	if (!clients.has(p_id)) {
		return;
	}
	Peer &peer = clients[p_id];
	String with_newline = p_data + "\n";
	peer.response_queue.push_back(with_newline.utf8());
}

void GodotMCPServer::_handle_request(const Dictionary &p_request) {
	String method = p_request.get("method", "");
	Dictionary params = p_request.get("params", Dictionary());
	Variant id = p_request.get("id", Variant());

	// Client notification - no response needed.
	if (method == "notifications/initialized") {
		return;
	}

	Dictionary result;
	if (method == "initialize") {
		result = _handle_initialize(params);
	} else if (method == "tools/list") {
		result = _handle_tools_list(params);
	} else if (method == "tools/call") {
		result = _handle_tools_call(params);
	} else if (method == "resources/list") {
		result = _handle_resources_list(params);
	} else if (method == "resources/read") {
		result = _handle_resources_read(params);
	} else if (method == "ping") {
		result = Dictionary();
	} else {
		_send_error(-32601, "Method not found: " + method, id);
		return;
	}

	Dictionary response;
	response["jsonrpc"] = "2.0";
	response["id"] = id;
	response["result"] = result;
	_send_response(response);
}

Dictionary GodotMCPServer::_handle_initialize(const Dictionary &p_params) {
	Dictionary result;

	Dictionary server_info;
	server_info["name"] = "godot-mcp";
	server_info["version"] = "1.0.0";
	result["serverInfo"] = server_info;

	Dictionary capabilities;

	Dictionary tools_cap;
	tools_cap["listChanged"] = false;
	capabilities["tools"] = tools_cap;

	Dictionary resources_cap;
	resources_cap["subscribe"] = false;
	resources_cap["listChanged"] = false;
	capabilities["resources"] = resources_cap;

	result["capabilities"] = capabilities;
	result["protocolVersion"] = PROTOCOL_VERSION;

	return result;
}

Dictionary GodotMCPServer::_define_tool(const String &p_name, const String &p_description, const Dictionary &p_schema) {
	Dictionary tool;
	tool["name"] = p_name;
	tool["description"] = p_description;

	if (p_schema.is_empty()) {
		Dictionary empty_schema;
		empty_schema["type"] = "object";
		empty_schema["properties"] = Dictionary();
		tool["inputSchema"] = empty_schema;
	} else {
		tool["inputSchema"] = p_schema;
	}

	return tool;
}

Dictionary GodotMCPServer::_make_schema(const Array &p_params) {
	Dictionary schema;
	schema["type"] = "object";

	Dictionary properties;
	Array required;

	for (int i = 0; i < p_params.size(); i++) {
		Dictionary param = p_params[i];
		String name = param.get("name", "");
		String desc = param.get("description", "");
		bool is_required = param.get("required", true);

		Dictionary prop;
		if (param.has("type")) {
			prop["type"] = param["type"];
		}
		prop["description"] = desc;
		properties[name] = prop;

		if (is_required) {
			required.push_back(name);
		}
	}

	schema["properties"] = properties;
	schema["required"] = required;
	return schema;
}

Dictionary GodotMCPServer::_handle_tools_list(const Dictionary &p_params) {
	Array tools;

	// Scene tree tools.
	tools.push_back(_define_tool(
			"godot_get_scene_tree",
			"Get the current scene tree structure as JSON",
			Dictionary()));

	Array add_node_params;
	add_node_params.push_back(Dictionary{ { "name", "parent_path" }, { "type", "string" }, { "description", "Path to parent node (e.g., '/root/Main')" }, { "required", true } });
	add_node_params.push_back(Dictionary{ { "name", "node_type" }, { "type", "string" }, { "description", "Godot node class name (e.g., 'CharacterBody3D')" }, { "required", true } });
	add_node_params.push_back(Dictionary{ { "name", "node_name" }, { "type", "string" }, { "description", "Name for the new node" }, { "required", true } });
	add_node_params.push_back(Dictionary{ { "name", "properties" }, { "type", "object" }, { "description", "Optional initial property values" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_add_node",
			"Add a new node to the scene tree",
			_make_schema(add_node_params)));

	Array remove_node_params;
	remove_node_params.push_back(Dictionary{ { "name", "node_path" }, { "type", "string" }, { "description", "Path to the node to remove" }, { "required", true } });
	tools.push_back(_define_tool(
			"godot_remove_node",
			"Remove a node from the scene tree",
			_make_schema(remove_node_params)));

	Array set_prop_params;
	set_prop_params.push_back(Dictionary{ { "name", "node_path" }, { "type", "string" }, { "description", "Path to the node" }, { "required", true } });
	set_prop_params.push_back(Dictionary{ { "name", "property" }, { "type", "string" }, { "description", "Property name" }, { "required", true } });
	set_prop_params.push_back(Dictionary{ { "name", "value" }, { "description", "New property value (as JSON)" }, { "required", true } });
	tools.push_back(_define_tool(
			"godot_set_property",
			"Set a property on a node",
			_make_schema(set_prop_params)));

	Array get_prop_params;
	get_prop_params.push_back(Dictionary{ { "name", "node_path" }, { "type", "string" }, { "description", "Path to the node" }, { "required", true } });
	get_prop_params.push_back(Dictionary{ { "name", "property" }, { "type", "string" }, { "description", "Property name" }, { "required", true } });
	tools.push_back(_define_tool(
			"godot_get_property",
			"Get a property value from a node",
			_make_schema(get_prop_params)));

	// Script tools.
	Array create_script_params;
	create_script_params.push_back(Dictionary{ { "name", "path" }, { "type", "string" }, { "description", "Resource path (must start with 'res://')" }, { "required", true } });
	create_script_params.push_back(Dictionary{ { "name", "content" }, { "type", "string" }, { "description", "Script content" }, { "required", true } });
	create_script_params.push_back(Dictionary{ { "name", "attach_to" }, { "type", "string" }, { "description", "Optional node path to attach script to" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_create_script",
			"Create a new GDScript file",
			_make_schema(create_script_params)));

	Array read_script_params;
	read_script_params.push_back(Dictionary{ { "name", "path" }, { "type", "string" }, { "description", "Resource path to the script" }, { "required", true } });
	tools.push_back(_define_tool(
			"godot_read_script",
			"Read the content of a script file",
			_make_schema(read_script_params)));

	Array modify_script_params;
	modify_script_params.push_back(Dictionary{ { "name", "path" }, { "type", "string" }, { "description", "Resource path to the script" }, { "required", true } });
	modify_script_params.push_back(Dictionary{ { "name", "content" }, { "type", "string" }, { "description", "New script content" }, { "required", true } });
	tools.push_back(_define_tool(
			"godot_modify_script",
			"Modify an existing script file",
			_make_schema(modify_script_params)));

	// Selection tools.
	tools.push_back(_define_tool(
			"godot_get_selected_nodes",
			"Get the currently selected nodes in the editor",
			Dictionary()));

	Array select_nodes_params;
	select_nodes_params.push_back(Dictionary{ { "name", "node_paths" }, { "type", "array" }, { "description", "Array of node paths to select" }, { "required", true } });
	tools.push_back(_define_tool(
			"godot_select_nodes",
			"Select nodes in the editor",
			_make_schema(select_nodes_params)));

	// Execution tools.
	Array run_scene_params;
	run_scene_params.push_back(Dictionary{ { "name", "scene_path" }, { "type", "string" }, { "description", "Optional scene path, uses current if empty" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_run_scene",
			"Run the current scene or a specific scene",
			_make_schema(run_scene_params)));

	tools.push_back(_define_tool(
			"godot_stop_scene",
			"Stop the running scene",
			Dictionary()));

	// Runtime inspection tools.
	tools.push_back(_define_tool(
			"godot_get_runtime_scene_tree",
			"Get the scene tree from the currently running game instance",
			Dictionary()));

	Array get_output_params;
	get_output_params.push_back(Dictionary{ { "name", "limit" }, { "type", "integer" }, { "description", "Maximum number of messages to return (default 100)" }, { "required", false } });
	get_output_params.push_back(Dictionary{ { "name", "since_timestamp" }, { "type", "number" }, { "description", "Only return messages after this Unix timestamp" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_get_runtime_output",
			"Get output/log messages from the running game",
			_make_schema(get_output_params)));

	tools.push_back(_define_tool(
			"godot_capture_screenshot",
			"Capture a screenshot from the running game viewport",
			Dictionary()));

	// Runtime camera control tools.
	Array camera_control_params;
	camera_control_params.push_back(Dictionary{ { "name", "action" }, { "type", "string" }, { "description", "Camera action: 'enable', 'disable', 'move', 'look_at', or 'reset'" }, { "required", true } });
	camera_control_params.push_back(Dictionary{ { "name", "camera_type" }, { "type", "string" }, { "description", "Camera type: '3d' (default) or '2d'" }, { "required", false } });
	camera_control_params.push_back(Dictionary{ { "name", "position" }, { "type", "object" }, { "description", "Position {x, y, z} for 3D or {x, y} offset for 2D" }, { "required", false } });
	camera_control_params.push_back(Dictionary{ { "name", "rotation_degrees" }, { "type", "object" }, { "description", "Euler rotation {x, y, z} in degrees (3D only)" }, { "required", false } });
	camera_control_params.push_back(Dictionary{ { "name", "target" }, { "type", "object" }, { "description", "Look-at target {x, y, z} (3D 'look_at' action)" }, { "required", false } });
	camera_control_params.push_back(Dictionary{ { "name", "from" }, { "type", "object" }, { "description", "Camera position for 'look_at' action {x, y, z}" }, { "required", false } });
	camera_control_params.push_back(Dictionary{ { "name", "fov" }, { "type", "number" }, { "description", "Field of view in degrees (3D, default: 75)" }, { "required", false } });
	camera_control_params.push_back(Dictionary{ { "name", "zoom" }, { "type", "number" }, { "description", "Zoom level (2D, default: 1.0)" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_runtime_camera_control",
			"Control the debug camera in a running game. Enable camera override, move, look at targets, or reset.",
			_make_schema(camera_control_params)));

	Array camera_info_params;
	camera_info_params.push_back(Dictionary{ { "name", "camera_type" }, { "type", "string" }, { "description", "Camera type: '3d' (default) or '2d'" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_get_runtime_camera_info",
			"Get current camera state from the running game (position, rotation, FOV/zoom)",
			_make_schema(camera_info_params)));

	Dictionary result;
	result["tools"] = tools;
	return result;
}

Dictionary GodotMCPServer::_handle_tools_call(const Dictionary &p_params) {
	String name = p_params.get("name", "");
	Dictionary args = p_params.get("arguments", Dictionary());

	emit_signal("tool_called", name, args);

	Dictionary result;
	if (tool_handlers.has(name)) {
		result = (this->*tool_handlers[name])(args);
	} else {
		result = _error_result("Unknown tool: " + name);
	}

	// Wrap result in MCP content format.
	Dictionary mcp_result;
	Array content;

	// Add image content if present (used by screenshot tool).
	if (result.has("_image_data")) {
		Dictionary image_content;
		image_content["type"] = "image";
		image_content["mimeType"] = result.get("_image_mime", "image/png");
		image_content["data"] = result["_image_data"];
		content.push_back(image_content);

		// Remove internal image keys before serializing text result.
		result.erase("_image_data");
		result.erase("_image_mime");
	}

	// Add text content with the JSON result.
	Dictionary text_content;
	text_content["type"] = "text";
	text_content["text"] = JSON::stringify(result, "\t");
	content.push_back(text_content);
	mcp_result["content"] = content;

	return mcp_result;
}

Dictionary GodotMCPServer::_handle_resources_list(const Dictionary &p_params) {
	Array resources;

#ifdef TOOLS_ENABLED
	Node *scene = EditorInterface::get_singleton()->get_edited_scene_root();
	if (scene) {
		Dictionary scene_resource;
		scene_resource["uri"] = "godot://scene/current";
		scene_resource["name"] = "Current Scene";
		scene_resource["mimeType"] = "application/json";
		resources.push_back(scene_resource);
	}
#endif

	Dictionary result;
	result["resources"] = resources;
	return result;
}

Dictionary GodotMCPServer::_handle_resources_read(const Dictionary &p_params) {
	String uri = p_params.get("uri", "");

	if (uri == "godot://scene/current") {
		Dictionary scene_data = _tool_get_scene_tree(Dictionary());
		Dictionary result;
		Array contents;
		Dictionary text_content;
		text_content["uri"] = uri;
		text_content["mimeType"] = "application/json";
		text_content["text"] = JSON::stringify(scene_data, "\t");
		contents.push_back(text_content);
		result["contents"] = contents;
		return result;
	}

	return _error_result("Resource not found: " + uri);
}

void GodotMCPServer::_send_response(const Dictionary &p_response) {
	String json_str = JSON::stringify(p_response);
	_send_to_client(active_client_id, json_str);
}

void GodotMCPServer::_send_error(int p_code, const String &p_message, const Variant &p_id) {
	Dictionary response;
	response["jsonrpc"] = "2.0";
	response["id"] = p_id;

	Dictionary error;
	error["code"] = p_code;
	error["message"] = p_message;
	response["error"] = error;

	_send_response(response);
}

Dictionary GodotMCPServer::_success_result(const String &p_message, const Dictionary &p_data) {
	Dictionary result = p_data.duplicate();
	result["success"] = true;
	result["message"] = p_message;
	return result;
}

Dictionary GodotMCPServer::_error_result(const String &p_message) {
	Dictionary result;
	result["success"] = false;
	result["error"] = p_message;
	return result;
}

bool GodotMCPServer::_validate_node_path(const String &p_path, String &r_error) {
	if (p_path.is_empty()) {
		r_error = "Node path is empty";
		return false;
	}

	if (!p_path.begins_with("/root/")) {
		r_error = "Node path must start with /root/";
		return false;
	}

	return true;
}

bool GodotMCPServer::_validate_script_path(const String &p_path, String &r_error) {
	if (!p_path.begins_with("res://")) {
		r_error = "Path must start with res://";
		return false;
	}

	if (p_path.contains("..")) {
		r_error = "Path cannot contain parent traversal (..)";
		return false;
	}

	if (p_path.contains("/.") || p_path.begins_with("res://.")) {
		r_error = "Cannot create hidden files";
		return false;
	}

	if (!p_path.ends_with(".gd")) {
		r_error = "Script must have .gd extension";
		return false;
	}

	if (p_path.length() > 256) {
		r_error = "Path too long";
		return false;
	}

	return true;
}

bool GodotMCPServer::_validate_node_type(const String &p_type, String &r_error) {
	if (p_type.is_empty()) {
		r_error = "Node type is empty";
		return false;
	}

	if (!ClassDB::class_exists(p_type)) {
		r_error = "Unknown node type: " + p_type;
		return false;
	}

	if (!ClassDB::is_parent_class(p_type, "Node")) {
		r_error = "Type is not a Node: " + p_type;
		return false;
	}

	return true;
}

bool GodotMCPServer::_validate_resource_type(const String &p_type, String &r_error) {
	if (p_type.is_empty()) {
		r_error = "Resource type is empty";
		return false;
	}

	if (!ALLOWED_RESOURCE_TYPES.has(p_type)) {
		r_error = "Resource type not allowed: " + p_type + ". Only safe resource types can be instantiated.";
		return false;
	}

	if (!ClassDB::class_exists(p_type)) {
		r_error = "Unknown class: " + p_type;
		return false;
	}

	if (!ClassDB::can_instantiate(p_type)) {
		r_error = "Cannot instantiate: " + p_type;
		return false;
	}

	return true;
}

Node *GodotMCPServer::_get_scene_root() {
#ifdef TOOLS_ENABLED
	return EditorInterface::get_singleton()->get_edited_scene_root();
#else
	return nullptr;
#endif
}

Node *GodotMCPServer::_resolve_node_path(const String &p_path) {
	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return nullptr;
	}

	// Handle /root/SceneName format.
	if (p_path.begins_with("/root/")) {
		String relative_path = p_path.substr(6); // Remove "/root/"

		// If the path is just the scene root name.
		if (!relative_path.contains("/")) {
			if (relative_path == scene_root->get_name()) {
				return scene_root;
			}
			return nullptr;
		}

		// Split into scene name and rest of path.
		int first_slash = relative_path.find("/");
		String scene_name = relative_path.substr(0, first_slash);
		String rest_of_path = relative_path.substr(first_slash + 1);

		// Verify scene name matches.
		if (scene_name != scene_root->get_name()) {
			return nullptr;
		}

		// Get node relative to scene root.
		return scene_root->get_node_or_null(NodePath(rest_of_path));
	}

	return nullptr;
}

// Resource instantiation helper.
Ref<Resource> GodotMCPServer::_instantiate_resource(const String &p_type) {
	String error;
	if (!_validate_resource_type(p_type, error)) {
		return Ref<Resource>();
	}
	Object *obj = ClassDB::instantiate(p_type);
	if (!obj) {
		return Ref<Resource>();
	}
	Resource *res = Object::cast_to<Resource>(obj);
	if (!res) {
		memdelete(obj);
		return Ref<Resource>();
	}
	return Ref<Resource>(res);
}

// Type coercion for JSON values to Godot types.
Variant GodotMCPServer::_coerce_value(const Variant &p_value, Variant::Type p_target_type) {
	if (p_value.get_type() == p_target_type) {
		return p_value;
	}

	// Handle Dictionary -> Godot types.
	if (p_value.get_type() == Variant::DICTIONARY) {
		Dictionary dict = p_value;

		switch (p_target_type) {
			case Variant::VECTOR2: {
				return Vector2(
						dict.get("x", 0.0),
						dict.get("y", 0.0));
			}
			case Variant::VECTOR2I: {
				return Vector2i(
						dict.get("x", 0),
						dict.get("y", 0));
			}
			case Variant::VECTOR3: {
				return Vector3(
						dict.get("x", 0.0),
						dict.get("y", 0.0),
						dict.get("z", 0.0));
			}
			case Variant::VECTOR3I: {
				return Vector3i(
						dict.get("x", 0),
						dict.get("y", 0),
						dict.get("z", 0));
			}
			case Variant::VECTOR4: {
				return Vector4(
						dict.get("x", 0.0),
						dict.get("y", 0.0),
						dict.get("z", 0.0),
						dict.get("w", 0.0));
			}
			case Variant::COLOR: {
				return Color(
						dict.get("r", 1.0),
						dict.get("g", 1.0),
						dict.get("b", 1.0),
						dict.get("a", 1.0));
			}
			case Variant::RECT2: {
				return Rect2(
						dict.get("x", 0.0),
						dict.get("y", 0.0),
						dict.get("width", 0.0),
						dict.get("height", 0.0));
			}
			case Variant::RECT2I: {
				return Rect2i(
						dict.get("x", 0),
						dict.get("y", 0),
						dict.get("width", 0),
						dict.get("height", 0));
			}
			case Variant::OBJECT: {
				// Dict with _type -> Resource with properties
				if (dict.has("_type")) {
					String type_name = dict["_type"];
					Ref<Resource> ref = _instantiate_resource(type_name);
					if (ref.is_valid()) {
						// Apply properties from the dictionary
						for (const Variant *key = dict.next(); key; key = dict.next(key)) {
							String prop = *key;
							if (prop == "_type") {
								continue;
							}
							Variant val = dict[*key];
							Variant current = ref->get(prop);
							if (current.get_type() != Variant::NIL) {
								val = _coerce_value(val, current.get_type());
							}
							ref->set(prop, val);
						}
						return ref;
					}
				}
				break;
			}
			default:
				break;
		}
	}

	// Handle String -> Resource instantiation for OBJECT type
	if (p_target_type == Variant::OBJECT && p_value.get_type() == Variant::STRING) {
		Ref<Resource> ref = _instantiate_resource(p_value);
		if (ref.is_valid()) {
			return ref;
		}
	}

	// Handle numeric conversions.
	if (p_value.get_type() == Variant::FLOAT || p_value.get_type() == Variant::INT) {
		if (p_target_type == Variant::INT) {
			return (int64_t)p_value;
		} else if (p_target_type == Variant::FLOAT) {
			return (double)p_value;
		}
	}

	return p_value;
}

// File operation helpers for undo/redo.

void GodotMCPServer::_write_script_file(const String &p_path, const String &p_content) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	if (file.is_valid()) {
		file->store_string(p_content);
		file->close();
	}
#ifdef TOOLS_ENABLED
	EditorFileSystem::get_singleton()->scan_changes();
#endif
}

void GodotMCPServer::_delete_script_file(const String &p_path) {
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (da.is_valid()) {
		da->remove(p_path);
	}
#ifdef TOOLS_ENABLED
	EditorFileSystem::get_singleton()->scan_changes();
#endif
}

void GodotMCPServer::_attach_script_to_node(const String &p_node_path, const String &p_script_path) {
	Node *node = _resolve_node_path(p_node_path);
	if (node) {
		Ref<Script> script = ResourceLoader::load(p_script_path, "Script");
		if (script.is_valid()) {
			node->set_script(script);
		}
	}
}

void GodotMCPServer::_detach_script_from_node(const String &p_node_path) {
	Node *node = _resolve_node_path(p_node_path);
	if (node) {
		node->set_script(Variant());
	}
}

// Tool implementations.

Dictionary GodotMCPServer::_tool_get_scene_tree(const Dictionary &p_args) {
	Node *scene_root = _get_scene_root();

	if (!scene_root) {
		return _success_result("No scene is currently open", Dictionary{ { "scene", Variant() } });
	}

	Ref<MCPSceneSerializer> serializer;
	serializer.instantiate();

	Dictionary scene_data = serializer->serialize_scene(scene_root);

	return _success_result("Scene tree retrieved",
			Dictionary{ { "scene", scene_data }, { "scene_path", scene_root->get_scene_file_path() } });
}

Dictionary GodotMCPServer::_tool_add_node(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String parent_path = p_args.get("parent_path", "");
	String node_type = p_args.get("node_type", "");
	String node_name = p_args.get("node_name", "");
	Dictionary properties = p_args.get("properties", Dictionary());

	// Validate.
	String error;
	if (!_validate_node_path(parent_path, error)) {
		return _error_result(error);
	}
	if (!_validate_node_type(node_type, error)) {
		return _error_result(error);
	}

	Node *parent = _resolve_node_path(parent_path);
	if (!parent) {
		return _error_result("Parent node not found: " + parent_path);
	}

	// Create node.
	Node *new_node = Object::cast_to<Node>(ClassDB::instantiate(node_type));
	if (!new_node) {
		return _error_result("Failed to instantiate: " + node_type);
	}

	if (node_name.is_empty()) {
		node_name = node_type;
	}
	new_node->set_name(node_name);

	// Apply properties with type coercion.
	for (const Variant *key = properties.next(); key; key = properties.next(key)) {
		String prop_name = *key;
		Variant prop_value = properties[*key];
		Variant current = new_node->get(prop_name);
		if (current.get_type() != Variant::NIL) {
			prop_value = _coerce_value(prop_value, current.get_type());
		}
		new_node->set(prop_name, prop_value);
	}

	Node *scene_root = _get_scene_root();

	// Add with undo/redo.
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Add " + node_name);
	ur->add_do_method(parent, "add_child", new_node, true);
	ur->add_do_method(new_node, "set_owner", scene_root);
	ur->add_do_reference(new_node);
	ur->add_undo_method(parent, "remove_child", new_node);
	ur->commit_action();

	return _success_result("Created " + node_type + " '" + node_name + "'",
			Dictionary{ { "node_path", String(new_node->get_path()) } });
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_remove_node(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String node_path = p_args.get("node_path", "");

	String error;
	if (!_validate_node_path(node_path, error)) {
		return _error_result(error);
	}

	Node *node = _resolve_node_path(node_path);
	if (!node) {
		return _error_result("Node not found: " + node_path);
	}

	Node *scene_root = _get_scene_root();
	if (node == scene_root) {
		return _error_result("Cannot remove scene root node");
	}

	Node *parent = node->get_parent();
	if (!parent) {
		return _error_result("Node has no parent");
	}

	String node_name = node->get_name();

	// Remove with undo/redo.
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Remove " + node_name);
	ur->add_do_method(parent, "remove_child", node);
	ur->add_undo_method(parent, "add_child", node, true);
	ur->add_undo_method(node, "set_owner", scene_root);
	ur->add_undo_reference(node);
	ur->commit_action();

	return _success_result("Removed node: " + node_name);
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_set_property(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String node_path = p_args.get("node_path", "");
	String property = p_args.get("property", "");
	Variant value = p_args.get("value", Variant());

	String error;
	if (!_validate_node_path(node_path, error)) {
		return _error_result(error);
	}

	Node *node = _resolve_node_path(node_path);
	if (!node) {
		return _error_result("Node not found: " + node_path);
	}

	if (property.is_empty()) {
		return _error_result("Property name is empty");
	}

	// Get property info to determine target type.
	Variant current_value = node->get(property);
	Variant::Type target_type = current_value.get_type();

	// Special case: if current is null but property expects Object, check property info.
	// This handles cases like MeshInstance3D.mesh where the mesh is initially null.
	if (target_type == Variant::NIL) {
		List<PropertyInfo> props;
		node->get_property_list(&props);
		for (const PropertyInfo &pi : props) {
			if (pi.name == property && pi.type == Variant::OBJECT) {
				target_type = Variant::OBJECT;
				break;
			}
		}
	}

	// Coerce value to match the property's expected type.
	if (target_type != Variant::NIL) {
		value = _coerce_value(value, target_type);
	}

	// Get old value for undo.
	Variant old_value = current_value;

	// Set with undo/redo.
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Set " + property);
	ur->add_do_method(node, "set", property, value);
	ur->add_undo_method(node, "set", property, old_value);
	ur->commit_action();

	return _success_result("Set " + property + " on " + String(node->get_name()));
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_get_property(const Dictionary &p_args) {
	String node_path = p_args.get("node_path", "");
	String property = p_args.get("property", "");

	String error;
	if (!_validate_node_path(node_path, error)) {
		return _error_result(error);
	}

	Node *node = _resolve_node_path(node_path);
	if (!node) {
		return _error_result("Node not found: " + node_path);
	}

	if (property.is_empty()) {
		return _error_result("Property name is empty");
	}

	Variant value = node->get(property);

	return _success_result("Property retrieved",
			Dictionary{ { "property", property }, { "value", value }, { "type", Variant::get_type_name(value.get_type()) } });
}

Dictionary GodotMCPServer::_tool_create_script(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String path = p_args.get("path", "");
	String content = p_args.get("content", "");
	String attach_to = p_args.get("attach_to", "");

	String error;
	if (!_validate_script_path(path, error)) {
		return _error_result(error);
	}

	if (FileAccess::exists(path)) {
		return _error_result("Script already exists: " + path);
	}

	// Create directory if needed.
	String dir = path.get_base_dir();
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (!da->dir_exists(dir)) {
		Error err = da->make_dir_recursive(dir);
		if (err != OK) {
			return _error_result("Cannot create directory: " + dir);
		}
	}

	// Create file with undo/redo.
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Create Script");
	ur->add_do_method(this, "_write_script_file", path, content);
	ur->add_undo_method(this, "_delete_script_file", path);

	// Attach to node if specified.
	if (!attach_to.is_empty()) {
		String attach_error;
		if (_validate_node_path(attach_to, attach_error)) {
			Node *node = _resolve_node_path(attach_to);
			if (node) {
				ur->add_do_method(this, "_attach_script_to_node", attach_to, path);
				ur->add_undo_method(this, "_detach_script_from_node", attach_to);
			}
		}
	}

	ur->commit_action();

	Dictionary data;
	data["path"] = path;
	if (!attach_to.is_empty()) {
		data["attached_to"] = attach_to;
	}
	return _success_result("Created script: " + path, data);
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_read_script(const Dictionary &p_args) {
	String path = p_args.get("path", "");

	String error;
	if (!_validate_script_path(path, error)) {
		return _error_result(error);
	}

	if (!FileAccess::exists(path)) {
		return _error_result("Script not found: " + path);
	}

	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (!file.is_valid()) {
		return _error_result("Cannot read: " + path);
	}

	String content = file->get_as_text();

	return _success_result("Script read",
			Dictionary{ { "path", path }, { "content", content } });
}

Dictionary GodotMCPServer::_tool_modify_script(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String path = p_args.get("path", "");
	String content = p_args.get("content", "");

	String error;
	if (!_validate_script_path(path, error)) {
		return _error_result(error);
	}

	if (!FileAccess::exists(path)) {
		return _error_result("Script not found: " + path);
	}

	// Read existing content for undo.
	Ref<FileAccess> read_file = FileAccess::open(path, FileAccess::READ);
	if (!read_file.is_valid()) {
		return _error_result("Cannot read: " + path);
	}
	String old_content = read_file->get_as_text();
	read_file->close();

	// Write with undo/redo.
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Modify Script");
	ur->add_do_method(this, "_write_script_file", path, content);
	ur->add_undo_method(this, "_write_script_file", path, old_content);
	ur->commit_action();

	return _success_result("Modified script: " + path);
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_get_selected_nodes(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	EditorSelection *selection = EditorInterface::get_singleton()->get_selection();
	TypedArray<Node> selected = selection->get_selected_nodes();

	Array paths;
	for (int i = 0; i < selected.size(); i++) {
		Node *node = Object::cast_to<Node>(selected[i]);
		if (node) {
			paths.push_back(String(node->get_path()));
		}
	}

	return _success_result("Selected nodes retrieved",
			Dictionary{ { "selected", paths }, { "count", paths.size() } });
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_select_nodes(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	Array node_paths = p_args.get("node_paths", Array());

	EditorSelection *selection = EditorInterface::get_singleton()->get_selection();
	selection->clear();

	int selected_count = 0;
	for (int i = 0; i < node_paths.size(); i++) {
		String path = node_paths[i];
		Node *node = _resolve_node_path(path);
		if (node) {
			selection->add_node(node);
			selected_count++;
		}
	}

	return _success_result("Selected " + itos(selected_count) + " nodes",
			Dictionary{ { "count", selected_count } });
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_run_scene(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String scene_path = p_args.get("scene_path", "");
	EditorInterface *editor = EditorInterface::get_singleton();

	if (scene_path.is_empty()) {
		editor->play_current_scene();
	} else {
		editor->play_custom_scene(scene_path);
	}

	return _success_result("Running scene");
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_stop_scene(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	EditorInterface::get_singleton()->stop_playing_scene();
	return _success_result("Stopped scene");
#else
	return _error_result("Editor functionality not available");
#endif
}

// Helper to serialize remote tree from flat node list to hierarchical structure.
Dictionary GodotMCPServer::_serialize_remote_tree(const Array &p_nodes) {
	// The nodes come as a flat list with child_count indicating nesting.
	// We need to convert to a hierarchical dictionary structure.
	struct StackEntry {
		Dictionary node;
		int remaining_children;
	};
	Vector<StackEntry> stack;
	Dictionary root;

	for (int i = 0; i < p_nodes.size(); i++) {
		Dictionary node_data = p_nodes[i];

		Dictionary dict;
		dict["name"] = node_data.get("name", "");
		dict["type"] = node_data.get("type", "");
		dict["id"] = node_data.get("id", 0);
		dict["scene_file_path"] = node_data.get("scene_file_path", "");
		dict["children"] = Array();

		int child_count = node_data.get("child_count", 0);

		if (stack.is_empty()) {
			root = dict;
		} else {
			Array children = stack[stack.size() - 1].node["children"];
			children.push_back(dict);
			stack.write[stack.size() - 1].remaining_children--;
		}

		// Pop completed parents from stack.
		while (!stack.is_empty() && stack[stack.size() - 1].remaining_children == 0) {
			stack.resize(stack.size() - 1);
		}

		if (child_count > 0) {
			StackEntry entry;
			entry.node = dict;
			entry.remaining_children = child_count;
			stack.push_back(entry);
		}
	}

	return root;
}

Dictionary GodotMCPServer::_tool_get_runtime_scene_tree(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	if (!EditorInterface::get_singleton()->is_playing_scene()) {
		return _success_result("No game running",
				Dictionary{ { "running", false }, { "scene", Variant() } });
	}

	ScriptEditorDebugger *debugger = EditorDebuggerNode::get_singleton()->get_current_debugger();
	if (!debugger || !debugger->is_session_active()) {
		return _error_result("Debugger session not active");
	}

	// Request fresh tree data.
	debugger->request_remote_tree();

	// Poll with timeout waiting for the tree data.
	uint64_t start = Time::get_singleton()->get_ticks_msec();
	const uint64_t timeout_ms = 2000;

	while (Time::get_singleton()->get_ticks_msec() - start < timeout_ms) {
		// Run a full frame to process debugger messages.
		// Using Main::iteration() is an established Godot pattern (see editor_interface.cpp).
		DisplayServer::get_singleton()->process_events();
		Main::iteration();

		const SceneDebuggerTree *tree = debugger->get_remote_tree();
		if (tree && !tree->nodes.is_empty()) {
			// Convert to JSON-serializable format.
			Array nodes_array;
			for (const SceneDebuggerTree::RemoteNode &node : tree->nodes) {
				Dictionary node_dict;
				node_dict["name"] = node.name;
				node_dict["type"] = node.type_name;
				node_dict["id"] = (int64_t)node.id;
				node_dict["scene_file_path"] = node.scene_file_path;
				node_dict["child_count"] = node.child_count;
				nodes_array.push_back(node_dict);
			}

			Dictionary scene = _serialize_remote_tree(nodes_array);
			return _success_result("Runtime scene tree retrieved",
					Dictionary{ { "running", true }, { "scene", scene } });
		}
	}

	return _error_result("Timeout waiting for runtime scene tree. Game may not be responding to debugger.");
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_get_runtime_output(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	bool is_running = EditorInterface::get_singleton()->is_playing_scene();
	int limit = p_args.get("limit", 100);
	double since = p_args.get("since_timestamp", 0.0);

	Array messages;
	int count = 0;

	// Return messages from buffer (newest first).
	for (int i = output_buffer.size() - 1; i >= 0 && count < limit; i--) {
		const OutputMessage &msg = output_buffer[i];
		if (since > 0.0 && msg.timestamp < since) {
			continue;
		}

		Dictionary m;
		m["type"] = msg.type;
		m["text"] = msg.text;
		m["timestamp"] = msg.timestamp;
		messages.push_back(m);
		count++;
	}

	return _success_result("Output retrieved",
			Dictionary{ { "running", is_running }, { "message_count", messages.size() }, { "messages", messages } });
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_capture_screenshot(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	if (!EditorInterface::get_singleton()->is_playing_scene()) {
		return _error_result("No game running");
	}

	// Find the GameViewDebugger via the editor plugin system.
	GameViewDebugger *game_debugger = nullptr;
	EditorData &editor_data = EditorNode::get_editor_data();
	for (int i = 0; i < editor_data.get_editor_plugin_count(); i++) {
		GameViewPluginBase *game_plugin = Object::cast_to<GameViewPluginBase>(editor_data.get_editor_plugin(i));
		if (game_plugin) {
			Ref<GameViewDebugger> debugger_ref = game_plugin->get_debugger();
			if (debugger_ref.is_valid()) {
				game_debugger = debugger_ref.ptr();
				break;
			}
		}
	}

	if (!game_debugger) {
		return _error_result("GameViewDebugger not available");
	}

	// Reset pending screenshot state.
	pending_screenshot = PendingScreenshot();

	// Register callback and request screenshot.
	Callable callback = callable_mp(this, &GodotMCPServer::_on_screenshot_captured);
	bool registered = game_debugger->add_screenshot_callback(callback, Rect2i());

	if (!registered) {
		return _error_result("Failed to register screenshot callback. Is the game running?");
	}

	// Poll with timeout waiting for screenshot.
	// The main editor loop will process debugger messages during the delay.
	uint64_t start = Time::get_singleton()->get_ticks_msec();
	const uint64_t timeout_ms = 5000; // 5 second timeout

	while (!pending_screenshot.completed && (Time::get_singleton()->get_ticks_msec() - start < timeout_ms)) {
		// Run a full frame to process debugger messages and receive the screenshot.
		// Using Main::iteration() is an established Godot pattern (see editor_interface.cpp).
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}

	if (!pending_screenshot.completed) {
		return _error_result("Screenshot capture timed out");
	}

	// Read the PNG file and base64 encode it.
	Vector<uint8_t> file_data = FileAccess::get_file_as_bytes(pending_screenshot.file_path);
	if (file_data.is_empty()) {
		return _error_result("Cannot read screenshot file: " + pending_screenshot.file_path);
	}

	String base64_data = CryptoCore::b64_encode_str(file_data.ptr(), file_data.size());

	// Clean up the temp file.
	DirAccess::remove_absolute(pending_screenshot.file_path);

	// Return result with image data.
	Dictionary result;
	result["success"] = true;
	result["message"] = "Screenshot captured";
	result["width"] = pending_screenshot.width;
	result["height"] = pending_screenshot.height;
	result["_image_data"] = base64_data;
	result["_image_mime"] = "image/png";
	return result;
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_camera_control_3d(const String &p_action, const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	ScriptEditorDebugger *debugger = EditorDebuggerNode::get_singleton()->get_current_debugger();
	if (!debugger || !debugger->is_session_active()) {
		return _error_result("Debugger session not active");
	}

	if (p_action == "enable") {
		debugger->set_camera_override(EditorDebuggerNode::OVERRIDE_INGAME);
		return _success_result("3D camera override enabled");
	} else if (p_action == "disable") {
		debugger->set_camera_override(EditorDebuggerNode::OVERRIDE_NONE);
		return _success_result("3D camera override disabled");
	} else if (p_action == "reset") {
		// Send reset message to restore default camera.
		Array msg;
		debugger->send_message("scene:runtime_node_select_reset_camera_3d", msg);
		return _success_result("3D camera reset to default");
	} else if (p_action == "move") {
		// Build transform from position and rotation.
		Dictionary pos_dict = p_args.get("position", Dictionary());
		Dictionary rot_dict = p_args.get("rotation_degrees", Dictionary());

		Vector3 position(
				pos_dict.get("x", 0.0),
				pos_dict.get("y", 0.0),
				pos_dict.get("z", 0.0));

		Vector3 rotation_deg(
				rot_dict.get("x", 0.0),
				rot_dict.get("y", 0.0),
				rot_dict.get("z", 0.0));
		Vector3 rotation_rad(
				Math::deg_to_rad(rotation_deg.x),
				Math::deg_to_rad(rotation_deg.y),
				Math::deg_to_rad(rotation_deg.z));

		Basis basis;
		basis.set_euler(rotation_rad, EulerOrder::YXZ);
		Transform3D transform(basis, position);

		double fov = p_args.get("fov", 75.0);
		double near_clip = 0.05;
		double far_clip = 4000.0;

		// Message format: transform, is_perspective, fov_or_size, near, far
		Array msg;
		msg.push_back(transform);
		msg.push_back(true); // is_perspective
		msg.push_back(fov);
		msg.push_back(near_clip);
		msg.push_back(far_clip);
		debugger->send_message("scene:transform_camera_3d", msg);

		return _success_result("3D camera moved",
				Dictionary{ { "position", Dictionary{ { "x", position.x }, { "y", position.y }, { "z", position.z } } },
						{ "rotation_degrees", Dictionary{ { "x", rotation_deg.x }, { "y", rotation_deg.y }, { "z", rotation_deg.z } } },
						{ "fov", fov } });
	} else if (p_action == "look_at") {
		Dictionary from_dict = p_args.get("from", Dictionary());
		Dictionary target_dict = p_args.get("target", Dictionary());

		if (target_dict.is_empty()) {
			return _error_result("'look_at' action requires 'target' parameter");
		}

		Vector3 from_pos(
				from_dict.get("x", 0.0),
				from_dict.get("y", 0.0),
				from_dict.get("z", 0.0));
		Vector3 target(
				target_dict.get("x", 0.0),
				target_dict.get("y", 0.0),
				target_dict.get("z", 0.0));

		// Calculate transform looking at target.
		Transform3D transform;
		transform.origin = from_pos;
		if (from_pos != target) {
			transform = transform.looking_at(target, Vector3(0, 1, 0));
		}

		double fov = p_args.get("fov", 75.0);
		double near_clip = 0.05;
		double far_clip = 4000.0;

		Array msg;
		msg.push_back(transform);
		msg.push_back(true); // is_perspective
		msg.push_back(fov);
		msg.push_back(near_clip);
		msg.push_back(far_clip);
		debugger->send_message("scene:transform_camera_3d", msg);

		// Extract rotation from transform for response.
		Vector3 rotation_rad = transform.basis.get_euler(EulerOrder::YXZ);
		Vector3 rotation_deg(
				Math::rad_to_deg(rotation_rad.x),
				Math::rad_to_deg(rotation_rad.y),
				Math::rad_to_deg(rotation_rad.z));

		return _success_result("3D camera looking at target",
				Dictionary{ { "position", Dictionary{ { "x", from_pos.x }, { "y", from_pos.y }, { "z", from_pos.z } } },
						{ "target", Dictionary{ { "x", target.x }, { "y", target.y }, { "z", target.z } } },
						{ "rotation_degrees", Dictionary{ { "x", rotation_deg.x }, { "y", rotation_deg.y }, { "z", rotation_deg.z } } },
						{ "fov", fov } });
	}

	return _error_result("Unknown 3D camera action: " + p_action);
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_camera_control_2d(const String &p_action, const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	ScriptEditorDebugger *debugger = EditorDebuggerNode::get_singleton()->get_current_debugger();
	if (!debugger || !debugger->is_session_active()) {
		return _error_result("Debugger session not active");
	}

	if (p_action == "enable") {
		debugger->set_camera_override(EditorDebuggerNode::OVERRIDE_INGAME);
		return _success_result("2D camera override enabled");
	} else if (p_action == "disable") {
		debugger->set_camera_override(EditorDebuggerNode::OVERRIDE_NONE);
		return _success_result("2D camera override disabled");
	} else if (p_action == "reset") {
		// Reset 2D camera by sending identity transform.
		Transform2D transform;
		Array msg;
		msg.push_back(transform);
		debugger->send_message("scene:transform_camera_2d", msg);
		return _success_result("2D camera reset to default");
	} else if (p_action == "move") {
		Dictionary pos_dict = p_args.get("position", Dictionary());
		double zoom = p_args.get("zoom", 1.0);

		Vector2 offset(
				pos_dict.get("x", 0.0),
				pos_dict.get("y", 0.0));

		// Build transform: the scene debugger expects a transform where:
		// - origin (after affine_inverse) becomes the camera offset
		// - scale becomes the zoom
		// So we need: transform.affine_inverse().get_origin() = offset
		// And: transform.get_scale() = zoom
		Transform2D transform;
		transform = transform.scaled(Vector2(zoom, zoom));
		transform.set_origin(-offset * zoom); // Invert because affine_inverse will be applied.

		Array msg;
		msg.push_back(transform);
		debugger->send_message("scene:transform_camera_2d", msg);

		return _success_result("2D camera moved",
				Dictionary{ { "offset", Dictionary{ { "x", offset.x }, { "y", offset.y } } },
						{ "zoom", zoom } });
	}

	return _error_result("Unknown 2D camera action: " + p_action + ". 2D cameras support: enable, disable, reset, move");
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_runtime_camera_control(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	if (!EditorInterface::get_singleton()->is_playing_scene()) {
		return _error_result("No game running. Start a scene first with godot_run_scene.");
	}

	String action = p_args.get("action", "");
	if (action.is_empty()) {
		return _error_result("Missing required 'action' parameter. Use: enable, disable, move, look_at, or reset");
	}

	String camera_type = p_args.get("camera_type", "3d");
	if (camera_type == "3d") {
		return _camera_control_3d(action, p_args);
	}
	if (camera_type == "2d") {
		return _camera_control_2d(action, p_args);
	}
	return _error_result("Invalid camera_type: " + camera_type + ". Use '3d' or '2d'");
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_get_runtime_camera_info(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	if (!EditorInterface::get_singleton()->is_playing_scene()) {
		return _error_result("No game running. Start a scene first with godot_run_scene.");
	}

	ScriptEditorDebugger *debugger = EditorDebuggerNode::get_singleton()->get_current_debugger();
	if (!debugger || !debugger->is_session_active()) {
		return _error_result("Debugger session not active");
	}

	String camera_type = p_args.get("camera_type", "3d");
	EditorDebuggerNode::CameraOverride override_state = debugger->get_camera_override();

	Dictionary result;
	result["camera_type"] = camera_type;
	result["override_enabled"] = override_state != EditorDebuggerNode::OVERRIDE_NONE;

	if (override_state == EditorDebuggerNode::OVERRIDE_NONE) {
		result["note"] = "Camera override is disabled. Enable it first with action 'enable' to control the camera.";
	} else if (override_state == EditorDebuggerNode::OVERRIDE_EDITORS) {
		result["note"] = "Camera is currently controlled by editor viewports.";
	} else {
		result["note"] = "Camera override is enabled and can be controlled via MCP.";
	}

	return _success_result("Camera info retrieved", result);
#else
	return _error_result("Editor functionality not available");
#endif
}
