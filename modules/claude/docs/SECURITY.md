# Security Model

## Overview

The Claude MCP module implements a defense-in-depth security model to protect users from unintended modifications and data exposure. Since authentication is handled by Claude Code, the security focus is on safe tool execution.

## Threat Model

### Threats Addressed

| Threat | Risk Level | Mitigation |
|--------|------------|------------|
| Unintended file deletion | High | Undo/redo, path restrictions |
| Arbitrary code execution | Medium | Script validation, sandboxed paths |
| Project data exfiltration | Medium | Limited context serialization |
| Scene root removal | Medium | Explicit checks |
| Editor state corruption | Low | Editor internals protected |

### Out of Scope

- Authentication (handled by Claude Code)
- API rate limiting (handled by Claude Code)
- Network security (handled by Claude Code)
- Malicious Godot plugins (separate security domain)
- Compromised development machine

## Path Validation

### Script Path Requirements

All script operations are restricted to project paths:

```cpp
bool GodotMCPServer::_validate_script_path(const String &p_path, String &r_error) {
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

### Node Path Requirements

```cpp
bool GodotMCPServer::_validate_node_path(const String &p_path, String &r_error) {
    if (p_path.is_empty()) {
        r_error = "Node path is empty";
        return false;
    }

    if (!p_path.begins_with("/root/")) {
        r_error = "Node path must start with /root/";
        return false;
    }

    return true;
}
```

## Node Operation Safety

### Protected Operations

```cpp
// Cannot remove scene root
Node *scene_root = _get_scene_root();
if (node == scene_root) {
    return _error_result("Cannot remove scene root node");
}

// Cannot modify editor internals (paths starting with /root/EditorNode or /root/@)
```

### Undo/Redo Integration

All modifying operations are wrapped in undo/redo actions:

```cpp
EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
ur->create_action("MCP: Add " + node_name);
ur->add_do_method(parent, "add_child", new_node, true);
ur->add_do_method(new_node, "set_owner", scene_root);
ur->add_do_reference(new_node);
ur->add_undo_method(parent, "remove_child", new_node);
ur->commit_action();
```

This ensures:

- All changes can be reverted with Ctrl+Z
- Changes are grouped logically
- Node references are properly managed

## Context Serialization Security

### Property Filtering

Properties are filtered using two mechanisms:

**Substring matching** for security-sensitive patterns (catches variants like `user_password`, `my_api_key`):
- `password`, `api_key`, `secret`, `token`, `credential`, `auth`

**Exact matching** for large binary data (performance):
- `image`, `texture`, `audio`, `mesh`, `buffer`, `data`

**Exact matching** for internal engine properties:
- `_import_path`, `_bundled`

### Size Limits

```cpp
// Default limits
const int DEFAULT_MAX_DEPTH = 10;
const int DEFAULT_MAX_NODES = 500;
const int DEFAULT_MAX_PROPERTY_VALUE_LENGTH = 1000;
```

These limits prevent:

- Excessive memory usage
- Very large context payloads
- Infinite recursion in circular structures

## MCP Server Security

### Local-Only Communication

The MCP server:

- Binds TCP to `127.0.0.1` only (localhost, no external network exposure)
- Python bridge relays stdio↔TCP for Claude Code integration
- No authentication on the TCP port (any local process can connect)
- Default port 6009, server does not autostart (requires explicit opt-in)

### Input Validation

All tool arguments are validated before execution:

```cpp
// Type validation
if (!ClassDB::class_exists(node_type)) {
    return _error_result("Unknown node type: " + node_type);
}

if (!ClassDB::is_parent_class(node_type, "Node")) {
    return _error_result("Type is not a Node: " + node_type);
}
```

## Security Checklist

Before deployment, verify:

- [ ] All paths validated before file operations
- [ ] Cannot modify files outside `res://`
- [ ] Cannot delete scene root
- [ ] Cannot modify editor internals
- [ ] All operations support undo/redo
- [ ] Sensitive properties excluded from context
- [ ] Node limits prevent excessive serialization
- [ ] MCP server only listens on localhost

## Risk by Tool

| Tool | Risk Level | Safeguards |
|------|------------|------------|
| `godot_get_scene_tree` | Low | Property blacklist, size limits |
| `godot_get_property` | Low | Read-only |
| `godot_get_selected_nodes` | Low | Read-only |
| `godot_read_script` | Low | Read-only, path validation |
| `godot_select_nodes` | Low | UI-only change |
| `godot_set_property` | Medium | Undo/redo |
| `godot_add_node` | Medium | Undo/redo, type validation |
| `godot_create_script` | Medium | Path validation, undo/redo |
| `godot_run_scene` | Medium | Uses editor's run system |
| `godot_stop_scene` | Medium | Uses editor's run system |
| `godot_remove_node` | High | Undo/redo, root protection |
| `godot_modify_script` | High | Path validation, undo/redo |
