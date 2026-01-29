/**************************************************************************/
/*  mcp_scene_serializer.h                                                */
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

#ifndef MCP_SCENE_SERIALIZER_H
#define MCP_SCENE_SERIALIZER_H

#include "core/object/ref_counted.h"
#include "core/templates/hash_set.h"
#include "core/variant/typed_array.h"
#include "scene/main/node.h"

class MCPSceneSerializer : public RefCounted {
	GDCLASS(MCPSceneSerializer, RefCounted);

public:
	enum DetailLevel {
		DETAIL_MINIMAL,
		DETAIL_STANDARD,
		DETAIL_FULL,
	};

private:
	DetailLevel detail_level = DETAIL_STANDARD;
	int max_depth = 10;
	int max_nodes = 500;
	int max_property_value_length = 1000;
	int current_node_count = 0;

	HashSet<StringName> property_blacklist;
	Vector<String> security_patterns;
	HashSet<StringName> standard_properties;

	void _init_property_sets();
	Dictionary _serialize_node(Node *p_node, int p_depth);
	Dictionary _serialize_properties(Object *p_object);
	Variant _serialize_value(const Variant &p_value);
	Array _get_node_signals(Node *p_node);
	String _get_script_info(Node *p_node);

protected:
	static void _bind_methods();

public:
	Dictionary serialize_scene(Node *p_root);
	Dictionary serialize_selection(const TypedArray<Node> &p_nodes);
	Dictionary serialize_node_with_context(Node *p_node, int p_ancestor_levels = 2);

	void set_detail_level(DetailLevel p_level);
	DetailLevel get_detail_level() const { return detail_level; }
	void set_max_depth(int p_depth);
	int get_max_depth() const { return max_depth; }
	void set_max_nodes(int p_nodes);
	int get_max_nodes() const { return max_nodes; }

	static String node_path_to_string(const NodePath &p_path);

	MCPSceneSerializer();
};

VARIANT_ENUM_CAST(MCPSceneSerializer::DetailLevel);

#endif // MCP_SCENE_SERIALIZER_H
