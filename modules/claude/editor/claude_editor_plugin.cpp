/**************************************************************************/
/*  claude_editor_plugin.cpp                                              */
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

#include "claude_editor_plugin.h"

#include "editor/editor_node.h"
#include "editor/settings/editor_settings.h"

void ClaudeEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_mcp_server"), &ClaudeEditorPlugin::get_mcp_server);
}

ClaudeEditorPlugin::ClaudeEditorPlugin() {
	mcp_server.instantiate();

	// Register editor settings.
	_EDITOR_DEF("network/claude_mcp/port", GodotMCPServer::DEFAULT_PORT);
	_EDITOR_DEF("network/claude_mcp/host", String("127.0.0.1"));
	_EDITOR_DEF("network/claude_mcp/autostart", false);

	// Create and add dock.
	dock = memnew(ClaudeMCPDock);
	dock->set_mcp_server(mcp_server);
	add_control_to_dock(DOCK_SLOT_RIGHT_UL, dock);

	set_process_internal(true);
}

ClaudeEditorPlugin::~ClaudeEditorPlugin() {
	if (mcp_server.is_valid() && mcp_server->is_running()) {
		mcp_server->stop();
	}

	if (dock) {
		remove_control_from_docks(dock);
		memdelete(dock);
		dock = nullptr;
	}
}

void ClaudeEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!started && EditorNode::get_singleton()->is_editor_ready()) {
				bool autostart = EDITOR_GET("network/claude_mcp/autostart");
				if (autostart) {
					_start_server();
				}
				started = true;
			}

			if (mcp_server.is_valid() && mcp_server->is_running()) {
				mcp_server->poll();
			}
		} break;
	}
}

void ClaudeEditorPlugin::_start_server() {
	int port = EDITOR_GET("network/claude_mcp/port");
	String host = EDITOR_GET("network/claude_mcp/host");
	mcp_server->start(port, host);
	if (dock) {
		dock->update_status();
	}
}
