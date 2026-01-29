/**************************************************************************/
/*  godot_mcp_server.h                                                    */
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

#ifndef GODOT_MCP_SERVER_H
#define GODOT_MCP_SERVER_H

#include "core/io/json.h"
#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "core/object/ref_counted.h"

class Node;

class GodotMCPServer : public RefCounted {
	GDCLASS(GodotMCPServer, RefCounted);

public:
	static const int DEFAULT_PORT = 6009;
	static const int MAX_CLIENTS = 4;
	static const int MAX_BUFFER_SIZE = 4 * 1024 * 1024; // 4MB

private:
	struct Peer {
		Ref<StreamPeerTCP> connection;
		Vector<uint8_t> read_buf;
		Vector<CharString> response_queue;
		int response_sent = 0;
	};

	bool running = false;
	int port = DEFAULT_PORT;
	String host = "127.0.0.1";

	Ref<TCPServer> server;
	HashMap<int, Peer> clients;
	int next_client_id = 0;
	int active_client_id = -1;

	// Protocol version
	static const char *PROTOCOL_VERSION;

	// TCP handling
	void _on_client_connected();
	void _process_client(int p_id, Peer &p_peer);
	void _send_to_client(int p_id, const String &p_data);

	// MCP protocol handling
	void _handle_request(const Dictionary &p_request);
	Dictionary _handle_initialize(const Dictionary &p_params);
	Dictionary _handle_tools_list(const Dictionary &p_params);
	Dictionary _handle_tools_call(const Dictionary &p_params);
	Dictionary _handle_resources_list(const Dictionary &p_params);
	Dictionary _handle_resources_read(const Dictionary &p_params);

	// Response helpers
	void _send_response(const Dictionary &p_response);
	void _send_error(int p_code, const String &p_message, const Variant &p_id);
	Dictionary _success_result(const String &p_message, const Dictionary &p_data = Dictionary());
	Dictionary _error_result(const String &p_message);

	// Tool definition helpers
	Dictionary _define_tool(const String &p_name, const String &p_description, const Dictionary &p_schema);
	Dictionary _make_schema(const Array &p_params);

	// Tool implementations
	Dictionary _tool_get_scene_tree(const Dictionary &p_args);
	Dictionary _tool_add_node(const Dictionary &p_args);
	Dictionary _tool_remove_node(const Dictionary &p_args);
	Dictionary _tool_set_property(const Dictionary &p_args);
	Dictionary _tool_get_property(const Dictionary &p_args);
	Dictionary _tool_create_script(const Dictionary &p_args);
	Dictionary _tool_read_script(const Dictionary &p_args);
	Dictionary _tool_modify_script(const Dictionary &p_args);
	Dictionary _tool_get_selected_nodes(const Dictionary &p_args);
	Dictionary _tool_select_nodes(const Dictionary &p_args);
	Dictionary _tool_run_scene(const Dictionary &p_args);
	Dictionary _tool_stop_scene(const Dictionary &p_args);

	// Validation helpers
	bool _validate_node_path(const String &p_path, String &r_error);
	bool _validate_script_path(const String &p_path, String &r_error);
	bool _validate_node_type(const String &p_type, String &r_error);
	Node *_resolve_node_path(const String &p_path);
	Node *_get_scene_root();

	// Type coercion
	Variant _coerce_value(const Variant &p_value, Variant::Type p_target_type);

	// File operation helpers (used as undo/redo targets)
	void _write_script_file(const String &p_path, const String &p_content);
	void _delete_script_file(const String &p_path);
	void _attach_script_to_node(const String &p_node_path, const String &p_script_path);
	void _detach_script_from_node(const String &p_node_path);

protected:
	static void _bind_methods();

public:
	Error start(int p_port = DEFAULT_PORT, const String &p_host = "127.0.0.1");
	void stop();
	void poll();
	bool is_running() const { return running; }
	int get_port() const { return port; }
	int get_client_count() const { return clients.size(); }

	GodotMCPServer();
	~GodotMCPServer();
};

#endif // GODOT_MCP_SERVER_H
