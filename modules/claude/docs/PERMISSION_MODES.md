# Permission Modes

## Overview

The Claude module supports multiple permission modes for how changes are applied, matching the Claude Code VS Code extension experience. Users can toggle between modes using **Shift+Tab**.

## Permission Modes

### 1. Ask Mode (Default)

Claude proposes changes and waits for user approval before executing.

```
┌─────────────────────────────────────────────────────────────┐
│ Mode: ASK                                    [Shift+Tab ↻] │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Claude: I'll add a CharacterBody3D with collision.        │
│                                                             │
│  ┌─ Proposed Changes ─────────────────────────────────────┐ │
│  │ + Add CharacterBody3D "Player" to /root/Main           │ │
│  │ + Add CollisionShape3D to /root/Main/Player            │ │
│  │ + Create res://scripts/player.gd                       │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                             │
│  [Accept All]  [Accept Selected]  [Reject]  [Edit]         │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**Behavior:**
- All proposed changes shown in preview panel
- User must click Accept to apply changes
- Can accept all, select specific changes, or reject
- Can edit proposed changes before accepting
- Full undo/redo support after acceptance

### 2. Auto Mode

Claude automatically applies changes without confirmation for most operations.

```
┌─────────────────────────────────────────────────────────────┐
│ Mode: AUTO                                   [Shift+Tab ↻] │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Claude: Adding CharacterBody3D with collision...          │
│                                                             │
│  ✓ Added CharacterBody3D "Player" to /root/Main            │
│  ✓ Added CollisionShape3D to /root/Main/Player             │
│  ✓ Created res://scripts/player.gd                         │
│                                                             │
│  Done! Created player with movement script.                 │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**Behavior:**
- Changes applied as Claude generates them
- Progress shown in real-time
- Still prompts for high-risk operations (deletions, script modifications)
- Full undo/redo support (Ctrl+Z reverts all changes from request)

### 3. Plan Mode

Claude creates a detailed plan before any execution.

```
┌─────────────────────────────────────────────────────────────┐
│ Mode: PLAN                                   [Shift+Tab ↻] │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Claude: Here's my plan for adding a player character:      │
│                                                             │
│  ## Plan                                                    │
│  1. Create CharacterBody3D node structure                   │
│     - Add CharacterBody3D as child of Main                  │
│     - Add CollisionShape3D with capsule shape               │
│     - Add Camera3D for first-person view                    │
│                                                             │
│  2. Create movement script                                  │
│     - Handle WASD input                                     │
│     - Implement gravity and jumping                         │
│     - Add mouse look                                        │
│                                                             │
│  3. Configure collision layers                              │
│                                                             │
│  [Execute Plan]  [Modify Plan]  [Cancel]                    │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**Behavior:**
- Claude analyzes task and creates step-by-step plan
- User reviews and can modify plan
- Execution happens as batch with preview
- Good for complex multi-step operations

## Mode Toggle Implementation

### Keyboard Shortcut: Shift+Tab

```cpp
// modules/claude/editor/claude_dock.cpp

void ClaudeDock::_input(const Ref<InputEvent> &p_event) {
    Ref<InputEventKey> key = p_event;
    if (key.is_null() || !key->is_pressed()) {
        return;
    }

    // Shift+Tab toggles permission mode
    if (key->get_keycode() == KEY_TAB && key->is_shift_pressed()) {
        _cycle_permission_mode();
        accept_event();
    }
}

void ClaudeDock::_cycle_permission_mode() {
    switch (permission_mode) {
        case PERMISSION_ASK:
            permission_mode = PERMISSION_AUTO;
            break;
        case PERMISSION_AUTO:
            permission_mode = PERMISSION_PLAN;
            break;
        case PERMISSION_PLAN:
            permission_mode = PERMISSION_ASK;
            break;
    }

    _update_mode_indicator();
    emit_signal("permission_mode_changed", permission_mode);

    // Save preference
    EditorSettings::get_singleton()->set(
        "claude/behavior/permission_mode", permission_mode);
}
```

### Mode Indicator UI

```cpp
void ClaudeDock::_build_mode_indicator() {
    mode_container = memnew(HBoxContainer);
    header_hbox->add_child(mode_container);

    mode_label = memnew(Label);
    mode_container->add_child(mode_label);

    mode_shortcut_label = memnew(Label);
    mode_shortcut_label->set_text("[Shift+Tab]");
    mode_shortcut_label->add_theme_color_override("font_color",
        Color(0.5, 0.5, 0.5));
    mode_container->add_child(mode_shortcut_label);

    _update_mode_indicator();
}

void ClaudeDock::_update_mode_indicator() {
    String mode_text;
    Color mode_color;

    switch (permission_mode) {
        case PERMISSION_ASK:
            mode_text = "ASK";
            mode_color = Color(0.3, 0.7, 1.0); // Blue
            break;
        case PERMISSION_AUTO:
            mode_text = "AUTO";
            mode_color = Color(0.3, 1.0, 0.5); // Green
            break;
        case PERMISSION_PLAN:
            mode_text = "PLAN";
            mode_color = Color(1.0, 0.8, 0.3); // Yellow
            break;
    }

    mode_label->set_text("Mode: " + mode_text);
    mode_label->add_theme_color_override("font_color", mode_color);
}
```

## Mode-Specific Behavior

### ClaudeActionExecutor Mode Handling

```cpp
// modules/claude/actions/claude_action_executor.cpp

ClaudeActionExecutor::ExecutionResult ClaudeActionExecutor::execute_for_mode(
        const TypedArray<ClaudeAction> &p_actions,
        PermissionMode p_mode) {

    switch (p_mode) {
        case PERMISSION_ASK:
            // Return preview, don't execute
            return _create_preview_result(p_actions);

        case PERMISSION_AUTO:
            // Execute immediately, but pause for high-risk
            return _execute_with_risk_check(p_actions);

        case PERMISSION_PLAN:
            // Create plan representation
            return _create_plan_result(p_actions);
    }

    return ExecutionResult();
}

ClaudeActionExecutor::ExecutionResult ClaudeActionExecutor::_execute_with_risk_check(
        const TypedArray<ClaudeAction> &p_actions) {

    ExecutionResult result;
    result.success = true;

    TypedArray<ClaudeAction> auto_apply;
    TypedArray<ClaudeAction> needs_confirmation;

    // Separate by risk level
    for (int i = 0; i < p_actions.size(); i++) {
        Ref<ClaudeAction> action = p_actions[i];

        if (action->get_risk_level() <= ClaudeAction::RISK_MEDIUM) {
            auto_apply.push_back(action);
        } else {
            needs_confirmation.push_back(action);
        }
    }

    // Execute low-risk actions immediately
    if (!auto_apply.is_empty()) {
        result = execute_action_batch(auto_apply, "Claude: Auto-applied changes");
        if (!result.success) {
            return result;
        }
    }

    // Queue high-risk for confirmation
    if (!needs_confirmation.is_empty()) {
        result.pending_confirmation = needs_confirmation;
        result.needs_confirmation = true;
    }

    return result;
}
```

### Response Handling by Mode

```cpp
void ClaudeDock::_on_response_complete(const String &p_full_response) {
    TypedArray<ClaudeAction> actions = parser->parse_response(p_full_response);

    if (actions.is_empty()) {
        // No actions, just display response
        return;
    }

    switch (permission_mode) {
        case PERMISSION_ASK:
            // Show preview panel
            pending_actions = actions;
            _display_pending_actions();
            break;

        case PERMISSION_AUTO:
            // Execute and show results
            _execute_auto_mode(actions);
            break;

        case PERMISSION_PLAN:
            // Show plan view
            _display_plan(actions);
            break;
    }
}

void ClaudeDock::_execute_auto_mode(const TypedArray<ClaudeAction> &p_actions) {
    ExecutionResult result = executor->execute_for_mode(p_actions, PERMISSION_AUTO);

    // Show what was executed
    for (int i = 0; i < result.executed_actions.size(); i++) {
        Ref<ClaudeAction> action = result.executed_actions[i];
        _append_message("system",
            vformat("[color=green]✓ %s[/color]", action->get_display_text()));
    }

    // Show confirmation prompt for high-risk if any
    if (result.needs_confirmation) {
        pending_actions = result.pending_confirmation;
        _display_confirmation_panel();
    }
}
```

## Visual Feedback

### Auto Mode Progress

```cpp
void ClaudeDock::_display_auto_progress(const Ref<ClaudeAction> &p_action,
                                        bool p_success) {
    String icon = p_success ? "✓" : "✗";
    String color = p_success ? "green" : "red";

    String text = vformat("[color=%s]%s %s[/color]",
        color, icon, p_action->get_display_text());

    _append_message("system", text);

    // Scroll to show latest
    chat_scroll->ensure_control_visible(chat_display);
}
```

### Plan Mode Display

```cpp
void ClaudeDock::_display_plan(const TypedArray<ClaudeAction> &p_actions) {
    // Clear and show plan panel
    plan_panel->show();
    action_panel->hide();

    plan_tree->clear();
    TreeItem *root = plan_tree->create_item();
    root->set_text(0, "Execution Plan");

    // Group actions by category
    HashMap<String, Vector<Ref<ClaudeAction>>> grouped;
    for (int i = 0; i < p_actions.size(); i++) {
        Ref<ClaudeAction> action = p_actions[i];
        String category = _get_action_category(action);
        grouped[category].push_back(action);
    }

    // Build tree
    for (const KeyValue<String, Vector<Ref<ClaudeAction>>> &E : grouped) {
        TreeItem *category_item = plan_tree->create_item(root);
        category_item->set_text(0, E.key);
        category_item->set_icon(0, get_editor_theme_icon("Folder"));

        for (const Ref<ClaudeAction> &action : E.value) {
            TreeItem *action_item = plan_tree->create_item(category_item);
            action_item->set_text(0, action->get_display_text());
            action_item->set_icon(0, get_editor_theme_icon(action->get_icon_name()));
            action_item->set_tooltip_text(0, action->get_rationale());
        }
    }

    pending_actions = p_actions;
}
```

## Settings Integration

```cpp
void ClaudeEditorPlugin::_register_settings() {
    // Permission mode
    EDITOR_DEF_BASIC("claude/behavior/permission_mode", 0);
    EditorSettings::get_singleton()->add_property_hint(PropertyInfo(
        Variant::INT,
        "claude/behavior/permission_mode",
        PROPERTY_HINT_ENUM,
        "Ask,Auto,Plan"));

    // Auto mode threshold
    EDITOR_DEF_BASIC("claude/behavior/auto_apply_threshold", 1);
    EditorSettings::get_singleton()->add_property_hint(PropertyInfo(
        Variant::INT,
        "claude/behavior/auto_apply_threshold",
        PROPERTY_HINT_ENUM,
        "None (Always Ask),Low Risk,Medium Risk"));
}
```

## Mode Persistence

The selected mode persists across editor sessions:

```cpp
ClaudeDock::ClaudeDock() {
    // Load saved permission mode
    permission_mode = (PermissionMode)(int)EDITOR_GET("claude/behavior/permission_mode");

    // Build UI
    _build_ui();
}

void ClaudeDock::_cycle_permission_mode() {
    // ... cycle logic ...

    // Save immediately
    EditorSettings::get_singleton()->set(
        "claude/behavior/permission_mode", (int)permission_mode);
}
```

## Comparison with VS Code Extension

| Feature | VS Code Extension | Godot Implementation |
|---------|-------------------|---------------------|
| Toggle shortcut | Shift+Tab | Shift+Tab |
| Ask mode | ✓ | ✓ |
| Auto mode | ✓ | ✓ |
| Plan mode | ✓ | ✓ |
| Risk-based auto | ✓ | ✓ |
| Mode indicator | Status bar | Dock header |
| Mode persistence | Workspace | Editor settings |
