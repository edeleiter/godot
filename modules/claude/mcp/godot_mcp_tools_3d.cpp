/**************************************************************************/
/*  godot_mcp_tools_3d.cpp                                                */
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

#include "scene/3d/navigation/navigation_region_3d.h"

Dictionary GodotMCPServer::_tool_bake_navigation(const Dictionary &p_args) {
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

	NavigationRegion3D *nav_region = Object::cast_to<NavigationRegion3D>(node);
	if (!nav_region) {
		return _error_result("Node is not a NavigationRegion3D: " + node_path);
	}

	if (!nav_region->get_navigation_mesh().is_valid()) {
		return _error_result("NavigationRegion3D has no NavigationMesh resource. Set one first via set_property.");
	}

	if (nav_region->is_baking()) {
		return _error_result("Navigation mesh is already being baked");
	}

	// Bake on main thread to avoid race conditions with MCP response.
	nav_region->bake_navigation_mesh(false);

	return _success_result("Navigation mesh baked for " + String(nav_region->get_name()),
			Dictionary{ { "node_path", node_path } });
#else
	return _error_result("Editor functionality not available");
#endif
}
