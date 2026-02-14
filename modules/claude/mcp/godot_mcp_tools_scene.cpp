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
#include "scene/2d/node_2d.h"
#include "scene/3d/node_3d.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#endif

// Serialize a MethodInfo's argument list to an Array of {name, type} Dictionaries.
static Array _serialize_arguments(const Vector<PropertyInfo> &p_args) {
	Array out;
	for (const PropertyInfo &arg : p_args) {
		Dictionary arg_info;
		arg_info["name"] = arg.name;
		arg_info["type"] = Variant::get_type_name(arg.type);
		out.push_back(arg_info);
	}
	return out;
}

// Determine the target type for a property on a node, handling the case where
// the current value is null but the property expects an Object (e.g.,
// MeshInstance3D.mesh before any mesh is assigned).
static Variant::Type _resolve_property_type(Node *p_node, const String &p_property) {
	Variant current = p_node->get(p_property);
	Variant::Type type = current.get_type();
	if (type != Variant::NIL) {
		return type;
	}

	List<PropertyInfo> props;
	p_node->get_property_list(&props);
	for (const PropertyInfo &pi : props) {
		if (pi.name == p_property && pi.type == Variant::OBJECT) {
			return Variant::OBJECT;
		}
	}
	return Variant::NIL;
}

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

	// Coerce value to match the property's expected type.
	Variant::Type target_type = _resolve_property_type(node, property);
	if (target_type != Variant::NIL) {
		value = _coerce_value(value, target_type);
	}

	Variant old_value = node->get(property);

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

// Introspection tools.

Dictionary GodotMCPServer::_tool_get_class_info(const Dictionary &p_args) {
	String class_name = p_args.get("class_name", "");
	bool include_properties = p_args.get("include_properties", true);
	bool include_methods = p_args.get("include_methods", false);
	bool include_signals = p_args.get("include_signals", true);
	bool include_enums = p_args.get("include_enums", false);
	bool inherited = p_args.get("inherited", false);

	if (class_name.is_empty()) {
		return _error_result("Missing required 'class_name' parameter");
	}

	if (!ClassDB::class_exists(class_name)) {
		return _error_result("Unknown class: " + class_name);
	}

	Dictionary data;
	data["class_name"] = class_name;

	// Inheritance chain.
	Array chain;
	String current = class_name;
	while (!current.is_empty()) {
		chain.push_back(current);
		current = ClassDB::get_parent_class(current);
	}
	data["inheritance"] = chain;

	// Properties.
	if (include_properties) {
		Array props_out;
		List<PropertyInfo> props;
		ClassDB::get_property_list(class_name, &props, !inherited);
		for (const PropertyInfo &pi : props) {
			if (pi.name.is_empty() || pi.name.begins_with("_")) {
				continue;
			}
			if ((pi.usage & PROPERTY_USAGE_CATEGORY) || (pi.usage & PROPERTY_USAGE_GROUP) || (pi.usage & PROPERTY_USAGE_SUBGROUP)) {
				continue;
			}
			Dictionary prop;
			prop["name"] = pi.name;
			prop["type"] = Variant::get_type_name(pi.type);
			if (!pi.hint_string.is_empty()) {
				prop["hint"] = pi.hint_string;
			}
			props_out.push_back(prop);
		}
		data["properties"] = props_out;
	}

	// Methods.
	if (include_methods) {
		Array methods_out;
		List<MethodInfo> methods;
		ClassDB::get_method_list(class_name, &methods, !inherited);
		for (const MethodInfo &mi : methods) {
			if (mi.name.begins_with("_")) {
				continue;
			}
			Dictionary method;
			method["name"] = mi.name;
			if (mi.return_val.type != Variant::NIL) {
				method["return_type"] = Variant::get_type_name(mi.return_val.type);
			}
			Array args = _serialize_arguments(mi.arguments);
			if (!args.is_empty()) {
				method["arguments"] = args;
			}
			methods_out.push_back(method);
		}
		data["methods"] = methods_out;
	}

	// Signals.
	if (include_signals) {
		Array signals_out;
		List<MethodInfo> signals;
		ClassDB::get_signal_list(class_name, &signals, !inherited);
		for (const MethodInfo &si : signals) {
			Dictionary sig;
			sig["name"] = si.name;
			Array args = _serialize_arguments(si.arguments);
			if (!args.is_empty()) {
				sig["arguments"] = args;
			}
			signals_out.push_back(sig);
		}
		data["signals"] = signals_out;
	}

	// Enums.
	if (include_enums) {
		Dictionary enums_out;
		List<StringName> enum_list;
		ClassDB::get_enum_list(class_name, &enum_list, !inherited);
		for (const StringName &enum_name : enum_list) {
			Dictionary enum_values;
			List<StringName> constants;
			ClassDB::get_enum_constants(class_name, enum_name, &constants, !inherited);
			for (const StringName &constant : constants) {
				enum_values[constant] = ClassDB::get_integer_constant(class_name, constant);
			}
			enums_out[enum_name] = enum_values;
		}
		data["enums"] = enums_out;
	}

	return _success_result("Class info for " + class_name, data);
}

Dictionary GodotMCPServer::_tool_get_node_info(const Dictionary &p_args) {
	String node_path = p_args.get("node_path", "");
	bool include_properties = p_args.get("include_properties", true);
	bool include_methods = p_args.get("include_methods", false);
	bool include_signals = p_args.get("include_signals", false);

	String error;
	if (!_validate_node_path(node_path, error)) {
		return _error_result(error);
	}

	Node *node = _resolve_node_path(node_path);
	if (!node) {
		return _error_result("Node not found: " + node_path);
	}

	Dictionary data;
	data["node_path"] = String(node->get_path());
	data["class"] = node->get_class();
	data["name"] = node->get_name();

	// Script info.
	Ref<Script> script = node->get_script();
	if (script.is_valid()) {
		data["script_path"] = script->get_path();
	}

	// Children summary.
	Array children;
	for (int i = 0; i < node->get_child_count(); i++) {
		Node *child = node->get_child(i);
		Dictionary child_info;
		child_info["name"] = child->get_name();
		child_info["class"] = child->get_class();
		children.push_back(child_info);
	}
	data["children"] = children;
	data["child_count"] = node->get_child_count();

	// Properties with current values.
	if (include_properties) {
		Array props_out;
		List<PropertyInfo> props;
		node->get_property_list(&props);

		String current_category;
		for (const PropertyInfo &pi : props) {
			if (pi.usage & PROPERTY_USAGE_CATEGORY) {
				current_category = pi.name;
				continue;
			}
			if ((pi.usage & PROPERTY_USAGE_GROUP) || (pi.usage & PROPERTY_USAGE_SUBGROUP)) {
				continue;
			}
			if (pi.name.is_empty() || pi.name.begins_with("_")) {
				continue;
			}
			if (!(pi.usage & PROPERTY_USAGE_EDITOR)) {
				continue;
			}

			Dictionary prop;
			prop["name"] = pi.name;
			prop["type"] = Variant::get_type_name(pi.type);

			Variant value = node->get(pi.name);
			// Serialize Object references as type name + path.
			if (pi.type == Variant::OBJECT) {
				Object *obj = value.get_validated_object();
				if (obj) {
					Resource *res = Object::cast_to<Resource>(obj);
					if (res) {
						Dictionary res_info;
						res_info["class"] = res->get_class();
						if (!res->get_path().is_empty()) {
							res_info["path"] = res->get_path();
						}
						prop["value"] = res_info;
					} else {
						prop["value"] = obj->get_class();
					}
				} else {
					prop["value"] = Variant();
				}
			} else {
				prop["value"] = value;
			}

			if (!current_category.is_empty()) {
				prop["category"] = current_category;
			}
			if (!pi.hint_string.is_empty()) {
				prop["hint"] = pi.hint_string;
			}

			props_out.push_back(prop);
		}
		data["properties"] = props_out;
	}

	// Methods.
	if (include_methods) {
		Array methods_out;
		List<MethodInfo> methods;
		node->get_method_list(&methods);
		for (const MethodInfo &mi : methods) {
			if (mi.name.begins_with("_")) {
				continue;
			}
			Dictionary method;
			method["name"] = mi.name;
			if (mi.return_val.type != Variant::NIL) {
				method["return_type"] = Variant::get_type_name(mi.return_val.type);
			}
			methods_out.push_back(method);
		}
		data["methods"] = methods_out;
	}

	// Signals and connections.
	if (include_signals) {
		Array signals_out;
		List<MethodInfo> signals;
		node->get_signal_list(&signals);
		for (const MethodInfo &si : signals) {
			Dictionary sig;
			sig["name"] = si.name;

			// Get connections for this signal.
			Array sig_connections;
			List<Object::Connection> conns;
			node->get_signal_connection_list(si.name, &conns);
			for (const Object::Connection &conn : conns) {
				Dictionary conn_info;
				Object *target = conn.callable.get_object();
				if (target) {
					Node *target_node = Object::cast_to<Node>(target);
					if (target_node) {
						conn_info["target"] = String(target_node->get_path());
					}
				}
				conn_info["method"] = conn.callable.get_method();
				sig_connections.push_back(conn_info);
			}
			if (!sig_connections.is_empty()) {
				sig["connections"] = sig_connections;
			}

			signals_out.push_back(sig);
		}
		data["signals"] = signals_out;
	}

	return _success_result("Node info for " + String(node->get_name()), data);
}

// Batch property tool.

Dictionary GodotMCPServer::_tool_set_properties_batch(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	Array operations = p_args.get("operations", Array());

	if (operations.is_empty()) {
		return _error_result("'operations' array is empty");
	}

	// Validate all operations first before committing any.
	struct BatchOp {
		Node *node;
		String property;
		Variant new_value;
		Variant old_value;
	};
	Vector<BatchOp> ops;

	for (int i = 0; i < operations.size(); i++) {
		Dictionary op = operations[i];
		String node_path = op.get("node_path", "");
		String property = op.get("property", "");

		if (node_path.is_empty() || property.is_empty()) {
			return _error_result("Operation " + itos(i) + ": missing node_path or property");
		}

		String error;
		if (!_validate_node_path(node_path, error)) {
			return _error_result("Operation " + itos(i) + ": " + error);
		}

		Node *node = _resolve_node_path(node_path);
		if (!node) {
			return _error_result("Operation " + itos(i) + ": node not found: " + node_path);
		}

		if (!op.has("value")) {
			return _error_result("Operation " + itos(i) + ": missing value");
		}

		Variant value = op["value"];
		Variant::Type target_type = _resolve_property_type(node, property);
		if (target_type != Variant::NIL) {
			value = _coerce_value(value, target_type);
		}

		BatchOp batch_op;
		batch_op.node = node;
		batch_op.property = property;
		batch_op.new_value = value;
		batch_op.old_value = node->get(property);
		ops.push_back(batch_op);
	}

	// All validated — commit as a single undo action.
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Set " + itos(ops.size()) + " properties");

	for (const BatchOp &op : ops) {
		ur->add_do_method(op.node, "set", op.property, op.new_value);
		ur->add_undo_method(op.node, "set", op.property, op.old_value);
	}

	ur->commit_action();

	return _success_result("Set " + itos(ops.size()) + " properties in 1 undo action",
			Dictionary{ { "count", ops.size() } });
#else
	return _error_result("Editor functionality not available");
#endif
}

// --- Transform Nodes Tool ---

// Vector helpers (local to this TU, same as in runtime/editor tools).
static Vector3 _scene_dict_to_vec3(const Dictionary &p_dict) {
	return Vector3(p_dict.get("x", 0.0), p_dict.get("y", 0.0), p_dict.get("z", 0.0));
}

static Vector2 _scene_dict_to_vec2(const Dictionary &p_dict) {
	return Vector2(p_dict.get("x", 0.0), p_dict.get("y", 0.0));
}

Dictionary GodotMCPServer::_tool_transform_nodes(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String action = p_args.get("action", "");
	if (action.is_empty()) {
		return _error_result("Missing required 'action' parameter");
	}

	Array node_paths = p_args.get("node_paths", Array());
	if (node_paths.is_empty()) {
		return _error_result("Missing or empty 'node_paths' parameter");
	}

	bool local = p_args.get("local", false);

	// Resolve all nodes first.
	Vector<Node *> nodes;
	for (int i = 0; i < node_paths.size(); i++) {
		String path = node_paths[i];
		String error;
		if (!_validate_node_path(path, error)) {
			return _error_result("Node " + itos(i) + ": " + error);
		}
		Node *node = _resolve_node_path(path);
		if (!node) {
			return _error_result("Node not found: " + path);
		}
		nodes.push_back(node);
	}

	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();

	// --- translate ---
	if (action == "translate") {
		if (!p_args.has("value")) {
			return _error_result("'translate' requires 'value' parameter");
		}
		Dictionary val = p_args["value"];

		ur->create_action("MCP: Translate " + itos(nodes.size()) + " nodes");
		for (Node *node : nodes) {
			Node3D *n3d = Object::cast_to<Node3D>(node);
			Node2D *n2d = Object::cast_to<Node2D>(node);
			if (n3d) {
				Vector3 delta = _scene_dict_to_vec3(val);
				if (local) {
					ur->add_do_method(n3d, "set_position", n3d->get_position() + delta);
					ur->add_undo_method(n3d, "set_position", n3d->get_position());
				} else {
					ur->add_do_method(n3d, "set_global_position", n3d->get_global_position() + delta);
					ur->add_undo_method(n3d, "set_global_position", n3d->get_global_position());
				}
			} else if (n2d) {
				Vector2 delta = _scene_dict_to_vec2(val);
				if (local) {
					ur->add_do_method(n2d, "set_position", n2d->get_position() + delta);
					ur->add_undo_method(n2d, "set_position", n2d->get_position());
				} else {
					ur->add_do_method(n2d, "set_global_position", n2d->get_global_position() + delta);
					ur->add_undo_method(n2d, "set_global_position", n2d->get_global_position());
				}
			} else {
				continue;
			}
		}
		ur->commit_action();
		return _success_result("Translated " + itos(nodes.size()) + " nodes");
	}

	// --- rotate ---
	if (action == "rotate") {
		if (!p_args.has("value")) {
			return _error_result("'rotate' requires 'value' parameter");
		}
		Dictionary val = p_args["value"];

		ur->create_action("MCP: Rotate " + itos(nodes.size()) + " nodes");
		for (Node *node : nodes) {
			Node3D *n3d = Object::cast_to<Node3D>(node);
			Node2D *n2d = Object::cast_to<Node2D>(node);
			if (n3d) {
				Vector3 delta = _scene_dict_to_vec3(val);
				ur->add_do_method(n3d, "set_rotation", n3d->get_rotation() + delta);
				ur->add_undo_method(n3d, "set_rotation", n3d->get_rotation());
			} else if (n2d) {
				// 2D rotation: use x component as angle in radians.
				float delta = val.get("x", 0.0);
				ur->add_do_method(n2d, "set_rotation", n2d->get_rotation() + delta);
				ur->add_undo_method(n2d, "set_rotation", n2d->get_rotation());
			} else {
				continue;
			}
		}
		ur->commit_action();
		return _success_result("Rotated " + itos(nodes.size()) + " nodes");
	}

	// --- scale ---
	if (action == "scale") {
		if (!p_args.has("value")) {
			return _error_result("'scale' requires 'value' parameter (scale factor)");
		}
		Dictionary val = p_args["value"];

		ur->create_action("MCP: Scale " + itos(nodes.size()) + " nodes");
		for (Node *node : nodes) {
			Node3D *n3d = Object::cast_to<Node3D>(node);
			Node2D *n2d = Object::cast_to<Node2D>(node);
			if (n3d) {
				Vector3 factor = Vector3(val.get("x", 1.0), val.get("y", 1.0), val.get("z", 1.0));
				ur->add_do_method(n3d, "set_scale", n3d->get_scale() * factor);
				ur->add_undo_method(n3d, "set_scale", n3d->get_scale());
			} else if (n2d) {
				Vector2 factor = Vector2(val.get("x", 1.0), val.get("y", 1.0));
				ur->add_do_method(n2d, "set_scale", n2d->get_scale() * factor);
				ur->add_undo_method(n2d, "set_scale", n2d->get_scale());
			} else {
				continue;
			}
		}
		ur->commit_action();
		return _success_result("Scaled " + itos(nodes.size()) + " nodes");
	}

	// --- set_transform ---
	if (action == "set_transform") {
		if (!p_args.has("transform")) {
			return _error_result("'set_transform' requires 'transform' parameter {origin, rotation, scale}");
		}
		Dictionary xform = p_args["transform"];

		ur->create_action("MCP: Set transform on " + itos(nodes.size()) + " nodes");
		for (Node *node : nodes) {
			Node3D *n3d = Object::cast_to<Node3D>(node);
			Node2D *n2d = Object::cast_to<Node2D>(node);
			if (n3d) {
				if (xform.has("origin")) {
					ur->add_do_method(n3d, "set_position", _scene_dict_to_vec3(xform["origin"]));
					ur->add_undo_method(n3d, "set_position", n3d->get_position());
				}
				if (xform.has("rotation")) {
					ur->add_do_method(n3d, "set_rotation", _scene_dict_to_vec3(xform["rotation"]));
					ur->add_undo_method(n3d, "set_rotation", n3d->get_rotation());
				}
				if (xform.has("scale")) {
					Dictionary s3 = xform["scale"];
					ur->add_do_method(n3d, "set_scale", Vector3(s3.get("x", 1.0), s3.get("y", 1.0), s3.get("z", 1.0)));
					ur->add_undo_method(n3d, "set_scale", n3d->get_scale());
				}
			} else if (n2d) {
				if (xform.has("origin")) {
					ur->add_do_method(n2d, "set_position", _scene_dict_to_vec2(xform["origin"]));
					ur->add_undo_method(n2d, "set_position", n2d->get_position());
				}
				if (xform.has("rotation")) {
					float rot = ((Dictionary)xform["rotation"]).get("x", 0.0);
					ur->add_do_method(n2d, "set_rotation", rot);
					ur->add_undo_method(n2d, "set_rotation", n2d->get_rotation());
				}
				if (xform.has("scale")) {
					Dictionary s2 = xform["scale"];
					ur->add_do_method(n2d, "set_scale", Vector2(s2.get("x", 1.0), s2.get("y", 1.0)));
					ur->add_undo_method(n2d, "set_scale", n2d->get_scale());
				}
			}
		}
		ur->commit_action();
		return _success_result("Transform set on " + itos(nodes.size()) + " nodes");
	}

	return _error_result("Unknown action: '" + action + "'. Use: translate, rotate, scale, set_transform");
#else
	return _error_result("Editor functionality not available");
#endif
}

// --- Scene Operations Tool ---

static void _set_owner_recursive(Node *p_node, Node *p_owner) {
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *child = p_node->get_child(i);
		child->set_owner(p_owner);
		_set_owner_recursive(child, p_owner);
	}
}

Dictionary GodotMCPServer::_tool_scene_operations(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String action = p_args.get("action", "");
	if (action.is_empty()) {
		return _error_result("Missing required 'action' parameter");
	}

	Array node_paths = p_args.get("node_paths", Array());
	if (node_paths.is_empty()) {
		return _error_result("Missing or empty 'node_paths' parameter");
	}

	// Resolve all nodes first.
	Vector<Node *> nodes;
	for (int i = 0; i < node_paths.size(); i++) {
		String path = node_paths[i];
		String error;
		if (!_validate_node_path(path, error)) {
			return _error_result("Node " + itos(i) + ": " + error);
		}
		Node *node = _resolve_node_path(path);
		if (!node) {
			return _error_result("Node not found: " + path);
		}
		nodes.push_back(node);
	}

	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return _error_result("No scene is currently open");
	}

	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();

	// --- duplicate ---
	if (action == "duplicate") {
		Dictionary offset_dict = p_args.get("offset", Dictionary());
		Array new_paths;

		ur->create_action("MCP: Duplicate " + itos(nodes.size()) + " nodes");
		for (Node *node : nodes) {
			Node *parent = node->get_parent();
			if (!parent) {
				continue;
			}

			Node *dup = node->duplicate();
			if (!dup) {
				continue;
			}

			ur->add_do_method(parent, "add_child", dup, true);
			ur->add_do_method(dup, "set_owner", scene_root);
			ur->add_do_reference(dup);
			ur->add_undo_method(parent, "remove_child", dup);

			new_paths.push_back(String(parent->get_path()) + "/" + String(dup->get_name()));
		}
		ur->commit_action();

		// Fix ownership of all descendants after commit (do methods executed).
		// Re-resolve the duplicated nodes to set child ownership.
		for (int i = 0; i < new_paths.size(); i++) {
			Node *dup = _resolve_node_path(new_paths[i]);
			if (dup) {
				_set_owner_recursive(dup, scene_root);

				// Apply position offset if specified.
				if (!offset_dict.is_empty()) {
					Node3D *n3d = Object::cast_to<Node3D>(dup);
					Node2D *n2d = Object::cast_to<Node2D>(dup);
					if (n3d) {
						n3d->set_position(n3d->get_position() + _scene_dict_to_vec3(offset_dict));
					} else if (n2d) {
						n2d->set_position(n2d->get_position() + _scene_dict_to_vec2(offset_dict));
					}
				}
			}
		}

		return _success_result("Duplicated " + itos(new_paths.size()) + " nodes",
				Dictionary{ { "new_paths", new_paths } });
	}

	// --- reparent ---
	if (action == "reparent") {
		String new_parent_path = p_args.get("new_parent", "");
		if (new_parent_path.is_empty()) {
			return _error_result("'reparent' requires 'new_parent' parameter");
		}

		String error;
		if (!_validate_node_path(new_parent_path, error)) {
			return _error_result(error);
		}
		Node *new_parent = _resolve_node_path(new_parent_path);
		if (!new_parent) {
			return _error_result("New parent not found: " + new_parent_path);
		}

		ur->create_action("MCP: Reparent " + itos(nodes.size()) + " nodes");
		for (Node *node : nodes) {
			if (node == scene_root) {
				continue; // Cannot reparent the scene root.
			}
			Node *old_parent = node->get_parent();
			if (!old_parent || old_parent == new_parent) {
				continue;
			}

			ur->add_do_method(old_parent, "remove_child", node);
			ur->add_do_method(new_parent, "add_child", node, true);
			ur->add_do_method(node, "set_owner", scene_root);
			ur->add_undo_method(new_parent, "remove_child", node);
			ur->add_undo_method(old_parent, "add_child", node, true);
			ur->add_undo_method(node, "set_owner", scene_root);
			ur->add_undo_reference(node);
		}
		ur->commit_action();

		// Fix descendant ownership after reparent.
		for (Node *node : nodes) {
			_set_owner_recursive(node, scene_root);
		}

		return _success_result("Reparented " + itos(nodes.size()) + " nodes to " + new_parent_path);
	}

	// --- set_visible ---
	if (action == "set_visible") {
		bool visible = p_args.get("visible", true);

		ur->create_action("MCP: Set visibility on " + itos(nodes.size()) + " nodes");
		for (Node *node : nodes) {
			// Both Node3D and CanvasItem have a "visible" property.
			Variant old_val = node->get("visible");
			if (old_val.get_type() != Variant::NIL) {
				ur->add_do_method(node, "set", "visible", visible);
				ur->add_undo_method(node, "set", "visible", old_val);
			}
		}
		ur->commit_action();
		return _success_result(visible ? "Made visible" : "Made hidden");
	}

	// --- toggle_lock ---
	if (action == "toggle_lock") {
		ur->create_action("MCP: Toggle lock on " + itos(nodes.size()) + " nodes");
		for (Node *node : nodes) {
			if (node->has_meta("_edit_lock_")) {
				ur->add_do_method(node, "remove_meta", "_edit_lock_");
				ur->add_undo_method(node, "set_meta", "_edit_lock_", true);
			} else {
				ur->add_do_method(node, "set_meta", "_edit_lock_", true);
				ur->add_undo_method(node, "remove_meta", "_edit_lock_");
			}
		}
		ur->commit_action();
		return _success_result("Toggled lock on " + itos(nodes.size()) + " nodes");
	}

	// --- group ---
	if (action == "group") {
		String group_name = p_args.get("group_name", "");
		if (group_name.is_empty()) {
			return _error_result("'group' action requires 'group_name' parameter");
		}

		int added = 0;
		int removed = 0;
		ur->create_action("MCP: Toggle group '" + group_name + "' on " + itos(nodes.size()) + " nodes");
		for (Node *node : nodes) {
			if (node->is_in_group(group_name)) {
				ur->add_do_method(node, "remove_from_group", group_name);
				ur->add_undo_method(node, "add_to_group", group_name, true);
				removed++;
			} else {
				ur->add_do_method(node, "add_to_group", group_name, true);
				ur->add_undo_method(node, "remove_from_group", group_name);
				added++;
			}
		}
		ur->commit_action();

		return _success_result("Group '" + group_name + "': added " + itos(added) + ", removed " + itos(removed));
	}

	return _error_result("Unknown action: '" + action + "'. Use: duplicate, reparent, set_visible, toggle_lock, group");
#else
	return _error_result("Editor functionality not available");
#endif
}
