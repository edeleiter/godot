/**************************************************************************/
/*  godot_mcp_tools_schema.cpp                                            */
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
	set_prop_params.push_back(Dictionary{ { "name", "property" }, { "type", "string" }, { "description", "Property name. Use colon syntax for sub-resource properties (e.g., 'environment:rt_reflections_enabled', 'mesh:material:albedo_color')" }, { "required", true } });
	set_prop_params.push_back(Dictionary{ { "name", "value" }, { "description", "New property value (as JSON)" }, { "required", true } });
	tools.push_back(_define_tool(
			"godot_set_property",
			"Set a property on a node. Supports colon-separated sub-resource paths (e.g., 'environment:background_mode').",
			_make_schema(set_prop_params)));

	Array get_prop_params;
	get_prop_params.push_back(Dictionary{ { "name", "node_path" }, { "type", "string" }, { "description", "Path to the node" }, { "required", true } });
	get_prop_params.push_back(Dictionary{ { "name", "property" }, { "type", "string" }, { "description", "Property name. Use colon syntax for sub-resource properties (e.g., 'environment:rt_reflections_enabled')" }, { "required", true } });
	tools.push_back(_define_tool(
			"godot_get_property",
			"Get a property value from a node. Supports colon-separated sub-resource paths.",
			_make_schema(get_prop_params)));

	// Scene persistence tools.
	Array save_scene_params;
	save_scene_params.push_back(Dictionary{ { "name", "path" }, { "type", "string" }, { "description", "Optional path for 'save as' (e.g., 'res://levels/level1.tscn'). If empty, saves to the current scene's existing path." }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_save_scene",
			"Save the current scene to disk. Optionally specify a new path for 'save as'.",
			_make_schema(save_scene_params)));

	Array new_scene_params;
	new_scene_params.push_back(Dictionary{ { "name", "root_type" }, { "type", "string" }, { "description", "Node type for the scene root (e.g., 'Node3D', 'Node2D', 'Control')" }, { "required", true } });
	new_scene_params.push_back(Dictionary{ { "name", "root_name" }, { "type", "string" }, { "description", "Name for the root node (defaults to root_type)" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_new_scene",
			"Create a new empty scene with a typed root node and make it the active edited scene",
			_make_schema(new_scene_params)));

	Array open_scene_params;
	open_scene_params.push_back(Dictionary{ { "name", "path" }, { "type", "string" }, { "description", "Resource path to the scene file (e.g., 'res://main.tscn')" }, { "required", true } });
	tools.push_back(_define_tool(
			"godot_open_scene",
			"Open an existing scene file in the editor",
			_make_schema(open_scene_params)));

	Array instance_scene_params;
	instance_scene_params.push_back(Dictionary{ { "name", "scene_path" }, { "type", "string" }, { "description", "Resource path to the scene to instance (e.g., 'res://enemies/enemy.tscn')" }, { "required", true } });
	instance_scene_params.push_back(Dictionary{ { "name", "parent_path" }, { "type", "string" }, { "description", "Path to parent node in the current scene" }, { "required", true } });
	instance_scene_params.push_back(Dictionary{ { "name", "node_name" }, { "type", "string" }, { "description", "Name for the instanced node (defaults to scene root name)" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_instance_scene",
			"Instance a PackedScene as a child of a node in the current scene. This is Godot's composition/prefab pattern.",
			_make_schema(instance_scene_params)));

	// Signal tools.
	Array connect_signal_params;
	connect_signal_params.push_back(Dictionary{ { "name", "source_path" }, { "type", "string" }, { "description", "Path to the node that emits the signal" }, { "required", true } });
	connect_signal_params.push_back(Dictionary{ { "name", "signal_name" }, { "type", "string" }, { "description", "Name of the signal (e.g., 'pressed', 'body_entered')" }, { "required", true } });
	connect_signal_params.push_back(Dictionary{ { "name", "target_path" }, { "type", "string" }, { "description", "Path to the node that receives the signal" }, { "required", true } });
	connect_signal_params.push_back(Dictionary{ { "name", "method_name" }, { "type", "string" }, { "description", "Method to call on the target node" }, { "required", true } });
	tools.push_back(_define_tool(
			"godot_connect_signal",
			"Connect a signal from one node to a method on another node. The primary Godot event/messaging pattern.",
			_make_schema(connect_signal_params)));

	Array disconnect_signal_params;
	disconnect_signal_params.push_back(Dictionary{ { "name", "source_path" }, { "type", "string" }, { "description", "Path to the node that emits the signal" }, { "required", true } });
	disconnect_signal_params.push_back(Dictionary{ { "name", "signal_name" }, { "type", "string" }, { "description", "Name of the signal" }, { "required", true } });
	disconnect_signal_params.push_back(Dictionary{ { "name", "target_path" }, { "type", "string" }, { "description", "Path to the node that receives the signal" }, { "required", true } });
	disconnect_signal_params.push_back(Dictionary{ { "name", "method_name" }, { "type", "string" }, { "description", "Method name on the target" }, { "required", true } });
	tools.push_back(_define_tool(
			"godot_disconnect_signal",
			"Disconnect a signal connection between two nodes",
			_make_schema(disconnect_signal_params)));

	// Project settings tool.
	Array project_settings_params;
	project_settings_params.push_back(Dictionary{ { "name", "action" }, { "type", "string" }, { "description", "Action: 'get', 'set', or 'list'" }, { "required", true } });
	project_settings_params.push_back(Dictionary{ { "name", "setting" }, { "type", "string" }, { "description", "Setting path (e.g., 'display/window/size/viewport_width'). Required for 'get' and 'set'." }, { "required", false } });
	project_settings_params.push_back(Dictionary{ { "name", "value" }, { "description", "New value for the setting (required for 'set')" }, { "required", false } });
	project_settings_params.push_back(Dictionary{ { "name", "prefix" }, { "type", "string" }, { "description", "Filter prefix for 'list' action (e.g., 'display/window', 'input/'). If empty, lists common settings." }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_project_settings",
			"Get, set, or list project settings (window size, physics, input map, rendering, etc.)",
			_make_schema(project_settings_params)));

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

	// Script validation tool.
	Array validate_script_params;
	validate_script_params.push_back(Dictionary{ { "name", "path" }, { "type", "string" }, { "description", "Resource path to the script (e.g., 'res://scripts/player.gd')" }, { "required", true } });
	tools.push_back(_define_tool(
			"godot_validate_script",
			"Validate a GDScript file and return compilation errors/warnings without running the game.",
			_make_schema(validate_script_params)));

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

	// Wait tool.
	Array wait_params;
	wait_params.push_back(Dictionary{ { "name", "duration_ms" }, { "type", "integer" }, { "description", "Wall-clock time to wait in milliseconds (100-10000, default 1000). Editor continues processing events during wait." }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_wait",
			"Wait for a specified duration while keeping the editor responsive. Useful for temporal convergence of RT effects or waiting for game to initialize after godot_run_scene.",
			_make_schema(wait_params)));

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

	// Runtime errors tool.
	Array get_errors_params;
	get_errors_params.push_back(Dictionary{ { "name", "limit" }, { "type", "integer" }, { "description", "Maximum number of errors to return (default 50)" }, { "required", false } });
	get_errors_params.push_back(Dictionary{ { "name", "since_timestamp" }, { "type", "number" }, { "description", "Only return errors after this Unix timestamp" }, { "required", false } });
	get_errors_params.push_back(Dictionary{ { "name", "severity" }, { "type", "string" }, { "description", "Filter by severity: 'all' (default), 'error', or 'warning'" }, { "required", false } });
	get_errors_params.push_back(Dictionary{ { "name", "include_callstack" }, { "type", "boolean" }, { "description", "Include call stack frames (default: true)" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_get_runtime_errors",
			"Get structured runtime errors/warnings from the running game with source locations and call stacks. Shows the same data as the editor's Errors tab.",
			_make_schema(get_errors_params)));

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
			"Get camera override state from the running game",
			_make_schema(camera_info_params)));

	// Introspection tools.
	Array class_info_params;
	class_info_params.push_back(Dictionary{ { "name", "class_name" }, { "type", "string" }, { "description", "Godot class name (e.g., 'CharacterBody3D', 'GPUParticles3D')" }, { "required", true } });
	class_info_params.push_back(Dictionary{ { "name", "include_properties" }, { "type", "boolean" }, { "description", "Include property list (default: true)" }, { "required", false } });
	class_info_params.push_back(Dictionary{ { "name", "include_methods" }, { "type", "boolean" }, { "description", "Include method list (default: false)" }, { "required", false } });
	class_info_params.push_back(Dictionary{ { "name", "include_signals" }, { "type", "boolean" }, { "description", "Include signal list (default: true)" }, { "required", false } });
	class_info_params.push_back(Dictionary{ { "name", "include_enums" }, { "type", "boolean" }, { "description", "Include enum definitions (default: false)" }, { "required", false } });
	class_info_params.push_back(Dictionary{ { "name", "inherited" }, { "type", "boolean" }, { "description", "Include inherited members (default: false)" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_get_class_info",
			"Query ClassDB for a Godot class: properties, methods, signals, enums, and inheritance chain. Read-only introspection.",
			_make_schema(class_info_params)));

	Array node_info_params;
	node_info_params.push_back(Dictionary{ { "name", "node_path" }, { "type", "string" }, { "description", "Path to the node (e.g., '/root/Main/Player')" }, { "required", true } });
	node_info_params.push_back(Dictionary{ { "name", "include_properties" }, { "type", "boolean" }, { "description", "Include all properties with current values (default: true)" }, { "required", false } });
	node_info_params.push_back(Dictionary{ { "name", "include_methods" }, { "type", "boolean" }, { "description", "Include method list (default: false)" }, { "required", false } });
	node_info_params.push_back(Dictionary{ { "name", "include_signals" }, { "type", "boolean" }, { "description", "Include signals and connections (default: false)" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_get_node_info",
			"Full inspector for a single node: all properties with current values, script, signals, children. Returns everything in 1 call.",
			_make_schema(node_info_params)));

	// Batch property tool.
	Array batch_props_params;
	batch_props_params.push_back(Dictionary{ { "name", "operations" }, { "type", "array" }, { "description", "Array of {node_path, property, value} objects to set in a single undo action" }, { "required", true } });
	tools.push_back(_define_tool(
			"godot_set_properties_batch",
			"Set multiple properties across nodes in one call with a single undo action. Ctrl+Z reverts all changes together.",
			_make_schema(batch_props_params)));

	// Project files tool.
	Array project_files_params;
	project_files_params.push_back(Dictionary{ { "name", "action" }, { "type", "string" }, { "description", "Action: 'list' (browse files), 'scan' (trigger editor filesystem rescan), or 'diagnostics' (scan for invalid imports)" }, { "required", true } });
	project_files_params.push_back(Dictionary{ { "name", "path" }, { "type", "string" }, { "description", "Directory path (default: 'res://')" }, { "required", false } });
	project_files_params.push_back(Dictionary{ { "name", "recursive" }, { "type", "boolean" }, { "description", "List files recursively (default: false)" }, { "required", false } });
	project_files_params.push_back(Dictionary{ { "name", "extensions" }, { "type", "array" }, { "description", "Filter by file extensions, e.g. ['tscn', 'gd', 'tres']" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_project_files",
			"List project files with resource type metadata, trigger a filesystem rescan after external file changes, or run import diagnostics.",
			_make_schema(project_files_params)));

	// Input map tool.
	Array input_map_params;
	input_map_params.push_back(Dictionary{ { "name", "action" }, { "type", "string" }, { "description", "Action: 'list', 'add_action', 'remove_action', 'add_binding', or 'remove_binding'" }, { "required", true } });
	input_map_params.push_back(Dictionary{ { "name", "action_name" }, { "type", "string" }, { "description", "Input action name (e.g., 'move_forward'). Required for all actions except 'list'." }, { "required", false } });
	input_map_params.push_back(Dictionary{ { "name", "deadzone" }, { "type", "number" }, { "description", "Deadzone for the action (default: 0.5). Used with 'add_action'." }, { "required", false } });
	input_map_params.push_back(Dictionary{ { "name", "binding" }, { "type", "object" }, { "description", "Binding definition: {type: 'key'|'mouse_button'|'joypad_button'|'joypad_motion', key/button/axis, ...}" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_input_map",
			"Manage input actions and bindings. Add/remove actions, bind keys/buttons/axes for player controls.",
			_make_schema(input_map_params)));

	// Navigation baking tool.
	Array bake_nav_params;
	bake_nav_params.push_back(Dictionary{ { "name", "node_path" }, { "type", "string" }, { "description", "Path to a NavigationRegion3D node" }, { "required", true } });
	tools.push_back(_define_tool(
			"godot_bake_navigation",
			"Trigger navigation mesh baking on a NavigationRegion3D. Required for AI pathfinding.",
			_make_schema(bake_nav_params)));

	// Animation tools.
	Array create_anim_params;
	create_anim_params.push_back(Dictionary{ { "name", "node_path" }, { "type", "string" }, { "description", "Path to an AnimationPlayer node" }, { "required", true } });
	create_anim_params.push_back(Dictionary{ { "name", "animation_name" }, { "type", "string" }, { "description", "Name for the new animation (e.g., 'idle', 'walk', 'attack')" }, { "required", true } });
	create_anim_params.push_back(Dictionary{ { "name", "length" }, { "type", "number" }, { "description", "Animation length in seconds" }, { "required", true } });
	create_anim_params.push_back(Dictionary{ { "name", "library_name" }, { "type", "string" }, { "description", "Animation library name (default: '' for the default library)" }, { "required", false } });
	create_anim_params.push_back(Dictionary{ { "name", "loop_mode" }, { "type", "string" }, { "description", "Loop mode: 'none' (default), 'linear', or 'ping_pong'" }, { "required", false } });
	create_anim_params.push_back(Dictionary{ { "name", "tracks" }, { "type", "array" }, { "description", "Optional array of track definitions: [{type, path, keys}]. Types: 'value', 'position_3d', 'rotation_3d', 'scale_3d', 'method', 'blend_shape'" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_create_animation",
			"Create an animation with tracks and keyframes on an AnimationPlayer or AnimationMixer. Supports position/rotation/scale/value/method tracks.",
			_make_schema(create_anim_params)));

	Array anim_info_params;
	anim_info_params.push_back(Dictionary{ { "name", "node_path" }, { "type", "string" }, { "description", "Path to an AnimationPlayer or AnimationTree node" }, { "required", true } });
	anim_info_params.push_back(Dictionary{ { "name", "animation_name" }, { "type", "string" }, { "description", "Optional: get detailed info for a specific animation" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_get_animation_info",
			"Inspect animations on an AnimationPlayer or AnimationTree: library list, animation details, track summaries, state machine info.",
			_make_schema(anim_info_params)));

	// Editor log tool.
	Array editor_log_params;
	editor_log_params.push_back(Dictionary{ { "name", "limit" }, { "type", "integer" }, { "description", "Maximum number of messages to return (default 100)" }, { "required", false } });
	editor_log_params.push_back(Dictionary{ { "name", "types" }, { "type", "array" }, { "description", "Filter by message types: 'std', 'error', 'warning', 'editor', 'std_rich'. Returns all types if empty." }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_get_editor_log",
			"Get editor-level log messages from the Output panel (startup messages, tool script output, ERR_PRINT/WARN_PRINT from editor context).",
			_make_schema(editor_log_params)));

	// Editor screenshot tool.
	Array editor_screenshot_params;
	editor_screenshot_params.push_back(Dictionary{ { "name", "source" }, { "type", "string" }, { "description", "Screenshot source: '3d' (default), '2d', or 'game'" }, { "required", false } });
	editor_screenshot_params.push_back(Dictionary{ { "name", "viewport_idx" }, { "type", "integer" }, { "description", "3D viewport index 0-3 (default 0)" }, { "required", false } });
	editor_screenshot_params.push_back(Dictionary{ { "name", "scale" }, { "type", "number" }, { "description", "Downscale factor 0.1-1.0 (default 0.5 = half resolution)" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_editor_screenshot",
			"Capture a screenshot from the editor viewport (3D or 2D) or the running game. Returns a base64 PNG image.",
			_make_schema(editor_screenshot_params)));

	// Editor viewport camera tool.
	Array editor_camera_params;
	editor_camera_params.push_back(Dictionary{ { "name", "action" }, { "type", "string" }, { "description", "Camera action: 'move', 'orbit', 'look_at', 'focus', 'set_view', or 'get_state'" }, { "required", true } });
	editor_camera_params.push_back(Dictionary{ { "name", "viewport_idx" }, { "type", "integer" }, { "description", "3D viewport index 0-3 (default 0)" }, { "required", false } });
	editor_camera_params.push_back(Dictionary{ { "name", "position" }, { "type", "object" }, { "description", "Camera focal point {x, y, z} (for 'move')" }, { "required", false } });
	editor_camera_params.push_back(Dictionary{ { "name", "distance" }, { "type", "number" }, { "description", "Distance from focal point (for 'move', 'orbit')" }, { "required", false } });
	editor_camera_params.push_back(Dictionary{ { "name", "x_rotation" }, { "type", "number" }, { "description", "Pitch in radians (for 'move', 'orbit')" }, { "required", false } });
	editor_camera_params.push_back(Dictionary{ { "name", "y_rotation" }, { "type", "number" }, { "description", "Yaw in radians (for 'move', 'orbit')" }, { "required", false } });
	editor_camera_params.push_back(Dictionary{ { "name", "target" }, { "type", "object" }, { "description", "Look-at target {x, y, z} (for 'look_at')" }, { "required", false } });
	editor_camera_params.push_back(Dictionary{ { "name", "from" }, { "type", "object" }, { "description", "Camera position {x, y, z} (for 'look_at')" }, { "required", false } });
	editor_camera_params.push_back(Dictionary{ { "name", "view" }, { "type", "string" }, { "description", "View preset: 'front', 'back', 'left', 'right', 'top', 'bottom' (for 'set_view')" }, { "required", false } });
	editor_camera_params.push_back(Dictionary{ { "name", "orthogonal" }, { "type", "boolean" }, { "description", "Toggle orthogonal projection" }, { "required", false } });
	editor_camera_params.push_back(Dictionary{ { "name", "fov_scale" }, { "type", "number" }, { "description", "FOV scale 0.1-2.5" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_editor_viewport_camera",
			"Control the 3D editor viewport camera. Move, orbit, look at targets, focus on selection, or set preset views.",
			_make_schema(editor_camera_params)));

	// Editor control tool.
	Array editor_control_params;
	editor_control_params.push_back(Dictionary{ { "name", "action" }, { "type", "string" }, { "description", "Action: 'switch_panel', 'set_display_mode', 'toggle_grid', or 'get_state'" }, { "required", true } });
	editor_control_params.push_back(Dictionary{ { "name", "panel" }, { "type", "string" }, { "description", "'2D', '3D', 'Script', or 'AssetLib' (for 'switch_panel')" }, { "required", false } });
	editor_control_params.push_back(Dictionary{ { "name", "display_mode" }, { "type", "string" }, { "description", "'normal', 'wireframe', 'overdraw', 'lighting', or 'unshaded' (for 'set_display_mode')" }, { "required", false } });
	editor_control_params.push_back(Dictionary{ { "name", "viewport_idx" }, { "type", "integer" }, { "description", "Which 3D viewport to affect (default 0)" }, { "required", false } });
	editor_control_params.push_back(Dictionary{ { "name", "grid" }, { "type", "boolean" }, { "description", "Grid visibility (for 'toggle_grid')" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_editor_control",
			"Control editor workspace: switch panels, set 3D display mode, toggle grid visibility.",
			_make_schema(editor_control_params)));

	// Canvas view tool.
	Array canvas_view_params;
	canvas_view_params.push_back(Dictionary{ { "name", "action" }, { "type", "string" }, { "description", "Action: 'get_state', 'center_at', 'zoom', 'pan', 'focus', or 'set_snap'" }, { "required", true } });
	canvas_view_params.push_back(Dictionary{ { "name", "position" }, { "type", "object" }, { "description", "World position {x, y} (for 'center_at', 'pan')" }, { "required", false } });
	canvas_view_params.push_back(Dictionary{ { "name", "zoom" }, { "type", "number" }, { "description", "Zoom level (for 'zoom')" }, { "required", false } });
	canvas_view_params.push_back(Dictionary{ { "name", "grid_snap" }, { "type", "boolean" }, { "description", "Enable grid snapping (for 'set_snap')" }, { "required", false } });
	canvas_view_params.push_back(Dictionary{ { "name", "smart_snap" }, { "type", "boolean" }, { "description", "Enable smart snapping (for 'set_snap')" }, { "required", false } });
	canvas_view_params.push_back(Dictionary{ { "name", "grid_step" }, { "type", "object" }, { "description", "Grid step size {x, y} (for 'set_snap')" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_canvas_view",
			"Control the 2D canvas editor: pan, zoom, center on points, focus selection, configure snapping.",
			_make_schema(canvas_view_params)));

	// Transform nodes tool.
	Array transform_nodes_params;
	transform_nodes_params.push_back(Dictionary{ { "name", "action" }, { "type", "string" }, { "description", "Action: 'translate', 'rotate', 'scale', or 'set_transform'" }, { "required", true } });
	transform_nodes_params.push_back(Dictionary{ { "name", "node_paths" }, { "type", "array" }, { "description", "Node paths to transform" }, { "required", true } });
	transform_nodes_params.push_back(Dictionary{ { "name", "value" }, { "type", "object" }, { "description", "Delta vector {x, y, z} for translate/rotate/scale" }, { "required", false } });
	transform_nodes_params.push_back(Dictionary{ { "name", "local" }, { "type", "boolean" }, { "description", "Apply in local space (default false = global)" }, { "required", false } });
	transform_nodes_params.push_back(Dictionary{ { "name", "transform" }, { "type", "object" }, { "description", "Full transform for 'set_transform': {origin: {x,y,z}, rotation: {x,y,z}, scale: {x,y,z}}" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_transform_nodes",
			"Apply relative transforms to nodes (translate, rotate, scale by delta). Works on both Node3D and Node2D with undo/redo.",
			_make_schema(transform_nodes_params)));

	// Scene operations tool.
	Array scene_ops_params;
	scene_ops_params.push_back(Dictionary{ { "name", "action" }, { "type", "string" }, { "description", "Action: 'duplicate', 'reparent', 'set_visible', 'toggle_lock', or 'group'" }, { "required", true } });
	scene_ops_params.push_back(Dictionary{ { "name", "node_paths" }, { "type", "array" }, { "description", "Target node paths" }, { "required", true } });
	scene_ops_params.push_back(Dictionary{ { "name", "new_parent" }, { "type", "string" }, { "description", "New parent path (for 'reparent')" }, { "required", false } });
	scene_ops_params.push_back(Dictionary{ { "name", "visible" }, { "type", "boolean" }, { "description", "Visibility state (for 'set_visible')" }, { "required", false } });
	scene_ops_params.push_back(Dictionary{ { "name", "group_name" }, { "type", "string" }, { "description", "Group name (for 'group' — toggles membership)" }, { "required", false } });
	scene_ops_params.push_back(Dictionary{ { "name", "offset" }, { "type", "object" }, { "description", "Position offset {x,y,z} for duplicated nodes" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_scene_operations",
			"Hierarchy operations: duplicate nodes, reparent, set visibility, toggle editor lock, manage groups. All with undo/redo.",
			_make_schema(scene_ops_params)));

	// Editor state tool.
	Array editor_state_params;
	editor_state_params.push_back(Dictionary{ { "name", "include_3d" }, { "type", "boolean" }, { "description", "Include 3D viewport state (default true)" }, { "required", false } });
	editor_state_params.push_back(Dictionary{ { "name", "include_2d" }, { "type", "boolean" }, { "description", "Include 2D canvas state (default true)" }, { "required", false } });
	tools.push_back(_define_tool(
			"godot_editor_state",
			"Get comprehensive editor state in one call: viewport cameras, snap settings, scene info, selected nodes.",
			_make_schema(editor_state_params)));

	Dictionary result;
	result["tools"] = tools;
	return result;
}
