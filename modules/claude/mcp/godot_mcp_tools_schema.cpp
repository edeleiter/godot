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
