# Action System

## Overview

The action system parses Claude's responses into executable operations and applies them to the scene with full undo/redo support.

## Action Types

| Action | Risk Level | Description |
|--------|------------|-------------|
| `add_node` | Medium | Add a new node to the scene tree |
| `remove_node` | High | Remove a node and its children |
| `reparent_node` | Medium | Move a node to a different parent |
| `rename_node` | Low | Change a node's name |
| `set_property` | Low | Modify a single property |
| `set_properties_batch` | Low | Modify multiple properties at once |
| `create_script` | Medium | Create a new GDScript file |
| `attach_script` | Medium | Attach a script to a node |
| `modify_script` | High | Edit an existing script |
| `connect_signal` | Low | Connect a signal between nodes |
| `disconnect_signal` | Low | Disconnect a signal |
| `duplicate_node` | Medium | Duplicate a node and its children |
| `select_nodes` | Low | Change editor selection |

## ClaudeAction Class

### Header Definition

```cpp
// modules/claude/actions/claude_action.h

#ifndef CLAUDE_ACTION_H
#define CLAUDE_ACTION_H

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

class ClaudeAction : public RefCounted {
    GDCLASS(ClaudeAction, RefCounted);

public:
    enum ActionType {
        ACTION_NONE,
        ACTION_ADD_NODE,
        ACTION_REMOVE_NODE,
        ACTION_REPARENT_NODE,
        ACTION_RENAME_NODE,
        ACTION_SET_PROPERTY,
        ACTION_SET_PROPERTIES_BATCH,
        ACTION_CREATE_SCRIPT,
        ACTION_ATTACH_SCRIPT,
        ACTION_MODIFY_SCRIPT,
        ACTION_CONNECT_SIGNAL,
        ACTION_DISCONNECT_SIGNAL,
        ACTION_DUPLICATE_NODE,
        ACTION_SELECT_NODES,
        ACTION_COMPOSITE,
    };

    enum RiskLevel {
        RISK_LOW,      // Property changes, selections
        RISK_MEDIUM,   // Adding nodes, creating scripts
        RISK_HIGH,     // Removing nodes, modifying scripts
        RISK_CRITICAL, // Bulk operations, external changes
    };

private:
    ActionType type = ACTION_NONE;
    RiskLevel risk_level = RISK_LOW;
    Dictionary parameters;
    String description;
    String rationale;
    TypedArray<ClaudeAction> sub_actions;

    void _calculate_risk_level();

protected:
    static void _bind_methods();

public:
    // Factory methods for each action type
    static Ref<ClaudeAction> create_add_node(
        const String &p_parent_path,
        const String &p_type,
        const String &p_name,
        const Dictionary &p_properties = Dictionary());

    static Ref<ClaudeAction> create_remove_node(const String &p_node_path);

    static Ref<ClaudeAction> create_reparent_node(
        const String &p_node_path,
        const String &p_new_parent_path);

    static Ref<ClaudeAction> create_rename_node(
        const String &p_node_path,
        const String &p_new_name);

    static Ref<ClaudeAction> create_set_property(
        const String &p_node_path,
        const String &p_property,
        const Variant &p_value);

    static Ref<ClaudeAction> create_set_properties_batch(
        const String &p_node_path,
        const Dictionary &p_properties);

    static Ref<ClaudeAction> create_create_script(
        const String &p_path,
        const String &p_base_type,
        const String &p_content);

    static Ref<ClaudeAction> create_attach_script(
        const String &p_node_path,
        const String &p_script_path);

    static Ref<ClaudeAction> create_connect_signal(
        const String &p_source_path,
        const String &p_signal_name,
        const String &p_target_path,
        const String &p_method_name);

    static Ref<ClaudeAction> create_duplicate_node(
        const String &p_node_path,
        const String &p_new_name = "");

    static Ref<ClaudeAction> create_composite(
        const TypedArray<ClaudeAction> &p_sub_actions);

    // Accessors
    ActionType get_type() const { return type; }
    RiskLevel get_risk_level() const { return risk_level; }
    Dictionary get_parameters() const { return parameters; }
    String get_description() const { return description; }
    String get_rationale() const { return rationale; }
    TypedArray<ClaudeAction> get_sub_actions() const { return sub_actions; }

    void set_rationale(const String &p_rationale) { rationale = p_rationale; }

    // Validation
    bool is_valid() const;
    String get_validation_error() const;

    // Display
    String get_display_text() const;
    String get_icon_name() const;

    ClaudeAction();
};

VARIANT_ENUM_CAST(ClaudeAction::ActionType);
VARIANT_ENUM_CAST(ClaudeAction::RiskLevel);

#endif
```

### Factory Method Examples

```cpp
Ref<ClaudeAction> ClaudeAction::create_add_node(
        const String &p_parent_path,
        const String &p_type,
        const String &p_name,
        const Dictionary &p_properties) {

    Ref<ClaudeAction> action;
    action.instantiate();

    action->type = ACTION_ADD_NODE;
    action->risk_level = RISK_MEDIUM;

    Dictionary params;
    params["parent"] = p_parent_path;
    params["type"] = p_type;
    params["name"] = p_name;
    if (!p_properties.is_empty()) {
        params["properties"] = p_properties;
    }
    action->parameters = params;

    action->description = vformat("Add %s \"%s\" to %s",
        p_type, p_name, p_parent_path);

    return action;
}

Ref<ClaudeAction> ClaudeAction::create_create_script(
        const String &p_path,
        const String &p_base_type,
        const String &p_content) {

    Ref<ClaudeAction> action;
    action.instantiate();

    action->type = ACTION_CREATE_SCRIPT;
    action->risk_level = RISK_MEDIUM;

    Dictionary params;
    params["path"] = p_path;
    params["base_type"] = p_base_type;
    params["content"] = p_content;
    action->parameters = params;

    action->description = vformat("Create script %s (extends %s)",
        p_path, p_base_type);

    return action;
}
```

## ClaudeActionParser Class

### Header Definition

```cpp
// modules/claude/actions/claude_action_parser.h

#ifndef CLAUDE_ACTION_PARSER_H
#define CLAUDE_ACTION_PARSER_H

#include "claude_action.h"
#include "core/object/ref_counted.h"

class ClaudeActionParser : public RefCounted {
    GDCLASS(ClaudeActionParser, RefCounted);

private:
    Ref<ClaudeAction> _parse_single_action(const Dictionary &p_dict);
    Variant _parse_value(const Variant &p_value);

protected:
    static void _bind_methods();

public:
    // Main parsing method
    TypedArray<ClaudeAction> parse_response(const String &p_response);

    // Parse just the JSON array (for testing)
    TypedArray<ClaudeAction> parse_action_json(const String &p_json);

    // Extract action blocks from response
    PackedStringArray extract_action_blocks(const String &p_response);

    ClaudeActionParser();
};

#endif
```

### Implementation

```cpp
TypedArray<ClaudeAction> ClaudeActionParser::parse_response(const String &p_response) {
    TypedArray<ClaudeAction> actions;

    // Find all ```claude-actions blocks
    int search_pos = 0;
    while (true) {
        int start = p_response.find("```claude-actions", search_pos);
        if (start == -1) {
            break;
        }

        // Find the closing ```
        int content_start = p_response.find("\n", start) + 1;
        int end = p_response.find("```", content_start);
        if (end == -1) {
            break;
        }

        String json_block = p_response.substr(content_start, end - content_start);
        json_block = json_block.strip_edges();

        // Parse JSON
        JSON json;
        Error err = json.parse(json_block);
        if (err != OK) {
            WARN_PRINT(vformat("Failed to parse claude-actions block: %s (line %d)",
                json.get_error_message(), json.get_error_line()));
            search_pos = end + 3;
            continue;
        }

        Variant data = json.get_data();
        if (data.get_type() != Variant::ARRAY) {
            WARN_PRINT("claude-actions block must contain a JSON array");
            search_pos = end + 3;
            continue;
        }

        Array action_array = data;
        for (int i = 0; i < action_array.size(); i++) {
            if (action_array[i].get_type() != Variant::DICTIONARY) {
                WARN_PRINT(vformat("Action %d is not a dictionary", i));
                continue;
            }

            Ref<ClaudeAction> action = _parse_single_action(action_array[i]);
            if (action.is_valid() && action->is_valid()) {
                actions.push_back(action);
            } else if (action.is_valid()) {
                WARN_PRINT(vformat("Invalid action %d: %s",
                    i, action->get_validation_error()));
            }
        }

        search_pos = end + 3;
    }

    return actions;
}

Ref<ClaudeAction> ClaudeActionParser::_parse_single_action(const Dictionary &p_dict) {
    String action_type = p_dict.get("action", "");
    String rationale = p_dict.get("rationale", "");

    Ref<ClaudeAction> action;

    if (action_type == "add_node") {
        action = ClaudeAction::create_add_node(
            p_dict.get("parent", ""),
            p_dict.get("type", ""),
            p_dict.get("name", ""),
            p_dict.get("properties", Dictionary()));

    } else if (action_type == "remove_node") {
        action = ClaudeAction::create_remove_node(
            p_dict.get("node", ""));

    } else if (action_type == "reparent_node") {
        action = ClaudeAction::create_reparent_node(
            p_dict.get("node", ""),
            p_dict.get("new_parent", ""));

    } else if (action_type == "set_property") {
        action = ClaudeAction::create_set_property(
            p_dict.get("node", ""),
            p_dict.get("property", ""),
            _parse_value(p_dict.get("value", Variant())));

    } else if (action_type == "create_script") {
        action = ClaudeAction::create_create_script(
            p_dict.get("path", ""),
            p_dict.get("base_type", "Node"),
            p_dict.get("content", ""));

    } else if (action_type == "attach_script") {
        action = ClaudeAction::create_attach_script(
            p_dict.get("node", ""),
            p_dict.get("script", ""));

    } else if (action_type == "connect_signal") {
        action = ClaudeAction::create_connect_signal(
            p_dict.get("source", ""),
            p_dict.get("signal", ""),
            p_dict.get("target", ""),
            p_dict.get("method", ""));

    } else if (action_type == "duplicate_node") {
        action = ClaudeAction::create_duplicate_node(
            p_dict.get("node", ""),
            p_dict.get("new_name", ""));

    } else {
        WARN_PRINT(vformat("Unknown action type: %s", action_type));
        return Ref<ClaudeAction>();
    }

    if (action.is_valid() && !rationale.is_empty()) {
        action->set_rationale(rationale);
    }

    return action;
}
```

## ClaudeActionExecutor Class

### Header Definition

```cpp
// modules/claude/actions/claude_action_executor.h

#ifndef CLAUDE_ACTION_EXECUTOR_H
#define CLAUDE_ACTION_EXECUTOR_H

#include "claude_action.h"
#include "core/object/ref_counted.h"

class EditorUndoRedoManager;

class ClaudeActionExecutor : public RefCounted {
    GDCLASS(ClaudeActionExecutor, RefCounted);

public:
    enum ExecutionMode {
        MODE_IMMEDIATE,     // Execute immediately
        MODE_PREVIEW,       // Show preview, require confirmation
        MODE_DRY_RUN,       // Validate only
    };

    struct ExecutionResult {
        bool success = false;
        String error_message;
        Array affected_nodes;     // NodePaths of affected nodes
        Array created_resources;  // Resource paths created
    };

private:
    ExecutionMode mode = MODE_PREVIEW;
    bool require_confirmation_for_high_risk = true;
    int auto_apply_threshold = ClaudeAction::RISK_LOW;

    // Individual action executors
    ExecutionResult _execute_add_node(const Ref<ClaudeAction> &p_action);
    ExecutionResult _execute_remove_node(const Ref<ClaudeAction> &p_action);
    ExecutionResult _execute_reparent_node(const Ref<ClaudeAction> &p_action);
    ExecutionResult _execute_rename_node(const Ref<ClaudeAction> &p_action);
    ExecutionResult _execute_set_property(const Ref<ClaudeAction> &p_action);
    ExecutionResult _execute_set_properties_batch(const Ref<ClaudeAction> &p_action);
    ExecutionResult _execute_create_script(const Ref<ClaudeAction> &p_action);
    ExecutionResult _execute_attach_script(const Ref<ClaudeAction> &p_action);
    ExecutionResult _execute_connect_signal(const Ref<ClaudeAction> &p_action);
    ExecutionResult _execute_duplicate_node(const Ref<ClaudeAction> &p_action);

    // Validation helpers
    bool _validate_node_path(const String &p_path, String &r_error);
    bool _validate_node_type(const String &p_type, String &r_error);
    bool _validate_script_path(const String &p_path, String &r_error);
    bool _validate_property(Node *p_node, const String &p_property, String &r_error);

    // Utility
    Node *_get_node_by_path(const String &p_path);
    Node *_get_scene_root();

protected:
    static void _bind_methods();

public:
    // Execution
    ExecutionResult execute_action(const Ref<ClaudeAction> &p_action);
    ExecutionResult execute_action_batch(
        const TypedArray<ClaudeAction> &p_actions,
        const String &p_undo_name = "Claude Actions");

    // Preview (dry run with visual feedback)
    Dictionary preview_action(const Ref<ClaudeAction> &p_action);
    Array preview_action_batch(const TypedArray<ClaudeAction> &p_actions);

    // Validation only
    bool validate_action(const Ref<ClaudeAction> &p_action, String &r_error);
    bool validate_action_batch(const TypedArray<ClaudeAction> &p_actions,
                               Array &r_errors);

    // Configuration
    void set_execution_mode(ExecutionMode p_mode);
    ExecutionMode get_execution_mode() const;
    void set_require_confirmation(bool p_require);
    bool get_require_confirmation() const;
    void set_auto_apply_threshold(int p_risk_level);
    int get_auto_apply_threshold() const;

    // Safety
    bool is_action_allowed(const Ref<ClaudeAction> &p_action);

    ClaudeActionExecutor();
};

VARIANT_ENUM_CAST(ClaudeActionExecutor::ExecutionMode);

#endif
```

### Implementation: Add Node

Following the pattern from `scene_tree_dock.cpp`:

```cpp
ClaudeActionExecutor::ExecutionResult ClaudeActionExecutor::_execute_add_node(
        const Ref<ClaudeAction> &p_action) {

    ExecutionResult result;
    Dictionary params = p_action->get_parameters();

    String parent_path = params.get("parent", "");
    String node_type = params.get("type", "");
    String node_name = params.get("name", "");
    Dictionary properties = params.get("properties", Dictionary());

    // Validate parent exists
    String error;
    if (!_validate_node_path(parent_path, error)) {
        result.error_message = error;
        return result;
    }

    Node *parent = _get_node_by_path(parent_path);
    if (!parent) {
        result.error_message = vformat("Parent node not found: %s", parent_path);
        return result;
    }

    // Validate node type
    if (!_validate_node_type(node_type, error)) {
        result.error_message = error;
        return result;
    }

    // Create the node
    Node *new_node = Object::cast_to<Node>(ClassDB::instantiate(node_type));
    if (!new_node) {
        result.error_message = vformat("Failed to instantiate: %s", node_type);
        return result;
    }

    // Set name
    if (node_name.is_empty()) {
        node_name = node_type;
    }
    new_node->set_name(node_name);

    // Apply initial properties
    for (const Variant *key = properties.next(); key; key = properties.next(key)) {
        String prop_name = *key;
        Variant prop_value = properties[*key];

        if (ClassDB::has_property(node_type, prop_name)) {
            new_node->set(prop_name, prop_value);
        } else {
            WARN_PRINT(vformat("Unknown property: %s.%s", node_type, prop_name));
        }
    }

    // Get scene root for ownership
    Node *scene_root = _get_scene_root();

    // Create undo/redo action
    EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
    ur->create_action(TTR("Claude: Add ") + node_name);

    ur->add_do_method(parent, "add_child", new_node, true);
    ur->add_do_method(new_node, "set_owner", scene_root);
    ur->add_do_reference(new_node);

    ur->add_undo_method(parent, "remove_child", new_node);

    ur->commit_action();

    result.success = true;
    result.affected_nodes.push_back(new_node->get_path());
    return result;
}
```

### Implementation: Create Script

```cpp
ClaudeActionExecutor::ExecutionResult ClaudeActionExecutor::_execute_create_script(
        const Ref<ClaudeAction> &p_action) {

    ExecutionResult result;
    Dictionary params = p_action->get_parameters();

    String path = params.get("path", "");
    String base_type = params.get("base_type", "Node");
    String content = params.get("content", "");

    // Validate path
    String error;
    if (!_validate_script_path(path, error)) {
        result.error_message = error;
        return result;
    }

    // Check if file already exists
    if (FileAccess::exists(path)) {
        result.error_message = vformat("Script already exists: %s", path);
        return result;
    }

    // Ensure content extends the correct type
    if (!content.begins_with("extends ")) {
        content = "extends " + base_type + "\n\n" + content;
    }

    // Create directory if needed
    String dir = path.get_base_dir();
    Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
    if (!da->dir_exists(dir)) {
        Error err = da->make_dir_recursive(dir);
        if (err != OK) {
            result.error_message = vformat("Cannot create directory: %s", dir);
            return result;
        }
    }

    // Write the file
    Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
    if (!file.is_valid()) {
        result.error_message = vformat("Cannot write to: %s", path);
        return result;
    }

    file->store_string(content);
    file->close();

    // Refresh filesystem
    EditorFileSystem::get_singleton()->scan();

    // Create undo action (delete the file)
    EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
    ur->create_action(TTR("Claude: Create Script"));

    // Store content for undo (recreate on redo, delete on undo)
    ur->add_do_method(EditorFileSystem::get_singleton(), "scan");
    ur->add_undo_method(callable_mp_static(&ClaudeActionExecutor::_delete_file), path);
    ur->add_undo_method(EditorFileSystem::get_singleton(), "scan");

    ur->commit_action(false); // Don't execute do (already done)

    result.success = true;
    result.created_resources.push_back(path);
    return result;
}

void ClaudeActionExecutor::_delete_file(const String &p_path) {
    Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
    da->remove(p_path);
}
```

### Implementation: Attach Script

```cpp
ClaudeActionExecutor::ExecutionResult ClaudeActionExecutor::_execute_attach_script(
        const Ref<ClaudeAction> &p_action) {

    ExecutionResult result;
    Dictionary params = p_action->get_parameters();

    String node_path = params.get("node", "");
    String script_path = params.get("script", "");

    // Validate node
    String error;
    if (!_validate_node_path(node_path, error)) {
        result.error_message = error;
        return result;
    }

    Node *node = _get_node_by_path(node_path);
    if (!node) {
        result.error_message = vformat("Node not found: %s", node_path);
        return result;
    }

    // Load script
    Ref<Script> script = ResourceLoader::load(script_path, "Script");
    if (!script.is_valid()) {
        result.error_message = vformat("Cannot load script: %s", script_path);
        return result;
    }

    // Verify script is compatible with node
    StringName base_type = script->get_instance_base_type();
    if (!node->is_class(base_type)) {
        result.error_message = vformat("Script extends %s but node is %s",
            base_type, node->get_class());
        return result;
    }

    // Store old script for undo
    Ref<Script> old_script = node->get_script();

    // Create undo/redo action
    EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
    ur->create_action(TTR("Claude: Attach Script"));

    ur->add_do_method(node, "set_script", script);
    ur->add_undo_method(node, "set_script", old_script);

    ur->commit_action();

    result.success = true;
    result.affected_nodes.push_back(node_path);
    return result;
}
```

### Safety Checks

```cpp
bool ClaudeActionExecutor::is_action_allowed(const Ref<ClaudeAction> &p_action) {
    // Critical actions always require confirmation
    if (p_action->get_risk_level() == ClaudeAction::RISK_CRITICAL) {
        return false;
    }

    // Check user's auto-apply threshold
    if (p_action->get_risk_level() > auto_apply_threshold) {
        return false;
    }

    Dictionary params = p_action->get_parameters();
    ClaudeAction::ActionType type = p_action->get_type();

    // Prevent operations outside project directory
    if (type == ClaudeAction::ACTION_CREATE_SCRIPT ||
        type == ClaudeAction::ACTION_MODIFY_SCRIPT) {
        String path = params.get("path", "");
        if (!path.begins_with("res://")) {
            return false;
        }
    }

    // Prevent removing scene root
    if (type == ClaudeAction::ACTION_REMOVE_NODE) {
        String node_path = params.get("node", "");
        Node *scene_root = _get_scene_root();
        if (scene_root && node_path == String(scene_root->get_path())) {
            return false;
        }
    }

    // Prevent modifying engine internals
    if (type == ClaudeAction::ACTION_SET_PROPERTY) {
        String node_path = params.get("node", "");
        if (node_path.begins_with("/root/EditorNode")) {
            return false;
        }
    }

    return true;
}
```

## Batch Execution

```cpp
ClaudeActionExecutor::ExecutionResult ClaudeActionExecutor::execute_action_batch(
        const TypedArray<ClaudeAction> &p_actions,
        const String &p_undo_name) {

    ExecutionResult batch_result;
    batch_result.success = true;

    if (p_actions.is_empty()) {
        return batch_result;
    }

    // Validate all actions first
    Array errors;
    if (!validate_action_batch(p_actions, errors)) {
        batch_result.success = false;
        batch_result.error_message = "Validation failed: " + String(errors[0]);
        return batch_result;
    }

    // Create single undo action for entire batch
    EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
    ur->create_action(p_undo_name);

    // Execute each action
    for (int i = 0; i < p_actions.size(); i++) {
        Ref<ClaudeAction> action = p_actions[i];

        ExecutionResult single_result;
        switch (action->get_type()) {
            case ClaudeAction::ACTION_ADD_NODE:
                single_result = _execute_add_node(action);
                break;
            case ClaudeAction::ACTION_REMOVE_NODE:
                single_result = _execute_remove_node(action);
                break;
            case ClaudeAction::ACTION_SET_PROPERTY:
                single_result = _execute_set_property(action);
                break;
            case ClaudeAction::ACTION_CREATE_SCRIPT:
                single_result = _execute_create_script(action);
                break;
            case ClaudeAction::ACTION_ATTACH_SCRIPT:
                single_result = _execute_attach_script(action);
                break;
            // ... other action types
            default:
                single_result.error_message = "Unknown action type";
        }

        if (!single_result.success) {
            batch_result.success = false;
            batch_result.error_message = vformat("Action %d failed: %s",
                i, single_result.error_message);
            ur->commit_action(); // Commit partial for undo
            return batch_result;
        }

        // Accumulate results
        batch_result.affected_nodes.append_array(single_result.affected_nodes);
        batch_result.created_resources.append_array(single_result.created_resources);
    }

    ur->commit_action();
    return batch_result;
}
```
