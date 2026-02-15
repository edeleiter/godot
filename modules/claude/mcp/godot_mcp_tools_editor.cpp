/**************************************************************************/
/*  godot_mcp_tools_editor.cpp                                            */
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
#include "godot_mcp_type_helpers.h"

#include "core/crypto/crypto_core.h"
#include "core/math/math_funcs.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_log.h"
#include "editor/editor_node.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/scene/canvas_item_editor_plugin.h"
#include "main/main.h"
#include "scene/3d/node_3d.h"
#include "scene/main/canvas_item.h"
#include "servers/display/display_server.h"
#endif

#ifdef TOOLS_ENABLED
// Display mode constants matching Node3DEditorViewport's private enum.
// Order: VIEW_TOP(0)..VIEW_FRAME_TIME(21), then VIEW_DISPLAY_*.
// Must stay in sync with editor/scene/3d/node_3d_editor_plugin.h.
static const int VP_DISPLAY_NORMAL = 22;
static const int VP_DISPLAY_WIREFRAME = 23;
static const int VP_DISPLAY_OVERDRAW = 24;
static const int VP_DISPLAY_LIGHTING = 25;
static const int VP_DISPLAY_UNSHADED = 26;

static int _display_mode_from_string(const String &p_name) {
	if (p_name == "normal") {
		return VP_DISPLAY_NORMAL;
	} else if (p_name == "wireframe") {
		return VP_DISPLAY_WIREFRAME;
	} else if (p_name == "overdraw") {
		return VP_DISPLAY_OVERDRAW;
	} else if (p_name == "lighting") {
		return VP_DISPLAY_LIGHTING;
	} else if (p_name == "unshaded") {
		return VP_DISPLAY_UNSHADED;
	}
	return -1;
}

static String _display_mode_to_string(int p_mode) {
	switch (p_mode) {
		case VP_DISPLAY_NORMAL:
			return "normal";
		case VP_DISPLAY_WIREFRAME:
			return "wireframe";
		case VP_DISPLAY_OVERDRAW:
			return "overdraw";
		case VP_DISPLAY_LIGHTING:
			return "lighting";
		case VP_DISPLAY_UNSHADED:
			return "unshaded";
		default:
			return "unknown(" + itos(p_mode) + ")";
	}
}
#endif

// --- Editor Log Tool (existing) ---

Dictionary GodotMCPServer::_tool_get_editor_log(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	EditorLog *log = EditorNode::get_log();
	if (!log) {
		return _error_result("EditorLog not available");
	}

	int limit = p_args.get("limit", 100);
	Array type_filter = p_args.get("types", Array());

	// Build type filter set.
	HashSet<String> filter;
	for (int i = 0; i < type_filter.size(); i++) {
		filter.insert(String(type_filter[i]).to_lower());
	}

	auto type_to_string = [](EditorLog::MessageType p_type) -> String {
		switch (p_type) {
			case EditorLog::MSG_TYPE_STD:
				return "std";
			case EditorLog::MSG_TYPE_ERROR:
				return "error";
			case EditorLog::MSG_TYPE_WARNING:
				return "warning";
			case EditorLog::MSG_TYPE_EDITOR:
				return "editor";
			case EditorLog::MSG_TYPE_STD_RICH:
				return "std_rich";
			default:
				return "std";
		}
	};

	Array messages;
	int total = log->get_message_count();

	// Return messages from the end (newest first), up to limit.
	for (int i = total - 1; i >= 0 && messages.size() < limit; i--) {
		String type_str = type_to_string(log->get_message_type(i));

		if (!filter.is_empty() && !filter.has(type_str)) {
			continue;
		}

		Dictionary m;
		m["type"] = type_str;
		m["text"] = log->get_message_text(i);
		m["count"] = log->get_message_repeat_count(i);
		messages.push_back(m);
	}

	return _success_result("Editor log retrieved",
			Dictionary{ { "message_count", messages.size() }, { "messages", messages } });
#else
	return _error_result("Editor functionality not available");
#endif
}

// --- Editor Screenshot Tool ---

Dictionary GodotMCPServer::_tool_editor_screenshot(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String source = p_args.get("source", "3d");
	float scale = CLAMP((float)p_args.get("scale", 0.5), 0.1f, 1.0f);

	// Delegate game screenshots to the existing runtime capture tool.
	if (source == "game") {
		return _tool_capture_screenshot(p_args);
	}

	SubViewport *sub_vp = nullptr;

	if (source == "3d") {
		int idx = p_args.get("viewport_idx", 0);
		sub_vp = EditorInterface::get_singleton()->get_editor_viewport_3d(idx);
	} else if (source == "2d") {
		sub_vp = EditorInterface::get_singleton()->get_editor_viewport_2d();
	} else {
		return _error_result("Invalid source: '" + source + "'. Use '3d', '2d', or 'game'");
	}

	if (!sub_vp) {
		return _error_result("Viewport not available for source: " + source);
	}

	// Ensure the viewport has a fully rendered frame.
	DisplayServer::get_singleton()->process_events();
	Main::iteration();
	Main::iteration();

	Ref<Texture2D> texture = sub_vp->get_texture();
	if (texture.is_null()) {
		return _error_result("Failed to get viewport texture");
	}

	Ref<Image> image = texture->get_image();
	if (image.is_null() || image->is_empty()) {
		return _error_result("Failed to capture viewport image");
	}

	int orig_width = image->get_width();
	int orig_height = image->get_height();

	// Downscale to reduce token cost.
	if (scale < 1.0f) {
		int new_width = MAX(1, (int)(orig_width * scale));
		int new_height = MAX(1, (int)(orig_height * scale));
		image->resize(new_width, new_height, Image::INTERPOLATE_BILINEAR);
	}

	Vector<uint8_t> png_data = image->save_png_to_buffer();
	if (png_data.is_empty()) {
		return _error_result("Failed to encode image to PNG");
	}

	String base64_data = CryptoCore::b64_encode_str(png_data.ptr(), png_data.size());

	Dictionary result;
	result["success"] = true;
	result["message"] = "Editor screenshot captured";
	result["source"] = source;
	result["original_size"] = Dictionary{ { "width", orig_width }, { "height", orig_height } };
	result["captured_size"] = Dictionary{ { "width", image->get_width() }, { "height", image->get_height() } };
	result["_image_data"] = base64_data;
	result["_image_mime"] = "image/png";
	return result;
#else
	return _error_result("Editor functionality not available");
#endif
}

// --- Editor Viewport Camera Tool ---

Dictionary GodotMCPServer::_tool_editor_viewport_camera(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String action = p_args.get("action", "");
	if (action.is_empty()) {
		return _error_result("Missing required 'action' parameter");
	}

	int idx = p_args.get("viewport_idx", 0);

	Node3DEditor *editor_3d = Node3DEditor::get_singleton();
	if (!editor_3d) {
		return _error_result("3D editor not available. Switch to the 3D panel first.");
	}

	Node3DEditorViewport *viewport = editor_3d->get_editor_viewport(idx);
	if (!viewport) {
		return _error_result("Viewport " + itos(idx) + " not available");
	}

	// --- get_state ---
	if (action == "get_state") {
		Dictionary state = viewport->get_state();

		// Extract the key camera fields into a clean response.
		Dictionary camera;
		camera["position"] = state.get("position", Vector3());
		camera["x_rotation"] = state.get("x_rotation", 0.0);
		camera["y_rotation"] = state.get("y_rotation", 0.0);
		camera["distance"] = state.get("distance", 4.0);
		camera["orthogonal"] = state.get("orthogonal", false);
		camera["display_mode"] = _display_mode_to_string(state.get("display_mode", VP_DISPLAY_NORMAL));
		camera["grid"] = state.get("grid", true);
		camera["gizmos"] = state.get("gizmos", true);
		camera["viewport_idx"] = idx;

		return _success_result("Viewport camera state", camera);
	}

	// --- move ---
	if (action == "move") {
		Dictionary state;
		if (p_args.has("position")) {
			state["position"] = mcp_dict_to_vector3(p_args["position"]);
		}
		if (p_args.has("distance")) {
			state["distance"] = (float)p_args["distance"];
		}
		if (p_args.has("x_rotation")) {
			state["x_rotation"] = (float)p_args["x_rotation"];
		}
		if (p_args.has("y_rotation")) {
			state["y_rotation"] = (float)p_args["y_rotation"];
		}
		if (p_args.has("orthogonal")) {
			state["orthogonal"] = (bool)p_args["orthogonal"];
		}
		if (p_args.has("fov_scale")) {
			state["fov_scale"] = (float)p_args["fov_scale"];
		}

		viewport->set_state(state);
		return _success_result("Viewport camera moved", state);
	}

	// --- orbit ---
	if (action == "orbit") {
		Dictionary current = viewport->get_state();
		Dictionary state;

		if (p_args.has("x_rotation")) {
			state["x_rotation"] = (float)current.get("x_rotation", 0.0) + (float)p_args["x_rotation"];
		}
		if (p_args.has("y_rotation")) {
			state["y_rotation"] = (float)current.get("y_rotation", 0.0) + (float)p_args["y_rotation"];
		}
		if (p_args.has("distance")) {
			state["distance"] = (float)p_args["distance"];
		}

		viewport->set_state(state);
		return _success_result("Viewport camera orbited", state);
	}

	// --- look_at ---
	if (action == "look_at") {
		if (!p_args.has("target")) {
			return _error_result("'look_at' action requires 'target' parameter");
		}

		Vector3 target = mcp_dict_to_vector3(p_args["target"]);
		Vector3 from_pos = mcp_dict_to_vector3(p_args.get("from", Dictionary()));

		Vector3 dir = from_pos - target;
		float distance = dir.length();
		if (distance < 0.001f) {
			return _error_result("'from' and 'target' are too close together");
		}

		Vector3 dir_norm = dir / distance;
		float y_rot = Math::atan2(dir_norm.x, dir_norm.z);
		float x_rot = Math::asin(CLAMP(dir_norm.y, -1.0f, 1.0f));

		Dictionary state;
		state["position"] = target;
		state["x_rotation"] = x_rot;
		state["y_rotation"] = y_rot;
		state["distance"] = distance;

		viewport->set_state(state);

		return _success_result("Viewport camera looking at target",
				Dictionary{ { "position", mcp_vector3_to_dict(target) },
						{ "from", mcp_vector3_to_dict(from_pos) },
						{ "distance", distance },
						{ "x_rotation", x_rot },
						{ "y_rotation", y_rot } });
	}

	// --- focus ---
	if (action == "focus") {
		EditorSelection *selection = EditorInterface::get_singleton()->get_selection();
		TypedArray<Node> selected = selection->get_selected_nodes();

		Vector3 center;
		int count = 0;
		for (int i = 0; i < selected.size(); i++) {
			Node3D *n3d = Object::cast_to<Node3D>(selected[i]);
			if (n3d) {
				center += n3d->get_global_position();
				count++;
			}
		}
		if (count == 0) {
			return _error_result("No Node3D nodes selected");
		}
		center /= count;

		// Compute distance to frame the selection.
		float max_dist = 1.0f;
		for (int i = 0; i < selected.size(); i++) {
			Node3D *n3d = Object::cast_to<Node3D>(selected[i]);
			if (n3d) {
				float d = n3d->get_global_position().distance_to(center);
				max_dist = MAX(max_dist, d);
			}
		}

		Dictionary state;
		state["position"] = center;
		state["distance"] = MAX(max_dist * 2.5f, 3.0f);

		viewport->set_state(state);

		return _success_result("Viewport camera focused on selection",
				Dictionary{ { "center", mcp_vector3_to_dict(center) },
						{ "distance", (float)state["distance"] },
						{ "node_count", count } });
	}

	// --- set_view ---
	if (action == "set_view") {
		String view = p_args.get("view", "");
		if (view.is_empty()) {
			return _error_result("'set_view' action requires 'view' parameter");
		}

		Dictionary state;
		state["orthogonal"] = true;

		if (view == "front") {
			state["x_rotation"] = 0.0f;
			state["y_rotation"] = 0.0f;
		} else if (view == "back") {
			state["x_rotation"] = 0.0f;
			state["y_rotation"] = (float)Math::PI;
		} else if (view == "left") {
			state["x_rotation"] = 0.0f;
			state["y_rotation"] = (float)(Math::PI / 2.0);
		} else if (view == "right") {
			state["x_rotation"] = 0.0f;
			state["y_rotation"] = (float)(-Math::PI / 2.0);
		} else if (view == "top") {
			state["x_rotation"] = (float)(-Math::PI / 2.0);
			state["y_rotation"] = 0.0f;
		} else if (view == "bottom") {
			state["x_rotation"] = (float)(Math::PI / 2.0);
			state["y_rotation"] = 0.0f;
		} else {
			return _error_result("Unknown view preset: '" + view + "'. Use: front, back, left, right, top, bottom");
		}

		viewport->set_state(state);
		return _success_result("Viewport set to " + view + " view", state);
	}

	return _error_result("Unknown action: '" + action + "'. Use: get_state, move, orbit, look_at, focus, set_view");
#else
	return _error_result("Editor functionality not available");
#endif
}

// --- Editor Control Tool ---

Dictionary GodotMCPServer::_tool_editor_control(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String action = p_args.get("action", "");
	if (action.is_empty()) {
		return _error_result("Missing required 'action' parameter");
	}

	// --- switch_panel ---
	if (action == "switch_panel") {
		String panel = p_args.get("panel", "");
		if (panel.is_empty()) {
			return _error_result("'switch_panel' requires 'panel' parameter (2D, 3D, Script, AssetLib)");
		}
		EditorInterface::get_singleton()->set_main_screen_editor(panel);
		return _success_result("Switched to " + panel + " panel");
	}

	// --- set_display_mode ---
	if (action == "set_display_mode") {
		String mode_name = p_args.get("display_mode", "");
		int mode_value = _display_mode_from_string(mode_name);
		if (mode_value < 0) {
			return _error_result("Unknown display_mode: '" + mode_name + "'. Use: normal, wireframe, overdraw, lighting, unshaded");
		}

		int idx = p_args.get("viewport_idx", 0);
		Node3DEditor *editor_3d = Node3DEditor::get_singleton();
		if (!editor_3d) {
			return _error_result("3D editor not available");
		}
		Node3DEditorViewport *viewport = editor_3d->get_editor_viewport(idx);
		if (!viewport) {
			return _error_result("Viewport " + itos(idx) + " not available");
		}

		Dictionary state;
		state["display_mode"] = mode_value;
		viewport->set_state(state);

		return _success_result("Display mode set to " + mode_name);
	}

	// --- toggle_grid ---
	if (action == "toggle_grid") {
		bool grid = p_args.get("grid", true);
		int idx = p_args.get("viewport_idx", 0);

		Node3DEditor *editor_3d = Node3DEditor::get_singleton();
		if (!editor_3d) {
			return _error_result("3D editor not available");
		}
		Node3DEditorViewport *viewport = editor_3d->get_editor_viewport(idx);
		if (!viewport) {
			return _error_result("Viewport " + itos(idx) + " not available");
		}

		Dictionary state;
		state["grid"] = grid;
		viewport->set_state(state);

		return _success_result(grid ? "Grid enabled" : "Grid disabled");
	}

	// --- get_state ---
	if (action == "get_state") {
		int idx = p_args.get("viewport_idx", 0);

		Node3DEditor *editor_3d = Node3DEditor::get_singleton();
		Node3DEditorViewport *viewport = editor_3d ? editor_3d->get_editor_viewport(idx) : nullptr;

		Dictionary state;
		if (viewport) {
			Dictionary vp_state = viewport->get_state();
			state["display_mode"] = _display_mode_to_string(vp_state.get("display_mode", VP_DISPLAY_NORMAL));
			state["grid"] = vp_state.get("grid", true);
			state["gizmos"] = vp_state.get("gizmos", true);
			state["orthogonal"] = vp_state.get("orthogonal", false);
			state["half_res"] = vp_state.get("half_res", false);
			state["information"] = vp_state.get("information", false);
			state["frame_time"] = vp_state.get("frame_time", false);
		}
		if (editor_3d) {
			state["snap_enabled"] = editor_3d->is_snap_enabled();
		}

		return _success_result("Editor control state", state);
	}

	return _error_result("Unknown action: '" + action + "'. Use: switch_panel, set_display_mode, toggle_grid, get_state");
#else
	return _error_result("Editor functionality not available");
#endif
}

// --- Canvas View Tool ---

Dictionary GodotMCPServer::_tool_canvas_view(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String action = p_args.get("action", "");
	if (action.is_empty()) {
		return _error_result("Missing required 'action' parameter");
	}

	CanvasItemEditor *canvas = CanvasItemEditor::get_singleton();
	if (!canvas) {
		return _error_result("2D canvas editor not available. Switch to the 2D panel first.");
	}

	// --- get_state ---
	if (action == "get_state") {
		Dictionary state = canvas->get_state();

		Dictionary clean;
		clean["zoom"] = state.get("zoom", 1.0);
		clean["offset"] = mcp_vector2_to_dict(state.get("ofs", Vector2()));
		clean["grid_snap"] = state.get("grid_snap_active", false);
		clean["smart_snap"] = state.get("smart_snap_active", false);
		clean["grid_step"] = mcp_vector2_to_dict(state.get("grid_step", Vector2(8, 8)));
		clean["grid_offset"] = mcp_vector2_to_dict(state.get("grid_offset", Vector2()));
		clean["show_grid"] = (int)state.get("grid_visibility", 0) > 0;
		clean["show_rulers"] = state.get("show_rulers", true);
		clean["show_guides"] = state.get("show_guides", true);
		clean["show_origin"] = state.get("show_origin", true);
		clean["snap_rotation"] = state.get("snap_rotation", false);
		clean["snap_scale"] = state.get("snap_scale", false);
		clean["snap_relative"] = state.get("snap_relative", false);
		clean["snap_pixel"] = state.get("snap_pixel", false);

		return _success_result("Canvas view state", clean);
	}

	// --- center_at ---
	if (action == "center_at") {
		if (!p_args.has("position")) {
			return _error_result("'center_at' requires 'position' parameter {x, y}");
		}

		Vector2 target = mcp_dict_to_vector2(p_args["position"]);

		Dictionary state = canvas->get_state();
		float zoom = state.get("zoom", 1.0);
		Vector2 view_size = canvas->get_size();
		Vector2 ofs = target - view_size / (2.0f * zoom);

		Dictionary new_state;
		new_state["ofs"] = ofs;
		canvas->set_state(new_state);

		return _success_result("Canvas centered at " + String(target),
				Dictionary{ { "position", mcp_vector2_to_dict(target) }, { "zoom", zoom } });
	}

	// --- zoom ---
	if (action == "zoom") {
		if (!p_args.has("zoom")) {
			return _error_result("'zoom' action requires 'zoom' parameter");
		}

		float zoom = CLAMP((float)p_args["zoom"], 0.01f, 100.0f);
		Dictionary new_state;
		new_state["zoom"] = zoom;
		canvas->set_state(new_state);

		return _success_result("Canvas zoom set to " + String::num(zoom, 2),
				Dictionary{ { "zoom", zoom } });
	}

	// --- pan ---
	if (action == "pan") {
		if (!p_args.has("position")) {
			return _error_result("'pan' action requires 'position' parameter {x, y} (offset delta)");
		}

		Vector2 delta = mcp_dict_to_vector2(p_args["position"]);
		Dictionary state = canvas->get_state();
		Vector2 current_ofs = state.get("ofs", Vector2());

		Dictionary new_state;
		new_state["ofs"] = current_ofs + delta;
		canvas->set_state(new_state);

		return _success_result("Canvas panned",
				Dictionary{ { "offset", mcp_vector2_to_dict(current_ofs + delta) } });
	}

	// --- focus ---
	if (action == "focus") {
		EditorSelection *selection = EditorInterface::get_singleton()->get_selection();
		TypedArray<Node> selected = selection->get_selected_nodes();

		Vector2 center;
		int count = 0;
		for (int i = 0; i < selected.size(); i++) {
			CanvasItem *ci = Object::cast_to<CanvasItem>(selected[i]);
			if (ci) {
				center += ci->get_global_transform().get_origin();
				count++;
			}
		}
		if (count == 0) {
			return _error_result("No CanvasItem nodes selected");
		}
		center /= count;

		Dictionary state = canvas->get_state();
		float zoom = state.get("zoom", 1.0);
		Vector2 view_size = canvas->get_size();
		Vector2 ofs = center - view_size / (2.0f * zoom);

		Dictionary new_state;
		new_state["ofs"] = ofs;
		canvas->set_state(new_state);

		return _success_result("Canvas focused on selection",
				Dictionary{ { "center", mcp_vector2_to_dict(center) }, { "node_count", count } });
	}

	// --- set_snap ---
	if (action == "set_snap") {
		Dictionary new_state;
		if (p_args.has("grid_snap")) {
			new_state["grid_snap_active"] = (bool)p_args["grid_snap"];
		}
		if (p_args.has("smart_snap")) {
			new_state["smart_snap_active"] = (bool)p_args["smart_snap"];
		}
		if (p_args.has("grid_step")) {
			new_state["grid_step"] = mcp_dict_to_vector2(p_args["grid_step"]);
		}

		canvas->set_state(new_state);
		return _success_result("Canvas snap settings updated", new_state);
	}

	return _error_result("Unknown action: '" + action + "'. Use: get_state, center_at, zoom, pan, focus, set_snap");
#else
	return _error_result("Editor functionality not available");
#endif
}

// --- Editor State Tool ---

Dictionary GodotMCPServer::_tool_editor_state(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	bool include_3d = p_args.get("include_3d", true);
	bool include_2d = p_args.get("include_2d", true);

	Dictionary data;

	// Scene info.
	Node *scene_root = _get_scene_root();
	if (scene_root) {
		Dictionary scene;
		scene["path"] = scene_root->get_scene_file_path();
		scene["root_type"] = scene_root->get_class();
		scene["root_name"] = scene_root->get_name();

		// Selected nodes.
		EditorSelection *selection = EditorInterface::get_singleton()->get_selection();
		TypedArray<Node> selected = selection->get_selected_nodes();
		Array selected_paths;
		for (int i = 0; i < selected.size(); i++) {
			Node *node = Object::cast_to<Node>(selected[i]);
			if (node) {
				selected_paths.push_back(String(node->get_path()));
			}
		}
		scene["selected_nodes"] = selected_paths;
		scene["selected_count"] = selected_paths.size();

		data["scene"] = scene;
	} else {
		data["scene"] = Variant();
	}

	// 3D viewport state.
	if (include_3d) {
		Node3DEditor *editor_3d = Node3DEditor::get_singleton();
		if (editor_3d) {
			Node3DEditorViewport *viewport = editor_3d->get_editor_viewport(0);
			if (viewport) {
				Dictionary vp_state = viewport->get_state();

				Dictionary vp3d;
				vp3d["position"] = mcp_vector3_to_dict(vp_state.get("position", Vector3()));
				vp3d["x_rotation"] = vp_state.get("x_rotation", 0.0);
				vp3d["y_rotation"] = vp_state.get("y_rotation", 0.0);
				vp3d["distance"] = vp_state.get("distance", 4.0);
				vp3d["orthogonal"] = vp_state.get("orthogonal", false);
				vp3d["display_mode"] = _display_mode_to_string(vp_state.get("display_mode", VP_DISPLAY_NORMAL));
				vp3d["grid"] = vp_state.get("grid", true);
				vp3d["gizmos"] = vp_state.get("gizmos", true);

				data["viewport_3d"] = vp3d;
			}
		}
	}

	// 2D canvas state.
	if (include_2d) {
		CanvasItemEditor *canvas = CanvasItemEditor::get_singleton();
		if (canvas) {
			Dictionary canvas_state = canvas->get_state();

			Dictionary vp2d;
			vp2d["zoom"] = canvas_state.get("zoom", 1.0);
			vp2d["offset"] = mcp_vector2_to_dict(canvas_state.get("ofs", Vector2()));
			vp2d["grid_snap"] = canvas_state.get("grid_snap_active", false);
			vp2d["smart_snap"] = canvas_state.get("smart_snap_active", false);

			data["viewport_2d"] = vp2d;
		}
	}

	return _success_result("Editor state retrieved", data);
#else
	return _error_result("Editor functionality not available");
#endif
}
