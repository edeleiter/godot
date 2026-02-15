/**************************************************************************/
/*  claude_editor_plugin.h                                                */
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

#ifndef CLAUDE_EDITOR_PLUGIN_H
#define CLAUDE_EDITOR_PLUGIN_H

#include "../mcp/godot_mcp_server.h"
#include "claude_mcp_dock.h"
#include "editor/plugins/editor_plugin.h"

#ifdef WINDOWS_ENABLED
class ClaudeTerminalDock;
#endif

class ClaudeEditorPlugin : public EditorPlugin {
	GDCLASS(ClaudeEditorPlugin, EditorPlugin);

private:
	ClaudeMCPDock *dock = nullptr;
#ifdef WINDOWS_ENABLED
	ClaudeTerminalDock *terminal_dock = nullptr;
#endif
	Ref<GodotMCPServer> mcp_server;
	bool started = false;

	void _start_server();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return "Claude MCP"; }

	Ref<GodotMCPServer> get_mcp_server() const { return mcp_server; }

	ClaudeEditorPlugin();
	~ClaudeEditorPlugin();
};

#endif // CLAUDE_EDITOR_PLUGIN_H
