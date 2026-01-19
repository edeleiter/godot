# API Communication Layer

## Overview

The API communication layer handles all interaction with Claude's API, including authentication, request/response handling, and streaming.

## ClaudeClient Class

### Header Definition

```cpp
// modules/claude/api/claude_client.h

#ifndef CLAUDE_CLIENT_H
#define CLAUDE_CLIENT_H

#include "core/io/http_client.h"
#include "core/io/json.h"
#include "core/object/ref_counted.h"
#include "scene/main/http_request.h"

class ClaudeClient : public RefCounted {
    GDCLASS(ClaudeClient, RefCounted);

public:
    enum Status {
        STATUS_IDLE,
        STATUS_CONNECTING,
        STATUS_STREAMING,
        STATUS_ERROR,
    };

private:
    // Configuration
    String api_key;
    String api_endpoint = "https://api.anthropic.com/v1/messages";
    String model = "claude-sonnet-4-20250514";
    int max_tokens = 4096;

    // HTTP
    HTTPRequest *http_request = nullptr;
    Status status = STATUS_IDLE;

    // Streaming state
    String accumulated_response;
    bool is_streaming = false;

    // Rate limiting
    uint64_t last_request_time = 0;
    int requests_this_minute = 0;
    static const int MAX_REQUESTS_PER_MINUTE = 50;

    // Internal methods
    void _on_request_completed(int p_result, int p_code,
                               const PackedStringArray &p_headers,
                               const PackedByteArray &p_body);
    void _process_stream_chunk(const String &p_chunk);
    Error _validate_api_key();
    String _get_api_key_secure();

protected:
    static void _bind_methods();

public:
    // Configuration
    void set_api_key(const String &p_key);
    String get_api_key() const;
    void set_model(const String &p_model);
    String get_model() const;
    void set_max_tokens(int p_tokens);
    int get_max_tokens() const;

    // API calls
    Error send_message(const String &p_message, const Dictionary &p_context);
    Error send_message_streaming(const String &p_message, const Dictionary &p_context);
    void cancel_request();

    // State
    Status get_status() const;
    String get_accumulated_response() const;
    bool is_busy() const;

    ClaudeClient();
    ~ClaudeClient();
};

VARIANT_ENUM_CAST(ClaudeClient::Status);

#endif // CLAUDE_CLIENT_H
```

### Implementation Details

```cpp
// modules/claude/api/claude_client.cpp

#include "claude_client.h"
#include "core/os/os.h"
#include "editor/editor_settings.h"

void ClaudeClient::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_api_key", "key"), &ClaudeClient::set_api_key);
    ClassDB::bind_method(D_METHOD("get_api_key"), &ClaudeClient::get_api_key);
    ClassDB::bind_method(D_METHOD("set_model", "model"), &ClaudeClient::set_model);
    ClassDB::bind_method(D_METHOD("get_model"), &ClaudeClient::get_model);
    ClassDB::bind_method(D_METHOD("send_message", "message", "context"), &ClaudeClient::send_message);
    ClassDB::bind_method(D_METHOD("send_message_streaming", "message", "context"), &ClaudeClient::send_message_streaming);
    ClassDB::bind_method(D_METHOD("cancel_request"), &ClaudeClient::cancel_request);
    ClassDB::bind_method(D_METHOD("get_status"), &ClaudeClient::get_status);
    ClassDB::bind_method(D_METHOD("is_busy"), &ClaudeClient::is_busy);

    ADD_SIGNAL(MethodInfo("response_chunk", PropertyInfo(Variant::STRING, "chunk")));
    ADD_SIGNAL(MethodInfo("response_complete", PropertyInfo(Variant::STRING, "full_response")));
    ADD_SIGNAL(MethodInfo("request_failed", PropertyInfo(Variant::STRING, "error")));

    BIND_ENUM_CONSTANT(STATUS_IDLE);
    BIND_ENUM_CONSTANT(STATUS_CONNECTING);
    BIND_ENUM_CONSTANT(STATUS_STREAMING);
    BIND_ENUM_CONSTANT(STATUS_ERROR);
}

ClaudeClient::ClaudeClient() {
    http_request = memnew(HTTPRequest);
    http_request->set_use_threads(true);
    http_request->set_timeout(120.0); // 2 minute timeout
    http_request->connect("request_completed",
        callable_mp(this, &ClaudeClient::_on_request_completed));
}

ClaudeClient::~ClaudeClient() {
    if (http_request) {
        http_request->cancel_request();
        memdelete(http_request);
    }
}

String ClaudeClient::_get_api_key_secure() {
    // Priority 1: Environment variable (for CI/automation)
    if (OS::get_singleton()->has_environment("CLAUDE_API_KEY")) {
        return OS::get_singleton()->get_environment("CLAUDE_API_KEY");
    }

    // Priority 2: Editor settings
    if (api_key.is_empty()) {
        return EDITOR_GET("claude/api/key");
    }

    return api_key;
}

Error ClaudeClient::send_message_streaming(const String &p_message,
                                           const Dictionary &p_context) {
    String key = _get_api_key_secure();
    ERR_FAIL_COND_V_MSG(key.is_empty(), ERR_UNCONFIGURED,
        "Claude API key not configured. Set in Editor Settings > Claude > API > Key");

    ERR_FAIL_COND_V_MSG(status != STATUS_IDLE, ERR_BUSY,
        "Request already in progress");

    // Rate limiting check
    uint64_t current_time = OS::get_singleton()->get_ticks_msec();
    if (current_time - last_request_time < 60000) {
        if (requests_this_minute >= MAX_REQUESTS_PER_MINUTE) {
            emit_signal("request_failed", "Rate limit reached. Please wait.");
            return ERR_BUSY;
        }
    } else {
        requests_this_minute = 0;
        last_request_time = current_time;
    }

    // Build request body
    Dictionary body;
    body["model"] = model;
    body["max_tokens"] = max_tokens;
    body["stream"] = true;

    // Build messages array
    Array messages;
    Dictionary user_message;
    user_message["role"] = "user";
    user_message["content"] = p_message;
    messages.push_back(user_message);
    body["messages"] = messages;

    // System prompt from context
    if (p_context.has("system_prompt")) {
        body["system"] = p_context["system_prompt"];
    }

    // Headers
    PackedStringArray headers;
    headers.push_back("Content-Type: application/json");
    headers.push_back("x-api-key: " + key);
    headers.push_back("anthropic-version: 2023-06-01");

    String json_body = JSON::stringify(body);

    status = STATUS_CONNECTING;
    is_streaming = true;
    accumulated_response = "";

    Error err = http_request->request(api_endpoint, headers,
                                       HTTPClient::METHOD_POST, json_body);

    if (err != OK) {
        status = STATUS_ERROR;
        emit_signal("request_failed", "Failed to initiate HTTP request");
        return err;
    }

    requests_this_minute++;
    return OK;
}

void ClaudeClient::_on_request_completed(int p_result, int p_code,
                                         const PackedStringArray &p_headers,
                                         const PackedByteArray &p_body) {
    if (p_result != HTTPRequest::RESULT_SUCCESS) {
        status = STATUS_ERROR;
        String error_msg;
        switch (p_result) {
            case HTTPRequest::RESULT_CANT_CONNECT:
                error_msg = "Cannot connect to Claude API";
                break;
            case HTTPRequest::RESULT_TIMEOUT:
                error_msg = "Request timed out";
                break;
            case HTTPRequest::RESULT_TLS_HANDSHAKE_ERROR:
                error_msg = "TLS handshake failed";
                break;
            default:
                error_msg = "HTTP request failed with code: " + itos(p_result);
        }
        emit_signal("request_failed", error_msg);
        return;
    }

    if (p_code != 200) {
        status = STATUS_ERROR;
        String body_text = String::utf8((const char *)p_body.ptr(), p_body.size());

        // Parse error message from response
        JSON json;
        if (json.parse(body_text) == OK) {
            Dictionary response = json.get_data();
            if (response.has("error")) {
                Dictionary error = response["error"];
                emit_signal("request_failed",
                    String(error.get("message", "Unknown error")));
                return;
            }
        }

        emit_signal("request_failed", "HTTP " + itos(p_code) + ": " + body_text);
        return;
    }

    // Parse successful response
    String body_text = String::utf8((const char *)p_body.ptr(), p_body.size());

    if (is_streaming) {
        _process_stream_chunk(body_text);
    } else {
        JSON json;
        if (json.parse(body_text) == OK) {
            Dictionary response = json.get_data();
            if (response.has("content")) {
                Array content = response["content"];
                if (content.size() > 0) {
                    Dictionary first = content[0];
                    accumulated_response = first.get("text", "");
                }
            }
        }
    }

    status = STATUS_IDLE;
    emit_signal("response_complete", accumulated_response);
}

void ClaudeClient::_process_stream_chunk(const String &p_chunk) {
    // Parse Server-Sent Events format
    PackedStringArray lines = p_chunk.split("\n");

    for (int i = 0; i < lines.size(); i++) {
        String line = lines[i].strip_edges();

        if (line.begins_with("data: ")) {
            String data = line.substr(6);

            if (data == "[DONE]") {
                continue;
            }

            JSON json;
            if (json.parse(data) == OK) {
                Dictionary event = json.get_data();
                String type = event.get("type", "");

                if (type == "content_block_delta") {
                    Dictionary delta = event.get("delta", Dictionary());
                    String text = delta.get("text", "");
                    accumulated_response += text;
                    emit_signal("response_chunk", text);
                }
            }
        }
    }
}

void ClaudeClient::cancel_request() {
    if (http_request) {
        http_request->cancel_request();
    }
    status = STATUS_IDLE;
    is_streaming = false;
}
```

## ClaudeSceneSerializer Class

### Purpose

Converts the current scene tree into a JSON structure that Claude can understand and reference.

### Header Definition

```cpp
// modules/claude/api/claude_scene_serializer.h

#ifndef CLAUDE_SCENE_SERIALIZER_H
#define CLAUDE_SCENE_SERIALIZER_H

#include "core/object/ref_counted.h"
#include "scene/main/node.h"

class ClaudeSceneSerializer : public RefCounted {
    GDCLASS(ClaudeSceneSerializer, RefCounted);

public:
    enum DetailLevel {
        DETAIL_MINIMAL,    // Names and types only
        DETAIL_STANDARD,   // + key properties (transform, visibility, etc.)
        DETAIL_FULL,       // All exported properties
    };

private:
    DetailLevel detail_level = DETAIL_STANDARD;
    int max_depth = 10;
    int max_nodes = 500;
    int current_node_count = 0;

    // Properties to never include (security/size)
    HashSet<StringName> property_blacklist;

    // Properties to always include at DETAIL_STANDARD
    HashSet<StringName> standard_properties;

    void _init_property_sets();
    Dictionary _serialize_node(Node *p_node, int p_depth);
    Dictionary _serialize_properties(Object *p_object);
    Variant _serialize_value(const Variant &p_value);
    Array _get_node_signals(Node *p_node);
    String _get_script_info(Node *p_node);

protected:
    static void _bind_methods();

public:
    // Main serialization methods
    Dictionary serialize_scene(Node *p_root);
    Dictionary serialize_selection(const TypedArray<Node> &p_nodes);
    Dictionary serialize_node_with_context(Node *p_node, int p_ancestor_levels = 2);

    // Configuration
    void set_detail_level(DetailLevel p_level);
    DetailLevel get_detail_level() const;
    void set_max_depth(int p_depth);
    int get_max_depth() const;
    void set_max_nodes(int p_nodes);
    int get_max_nodes() const;

    // Utility
    String to_compact_text(const Dictionary &p_scene_data);
    static String node_path_to_string(const NodePath &p_path);

    ClaudeSceneSerializer();
};

VARIANT_ENUM_CAST(ClaudeSceneSerializer::DetailLevel);

#endif
```

### Output Format

```json
{
  "scene_path": "res://scenes/main.tscn",
  "root": {
    "name": "Main",
    "type": "Node3D",
    "path": "/root/Main",
    "properties": {
      "transform": {
        "origin": [0, 0, 0],
        "basis": [[1,0,0], [0,1,0], [0,0,1]]
      },
      "visible": true
    },
    "script": {
      "path": "res://scripts/main.gd",
      "class_name": "Main"
    },
    "signals": ["player_died", "level_completed"],
    "children": [
      {
        "name": "Player",
        "type": "CharacterBody3D",
        "path": "/root/Main/Player",
        "properties": {
          "collision_layer": 1,
          "collision_mask": 1
        },
        "children": [
          {
            "name": "CollisionShape3D",
            "type": "CollisionShape3D",
            "path": "/root/Main/Player/CollisionShape3D"
          },
          {
            "name": "Camera3D",
            "type": "Camera3D",
            "path": "/root/Main/Player/Camera3D"
          }
        ]
      },
      {
        "name": "Environment",
        "type": "Node3D",
        "path": "/root/Main/Environment",
        "children": ["...truncated..."]
      }
    ]
  },
  "selection": ["/root/Main/Player"],
  "available_resources": {
    "scripts": ["res://scripts/main.gd", "res://scripts/player.gd"],
    "scenes": ["res://scenes/enemy.tscn", "res://scenes/ui.tscn"],
    "textures": ["res://textures/player.png"]
  },
  "truncated": false,
  "node_count": 47
}
```

### Serialization Logic

```cpp
Dictionary ClaudeSceneSerializer::_serialize_node(Node *p_node, int p_depth) {
    Dictionary result;

    // Check limits
    if (p_depth > max_depth || current_node_count >= max_nodes) {
        result["truncated"] = true;
        return result;
    }

    current_node_count++;

    // Basic info
    result["name"] = p_node->get_name();
    result["type"] = p_node->get_class();
    result["path"] = node_path_to_string(p_node->get_path());

    // Properties based on detail level
    if (detail_level != DETAIL_MINIMAL) {
        Dictionary props = _serialize_properties(p_node);
        if (!props.is_empty()) {
            result["properties"] = props;
        }
    }

    // Script info
    Ref<Script> script = p_node->get_script();
    if (script.is_valid()) {
        Dictionary script_info;
        script_info["path"] = script->get_path();
        if (!script->get_global_name().is_empty()) {
            script_info["class_name"] = script->get_global_name();
        }
        result["script"] = script_info;
    }

    // Custom signals
    Array signals = _get_node_signals(p_node);
    if (!signals.is_empty()) {
        result["signals"] = signals;
    }

    // Children
    int child_count = p_node->get_child_count();
    if (child_count > 0) {
        Array children;
        for (int i = 0; i < child_count; i++) {
            Node *child = p_node->get_child(i);

            // Skip internal nodes
            if (child->get_name().operator String().begins_with("_")) {
                continue;
            }

            Dictionary child_data = _serialize_node(child, p_depth + 1);
            children.push_back(child_data);
        }
        result["children"] = children;
    }

    return result;
}
```

## ClaudePromptBuilder Class

### Purpose

Constructs the system prompt that instructs Claude on how to understand and interact with Godot scenes.

### System Prompt Template

```cpp
String ClaudePromptBuilder::build_system_prompt(const Dictionary &p_context) {
    String prompt = R"PROMPT(You are Claude, an AI assistant integrated into the Godot 4 game engine editor. You help users build games by understanding their scene structure and generating precise editor actions.

## Your Capabilities

You can perform these actions on the user's scene:
- Add, remove, rename, and reparent nodes
- Modify node properties
- Create and attach GDScript files
- Connect signals between nodes
- Duplicate nodes

## Response Format

When the user asks you to modify their scene or project, respond with:

1. A brief explanation of your approach
2. A ```claude-actions code block containing a JSON array of actions
3. Any additional context or tips

Example:
```claude-actions
[
  {
    "action": "add_node",
    "parent": "/root/Main",
    "type": "CharacterBody3D",
    "name": "Player",
    "rationale": "CharacterBody3D provides physics-based movement"
  }
]
```

## Action Types

### add_node
Add a new node to the scene tree.
```json
{
  "action": "add_node",
  "parent": "/root/Main",           // Parent node path
  "type": "CharacterBody3D",        // Godot class name
  "name": "Player",                 // Node name
  "properties": {                   // Optional initial properties
    "collision_layer": 1,
    "collision_mask": 1
  },
  "rationale": "Why this node type"
}
```

### remove_node
Remove a node from the scene.
```json
{
  "action": "remove_node",
  "node": "/root/Main/OldNode",
  "rationale": "Why removing"
}
```

### set_property
Modify a node's property.
```json
{
  "action": "set_property",
  "node": "/root/Main/Player",
  "property": "speed",
  "value": 5.0,
  "rationale": "Adjusting movement speed"
}
```

### create_script
Create a new GDScript file.
```json
{
  "action": "create_script",
  "path": "res://scripts/player.gd",
  "base_type": "CharacterBody3D",
  "content": "extends CharacterBody3D\n\n@export var speed := 5.0\n\nfunc _physics_process(delta: float) -> void:\n    # Movement code\n    pass",
  "rationale": "Player movement controller"
}
```

### attach_script
Attach an existing script to a node.
```json
{
  "action": "attach_script",
  "node": "/root/Main/Player",
  "script": "res://scripts/player.gd",
  "rationale": "Adding movement behavior"
}
```

### reparent_node
Move a node to a different parent.
```json
{
  "action": "reparent_node",
  "node": "/root/Main/OldParent/Child",
  "new_parent": "/root/Main/NewParent",
  "rationale": "Reorganizing scene structure"
}
```

### connect_signal
Connect a signal between nodes.
```json
{
  "action": "connect_signal",
  "source": "/root/Main/Button",
  "signal": "pressed",
  "target": "/root/Main/Player",
  "method": "_on_button_pressed",
  "rationale": "Handle button click"
}
```

## Guidelines

1. **Node Paths**: Always use absolute paths starting with /root/
2. **GDScript**: Use Godot 4.x syntax with type hints
3. **Node Types**: Use appropriate types (CharacterBody3D for players, StaticBody3D for static objects, etc.)
4. **Exports**: Use @export for configurable properties
5. **Comments**: Add brief comments explaining non-obvious code
6. **Naming**: PascalCase for classes, snake_case for variables/functions

)PROMPT";

    // Append current context
    if (p_context.has("scene")) {
        prompt += "\n## Current Scene\n\n```json\n";
        prompt += JSON::stringify(p_context["scene"], "  ", false);
        prompt += "\n```\n";
    }

    if (p_context.has("selection") && !Array(p_context["selection"]).is_empty()) {
        prompt += "\n## Selected Nodes\n\n";
        Array selection = p_context["selection"];
        for (int i = 0; i < selection.size(); i++) {
            prompt += "- " + String(selection[i]) + "\n";
        }
    }

    if (p_context.has("current_script")) {
        prompt += "\n## Currently Open Script\n\n```gdscript\n";
        prompt += String(p_context["current_script"]);
        prompt += "\n```\n";
    }

    return prompt;
}
```

## HTTP Request Pattern

Based on Godot's Asset Library implementation:

```cpp
// Setup helper (from asset_library_editor_plugin.cpp)
static inline void setup_http_request(HTTPRequest *request) {
    request->set_use_threads(EDITOR_GET("asset_library/use_threads"));

    const String proxy_host = EDITOR_GET("network/http_proxy/host");
    const int proxy_port = EDITOR_GET("network/http_proxy/port");

    request->set_http_proxy(proxy_host, proxy_port);
    request->set_https_proxy(proxy_host, proxy_port);
}
```

## Error Handling

| Error | Handling |
|-------|----------|
| No API key | Show settings dialog prompt |
| Rate limited | Queue request with delay |
| Timeout | Offer retry option |
| Invalid response | Log error, show user-friendly message |
| Network error | Check connectivity, offer retry |

## Security Considerations

1. **API Key Storage**
   - Stored in EditorSettings with `PROPERTY_USAGE_SECRET`
   - Supports environment variable override
   - Never included in project exports

2. **Request Validation**
   - Validate all user input before sending
   - Sanitize context data (no passwords, keys, etc.)
   - Limit request size

3. **Response Validation**
   - Parse response safely
   - Handle malformed JSON gracefully
   - Validate action parameters before execution
