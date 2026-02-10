/**************************************************************************/
/*  godot_mcp_tools_resource.cpp                                          */
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

#ifdef TOOLS_ENABLED
#include "editor/editor_interface.h"
#include "editor/file_system/editor_file_system.h"
#endif

Dictionary GodotMCPServer::_tool_project_files(const Dictionary &p_args) {
#ifdef TOOLS_ENABLED
	String action = p_args.get("action", "");

	if (action.is_empty()) {
		return _error_result("Missing required 'action' parameter. Use: 'list' or 'scan'");
	}

	EditorFileSystem *efs = EditorFileSystem::get_singleton();
	if (!efs) {
		return _error_result("EditorFileSystem not available");
	}

	if (action == "scan") {
		efs->scan_changes();
		return _success_result("Filesystem rescan triggered. New/modified files will appear in the editor.");

	} else if (action == "list") {
		String path = p_args.get("path", "res://");
		bool recursive = p_args.get("recursive", false);
		Array extensions = p_args.get("extensions", Array());

		String error;
		if (!_validate_resource_path(path, error)) {
			return _error_result(error);
		}

		// Build extension filter set.
		HashSet<String> ext_filter;
		for (int i = 0; i < extensions.size(); i++) {
			ext_filter.insert(String(extensions[i]).to_lower());
		}

		// Find the directory in EditorFileSystem.
		EditorFileSystemDirectory *dir = efs->get_filesystem_path(path);
		if (!dir) {
			return _error_result("Directory not found: " + path);
		}

		Array files;

		// Recursive helper for collecting files from a directory.
		struct ListHelper {
			static void list_dir(EditorFileSystemDirectory *p_dir, Array &r_files, const HashSet<String> &p_ext_filter, bool p_recursive) {
				// Files in this directory.
				for (int i = 0; i < p_dir->get_file_count(); i++) {
					String file_path = p_dir->get_file_path(i);
					String ext = file_path.get_extension().to_lower();

					if (!p_ext_filter.is_empty() && !p_ext_filter.has(ext)) {
						continue;
					}

					Dictionary file_info;
					file_info["path"] = file_path;
					file_info["type"] = p_dir->get_file_type(i);
					r_files.push_back(file_info);
				}

				// Recurse into subdirectories.
				if (p_recursive) {
					for (int i = 0; i < p_dir->get_subdir_count(); i++) {
						list_dir(p_dir->get_subdir(i), r_files, p_ext_filter, true);
					}
				}
			}
		};

		ListHelper::list_dir(dir, files, ext_filter, recursive);

		// Subdirectories (always list immediate subdirs for navigation).
		Array subdirs;
		for (int i = 0; i < dir->get_subdir_count(); i++) {
			subdirs.push_back(dir->get_subdir(i)->get_path());
		}

		Dictionary data;
		data["path"] = path;
		data["files"] = files;
		data["file_count"] = files.size();
		data["subdirectories"] = subdirs;
		return _success_result("Listed " + itos(files.size()) + " files in " + path, data);

	} else {
		return _error_result("Unknown action: " + action + ". Use: 'list' or 'scan'");
	}
#else
	return _error_result("Editor functionality not available");
#endif
}
