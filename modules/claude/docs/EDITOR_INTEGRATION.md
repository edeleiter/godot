# Editor Integration

## Overview

The editor integration consists of two main components:
1. `ClaudeDock` - The UI panel for interacting with Claude
2. `ClaudeEditorPlugin` - The plugin wrapper that manages lifecycle and settings

## ClaudeDock Class

### Header Definition

```cpp
// modules/claude/editor/claude_dock.h

#ifndef CLAUDE_DOCK_H
#define CLAUDE_DOCK_H

#include "editor/editor_dock.h"
#include "../api/claude_client.h"
#include "../api/claude_scene_serializer.h"
#include "../api/claude_prompt_builder.h"
#include "../actions/claude_action.h"
#include "../actions/claude_action_parser.h"
#include "../actions/claude_action_executor.h"

class Button;
class CheckBox;
class ItemList;
class Label;
class OptionButton;
class PanelContainer;
class ProgressBar;
class RichTextLabel;
class ScrollContainer;
class TextEdit;
class VBoxContainer;

class ClaudeDock : public EditorDock {
    GDCLASS(ClaudeDock, EditorDock);

private:
    // Core components
    Ref<ClaudeClient> client;
    Ref<ClaudeSceneSerializer> serializer;
    Ref<ClaudePromptBuilder> prompt_builder;
    Ref<ClaudeActionParser> parser;
    Ref<ClaudeActionExecutor> executor;

    // Main layout
    VBoxContainer *main_vbox = nullptr;

    // Chat area
    ScrollContainer *chat_scroll = nullptr;
    RichTextLabel *chat_display = nullptr;

    // Action preview
    PanelContainer *action_panel = nullptr;
    Label *action_header = nullptr;
    ItemList *action_list = nullptr;
    HBoxContainer *action_buttons = nullptr;
    Button *apply_all_btn = nullptr;
    Button *apply_selected_btn = nullptr;
    Button *reject_btn = nullptr;

    // Context options
    HBoxContainer *context_options = nullptr;
    CheckBox *include_scene_cb = nullptr;
    CheckBox *include_selection_cb = nullptr;
    CheckBox *include_script_cb = nullptr;
    OptionButton *detail_level_opt = nullptr;

    // Input area
    TextEdit *input_field = nullptr;
    HBoxContainer *input_buttons = nullptr;
    Button *send_btn = nullptr;
    Button *cancel_btn = nullptr;

    // Status
    HBoxContainer *status_bar = nullptr;
    Label *status_label = nullptr;
    ProgressBar *progress_bar = nullptr;

    // State
    TypedArray<ClaudeAction> pending_actions;
    Array conversation_history;
    bool is_waiting_response = false;

    // UI building
    void _build_ui();
    void _build_chat_area();
    void _build_action_preview();
    void _build_context_options();
    void _build_input_area();
    void _build_status_bar();
    void _update_theme();

    // Event handlers
    void _on_send_pressed();
    void _on_cancel_pressed();
    void _on_input_text_changed();
    void _on_input_gui_input(const Ref<InputEvent> &p_event);

    // Response handlers
    void _on_response_chunk(const String &p_chunk);
    void _on_response_complete(const String &p_full_response);
    void _on_request_failed(const String &p_error);

    // Action handlers
    void _on_apply_all_pressed();
    void _on_apply_selected_pressed();
    void _on_reject_pressed();
    void _on_action_selected(int p_index);
    void _on_action_activated(int p_index);

    // Context
    Dictionary _build_context();
    void _append_message(const String &p_role, const String &p_content);
    void _display_pending_actions();
    void _clear_pending_actions();
    void _update_action_buttons();

    // Editor notifications
    void _on_selection_changed();
    void _on_scene_changed();

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    void send_message(const String &p_message);
    void clear_conversation();
    void focus_input();

    ClaudeDock();
    ~ClaudeDock();
};

#endif
```

### UI Layout

```
┌─────────────────────────────────────────────────────────────┐
│ Claude Assistant                                       [⚙] │ ← Title bar
├─────────────────────────────────────────────────────────────┤
│ ┌─────────────────────────────────────────────────────────┐ │
│ │                                                         │ │
│ │  [User]                                                 │ │
│ │  Create a player character with movement                │ │
│ │                                                         │ │
│ │  [Claude]                                               │ │
│ │  I'll create a CharacterBody3D with a collision shape   │ │
│ │  and a movement script...                               │ │
│ │                                                         │ │
│ │                                                         │ │
│ └─────────────────────────────────────────────────────────┘ │ ← Chat scroll
├─────────────────────────────────────────────────────────────┤
│ ┌─ Pending Actions (3) ───────────────────────────────────┐ │
│ │  ☑ Add CharacterBody3D "Player" to /root/Main          │ │
│ │  ☑ Add CollisionShape3D to /root/Main/Player           │ │
│ │  ☑ Create script res://scripts/player.gd               │ │
│ └─────────────────────────────────────────────────────────┘ │ ← Action list
│ [Apply All]  [Apply Selected]  [Reject]                     │ ← Action buttons
├─────────────────────────────────────────────────────────────┤
│ Context: [☑] Scene  [☑] Selection  [ ] Script   [Standard▼]│ ← Context opts
├─────────────────────────────────────────────────────────────┤
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ Type your message...                                    │ │
│ │                                                         │ │
│ └─────────────────────────────────────────────────────────┘ │ ← Input field
│ [Send]                                             [Cancel] │ ← Input buttons
├─────────────────────────────────────────────────────────────┤
│ ●  Ready                                                    │ ← Status bar
└─────────────────────────────────────────────────────────────┘
```

### Implementation: UI Building

```cpp
void ClaudeDock::_build_ui() {
    // Configure dock properties
    set_title("Claude");
    set_icon_name("Claude");
    set_default_slot(DOCK_SLOT_RIGHT_UL);
    set_available_layouts(DOCK_LAYOUT_VERTICAL | DOCK_LAYOUT_FLOATING);

    // Main container
    main_vbox = memnew(VBoxContainer);
    main_vbox->set_anchors_preset(PRESET_FULL_RECT);
    add_child(main_vbox);

    _build_chat_area();
    _build_action_preview();
    _build_context_options();
    _build_input_area();
    _build_status_bar();
}

void ClaudeDock::_build_chat_area() {
    chat_scroll = memnew(ScrollContainer);
    chat_scroll->set_v_size_flags(SIZE_EXPAND_FILL);
    chat_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
    main_vbox->add_child(chat_scroll);

    chat_display = memnew(RichTextLabel);
    chat_display->set_use_bbcode(true);
    chat_display->set_fit_content(true);
    chat_display->set_scroll_active(false);
    chat_display->set_selection_enabled(true);
    chat_display->set_context_menu_enabled(true);
    chat_scroll->add_child(chat_display);
}

void ClaudeDock::_build_action_preview() {
    action_panel = memnew(PanelContainer);
    action_panel->hide(); // Hidden until actions are pending
    main_vbox->add_child(action_panel);

    VBoxContainer *action_vbox = memnew(VBoxContainer);
    action_panel->add_child(action_vbox);

    action_header = memnew(Label);
    action_header->set_text("Pending Actions");
    action_vbox->add_child(action_header);

    action_list = memnew(ItemList);
    action_list->set_max_columns(1);
    action_list->set_select_mode(ItemList::SELECT_MULTI);
    action_list->set_custom_minimum_size(Size2(0, 100));
    action_list->connect("item_selected",
        callable_mp(this, &ClaudeDock::_on_action_selected));
    action_list->connect("item_activated",
        callable_mp(this, &ClaudeDock::_on_action_activated));
    action_vbox->add_child(action_list);

    action_buttons = memnew(HBoxContainer);
    action_vbox->add_child(action_buttons);

    apply_all_btn = memnew(Button);
    apply_all_btn->set_text("Apply All");
    apply_all_btn->connect("pressed",
        callable_mp(this, &ClaudeDock::_on_apply_all_pressed));
    action_buttons->add_child(apply_all_btn);

    apply_selected_btn = memnew(Button);
    apply_selected_btn->set_text("Apply Selected");
    apply_selected_btn->set_disabled(true);
    apply_selected_btn->connect("pressed",
        callable_mp(this, &ClaudeDock::_on_apply_selected_pressed));
    action_buttons->add_child(apply_selected_btn);

    action_buttons->add_spacer();

    reject_btn = memnew(Button);
    reject_btn->set_text("Reject");
    reject_btn->connect("pressed",
        callable_mp(this, &ClaudeDock::_on_reject_pressed));
    action_buttons->add_child(reject_btn);
}

void ClaudeDock::_build_context_options() {
    context_options = memnew(HBoxContainer);
    main_vbox->add_child(context_options);

    Label *ctx_label = memnew(Label);
    ctx_label->set_text("Context:");
    context_options->add_child(ctx_label);

    include_scene_cb = memnew(CheckBox);
    include_scene_cb->set_text("Scene");
    include_scene_cb->set_pressed(true);
    context_options->add_child(include_scene_cb);

    include_selection_cb = memnew(CheckBox);
    include_selection_cb->set_text("Selection");
    include_selection_cb->set_pressed(true);
    context_options->add_child(include_selection_cb);

    include_script_cb = memnew(CheckBox);
    include_script_cb->set_text("Script");
    include_script_cb->set_pressed(false);
    context_options->add_child(include_script_cb);

    context_options->add_spacer();

    detail_level_opt = memnew(OptionButton);
    detail_level_opt->add_item("Minimal", ClaudeSceneSerializer::DETAIL_MINIMAL);
    detail_level_opt->add_item("Standard", ClaudeSceneSerializer::DETAIL_STANDARD);
    detail_level_opt->add_item("Full", ClaudeSceneSerializer::DETAIL_FULL);
    detail_level_opt->select(1); // Standard
    context_options->add_child(detail_level_opt);
}

void ClaudeDock::_build_input_area() {
    input_field = memnew(TextEdit);
    input_field->set_placeholder("Type your message...");
    input_field->set_custom_minimum_size(Size2(0, 60));
    input_field->set_line_wrapping_mode(TextEdit::LINE_WRAPPING_BOUNDARY);
    input_field->connect("text_changed",
        callable_mp(this, &ClaudeDock::_on_input_text_changed));
    input_field->connect("gui_input",
        callable_mp(this, &ClaudeDock::_on_input_gui_input));
    main_vbox->add_child(input_field);

    input_buttons = memnew(HBoxContainer);
    main_vbox->add_child(input_buttons);

    send_btn = memnew(Button);
    send_btn->set_text("Send");
    send_btn->set_disabled(true);
    send_btn->connect("pressed",
        callable_mp(this, &ClaudeDock::_on_send_pressed));
    input_buttons->add_child(send_btn);

    input_buttons->add_spacer();

    cancel_btn = memnew(Button);
    cancel_btn->set_text("Cancel");
    cancel_btn->set_disabled(true);
    cancel_btn->connect("pressed",
        callable_mp(this, &ClaudeDock::_on_cancel_pressed));
    input_buttons->add_child(cancel_btn);
}
```

### Implementation: Message Handling

```cpp
void ClaudeDock::_on_send_pressed() {
    String message = input_field->get_text().strip_edges();
    if (message.is_empty()) {
        return;
    }

    // Clear input
    input_field->set_text("");
    send_btn->set_disabled(true);
    cancel_btn->set_disabled(false);

    // Display user message
    _append_message("user", message);

    // Build context
    Dictionary context = _build_context();

    // Send to Claude
    is_waiting_response = true;
    status_label->set_text("Sending...");
    progress_bar->set_value(0);
    progress_bar->show();

    Error err = client->send_message_streaming(message, context);
    if (err != OK) {
        _on_request_failed("Failed to send message");
    }
}

Dictionary ClaudeDock::_build_context() {
    Dictionary context;

    // Scene context
    if (include_scene_cb->is_pressed()) {
        Node *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();
        if (scene_root) {
            serializer->set_detail_level(
                (ClaudeSceneSerializer::DetailLevel)detail_level_opt->get_selected_id());
            context["scene"] = serializer->serialize_scene(scene_root);
        }
    }

    // Selection context
    if (include_selection_cb->is_pressed()) {
        EditorSelection *selection = EditorInterface::get_singleton()->get_selection();
        TypedArray<Node> selected = selection->get_selected_nodes();
        if (!selected.is_empty()) {
            Array paths;
            for (int i = 0; i < selected.size(); i++) {
                Node *node = Object::cast_to<Node>(selected[i]);
                if (node) {
                    paths.push_back(String(node->get_path()));
                }
            }
            context["selection"] = paths;
        }
    }

    // Script context
    if (include_script_cb->is_pressed()) {
        // Get currently open script from ScriptEditor
        ScriptEditor *se = ScriptEditor::get_singleton();
        if (se) {
            Ref<Script> script = se->get_current_script();
            if (script.is_valid()) {
                context["current_script"] = script->get_source_code();
                context["current_script_path"] = script->get_path();
            }
        }
    }

    // Build system prompt
    context["system_prompt"] = prompt_builder->build_system_prompt(context);

    return context;
}

void ClaudeDock::_on_response_chunk(const String &p_chunk) {
    // Append to chat display in real-time
    // This shows the response as it streams in
    chat_display->append_text(p_chunk);
    chat_scroll->ensure_control_visible(chat_display);

    // Update progress (indeterminate)
    progress_bar->set_value(fmod(progress_bar->get_value() + 5, 100));
}

void ClaudeDock::_on_response_complete(const String &p_full_response) {
    is_waiting_response = false;
    cancel_btn->set_disabled(true);
    progress_bar->hide();
    status_label->set_text("Ready");

    // Parse actions from response
    TypedArray<ClaudeAction> actions = parser->parse_response(p_full_response);

    if (!actions.is_empty()) {
        pending_actions = actions;
        _display_pending_actions();
    }

    // Store in conversation history
    Dictionary msg;
    msg["role"] = "assistant";
    msg["content"] = p_full_response;
    conversation_history.push_back(msg);
}

void ClaudeDock::_display_pending_actions() {
    action_list->clear();

    for (int i = 0; i < pending_actions.size(); i++) {
        Ref<ClaudeAction> action = pending_actions[i];

        // Add to list with icon
        int idx = action_list->add_item(action->get_display_text());
        action_list->set_item_icon(idx,
            get_editor_theme_icon(action->get_icon_name()));

        // Color by risk level
        Color color;
        switch (action->get_risk_level()) {
            case ClaudeAction::RISK_LOW:
                color = Color(0.5, 1.0, 0.5); // Green
                break;
            case ClaudeAction::RISK_MEDIUM:
                color = Color(1.0, 1.0, 0.5); // Yellow
                break;
            case ClaudeAction::RISK_HIGH:
                color = Color(1.0, 0.5, 0.5); // Red
                break;
            default:
                color = Color(1.0, 0.5, 1.0); // Purple for critical
        }
        action_list->set_item_custom_fg_color(idx, color);

        // Tooltip with rationale
        if (!action->get_rationale().is_empty()) {
            action_list->set_item_tooltip(idx, action->get_rationale());
        }
    }

    action_header->set_text(vformat("Pending Actions (%d)", pending_actions.size()));
    action_panel->show();
    _update_action_buttons();
}
```

### Implementation: Action Execution

```cpp
void ClaudeDock::_on_apply_all_pressed() {
    if (pending_actions.is_empty()) {
        return;
    }

    // Execute all actions
    ClaudeActionExecutor::ExecutionResult result =
        executor->execute_action_batch(pending_actions, "Claude Actions");

    if (result.success) {
        _append_message("system",
            vformat("[color=green]Applied %d actions successfully.[/color]",
                pending_actions.size()));
    } else {
        _append_message("system",
            vformat("[color=red]Error: %s[/color]", result.error_message));
    }

    _clear_pending_actions();
}

void ClaudeDock::_on_apply_selected_pressed() {
    PackedInt32Array selected = action_list->get_selected_items();
    if (selected.is_empty()) {
        return;
    }

    // Build array of selected actions
    TypedArray<ClaudeAction> to_apply;
    for (int i = 0; i < selected.size(); i++) {
        to_apply.push_back(pending_actions[selected[i]]);
    }

    // Execute selected actions
    ClaudeActionExecutor::ExecutionResult result =
        executor->execute_action_batch(to_apply, "Claude Actions (Selected)");

    if (result.success) {
        _append_message("system",
            vformat("[color=green]Applied %d actions successfully.[/color]",
                to_apply.size()));

        // Remove applied actions from pending
        // (iterate in reverse to maintain indices)
        for (int i = selected.size() - 1; i >= 0; i--) {
            pending_actions.remove_at(selected[i]);
        }

        if (pending_actions.is_empty()) {
            _clear_pending_actions();
        } else {
            _display_pending_actions();
        }
    } else {
        _append_message("system",
            vformat("[color=red]Error: %s[/color]", result.error_message));
    }
}

void ClaudeDock::_on_reject_pressed() {
    _clear_pending_actions();
    _append_message("system", "[color=gray]Actions rejected.[/color]");
}

void ClaudeDock::_clear_pending_actions() {
    pending_actions.clear();
    action_list->clear();
    action_panel->hide();
}
```

## ClaudeEditorPlugin Class

### Header Definition

```cpp
// modules/claude/editor/claude_editor_plugin.h

#ifndef CLAUDE_EDITOR_PLUGIN_H
#define CLAUDE_EDITOR_PLUGIN_H

#include "editor/editor_plugin.h"
#include "claude_dock.h"

class ClaudeEditorPlugin : public EditorPlugin {
    GDCLASS(ClaudeEditorPlugin, EditorPlugin);

private:
    ClaudeDock *dock = nullptr;

    void _register_settings();
    void _on_shortcut_focus_dock();

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    virtual String get_name() const override { return "Claude"; }
    virtual bool has_main_screen() const override { return false; }

    virtual void edit(Object *p_object) override;
    virtual bool handles(Object *p_object) const override;

    ClaudeEditorPlugin();
    ~ClaudeEditorPlugin();
};

#endif
```

### Implementation

```cpp
// modules/claude/editor/claude_editor_plugin.cpp

#include "claude_editor_plugin.h"
#include "editor/editor_settings.h"
#include "editor/docks/editor_dock_manager.h"

void ClaudeEditorPlugin::_bind_methods() {
    // No additional bindings needed
}

ClaudeEditorPlugin::ClaudeEditorPlugin() {
    // Register editor settings
    _register_settings();

    // Create dock
    dock = memnew(ClaudeDock);
    add_dock(dock);
}

ClaudeEditorPlugin::~ClaudeEditorPlugin() {
    if (dock) {
        remove_dock(dock);
        memdelete(dock);
        dock = nullptr;
    }
}

void ClaudeEditorPlugin::_register_settings() {
    // API Settings
    EDITOR_DEF_BASIC("claude/api/key", "");
    EditorSettings::get_singleton()->set_initial_value("claude/api/key", "", true);
    EditorSettings::get_singleton()->add_property_hint(PropertyInfo(
        Variant::STRING,
        "claude/api/key",
        PROPERTY_HINT_PASSWORD));

    EDITOR_DEF_BASIC("claude/api/model", "claude-sonnet-4-20250514");
    EditorSettings::get_singleton()->add_property_hint(PropertyInfo(
        Variant::STRING,
        "claude/api/model",
        PROPERTY_HINT_ENUM,
        "claude-sonnet-4-20250514,claude-opus-4-20250514,claude-3-5-sonnet-20241022"));

    EDITOR_DEF_BASIC("claude/api/max_tokens", 4096);
    EditorSettings::get_singleton()->add_property_hint(PropertyInfo(
        Variant::INT,
        "claude/api/max_tokens",
        PROPERTY_HINT_RANGE,
        "256,8192,256"));

    // Behavior Settings
    EDITOR_DEF_BASIC("claude/behavior/require_confirmation", true);

    EDITOR_DEF_BASIC("claude/behavior/auto_apply_risk_level", 0);
    EditorSettings::get_singleton()->add_property_hint(PropertyInfo(
        Variant::INT,
        "claude/behavior/auto_apply_risk_level",
        PROPERTY_HINT_ENUM,
        "None,Low,Medium"));

    EDITOR_DEF_BASIC("claude/behavior/include_scene_by_default", true);
    EDITOR_DEF_BASIC("claude/behavior/include_selection_by_default", true);

    // Context Settings
    EDITOR_DEF_BASIC("claude/context/max_depth", 10);
    EditorSettings::get_singleton()->add_property_hint(PropertyInfo(
        Variant::INT,
        "claude/context/max_depth",
        PROPERTY_HINT_RANGE,
        "1,50,1"));

    EDITOR_DEF_BASIC("claude/context/max_nodes", 500);
    EditorSettings::get_singleton()->add_property_hint(PropertyInfo(
        Variant::INT,
        "claude/context/max_nodes",
        PROPERTY_HINT_RANGE,
        "10,5000,10"));

    EDITOR_DEF_BASIC("claude/context/detail_level", 1);
    EditorSettings::get_singleton()->add_property_hint(PropertyInfo(
        Variant::INT,
        "claude/context/detail_level",
        PROPERTY_HINT_ENUM,
        "Minimal,Standard,Full"));
}

void ClaudeEditorPlugin::_notification(int p_what) {
    switch (p_what) {
        case NOTIFICATION_ENTER_TREE: {
            // Connect to editor signals
            EditorSelection *selection = get_editor_interface()->get_selection();
            selection->connect("selection_changed",
                callable_mp(dock, &ClaudeDock::_on_selection_changed));
        } break;

        case NOTIFICATION_EXIT_TREE: {
            // Disconnect signals
        } break;
    }
}

void ClaudeEditorPlugin::edit(Object *p_object) {
    // Could implement context-aware editing
    // e.g., automatically include selected node in context
}

bool ClaudeEditorPlugin::handles(Object *p_object) const {
    // Claude dock doesn't handle specific object types
    return false;
}
```

## Registration

### Module Registration

```cpp
// modules/claude/register_types.cpp

#include "register_types.h"

#include "api/claude_client.h"
#include "api/claude_scene_serializer.h"
#include "api/claude_prompt_builder.h"
#include "actions/claude_action.h"
#include "actions/claude_action_parser.h"
#include "actions/claude_action_executor.h"

#ifdef TOOLS_ENABLED
#include "editor/claude_dock.h"
#include "editor/claude_editor_plugin.h"
#endif

void initialize_claude_module(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        // Core classes available at runtime
        GDREGISTER_CLASS(ClaudeClient);
        GDREGISTER_CLASS(ClaudeSceneSerializer);
        GDREGISTER_CLASS(ClaudePromptBuilder);
        GDREGISTER_CLASS(ClaudeAction);
        GDREGISTER_CLASS(ClaudeActionParser);
        GDREGISTER_CLASS(ClaudeActionExecutor);
    }

#ifdef TOOLS_ENABLED
    if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
        // Editor-only classes
        GDREGISTER_CLASS(ClaudeDock);
        GDREGISTER_CLASS(ClaudeEditorPlugin);
        EditorPlugins::add_by_type<ClaudeEditorPlugin>();
    }
#endif
}

void uninitialize_claude_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
    if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
        // Plugin is auto-removed by EditorPlugins system
    }
#endif
}
```

```cpp
// modules/claude/register_types.h

#ifndef CLAUDE_REGISTER_TYPES_H
#define CLAUDE_REGISTER_TYPES_H

#include "modules/register_module_types.h"

void initialize_claude_module(ModuleInitializationLevel p_level);
void uninitialize_claude_module(ModuleInitializationLevel p_level);

#endif
```

## Keyboard Shortcuts

```cpp
void ClaudeEditorPlugin::_register_shortcuts() {
    // Focus Claude dock: Ctrl+Shift+C
    Ref<Shortcut> focus_shortcut;
    focus_shortcut.instantiate();

    Ref<InputEventKey> key;
    key.instantiate();
    key->set_keycode(KEY_C);
    key->set_ctrl_pressed(true);
    key->set_shift_pressed(true);

    focus_shortcut->set_events({ key });

    dock->set_dock_shortcut(focus_shortcut);
}
```

## Theme Integration

```cpp
void ClaudeDock::_update_theme() {
    if (!is_inside_tree()) {
        return;
    }

    // Use editor theme icons
    send_btn->set_button_icon(get_editor_theme_icon("Play"));
    cancel_btn->set_button_icon(get_editor_theme_icon("Stop"));
    apply_all_btn->set_button_icon(get_editor_theme_icon("ImportCheck"));
    reject_btn->set_button_icon(get_editor_theme_icon("Remove"));

    // Chat display styling
    chat_display->add_theme_stylebox_override("normal",
        get_theme_stylebox("panel", "Tree"));
}
```
