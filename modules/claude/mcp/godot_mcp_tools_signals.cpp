/**************************************************************************/
/*  godot_mcp_tools_signals.cpp                                          */
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
#include "editor/editor_undo_redo_manager.h"
#endif

Dictionary GodotMCPServer::_tool_connect_signal(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String source_path = p_args.get("source_path", "");
	String signal_name = p_args.get("signal_name", "");
	String target_path = p_args.get("target_path", "");
	String method_name = p_args.get("method_name", "");

	// Validate paths.
	String error;
	if (!_validate_node_path(source_path, error)) {
		return _error_result("source_path: " + error);
	}
	if (!_validate_node_path(target_path, error)) {
		return _error_result("target_path: " + error);
	}

	if (signal_name.is_empty()) {
		return _error_result("signal_name is empty");
	}
	if (method_name.is_empty()) {
		return _error_result("method_name is empty");
	}

	Node *source = _resolve_node_path(source_path);
	if (!source) {
		return _error_result("Source node not found: " + source_path);
	}

	Node *target = _resolve_node_path(target_path);
	if (!target) {
		return _error_result("Target node not found: " + target_path);
	}

	// Verify the signal exists on the source node.
	if (!source->has_signal(signal_name)) {
		return _error_result("Signal '" + signal_name + "' not found on " + source_path);
	}

	// Check if already connected.
	if (source->is_connected(signal_name, Callable(target, method_name))) {
		return _error_result("Signal '" + signal_name + "' is already connected to " + target_path + "::" + method_name);
	}

	// Connect with undo/redo.
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Connect " + signal_name);
	ur->add_do_method(source, "connect", signal_name, Callable(target, method_name));
	ur->add_undo_method(source, "disconnect", signal_name, Callable(target, method_name));
	ur->commit_action();

	return _success_result("Connected " + source_path + "::" + signal_name + " -> " + target_path + "::" + method_name,
			Dictionary{ { "source", source_path }, { "signal", signal_name },
					{ "target", target_path }, { "method", method_name } });
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_disconnect_signal(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String source_path = p_args.get("source_path", "");
	String signal_name = p_args.get("signal_name", "");
	String target_path = p_args.get("target_path", "");
	String method_name = p_args.get("method_name", "");

	// Validate paths.
	String error;
	if (!_validate_node_path(source_path, error)) {
		return _error_result("source_path: " + error);
	}
	if (!_validate_node_path(target_path, error)) {
		return _error_result("target_path: " + error);
	}

	if (signal_name.is_empty()) {
		return _error_result("signal_name is empty");
	}
	if (method_name.is_empty()) {
		return _error_result("method_name is empty");
	}

	Node *source = _resolve_node_path(source_path);
	if (!source) {
		return _error_result("Source node not found: " + source_path);
	}

	Node *target = _resolve_node_path(target_path);
	if (!target) {
		return _error_result("Target node not found: " + target_path);
	}

	// Check if the connection exists.
	if (!source->is_connected(signal_name, Callable(target, method_name))) {
		return _error_result("No connection from " + source_path + "::" + signal_name + " to " + target_path + "::" + method_name);
	}

	// Disconnect with undo/redo.
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Disconnect " + signal_name);
	ur->add_do_method(source, "disconnect", signal_name, Callable(target, method_name));
	ur->add_undo_method(source, "connect", signal_name, Callable(target, method_name));
	ur->commit_action();

	return _success_result("Disconnected " + source_path + "::" + signal_name + " -> " + target_path + "::" + method_name);
#else
	return _error_result("Editor functionality not available");
#endif
}
