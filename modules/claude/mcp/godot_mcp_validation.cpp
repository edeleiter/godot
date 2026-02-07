/**************************************************************************/
/*  godot_mcp_validation.cpp                                              */
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

#include "core/object/class_db.h"
#include "scene/main/node.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_interface.h"
#endif

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
