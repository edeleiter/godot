/**************************************************************************/
/*  claude_mcp_dock.h                                                     */
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

#pragma once

#include "../mcp/godot_mcp_server.h"
#include "scene/gui/box_container.h"

class AcceptDialog;
class Button;
class CheckBox;
class ItemList;
class Label;
class LineEdit;
class RichTextLabel;
class SpinBox;

class ClaudeMCPDock : public VBoxContainer {
	GDCLASS(ClaudeMCPDock, VBoxContainer);

private:
	Ref<GodotMCPServer> mcp_server;

	// UI Elements
	Label *status_label = nullptr;
	Label *clients_label = nullptr;
	Button *toggle_button = nullptr;
	Button *settings_button = nullptr;
	RichTextLabel *config_text = nullptr;
	ItemList *log_list = nullptr;
	Button *clear_log_button = nullptr;

	// Settings dialog (lazily created).
	AcceptDialog *settings_dialog = nullptr;
	SpinBox *port_spinbox = nullptr;
	LineEdit *host_edit = nullptr;
	CheckBox *autostart_check = nullptr;
	Label *restart_hint_label = nullptr;
	Button *restart_server_button = nullptr;

	// State
	int last_client_count = -1;

	void _build_ui();
	void _on_toggle_pressed();
	void _on_clear_log_pressed();
	void _on_tool_called(const String &p_name, const Dictionary &p_args);
	void _on_copy_config_pressed();
	void _update_config_snippet();
	String _ensure_bridge_script() const;
	String _get_bridge_path() const;
	int _get_current_port() const;
	String _generate_config_json(const String &p_bridge_path, int p_port) const;

	void _on_settings_pressed();
	void _on_settings_confirmed();
	void _build_settings_dialog();
	void _on_port_changed(double p_value);
	void _on_host_changed(const String &p_text);
	void _on_autostart_toggled(bool p_enabled);
	void _on_restart_server_pressed();
	void _set_setting(const String &p_key, const Variant &p_value);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_mcp_server(const Ref<GodotMCPServer> &p_server);
	Ref<GodotMCPServer> get_mcp_server() const { return mcp_server; }
	void update_status();

	ClaudeMCPDock();
	~ClaudeMCPDock();
};

