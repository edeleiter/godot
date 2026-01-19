# MCP Server Architecture

## Overview

The Godot-Claude integration uses the **Model Context Protocol (MCP)** to expose Godot editor capabilities as tools that Claude can invoke. This approach:

- Leverages the existing Claude Code infrastructure
- Allows any MCP-compatible client to interact with Godot
- Keeps the Godot module lightweight (just the server + UI)
- Enables Claude Code CLI to work with Godot projects directly

## Architecture

```text
┌─────────────────────────────────────────────────────────────────────────┐
│                           User's Machine                                 │
│                                                                          │
│  ┌────────────────────┐         ┌────────────────────────────────────┐  │
│  │   Claude Code CLI  │         │          Godot Editor              │  │
│  │   or VS Code Ext   │         │                                    │  │
│  │                    │  MCP    │  ┌──────────────────────────────┐  │  │
│  │  ┌──────────────┐  │ Protocol│  │      MCP Server Module       │  │  │
│  │  │ MCP Client   │◄─┼─────────┼──┤                              │  │  │
│  │  └──────────────┘  │  JSON   │  │  - Tool definitions          │  │  │
│  │         │          │  over   │  │  - Scene manipulation        │  │  │
│  │         ▼          │  stdio  │  │  - Node operations           │  │  │
│  │  ┌──────────────┐  │   or    │  │  - Script management         │  │  │
│  │  │ Claude API   │  │  HTTP   │  │  - Resource access           │  │  │
│  │  └──────────────┘  │         │  └──────────────────────────────┘  │  │
│  │                    │         │                 │                   │  │
│  └────────────────────┘         │                 ▼                   │  │
│                                 │  ┌──────────────────────────────┐  │  │
│                                 │  │     Godot Editor Core        │  │  │
│                                 │  │                              │  │  │
│                                 │  │  - EditorInterface           │  │  │
│                                 │  │  - EditorUndoRedoManager     │  │  │
│                                 │  │  - SceneTree                 │  │  │
│                                 │  │  - FileSystem                │  │  │
│                                 │  └──────────────────────────────┘  │  │
│                                 │                                    │  │
│                                 └────────────────────────────────────┘  │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

## MCP Server Implementation

### Server Class

```cpp
// modules/claude/mcp/godot_mcp_server.h

#ifndef GODOT_MCP_SERVER_H
#define GODOT_MCP_SERVER_H

#include "core/object/ref_counted.h"
#include "core/io/json.h"
#include "core/io/tcp_server.h"
#include "core/io/stream_peer_tcp.h"

class GodotMCPServer : public RefCounted {
    GDCLASS(GodotMCPServer, RefCounted);

public:
    enum TransportMode {
        TRANSPORT_STDIO,    // For subprocess invocation
        TRANSPORT_HTTP,     // For network connections
    };

private:
    TransportMode transport_mode = TRANSPORT_STDIO;
    Ref<TCPServer> tcp_server;
    int http_port = 3000;

    // Tool handlers
    HashMap<String, Callable> tool_handlers;

    // MCP protocol handling
    void _handle_request(const Dictionary &p_request);
    Dictionary _handle_initialize(const Dictionary &p_params);
    Dictionary _handle_tools_list(const Dictionary &p_params);
    Dictionary _handle_tools_call(const Dictionary &p_params);
    Dictionary _handle_resources_list(const Dictionary &p_params);
    Dictionary _handle_resources_read(const Dictionary &p_params);

    // Response building
    void _send_response(const Dictionary &p_response);
    void _send_error(int p_code, const String &p_message, const Variant &p_id);

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

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    Error start(TransportMode p_mode = TRANSPORT_STDIO);
    void stop();
    bool is_running() const;

    // For stdio mode
    void process_stdin();

    GodotMCPServer();
    ~GodotMCPServer();
};

VARIANT_ENUM_CAST(GodotMCPServer::TransportMode);

#endif
```

### Tool Definitions

The MCP server exposes these tools to Claude:

```cpp
Dictionary GodotMCPServer::_handle_tools_list(const Dictionary &p_params) {
    Array tools;

    // Scene Tree Tools
    tools.push_back(_define_tool(
        "godot_get_scene_tree",
        "Get the current scene tree structure as JSON",
        Dictionary() // No required parameters
    ));

    tools.push_back(_define_tool(
        "godot_add_node",
        "Add a new node to the scene tree",
        _make_schema({
            {"parent_path", "string", "Path to parent node (e.g., '/root/Main')"},
            {"node_type", "string", "Godot node class name (e.g., 'CharacterBody3D')"},
            {"node_name", "string", "Name for the new node"},
            {"properties", "object", "Optional initial property values", false}
        })
    ));

    tools.push_back(_define_tool(
        "godot_remove_node",
        "Remove a node from the scene tree",
        _make_schema({
            {"node_path", "string", "Path to the node to remove"}
        })
    ));

    tools.push_back(_define_tool(
        "godot_set_property",
        "Set a property on a node",
        _make_schema({
            {"node_path", "string", "Path to the node"},
            {"property", "string", "Property name"},
            {"value", "any", "New property value"}
        })
    ));

    tools.push_back(_define_tool(
        "godot_get_property",
        "Get a property value from a node",
        _make_schema({
            {"node_path", "string", "Path to the node"},
            {"property", "string", "Property name"}
        })
    ));

    // Script Tools
    tools.push_back(_define_tool(
        "godot_create_script",
        "Create a new GDScript file",
        _make_schema({
            {"path", "string", "Resource path (must start with 'res://')"},
            {"content", "string", "Script content"},
            {"attach_to", "string", "Optional node path to attach script", false}
        })
    ));

    tools.push_back(_define_tool(
        "godot_read_script",
        "Read the content of a script file",
        _make_schema({
            {"path", "string", "Resource path to the script"}
        })
    ));

    tools.push_back(_define_tool(
        "godot_modify_script",
        "Modify an existing script file",
        _make_schema({
            {"path", "string", "Resource path to the script"},
            {"content", "string", "New script content"}
        })
    ));

    // Selection Tools
    tools.push_back(_define_tool(
        "godot_get_selected_nodes",
        "Get the currently selected nodes in the editor",
        Dictionary()
    ));

    tools.push_back(_define_tool(
        "godot_select_nodes",
        "Select nodes in the editor",
        _make_schema({
            {"node_paths", "array", "Array of node paths to select"}
        })
    ));

    // Execution Tools
    tools.push_back(_define_tool(
        "godot_run_scene",
        "Run the current scene or a specific scene",
        _make_schema({
            {"scene_path", "string", "Optional scene path, uses current if empty", false}
        })
    ));

    tools.push_back(_define_tool(
        "godot_stop_scene",
        "Stop the running scene",
        Dictionary()
    ));

    Dictionary result;
    result["tools"] = tools;
    return result;
}
```

### Tool Implementation Examples

```cpp
Dictionary GodotMCPServer::_tool_add_node(const Dictionary &p_args) {
    String parent_path = p_args.get("parent_path", "");
    String node_type = p_args.get("node_type", "");
    String node_name = p_args.get("node_name", "");
    Dictionary properties = p_args.get("properties", Dictionary());

    // Validate
    ERR_FAIL_COND_V_MSG(parent_path.is_empty(), _error_result("parent_path is required"), "");
    ERR_FAIL_COND_V_MSG(node_type.is_empty(), _error_result("node_type is required"), "");

    // Get parent node
    Node *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();
    ERR_FAIL_NULL_V_MSG(scene_root, _error_result("No scene is open"), "");

    Node *parent = _resolve_node_path(parent_path, scene_root);
    ERR_FAIL_NULL_V_MSG(parent, _error_result("Parent node not found: " + parent_path), "");

    // Validate node type
    ERR_FAIL_COND_V_MSG(!ClassDB::class_exists(node_type),
        _error_result("Unknown node type: " + node_type), "");

    // Create node
    Node *new_node = Object::cast_to<Node>(ClassDB::instantiate(node_type));
    ERR_FAIL_NULL_V_MSG(new_node, _error_result("Failed to create node"), "");

    if (node_name.is_empty()) {
        node_name = node_type;
    }
    new_node->set_name(node_name);

    // Apply properties
    for (const Variant *key = properties.next(); key; key = properties.next(key)) {
        new_node->set(*key, properties[*key]);
    }

    // Add with undo/redo
    EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
    ur->create_action("MCP: Add Node");
    ur->add_do_method(parent, "add_child", new_node, true);
    ur->add_do_method(new_node, "set_owner", scene_root);
    ur->add_do_reference(new_node);
    ur->add_undo_method(parent, "remove_child", new_node);
    ur->commit_action();

    // Return result
    Dictionary result;
    result["success"] = true;
    result["node_path"] = String(new_node->get_path());
    result["message"] = "Created " + node_type + " '" + node_name + "'";
    return result;
}

Dictionary GodotMCPServer::_tool_get_scene_tree(const Dictionary &p_args) {
    Node *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();

    if (!scene_root) {
        Dictionary result;
        result["success"] = true;
        result["scene"] = Variant();
        result["message"] = "No scene is currently open";
        return result;
    }

    // Use scene serializer
    Ref<ClaudeSceneSerializer> serializer;
    serializer.instantiate();

    Dictionary scene_data = serializer->serialize_scene(scene_root);

    Dictionary result;
    result["success"] = true;
    result["scene"] = scene_data;
    result["scene_path"] = scene_root->get_scene_file_path();
    return result;
}
```

## MCP Protocol Implementation

### Request/Response Format

```cpp
void GodotMCPServer::_handle_request(const Dictionary &p_request) {
    String method = p_request.get("method", "");
    Dictionary params = p_request.get("params", Dictionary());
    Variant id = p_request.get("id", Variant());

    Dictionary response;
    response["jsonrpc"] = "2.0";
    response["id"] = id;

    if (method == "initialize") {
        response["result"] = _handle_initialize(params);
    } else if (method == "tools/list") {
        response["result"] = _handle_tools_list(params);
    } else if (method == "tools/call") {
        response["result"] = _handle_tools_call(params);
    } else if (method == "resources/list") {
        response["result"] = _handle_resources_list(params);
    } else if (method == "resources/read") {
        response["result"] = _handle_resources_read(params);
    } else {
        _send_error(-32601, "Method not found: " + method, id);
        return;
    }

    _send_response(response);
}

Dictionary GodotMCPServer::_handle_initialize(const Dictionary &p_params) {
    Dictionary result;

    Dictionary server_info;
    server_info["name"] = "godot-mcp";
    server_info["version"] = "1.0.0";
    result["serverInfo"] = server_info;

    Dictionary capabilities;

    // Declare tool support
    Dictionary tools_cap;
    tools_cap["listChanged"] = false;
    capabilities["tools"] = tools_cap;

    // Declare resource support
    Dictionary resources_cap;
    resources_cap["subscribe"] = false;
    resources_cap["listChanged"] = false;
    capabilities["resources"] = resources_cap;

    result["capabilities"] = capabilities;
    result["protocolVersion"] = "2024-11-05";

    return result;
}
```

### Stdio Transport

```cpp
void GodotMCPServer::process_stdin() {
    // Read line from stdin
    String line;
    while (true) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) {
            break;
        }
        if (c == '\n') {
            break;
        }
        line += c;
    }

    if (line.is_empty()) {
        return;
    }

    // Parse JSON-RPC request
    JSON json;
    Error err = json.parse(line);
    if (err != OK) {
        _send_error(-32700, "Parse error", Variant());
        return;
    }

    _handle_request(json.get_data());
}

void GodotMCPServer::_send_response(const Dictionary &p_response) {
    String json_str = JSON::stringify(p_response);

    if (transport_mode == TRANSPORT_STDIO) {
        // Write to stdout
        print_line(json_str);
        fflush(stdout);
    } else {
        // HTTP response (handled by connection)
    }
}
```

## Claude Code Configuration

### MCP Settings for Claude Code

Users add this to their Claude Code MCP settings:

```json
{
  "mcpServers": {
    "godot": {
      "command": "/path/to/godot",
      "args": ["--mcp-server"],
      "env": {}
    }
  }
}
```

Or for HTTP mode:

```json
{
  "mcpServers": {
    "godot": {
      "url": "http://localhost:3000/mcp"
    }
  }
}
```

### Command Line Arguments

```cpp
// In main.cpp or editor initialization
if (args.find("--mcp-server") != -1) {
    // Start in MCP server mode
    Ref<GodotMCPServer> mcp_server;
    mcp_server.instantiate();
    mcp_server->start(GodotMCPServer::TRANSPORT_STDIO);

    // Run event loop for MCP processing
    while (true) {
        mcp_server->process_stdin();
        OS::get_singleton()->delay_usec(1000);
    }
}
```

## Resources (Read-Only Context)

MCP Resources provide read-only access to project data:

```cpp
Dictionary GodotMCPServer::_handle_resources_list(const Dictionary &p_params) {
    Array resources;

    // Current scene
    Node *scene = EditorInterface::get_singleton()->get_edited_scene_root();
    if (scene) {
        Dictionary scene_resource;
        scene_resource["uri"] = "godot://scene/current";
        scene_resource["name"] = "Current Scene";
        scene_resource["mimeType"] = "application/json";
        resources.push_back(scene_resource);
    }

    // Project scripts
    _add_file_resources(resources, "res://", "*.gd", "godot://script/");

    // Project scenes
    _add_file_resources(resources, "res://", "*.tscn", "godot://scene/");

    Dictionary result;
    result["resources"] = resources;
    return result;
}

Dictionary GodotMCPServer::_handle_resources_read(const Dictionary &p_params) {
    String uri = p_params.get("uri", "");

    if (uri == "godot://scene/current") {
        return _tool_get_scene_tree(Dictionary());
    }

    if (uri.begins_with("godot://script/")) {
        String path = "res://" + uri.substr(15);
        return _tool_read_script({{"path", path}});
    }

    if (uri.begins_with("godot://scene/")) {
        String path = "res://" + uri.substr(14);
        // Load and serialize scene
        Ref<PackedScene> scene = ResourceLoader::load(path);
        if (scene.is_valid()) {
            Node *instance = scene->instantiate();
            Ref<ClaudeSceneSerializer> serializer;
            serializer.instantiate();
            Dictionary data = serializer->serialize_scene(instance);
            memdelete(instance);

            Dictionary result;
            result["contents"] = Array::make(
                Dictionary{{"text", JSON::stringify(data)}}
            );
            return result;
        }
    }

    return _error_result("Resource not found: " + uri);
}
```

## Integration with Claude Code CLI

Once configured, users can interact with Godot through Claude Code:

```bash
# In project directory with .claude/mcp.json configured
claude

> Add a player character with WASD movement to my scene

Claude: I'll add a player character to your scene. Let me first check the current scene structure.

[Calling godot_get_scene_tree...]

I see you have a Main node. I'll add a CharacterBody3D player with collision and a movement script.

[Calling godot_add_node with parent_path="/root/Main", node_type="CharacterBody3D", node_name="Player"]
[Calling godot_add_node with parent_path="/root/Main/Player", node_type="CollisionShape3D", node_name="Collision"]
[Calling godot_create_script with path="res://scripts/player.gd", content="...", attach_to="/root/Main/Player"]

Done! I've created:
- CharacterBody3D "Player" with CollisionShape3D
- Movement script with WASD controls and jumping

You can test it by pressing F5 to run the scene.
```

## Benefits of MCP Approach

| Benefit | Description |
|---------|-------------|
| **Reuses Claude Code** | No need to reimplement Claude client, auth, or UI |
| **Cross-client** | Works with CLI, VS Code extension, any MCP client |
| **Standard protocol** | Well-documented, extensible MCP specification |
| **Lightweight module** | Godot only needs the server, not full integration |
| **Offline capable** | Claude Code handles all API communication |
| **Version independent** | MCP protocol is stable across Claude updates |

## Optional: Embedded UI

For convenience, the module can still include a simple dock UI that:
- Shows MCP server status
- Provides quick actions (start/stop server)
- Displays recent tool calls for debugging
- Offers direct input that routes through MCP

```cpp
class ClaudeMCPDock : public EditorDock {
    // Simple status display + server controls
    // NOT a full chat UI - that's Claude Code's job
};
```
