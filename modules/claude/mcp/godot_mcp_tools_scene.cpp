/**************************************************************************/
/*  godot_mcp_tools_scene.cpp                                             */
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
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#endif

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

// Scene persistence tools.

Dictionary GodotMCPServer::_tool_save_scene(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String path = p_args.get("path", "");

	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return _error_result("No scene is currently open");
	}

	if (!path.is_empty()) {
		// "Save as" to a specific path.
		String error;
		if (!_validate_scene_path(path, error)) {
			return _error_result(error);
		}
		EditorNode::get_singleton()->save_scene_to_path(path);
	} else {
		// Save to existing path.
		String current_path = scene_root->get_scene_file_path();
		if (current_path.is_empty()) {
			return _error_result("Scene has no file path. Provide a 'path' parameter for the initial save.");
		}
		Error err = EditorInterface::get_singleton()->save_scene();
		if (err != OK) {
			return _error_result("Failed to save scene (error " + itos(err) + ")");
		}
	}

	String saved_path = path.is_empty() ? scene_root->get_scene_file_path() : path;
	return _success_result("Scene saved: " + saved_path,
			Dictionary{ { "path", saved_path } });
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_new_scene(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String root_type = p_args.get("root_type", "");
	String root_name = p_args.get("root_name", "");

	String error;
	if (!_validate_node_type(root_type, error)) {
		return _error_result(error);
	}

	// Create the root node.
	Node *root = Object::cast_to<Node>(ClassDB::instantiate(root_type));
	if (!root) {
		return _error_result("Failed to instantiate: " + root_type);
	}

	if (root_name.is_empty()) {
		root_name = root_type;
	}
	root->set_name(root_name);

	// Create a new scene tab and set the root.
	EditorNode::get_singleton()->new_scene();
	EditorNode::get_singleton()->set_edited_scene(root);

	return _success_result("Created new scene with " + root_type + " root",
			Dictionary{ { "root_type", root_type }, { "root_name", root_name } });
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_open_scene(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String path = p_args.get("path", "");

	String error;
	if (!_validate_scene_path(path, error)) {
		return _error_result(error);
	}

	if (!FileAccess::exists(path)) {
		return _error_result("Scene file not found: " + path);
	}

	EditorInterface::get_singleton()->open_scene_from_path(path);

	return _success_result("Opened scene: " + path,
			Dictionary{ { "path", path } });
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_instance_scene(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String scene_path = p_args.get("scene_path", "");
	String parent_path = p_args.get("parent_path", "");
	String node_name = p_args.get("node_name", "");

	// Validate scene path.
	String error;
	if (!_validate_scene_path(scene_path, error)) {
		return _error_result(error);
	}

	if (!FileAccess::exists(scene_path)) {
		return _error_result("Scene file not found: " + scene_path);
	}

	// Validate parent.
	if (!_validate_node_path(parent_path, error)) {
		return _error_result(error);
	}

	Node *parent = _resolve_node_path(parent_path);
	if (!parent) {
		return _error_result("Parent node not found: " + parent_path);
	}

	// Load and instantiate the PackedScene.
	Ref<PackedScene> packed = ResourceLoader::load(scene_path, "PackedScene");
	if (!packed.is_valid()) {
		return _error_result("Failed to load scene: " + scene_path);
	}

	Node *instance = packed->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
	if (!instance) {
		return _error_result("Failed to instantiate scene: " + scene_path);
	}

	if (!node_name.is_empty()) {
		instance->set_name(node_name);
	}

	Node *scene_root = _get_scene_root();

	// Add with undo/redo.
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Instance " + scene_path.get_file());
	ur->add_do_method(parent, "add_child", instance, true);
	ur->add_do_method(instance, "set_owner", scene_root);
	ur->add_do_reference(instance);
	ur->add_undo_method(parent, "remove_child", instance);
	ur->commit_action();

	return _success_result("Instanced " + scene_path.get_file(),
			Dictionary{ { "node_path", String(instance->get_path()) },
					{ "scene_path", scene_path } });
#else
	return _error_result("Editor functionality not available");
#endif
}
