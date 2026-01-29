/**************************************************************************/
/*  mcp_scene_serializer.cpp                                              */
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

#include "mcp_scene_serializer.h"

#include "core/io/resource_loader.h"
#include "core/object/script_language.h"
#include "scene/resources/packed_scene.h"

void MCPSceneSerializer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("serialize_scene", "root"), &MCPSceneSerializer::serialize_scene);
	ClassDB::bind_method(D_METHOD("serialize_selection", "nodes"), &MCPSceneSerializer::serialize_selection);
	ClassDB::bind_method(D_METHOD("serialize_node_with_context", "node", "ancestor_levels"), &MCPSceneSerializer::serialize_node_with_context, DEFVAL(2));

	ClassDB::bind_method(D_METHOD("set_detail_level", "level"), &MCPSceneSerializer::set_detail_level);
	ClassDB::bind_method(D_METHOD("get_detail_level"), &MCPSceneSerializer::get_detail_level);
	ClassDB::bind_method(D_METHOD("set_max_depth", "depth"), &MCPSceneSerializer::set_max_depth);
	ClassDB::bind_method(D_METHOD("get_max_depth"), &MCPSceneSerializer::get_max_depth);
	ClassDB::bind_method(D_METHOD("set_max_nodes", "nodes"), &MCPSceneSerializer::set_max_nodes);
	ClassDB::bind_method(D_METHOD("get_max_nodes"), &MCPSceneSerializer::get_max_nodes);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "detail_level", PROPERTY_HINT_ENUM, "Minimal,Standard,Full"), "set_detail_level", "get_detail_level");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_depth"), "set_max_depth", "get_max_depth");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_nodes"), "set_max_nodes", "get_max_nodes");

	BIND_ENUM_CONSTANT(DETAIL_MINIMAL);
	BIND_ENUM_CONSTANT(DETAIL_STANDARD);
	BIND_ENUM_CONSTANT(DETAIL_FULL);
}

MCPSceneSerializer::MCPSceneSerializer() {
	_init_property_sets();
}

void MCPSceneSerializer::_init_property_sets() {
	// Security-sensitive patterns - substring matched, never serialize.
	security_patterns.push_back("password");
	security_patterns.push_back("api_key");
	security_patterns.push_back("secret");
	security_patterns.push_back("token");
	security_patterns.push_back("credential");
	security_patterns.push_back("auth");

	// Large binary data - exact match, skip for performance.
	property_blacklist.insert("image");
	property_blacklist.insert("texture");
	property_blacklist.insert("audio");
	property_blacklist.insert("mesh");
	property_blacklist.insert("buffer");
	property_blacklist.insert("data");

	// Internal engine properties.
	property_blacklist.insert("_import_path");
	property_blacklist.insert("_bundled");

	// Properties to always include at DETAIL_STANDARD
	standard_properties.insert("position");
	standard_properties.insert("rotation");
	standard_properties.insert("scale");
	standard_properties.insert("transform");
	standard_properties.insert("global_position");
	standard_properties.insert("global_transform");
	standard_properties.insert("visible");
	standard_properties.insert("modulate");
	standard_properties.insert("z_index");
	standard_properties.insert("collision_layer");
	standard_properties.insert("collision_mask");
	standard_properties.insert("process_mode");
	standard_properties.insert("unique_name_in_owner");
}

void MCPSceneSerializer::set_detail_level(DetailLevel p_level) {
	detail_level = p_level;
}

void MCPSceneSerializer::set_max_depth(int p_depth) {
	max_depth = MAX(1, p_depth);
}

void MCPSceneSerializer::set_max_nodes(int p_nodes) {
	max_nodes = MAX(1, p_nodes);
}

String MCPSceneSerializer::node_path_to_string(const NodePath &p_path) {
	return String(p_path);
}

Dictionary MCPSceneSerializer::serialize_scene(Node *p_root) {
	ERR_FAIL_NULL_V(p_root, Dictionary());

	current_node_count = 0;

	Dictionary result;
	result["scene_path"] = p_root->get_scene_file_path();
	result["root"] = _serialize_node(p_root, 0);
	result["truncated"] = current_node_count >= max_nodes;
	result["node_count"] = current_node_count;

	return result;
}

Dictionary MCPSceneSerializer::serialize_selection(const TypedArray<Node> &p_nodes) {
	current_node_count = 0;

	Array nodes;
	for (int i = 0; i < p_nodes.size(); i++) {
		Node *node = Object::cast_to<Node>(p_nodes[i]);
		if (node) {
			nodes.push_back(_serialize_node(node, 0));
		}
	}

	Dictionary result;
	result["selected_nodes"] = nodes;
	result["count"] = nodes.size();
	return result;
}

Dictionary MCPSceneSerializer::serialize_node_with_context(Node *p_node, int p_ancestor_levels) {
	ERR_FAIL_NULL_V(p_node, Dictionary());

	current_node_count = 0;

	// Find ancestor at specified level
	Node *context_root = p_node;
	for (int i = 0; i < p_ancestor_levels && context_root->get_parent(); i++) {
		context_root = context_root->get_parent();
	}

	Dictionary result;
	result["context"] = _serialize_node(context_root, 0);
	result["target_path"] = node_path_to_string(p_node->get_path());
	return result;
}

Dictionary MCPSceneSerializer::_serialize_node(Node *p_node, int p_depth) {
	Dictionary result;

	// Check limits
	if (p_depth > max_depth || current_node_count >= max_nodes) {
		result["truncated"] = true;
		return result;
	}

	current_node_count++;

	// Basic info (always included)
	result["name"] = p_node->get_name();
	result["type"] = p_node->get_class();
	result["path"] = node_path_to_string(p_node->get_path());

	// Properties based on detail level
	if (detail_level != DETAIL_MINIMAL) {
		Dictionary props = _serialize_properties(p_node);
		if (!props.is_empty()) {
			result["properties"] = props;
		}
	}

	// Script info
	Ref<Script> script = p_node->get_script();
	if (script.is_valid()) {
		Dictionary script_info;
		script_info["path"] = script->get_path();
		StringName global_name = script->get_global_name();
		if (!String(global_name).is_empty()) {
			script_info["class_name"] = String(global_name);
		}
		result["script"] = script_info;
	}

	// Custom signals (not built-in ones)
	Array signals = _get_node_signals(p_node);
	if (!signals.is_empty()) {
		result["signals"] = signals;
	}

	// Children
	int child_count = p_node->get_child_count();
	if (child_count > 0 && p_depth < max_depth) {
		Array children;
		for (int i = 0; i < child_count; i++) {
			Node *child = p_node->get_child(i);

			// Skip internal nodes (names starting with _)
			String child_name = child->get_name();
			if (child_name.begins_with("_")) {
				continue;
			}

			// Skip if we've hit the node limit
			if (current_node_count >= max_nodes) {
				Dictionary truncated_child;
				truncated_child["truncated"] = true;
				truncated_child["remaining"] = child_count - i;
				children.push_back(truncated_child);
				break;
			}

			Dictionary child_data = _serialize_node(child, p_depth + 1);
			children.push_back(child_data);
		}
		if (!children.is_empty()) {
			result["children"] = children;
		}
	}

	return result;
}

Dictionary MCPSceneSerializer::_serialize_properties(Object *p_object) {
	Dictionary result;

	List<PropertyInfo> properties;
	p_object->get_property_list(&properties);

	for (const PropertyInfo &prop : properties) {
		// Skip properties not meant for storage
		if (!(prop.usage & PROPERTY_USAGE_STORAGE)) {
			continue;
		}

		// Skip blacklisted properties (exact match).
		if (property_blacklist.has(prop.name)) {
			continue;
		}

		// Skip security-sensitive properties (substring match).
		String prop_name_lower = String(prop.name).to_lower();
		bool is_sensitive = false;
		for (const String &pattern : security_patterns) {
			if (prop_name_lower.contains(pattern)) {
				is_sensitive = true;
				break;
			}
		}
		if (is_sensitive) {
			continue;
		}

		// Skip properties with certain name patterns
		String name = prop.name;
		if (name.begins_with("_") || name.contains("/")) {
			continue;
		}

		// At STANDARD level, only include key properties
		if (detail_level == DETAIL_STANDARD && !standard_properties.has(prop.name)) {
			continue;
		}

		// Get and serialize value
		Variant value = p_object->get(prop.name);

		// Skip default/empty values at non-FULL levels
		if (detail_level != DETAIL_FULL) {
			if (value.get_type() == Variant::NIL) {
				continue;
			}
			if (value.get_type() == Variant::ARRAY && Array(value).is_empty()) {
				continue;
			}
			if (value.get_type() == Variant::DICTIONARY && Dictionary(value).is_empty()) {
				continue;
			}
		}

		Variant serialized = _serialize_value(value);
		if (serialized.get_type() != Variant::NIL) {
			result[prop.name] = serialized;
		}
	}

	return result;
}

Variant MCPSceneSerializer::_serialize_value(const Variant &p_value) {
	switch (p_value.get_type()) {
		case Variant::NIL:
		case Variant::BOOL:
		case Variant::INT:
		case Variant::FLOAT:
			return p_value;

		case Variant::STRING: {
			String str = p_value;
			if (str.length() > max_property_value_length) {
				return str.substr(0, max_property_value_length) + "...[truncated]";
			}
			return str;
		}

		case Variant::VECTOR2: {
			Vector2 v = p_value;
			Dictionary result;
			result["x"] = v.x;
			result["y"] = v.y;
			return result;
		}

		case Variant::VECTOR3: {
			Vector3 v = p_value;
			Dictionary result;
			result["x"] = v.x;
			result["y"] = v.y;
			result["z"] = v.z;
			return result;
		}

		case Variant::TRANSFORM2D: {
			Transform2D t = p_value;
			Dictionary result;
			result["origin"] = _serialize_value(t.get_origin());
			result["rotation"] = t.get_rotation();
			result["scale"] = _serialize_value(t.get_scale());
			return result;
		}

		case Variant::TRANSFORM3D: {
			Transform3D t = p_value;
			Dictionary result;
			result["origin"] = _serialize_value(t.origin);
			// Simplified basis representation
			result["rotation"] = _serialize_value(t.basis.get_euler());
			result["scale"] = _serialize_value(t.basis.get_scale());
			return result;
		}

		case Variant::COLOR: {
			Color c = p_value;
			Dictionary result;
			result["r"] = c.r;
			result["g"] = c.g;
			result["b"] = c.b;
			result["a"] = c.a;
			return result;
		}

		case Variant::NODE_PATH:
			return String(NodePath(p_value));

		case Variant::OBJECT: {
			Object *obj = p_value;
			if (!obj) {
				return Variant();
			}

			Resource *res = Object::cast_to<Resource>(obj);
			if (res) {
				String path = res->get_path();
				if (!path.is_empty()) {
					return path;
				}
				return "<" + res->get_class() + ">";
			}

			return "<" + obj->get_class() + ">";
		}

		case Variant::ARRAY: {
			Array arr = p_value;
			if (arr.size() > 10) {
				// Truncate large arrays
				Array result;
				for (int i = 0; i < 10; i++) {
					result.push_back(_serialize_value(arr[i]));
				}
				result.push_back("...[" + itos(arr.size() - 10) + " more]");
				return result;
			}

			Array result;
			for (int i = 0; i < arr.size(); i++) {
				result.push_back(_serialize_value(arr[i]));
			}
			return result;
		}

		case Variant::DICTIONARY: {
			Dictionary dict = p_value;
			if (dict.size() > 20) {
				// Skip very large dictionaries
				return "<Dictionary with " + itos(dict.size()) + " entries>";
			}

			Dictionary result;
			for (const Variant *key = dict.next(); key; key = dict.next(key)) {
				result[*key] = _serialize_value(dict[*key]);
			}
			return result;
		}

		default:
			// For other types, just return type name
			return "<" + Variant::get_type_name(p_value.get_type()) + ">";
	}
}

Array MCPSceneSerializer::_get_node_signals(Node *p_node) {
	Array result;

	List<MethodInfo> signals;
	p_node->get_signal_list(&signals);

	for (const MethodInfo &sig : signals) {
		// Skip built-in signals (signals from parent classes)
		bool is_builtin = false;
		StringName parent_class = p_node->get_class();
		while (parent_class != StringName()) {
			List<MethodInfo> parent_signals;
			ClassDB::get_signal_list(parent_class, &parent_signals, false);
			for (const MethodInfo &parent_sig : parent_signals) {
				if (parent_sig.name == sig.name) {
					is_builtin = true;
					break;
				}
			}
			if (is_builtin) {
				break;
			}
			parent_class = ClassDB::get_parent_class(parent_class);
		}

		if (!is_builtin) {
			result.push_back(String(sig.name));
		}
	}

	return result;
}

String MCPSceneSerializer::_get_script_info(Node *p_node) {
	Ref<Script> script = p_node->get_script();
	if (!script.is_valid()) {
		return "";
	}

	return script->get_path();
}
