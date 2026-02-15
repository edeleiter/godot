/**************************************************************************/
/*  godot_mcp_type_helpers.h                                              */
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

#ifndef GODOT_MCP_TYPE_HELPERS_H
#define GODOT_MCP_TYPE_HELPERS_H

#include "core/variant/dictionary.h"
#include "core/math/vector2.h"
#include "core/math/vector3.h"

// Dictionary <-> Vector conversion helpers shared across MCP tool files.

static inline Vector3 mcp_dict_to_vector3(const Dictionary &p_dict) {
	return Vector3(
			p_dict.get("x", 0.0),
			p_dict.get("y", 0.0),
			p_dict.get("z", 0.0));
}

static inline Dictionary mcp_vector3_to_dict(const Vector3 &p_vec) {
	return Dictionary{ { "x", p_vec.x }, { "y", p_vec.y }, { "z", p_vec.z } };
}

static inline Vector2 mcp_dict_to_vector2(const Dictionary &p_dict) {
	return Vector2(
			p_dict.get("x", 0.0),
			p_dict.get("y", 0.0));
}

static inline Dictionary mcp_vector2_to_dict(const Vector2 &p_vec) {
	return Dictionary{ { "x", p_vec.x }, { "y", p_vec.y } };
}

#endif // GODOT_MCP_TYPE_HELPERS_H
