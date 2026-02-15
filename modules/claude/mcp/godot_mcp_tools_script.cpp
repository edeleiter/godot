/**************************************************************************/
/*  godot_mcp_tools_script.cpp                                            */
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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/script_language.h"
#include "scene/main/node.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_undo_redo_manager.h"
#include "editor/file_system/editor_file_system.h"
#endif

// File operation helpers for undo/redo.

void GodotMCPServer::_write_script_file(const String &p_path, const String &p_content) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	if (file.is_valid()) {
		file->store_string(p_content);
		file->close();
	}
#ifdef TOOLS_ENABLED
	EditorFileSystem::get_singleton()->scan_changes();
#endif
}

void GodotMCPServer::_delete_script_file(const String &p_path) {
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (da.is_valid()) {
		da->remove(p_path);
	}
#ifdef TOOLS_ENABLED
	EditorFileSystem::get_singleton()->scan_changes();
#endif
}

void GodotMCPServer::_attach_script_to_node(const String &p_node_path, const String &p_script_path) {
	Node *node = _resolve_node_path(p_node_path);
	if (node) {
		Ref<Script> script = ResourceLoader::load(p_script_path, "Script");
		if (script.is_valid()) {
			node->set_script(script);
		}
	}
}

void GodotMCPServer::_detach_script_from_node(const String &p_node_path) {
	Node *node = _resolve_node_path(p_node_path);
	if (node) {
		node->set_script(Variant());
	}
}

// Tool implementations.

Dictionary GodotMCPServer::_tool_create_script(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String path = p_args.get("path", "");
	String content = p_args.get("content", "");
	String attach_to = p_args.get("attach_to", "");

	String error;
	if (!_validate_script_path(path, error)) {
		return _error_result(error);
	}

	if (FileAccess::exists(path)) {
		return _error_result("Script already exists: " + path);
	}

	// Create directory if needed.
	String dir = path.get_base_dir();
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (!da->dir_exists(dir)) {
		Error err = da->make_dir_recursive(dir);
		if (err != OK) {
			return _error_result("Cannot create directory: " + dir);
		}
	}

	// Create file with undo/redo.
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Create Script");
	ur->add_do_method(this, "_write_script_file", path, content);
	ur->add_undo_method(this, "_delete_script_file", path);

	// Attach to node if specified.
	if (!attach_to.is_empty()) {
		String attach_error;
		if (_validate_node_path(attach_to, attach_error)) {
			Node *node = _resolve_node_path(attach_to);
			if (node) {
				ur->add_do_method(this, "_attach_script_to_node", attach_to, path);
				ur->add_undo_method(this, "_detach_script_from_node", attach_to);
			}
		}
	}

	ur->commit_action();

	Dictionary data;
	data["path"] = path;
	if (!attach_to.is_empty()) {
		data["attached_to"] = attach_to;
	}
	return _success_result("Created script: " + path, data);
#else
	return _error_result("Editor functionality not available");
#endif
}

Dictionary GodotMCPServer::_tool_read_script(const Dictionary &p_args) {
	String path = p_args.get("path", "");

	String error;
	if (!_validate_script_path(path, error)) {
		return _error_result(error);
	}

	if (!FileAccess::exists(path)) {
		return _error_result("Script not found: " + path);
	}

	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (!file.is_valid()) {
		return _error_result("Cannot read: " + path);
	}

	String content = file->get_as_text();

	return _success_result("Script read",
			Dictionary{ { "path", path }, { "content", content } });
}

Dictionary GodotMCPServer::_tool_validate_script(const Dictionary &p_args) {
	String path = p_args.get("path", "");

	String error;
	if (!_validate_script_path(path, error)) {
		return _error_result(error);
	}

	if (!FileAccess::exists(path)) {
		return _error_result("Script not found: " + path);
	}

	// Read script source from disk.
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (!file.is_valid()) {
		return _error_result("Cannot read: " + path);
	}
	String source = file->get_as_text();
	file->close();

	// Find the matching ScriptLanguage by file extension.
	String ext = path.get_extension();
	ScriptLanguage *lang = ScriptServer::get_language_for_extension(ext);
	if (!lang) {
		return _error_result("No script language found for extension: " + ext);
	}

	List<String> functions;
	List<ScriptLanguage::ScriptError> errors_list;
	List<ScriptLanguage::Warning> warnings_list;
	HashSet<int> safe_lines;

	bool valid = lang->validate(source, path, &functions, &errors_list, &warnings_list, &safe_lines);

	// Separate own errors from depended errors (errors in other files).
	Array own_errors;
	Dictionary depended_errors;

	for (const ScriptLanguage::ScriptError &e : errors_list) {
		Dictionary err;
		err["line"] = e.line;
		err["column"] = e.column;
		err["message"] = e.message;

		if (!e.path.is_empty() && e.path != path) {
			// Error belongs to a dependency.
			String dep_path = e.path;
			err["path"] = dep_path;
			Array dep_list;
			if (depended_errors.has(dep_path)) {
				dep_list = depended_errors[dep_path];
			}
			dep_list.push_back(err);
			depended_errors[dep_path] = dep_list;
		} else {
			err["path"] = path;
			own_errors.push_back(err);
		}
	}

	Array warns;
	for (const ScriptLanguage::Warning &w : warnings_list) {
		Dictionary warn;
		warn["start_line"] = w.start_line;
		warn["end_line"] = w.end_line;
		warn["code"] = w.string_code;
		warn["message"] = w.message;
		warns.push_back(warn);
	}

	Dictionary data;
	data["path"] = path;
	data["valid"] = valid;
	data["errors"] = own_errors;
	data["warnings"] = warns;
	if (!depended_errors.is_empty()) {
		data["depended_errors"] = depended_errors;
	}

	return _success_result(valid ? "Script is valid" : "Script has errors", data);
}

Dictionary GodotMCPServer::_tool_modify_script(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String path = p_args.get("path", "");
	String content = p_args.get("content", "");

	String error;
	if (!_validate_script_path(path, error)) {
		return _error_result(error);
	}

	if (!FileAccess::exists(path)) {
		return _error_result("Script not found: " + path);
	}

	// Read existing content for undo.
	Ref<FileAccess> read_file = FileAccess::open(path, FileAccess::READ);
	if (!read_file.is_valid()) {
		return _error_result("Cannot read: " + path);
	}
	String old_content = read_file->get_as_text();
	read_file->close();

	// Write with undo/redo.
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action("MCP: Modify Script");
	ur->add_do_method(this, "_write_script_file", path, content);
	ur->add_undo_method(this, "_write_script_file", path, old_content);
	ur->commit_action();

	return _success_result("Modified script: " + path);
#else
	return _error_result("Editor functionality not available");
#endif
}
