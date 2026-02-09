/**************************************************************************/
/*  godot_mcp_tools_runtime.cpp                                           */
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

#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/math/math_funcs.h"
#include "core/os/time.h"
#include "main/main.h"
#include "servers/display/display_server.h"

#ifdef TOOLS_ENABLED
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_log.h"
#include "editor/editor_node.h"
#include "editor/run/game_view_plugin.h"
#include "scene/debugger/scene_debugger_object.h"
#endif

// Dictionary <-> Vector conversion helpers.

static Vector3 _dict_to_vector3(const Dictionary &p_dict) {
	return Vector3(
			p_dict.get("x", 0.0),
			p_dict.get("y", 0.0),
			p_dict.get("z", 0.0));
}

static Vector2 _dict_to_vector2(const Dictionary &p_dict) {
	return Vector2(
			p_dict.get("x", 0.0),
			p_dict.get("y", 0.0));
}

static Dictionary _vector3_to_dict(const Vector3 &p_vec) {
	return Dictionary{ { "x", p_vec.x }, { "y", p_vec.y }, { "z", p_vec.z } };
}

static Dictionary _vector2_to_dict(const Vector2 &p_vec) {
	return Dictionary{ { "x", p_vec.x }, { "y", p_vec.y } };
}

#ifdef TOOLS_ENABLED
// Send a 3D camera override transform via the debugger.
static void _send_camera_3d_transform(ScriptEditorDebugger *p_debugger, const Transform3D &p_transform, double p_fov) {
	Array msg;
	msg.push_back(p_transform);
	msg.push_back(true); // is_perspective
	msg.push_back(p_fov);
	msg.push_back(0.05); // near_clip
	msg.push_back(4000.0); // far_clip
	p_debugger->send_message("scene:transform_camera_3d", msg);
}
#endif

// Debugger signal connections.

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

// Tool implementations.

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

	// Return messages from buffer (newest first).
	// Messages are appended chronologically, so once we hit one older than
	// `since` we can stop scanning.
	for (int i = output_buffer.size() - 1; i >= 0 && messages.size() < limit; i--) {
		const OutputMessage &msg = output_buffer[i];
		if (since > 0.0 && msg.timestamp < since) {
			break;
		}

		Dictionary m;
		m["type"] = msg.type;
		m["text"] = msg.text;
		m["timestamp"] = msg.timestamp;
		messages.push_back(m);
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

// Camera control.

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
		Vector3 position = _dict_to_vector3(p_args.get("position", Dictionary()));
		Vector3 rotation_deg = _dict_to_vector3(p_args.get("rotation_degrees", Dictionary()));
		Vector3 rotation_rad(
				Math::deg_to_rad(rotation_deg.x),
				Math::deg_to_rad(rotation_deg.y),
				Math::deg_to_rad(rotation_deg.z));

		Basis basis;
		basis.set_euler(rotation_rad, EulerOrder::YXZ);
		Transform3D transform(basis, position);

		double fov = p_args.get("fov", 75.0);
		_send_camera_3d_transform(debugger, transform, fov);

		return _success_result("3D camera moved",
				Dictionary{ { "position", _vector3_to_dict(position) },
						{ "rotation_degrees", _vector3_to_dict(rotation_deg) },
						{ "fov", fov } });
	} else if (p_action == "look_at") {
		Dictionary target_dict = p_args.get("target", Dictionary());
		if (target_dict.is_empty()) {
			return _error_result("'look_at' action requires 'target' parameter");
		}

		Vector3 from_pos = _dict_to_vector3(p_args.get("from", Dictionary()));
		Vector3 target = _dict_to_vector3(target_dict);

		Transform3D transform;
		transform.origin = from_pos;
		if (from_pos != target) {
			transform = transform.looking_at(target, Vector3(0, 1, 0));
		}

		double fov = p_args.get("fov", 75.0);
		_send_camera_3d_transform(debugger, transform, fov);

		Vector3 rotation_rad = transform.basis.get_euler(EulerOrder::YXZ);
		Vector3 rotation_deg(
				Math::rad_to_deg(rotation_rad.x),
				Math::rad_to_deg(rotation_rad.y),
				Math::rad_to_deg(rotation_rad.z));

		return _success_result("3D camera looking at target",
				Dictionary{ { "position", _vector3_to_dict(from_pos) },
						{ "target", _vector3_to_dict(target) },
						{ "rotation_degrees", _vector3_to_dict(rotation_deg) },
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
		Vector2 offset = _dict_to_vector2(p_args.get("position", Dictionary()));
		double zoom = p_args.get("zoom", 1.0);

		// The scene debugger applies affine_inverse to the transform, so we
		// pre-invert the origin and bake the zoom into the scale.
		Transform2D transform;
		transform = transform.scaled(Vector2(zoom, zoom));
		transform.set_origin(-offset * zoom);

		Array msg;
		msg.push_back(transform);
		debugger->send_message("scene:transform_camera_2d", msg);

		return _success_result("2D camera moved",
				Dictionary{ { "offset", _vector2_to_dict(offset) },
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
