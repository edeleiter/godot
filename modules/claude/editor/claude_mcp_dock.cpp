/**************************************************************************/
/*  claude_mcp_dock.cpp                                                   */
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

#include "claude_mcp_dock.h"

#include "core/os/os.h"
#include "core/os/time.h"
#include "editor/editor_interface.h"
#include "editor/settings/editor_settings.h"
#include "scene/gui/button.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/separator.h"

void ClaudeMCPDock::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mcp_server", "server"), &ClaudeMCPDock::set_mcp_server);
	ClassDB::bind_method(D_METHOD("get_mcp_server"), &ClaudeMCPDock::get_mcp_server);
	ClassDB::bind_method(D_METHOD("update_status"), &ClaudeMCPDock::update_status);
}

ClaudeMCPDock::ClaudeMCPDock() {
	set_name("Claude MCP");
	_build_ui();
}

ClaudeMCPDock::~ClaudeMCPDock() {
}

void ClaudeMCPDock::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			update_status();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			if (toggle_button) {
				update_status();
			}
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			// Update client count display when it changes.
			if (mcp_server.is_valid() && mcp_server->is_running()) {
				int count = mcp_server->get_client_count();
				if (count != last_client_count) {
					last_client_count = count;
					clients_label->set_text(vformat("Clients: %d", count));
				}
			}
		} break;
	}
}

void ClaudeMCPDock::_build_ui() {
	// Status section.
	HBoxContainer *status_hbox = memnew(HBoxContainer);
	add_child(status_hbox);

	Label *status_title = memnew(Label);
	status_title->set_text("MCP Server:");
	status_hbox->add_child(status_title);

	status_label = memnew(Label);
	status_label->set_text("Not Started");
	status_label->set_h_size_flags(SIZE_EXPAND_FILL);
	status_hbox->add_child(status_label);

	toggle_button = memnew(Button);
	toggle_button->set_text("Start");
	toggle_button->connect("pressed", callable_mp(this, &ClaudeMCPDock::_on_toggle_pressed));
	status_hbox->add_child(toggle_button);

	// Clients label.
	clients_label = memnew(Label);
	clients_label->set_text("Clients: 0");
	add_child(clients_label);

	add_child(memnew(HSeparator));

	// Info section.
	Label *info_label = memnew(Label);
	info_label->set_text("Add to Claude Desktop/Code settings (update path first):");
	info_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
	add_child(info_label);

	// Config snippet.
	config_text = memnew(RichTextLabel);
	config_text->set_use_bbcode(true);
	config_text->set_fit_content(true);
	config_text->set_scroll_active(false);
	config_text->set_selection_enabled(true);
	config_text->set_custom_minimum_size(Size2(0, 100));
	add_child(config_text);

	Button *copy_btn = memnew(Button);
	copy_btn->set_text("Copy Config to Clipboard");
	copy_btn->connect("pressed", callable_mp(this, &ClaudeMCPDock::_on_copy_config_pressed));
	add_child(copy_btn);

	add_child(memnew(HSeparator));

	// Log section.
	Label *log_title = memnew(Label);
	log_title->set_text("Recent Tool Calls:");
	add_child(log_title);

	log_list = memnew(ItemList);
	log_list->set_v_size_flags(SIZE_EXPAND_FILL);
	log_list->set_custom_minimum_size(Size2(0, 100));
	log_list->set_max_columns(1);
	log_list->set_allow_reselect(false);
	add_child(log_list);

	clear_log_button = memnew(Button);
	clear_log_button->set_text("Clear Log");
	clear_log_button->connect("pressed", callable_mp(this, &ClaudeMCPDock::_on_clear_log_pressed));
	add_child(clear_log_button);

	set_process_internal(true);
}

void ClaudeMCPDock::set_mcp_server(const Ref<GodotMCPServer> &p_server) {
	if (mcp_server.is_valid()) {
		mcp_server->disconnect("tool_called", callable_mp(this, &ClaudeMCPDock::_on_tool_called));
	}

	mcp_server = p_server;

	if (mcp_server.is_valid()) {
		mcp_server->connect("tool_called", callable_mp(this, &ClaudeMCPDock::_on_tool_called));
	}

	update_status();
}

void ClaudeMCPDock::update_status() {
	if (!mcp_server.is_valid()) {
		status_label->set_text("Not Initialized");
		toggle_button->set_text("Start");
		toggle_button->set_disabled(true);
		clients_label->set_text("Clients: 0");
		return;
	}

	toggle_button->set_disabled(false);

	if (mcp_server->is_running()) {
		status_label->set_text(vformat("Running (port %d)", mcp_server->get_port()));
		status_label->add_theme_color_override("font_color", Color(0.3, 0.8, 0.3));
		toggle_button->set_text("Stop");
		last_client_count = mcp_server->get_client_count();
		clients_label->set_text(vformat("Clients: %d", last_client_count));
	} else {
		status_label->set_text("Stopped");
		status_label->remove_theme_color_override("font_color");
		toggle_button->set_text("Start");
		clients_label->set_text("Clients: 0");
		last_client_count = -1;
	}

	_update_config_snippet();
}

void ClaudeMCPDock::_update_config_snippet() {
	int port = GodotMCPServer::DEFAULT_PORT;
	if (mcp_server.is_valid() && mcp_server->is_running()) {
		port = mcp_server->get_port();
	}

	String config = vformat(R"([code]{
  "mcpServers": {
    "godot": {
      "command": "python",
      "args": ["<BRIDGE_PATH>", "--port", "%d"]
    }
  }
}[/code]

[b]Important:[/b] Replace [code]<BRIDGE_PATH>[/code] with the full path to:
[code]modules/claude/bridge/claude_mcp_bridge.py[/code]
in your Godot source directory.)",
			port);
	config_text->set_text(config);
}

void ClaudeMCPDock::_on_toggle_pressed() {
	if (!mcp_server.is_valid()) {
		return;
	}

	if (mcp_server->is_running()) {
		mcp_server->stop();
	} else {
		int port = EDITOR_GET("network/claude_mcp/port");
		String host = EDITOR_GET("network/claude_mcp/host");
		mcp_server->start(port, host);
	}

	update_status();
}

void ClaudeMCPDock::_on_clear_log_pressed() {
	log_list->clear();
}

void ClaudeMCPDock::_on_tool_called(const String &p_name, const Dictionary &p_args) {
	String timestamp = Time::get_singleton()->get_time_string_from_system();
	String entry = vformat("[%s] %s", timestamp, p_name);

	// Add to top of list.
	log_list->add_item(entry);

	// Keep only last 50 entries.
	while (log_list->get_item_count() > 50) {
		log_list->remove_item(0);
	}
}

void ClaudeMCPDock::_on_copy_config_pressed() {
	int port = GodotMCPServer::DEFAULT_PORT;
	if (mcp_server.is_valid() && mcp_server->is_running()) {
		port = mcp_server->get_port();
	}

	String config = vformat(R"({
  "mcpServers": {
    "godot": {
      "command": "python",
      "args": ["<BRIDGE_PATH>", "--port", "%d"]
    }
  }
})",
			port);

	DisplayServer::get_singleton()->clipboard_set(config);
}
