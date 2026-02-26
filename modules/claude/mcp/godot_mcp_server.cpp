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

#include "core/io/ip_address.h"
#include "core/os/os.h"
#include "scene/main/node.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_interface.h"
#endif

const char *GodotMCPServer::PROTOCOL_VERSION = "2024-11-05";

// Resource types safe for instantiation via MCP.
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
	// Particles
	"ParticleProcessMaterial",
	// Animation
	"Animation", "AnimationLibrary",
	"AnimationNodeStateMachine", "AnimationNodeAnimation",
	"AnimationNodeBlendTree", "AnimationNodeBlendSpace1D", "AnimationNodeBlendSpace2D",
	// Navigation
	"NavigationMesh",
	// Noise
	"FastNoiseLite",
	// Shaders
	"Shader", "ShaderInclude",
	// Theme
	"Theme",
	// Input events
	"InputEventKey", "InputEventMouseButton",
	"InputEventJoypadButton", "InputEventJoypadMotion",
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
	ClassDB::bind_method(D_METHOD("_on_debugger_data", "msg", "data"), &GodotMCPServer::_on_debugger_data);

	ADD_SIGNAL(MethodInfo("tool_called", PropertyInfo(Variant::STRING, "tool_name"), PropertyInfo(Variant::DICTIONARY, "args")));
	ADD_SIGNAL(MethodInfo("client_connected", PropertyInfo(Variant::INT, "client_id")));
	ADD_SIGNAL(MethodInfo("client_disconnected", PropertyInfo(Variant::INT, "client_id")));
}

GodotMCPServer::GodotMCPServer() {
	server.instantiate();

	// Initialize tool dispatch map.
	// Scene tools.
	tool_handlers["godot_get_scene_tree"] = &GodotMCPServer::_tool_get_scene_tree;
	tool_handlers["godot_add_node"] = &GodotMCPServer::_tool_add_node;
	tool_handlers["godot_remove_node"] = &GodotMCPServer::_tool_remove_node;
	tool_handlers["godot_set_property"] = &GodotMCPServer::_tool_set_property;
	tool_handlers["godot_get_property"] = &GodotMCPServer::_tool_get_property;
	tool_handlers["godot_save_scene"] = &GodotMCPServer::_tool_save_scene;
	tool_handlers["godot_new_scene"] = &GodotMCPServer::_tool_new_scene;
	tool_handlers["godot_open_scene"] = &GodotMCPServer::_tool_open_scene;
	tool_handlers["godot_instance_scene"] = &GodotMCPServer::_tool_instance_scene;

	// Script tools.
	tool_handlers["godot_create_script"] = &GodotMCPServer::_tool_create_script;
	tool_handlers["godot_read_script"] = &GodotMCPServer::_tool_read_script;
	tool_handlers["godot_modify_script"] = &GodotMCPServer::_tool_modify_script;
	tool_handlers["godot_validate_script"] = &GodotMCPServer::_tool_validate_script;

	// Selection tools.
	tool_handlers["godot_get_selected_nodes"] = &GodotMCPServer::_tool_get_selected_nodes;
	tool_handlers["godot_select_nodes"] = &GodotMCPServer::_tool_select_nodes;

	// Signal tools.
	tool_handlers["godot_connect_signal"] = &GodotMCPServer::_tool_connect_signal;
	tool_handlers["godot_disconnect_signal"] = &GodotMCPServer::_tool_disconnect_signal;

	// Project tools.
	tool_handlers["godot_project_settings"] = &GodotMCPServer::_tool_project_settings;
	tool_handlers["godot_input_map"] = &GodotMCPServer::_tool_input_map;

	// Introspection tools.
	tool_handlers["godot_get_class_info"] = &GodotMCPServer::_tool_get_class_info;
	tool_handlers["godot_get_node_info"] = &GodotMCPServer::_tool_get_node_info;

	// Batch tools.
	tool_handlers["godot_set_properties_batch"] = &GodotMCPServer::_tool_set_properties_batch;

	// Resource tools.
	tool_handlers["godot_project_files"] = &GodotMCPServer::_tool_project_files;

	// 3D tools.
	tool_handlers["godot_bake_navigation"] = &GodotMCPServer::_tool_bake_navigation;

	// Animation tools.
	tool_handlers["godot_create_animation"] = &GodotMCPServer::_tool_create_animation;
	tool_handlers["godot_get_animation_info"] = &GodotMCPServer::_tool_get_animation_info;

	// Runtime tools.
	tool_handlers["godot_run_scene"] = &GodotMCPServer::_tool_run_scene;
	tool_handlers["godot_stop_scene"] = &GodotMCPServer::_tool_stop_scene;
	tool_handlers["godot_get_runtime_scene_tree"] = &GodotMCPServer::_tool_get_runtime_scene_tree;
	tool_handlers["godot_get_runtime_output"] = &GodotMCPServer::_tool_get_runtime_output;
	tool_handlers["godot_get_runtime_errors"] = &GodotMCPServer::_tool_get_runtime_errors;
	tool_handlers["godot_capture_screenshot"] = &GodotMCPServer::_tool_capture_screenshot;
	tool_handlers["godot_runtime_camera_control"] = &GodotMCPServer::_tool_runtime_camera_control;
	tool_handlers["godot_get_runtime_camera_info"] = &GodotMCPServer::_tool_get_runtime_camera_info;
	tool_handlers["godot_wait"] = &GodotMCPServer::_tool_wait;

	// Editor tools.
	tool_handlers["godot_get_editor_log"] = &GodotMCPServer::_tool_get_editor_log;
	tool_handlers["godot_editor_screenshot"] = &GodotMCPServer::_tool_editor_screenshot;
	tool_handlers["godot_editor_viewport_camera"] = &GodotMCPServer::_tool_editor_viewport_camera;
	tool_handlers["godot_editor_control"] = &GodotMCPServer::_tool_editor_control;
	tool_handlers["godot_canvas_view"] = &GodotMCPServer::_tool_canvas_view;
	tool_handlers["godot_editor_state"] = &GodotMCPServer::_tool_editor_state;

	// Transform and scene operation tools.
	tool_handlers["godot_transform_nodes"] = &GodotMCPServer::_tool_transform_nodes;
	tool_handlers["godot_scene_operations"] = &GodotMCPServer::_tool_scene_operations;
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
			line = String::utf8((const char *)p_peer.read_buf.ptr(), newline_pos).strip_edges();
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
	while (!p_peer.response_queue.is_empty()) {
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

// MCP protocol handling.

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

// Response helpers.

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
