# Security Model

## Overview

The Claude integration implements a defense-in-depth security model to protect users from accidental data exposure, unintended modifications, and potential API misuse.

## Threat Model

### Threats Addressed

| Threat | Risk Level | Mitigation |
|--------|------------|------------|
| API key exposure | High | Secure storage, environment variable support |
| Unintended file deletion | High | Preview mode, undo/redo, path restrictions |
| Arbitrary code execution | Medium | Script validation, sandboxed paths |
| Project data exfiltration | Medium | Limited context serialization |
| Rate limiting/cost | Low | Client-side throttling |
| Man-in-the-middle | Low | TLS enforcement |

### Out of Scope

- Malicious Godot plugins (separate security domain)
- Compromised development machine
- Insider threats with source access

## API Key Security

### Storage Locations

Priority order for API key resolution:

```cpp
String ClaudeClient::_get_api_key_secure() {
    // 1. Environment variable (CI/automation, highest priority)
    if (OS::get_singleton()->has_environment("CLAUDE_API_KEY")) {
        return OS::get_singleton()->get_environment("CLAUDE_API_KEY");
    }

    // 2. Explicitly set key (runtime override)
    if (!api_key.is_empty()) {
        return api_key;
    }

    // 3. Editor settings (user configuration)
    return EDITOR_GET("claude/api/key");
}
```

### Editor Settings Security

```cpp
// Registration with SECRET usage flag
EDITOR_DEF_BASIC("claude/api/key", "");
EditorSettings::get_singleton()->add_property_hint(PropertyInfo(
    Variant::STRING,
    "claude/api/key",
    PROPERTY_HINT_PASSWORD,  // Masks in UI
    "",
    PROPERTY_USAGE_SECRET    // Never exported
));
```

**Security Properties:**
- `PROPERTY_HINT_PASSWORD`: Displays as asterisks in settings UI
- `PROPERTY_USAGE_SECRET`: Excluded from project export
- Stored in user's `editor_settings-4.cfg` (outside project)
- Not tracked by version control (user-specific path)

### Storage Locations by Platform

| Platform | Settings Path |
|----------|--------------|
| Windows | `%APPDATA%\Godot\editor_settings-4.cfg` |
| macOS | `~/Library/Application Support/Godot/editor_settings-4.cfg` |
| Linux | `~/.config/godot/editor_settings-4.cfg` |

## Action Safety

### Risk Classification

```cpp
enum RiskLevel {
    RISK_LOW,      // Read-only, selection changes
    RISK_MEDIUM,   // Additive changes (add nodes, create files)
    RISK_HIGH,     // Destructive changes (remove nodes, modify scripts)
    RISK_CRITICAL, // Bulk operations, external system changes
};
```

### Risk by Action Type

| Action | Risk Level | Rationale |
|--------|------------|-----------|
| `select_nodes` | Low | No data modification |
| `set_property` | Low | Single property change |
| `connect_signal` | Low | Non-destructive |
| `add_node` | Medium | Additive, easily undone |
| `create_script` | Medium | Creates new file |
| `attach_script` | Medium | Changes node behavior |
| `duplicate_node` | Medium | Additive, easily undone |
| `reparent_node` | Medium | Structural change |
| `remove_node` | High | Destructive, may lose children |
| `modify_script` | High | Changes existing code |
| `composite` (bulk) | Critical | Multiple changes at once |

### Safety Checks

```cpp
bool ClaudeActionExecutor::is_action_allowed(const Ref<ClaudeAction> &p_action) {
    Dictionary params = p_action->get_parameters();
    ClaudeAction::ActionType type = p_action->get_type();

    // === Path Restrictions ===

    // Script operations must use res:// path
    if (type == ACTION_CREATE_SCRIPT || type == ACTION_MODIFY_SCRIPT) {
        String path = params.get("path", "");
        if (!path.begins_with("res://")) {
            ERR_PRINT("Scripts must be in project directory (res://)");
            return false;
        }

        // Block sensitive paths
        if (path.contains("..") ||
            path.begins_with("res://.") ||
            path.contains("/.")) {
            ERR_PRINT("Invalid script path (hidden/parent traversal)");
            return false;
        }
    }

    // === Node Restrictions ===

    // Cannot remove scene root
    if (type == ACTION_REMOVE_NODE) {
        String node_path = params.get("node", "");
        Node *scene_root = _get_scene_root();
        if (scene_root && node_path == String(scene_root->get_path())) {
            ERR_PRINT("Cannot remove scene root node");
            return false;
        }
    }

    // Cannot modify editor internals
    if (type == ACTION_SET_PROPERTY || type == ACTION_REMOVE_NODE) {
        String node_path = params.get("node", "");
        if (node_path.begins_with("/root/EditorNode") ||
            node_path.begins_with("/root/@") ||
            node_path.contains("_internal")) {
            ERR_PRINT("Cannot modify editor internal nodes");
            return false;
        }
    }

    // === Risk Level Checks ===

    // Critical actions always require confirmation
    if (p_action->get_risk_level() == RISK_CRITICAL) {
        return false; // Must go through preview UI
    }

    // Check user's auto-apply threshold
    int threshold = EDITOR_GET("claude/behavior/auto_apply_risk_level");
    if ((int)p_action->get_risk_level() > threshold) {
        return false; // Requires confirmation
    }

    return true;
}
```

### Path Validation

```cpp
bool ClaudeActionExecutor::_validate_script_path(const String &p_path, String &r_error) {
    // Must use res:// scheme
    if (!p_path.begins_with("res://")) {
        r_error = "Path must start with res://";
        return false;
    }

    // No parent directory traversal
    if (p_path.contains("..")) {
        r_error = "Path cannot contain parent traversal (..)";
        return false;
    }

    // No hidden files/directories
    if (p_path.contains("/.") || p_path.begins_with("res://.")) {
        r_error = "Cannot create hidden files";
        return false;
    }

    // Must be .gd extension for scripts
    if (!p_path.ends_with(".gd")) {
        r_error = "Script must have .gd extension";
        return false;
    }

    // Path length limit
    if (p_path.length() > 256) {
        r_error = "Path too long";
        return false;
    }

    return true;
}
```

## Context Serialization Security

### Property Blacklist

Never serialize these properties:

```cpp
void ClaudeSceneSerializer::_init_property_sets() {
    // Security-sensitive properties
    property_blacklist.insert("password");
    property_blacklist.insert("api_key");
    property_blacklist.insert("secret");
    property_blacklist.insert("token");
    property_blacklist.insert("credential");
    property_blacklist.insert("auth");

    // Large binary data
    property_blacklist.insert("image");
    property_blacklist.insert("texture");
    property_blacklist.insert("audio");
    property_blacklist.insert("mesh");
    property_blacklist.insert("buffer");

    // Internal engine properties
    property_blacklist.insert("_import_path");
    property_blacklist.insert("_bundled");
}
```

### Size Limits

```cpp
// Default limits (configurable in editor settings)
const int DEFAULT_MAX_DEPTH = 10;
const int DEFAULT_MAX_NODES = 500;
const int DEFAULT_MAX_PROPERTY_VALUE_LENGTH = 1000;

// Truncate long property values
Variant ClaudeSceneSerializer::_serialize_value(const Variant &p_value) {
    if (p_value.get_type() == Variant::STRING) {
        String str = p_value;
        if (str.length() > max_property_value_length) {
            return str.substr(0, max_property_value_length) + "...[truncated]";
        }
    }
    return p_value;
}
```

### Script Content Limits

When including script content in context:

```cpp
String ClaudeSceneSerializer::_get_script_content(const Ref<Script> &p_script) {
    String content = p_script->get_source_code();

    // Limit script size in context
    const int MAX_SCRIPT_CHARS = 10000;
    if (content.length() > MAX_SCRIPT_CHARS) {
        content = content.substr(0, MAX_SCRIPT_CHARS);
        content += "\n\n# ... [truncated, " +
            itos(p_script->get_source_code().length() - MAX_SCRIPT_CHARS) +
            " more characters]";
    }

    return content;
}
```

## Undo/Redo Safety

All operations are wrapped in undo/redo actions:

```cpp
void ClaudeActionExecutor::_execute_with_undo(
        const Callable &p_do_action,
        const Callable &p_undo_action,
        const String &p_action_name) {

    EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
    ur->create_action(p_action_name);

    ur->add_do_method(p_do_action);
    ur->add_undo_method(p_undo_action);

    ur->commit_action();
}
```

### File Operations

For file creation (scripts), store content for restoration:

```cpp
ExecutionResult ClaudeActionExecutor::_execute_create_script(
        const Ref<ClaudeAction> &p_action) {

    // ... create file ...

    // Create undo action that deletes the file
    EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
    ur->create_action(TTR("Claude: Create Script"));

    // Undo = delete file and rescan
    ur->add_undo_method(callable_mp_static(&_delete_file), path);
    ur->add_undo_method(EditorFileSystem::get_singleton(), "scan");

    // Redo = rewrite file and rescan (in case of undo then redo)
    ur->add_do_method(callable_mp_static(&_write_file), path, content);
    ur->add_do_method(EditorFileSystem::get_singleton(), "scan");

    ur->commit_action(false); // Don't execute do (already done)
}
```

## Network Security

### TLS Enforcement

```cpp
// ClaudeClient always uses HTTPS
const String DEFAULT_ENDPOINT = "https://api.anthropic.com/v1/messages";

Error ClaudeClient::send_message_streaming(...) {
    // Verify endpoint uses HTTPS
    if (!api_endpoint.begins_with("https://")) {
        ERR_PRINT("Claude API endpoint must use HTTPS");
        return ERR_INVALID_PARAMETER;
    }

    // ... proceed with request ...
}
```

### Request Headers

```cpp
PackedStringArray ClaudeClient::_build_headers() {
    PackedStringArray headers;

    headers.push_back("Content-Type: application/json");
    headers.push_back("x-api-key: " + _get_api_key_secure());
    headers.push_back("anthropic-version: 2023-06-01");

    // Identify client for debugging/rate limiting
    headers.push_back("User-Agent: Godot-Claude-Module/1.0");

    return headers;
}
```

### Rate Limiting

```cpp
// Client-side rate limiting to avoid API throttling
const int MAX_REQUESTS_PER_MINUTE = 50;
const int MIN_REQUEST_INTERVAL_MS = 1000; // 1 second minimum between requests

Error ClaudeClient::send_message_streaming(...) {
    uint64_t current_time = OS::get_singleton()->get_ticks_msec();

    // Check minute window
    if (current_time - last_request_time < 60000) {
        if (requests_this_minute >= MAX_REQUESTS_PER_MINUTE) {
            emit_signal("request_failed", "Rate limit reached. Please wait.");
            return ERR_BUSY;
        }
    } else {
        requests_this_minute = 0;
        last_request_time = current_time;
    }

    // Minimum interval between requests
    if (current_time - last_request_time < MIN_REQUEST_INTERVAL_MS) {
        emit_signal("request_failed", "Please wait before sending another request.");
        return ERR_BUSY;
    }

    // ... proceed with request ...

    requests_this_minute++;
    return OK;
}
```

## User Confirmation

### Preview Mode

All actions go through preview by default:

```cpp
void ClaudeDock::_on_response_complete(const String &p_full_response) {
    TypedArray<ClaudeAction> actions = parser->parse_response(p_full_response);

    if (!actions.is_empty()) {
        // Always show preview panel
        pending_actions = actions;
        _display_pending_actions();

        // User must explicitly click Apply
    }
}
```

### Confirmation Dialog for High-Risk

```cpp
void ClaudeDock::_on_apply_all_pressed() {
    // Check if any actions are high risk
    bool has_high_risk = false;
    for (int i = 0; i < pending_actions.size(); i++) {
        Ref<ClaudeAction> action = pending_actions[i];
        if (action->get_risk_level() >= ClaudeAction::RISK_HIGH) {
            has_high_risk = true;
            break;
        }
    }

    if (has_high_risk && EDITOR_GET("claude/behavior/require_confirmation")) {
        // Show confirmation dialog
        ConfirmationDialog *dialog = memnew(ConfirmationDialog);
        dialog->set_text(TTR("Some actions may modify or delete existing content. Continue?"));
        dialog->connect("confirmed", callable_mp(this, &ClaudeDock::_execute_pending_actions));
        dialog->popup_centered();
        add_child(dialog);
        return;
    }

    _execute_pending_actions();
}
```

## Audit Logging

```cpp
void ClaudeActionExecutor::_log_action(const Ref<ClaudeAction> &p_action,
                                       const ExecutionResult &p_result) {
    String log_entry = vformat("[Claude] %s: %s",
        p_action->get_display_text(),
        p_result.success ? "Success" : ("Failed: " + p_result.error_message));

    // Log to editor output
    print_line(log_entry);

    // Could also log to file for security audit
    // _append_to_audit_log(log_entry);
}
```

## Security Checklist

Before release, verify:

- [ ] API key never appears in logs or error messages
- [ ] API key not included in project export
- [ ] All paths validated before file operations
- [ ] Cannot modify files outside `res://`
- [ ] Cannot delete scene root
- [ ] Cannot modify editor internals
- [ ] All operations support undo/redo
- [ ] TLS enforced for API communication
- [ ] Rate limiting prevents runaway costs
- [ ] Large responses handled gracefully
- [ ] Sensitive properties excluded from context
