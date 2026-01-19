# Claude Module Architecture

## Overview

The Claude module follows Godot's architectural patterns and integrates with existing editor systems. It is implemented as an optional module that can be enabled/disabled at build time.

## Component Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           Godot Editor                                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌──────────────────┐    ┌──────────────────┐    ┌──────────────────┐   │
│  │   EditorNode     │    │ EditorDockManager│    │  EditorInterface │   │
│  │   (Singleton)    │◄───│   (Singleton)    │    │   (Singleton)    │   │
│  └────────┬─────────┘    └────────┬─────────┘    └────────┬─────────┘   │
│           │                       │                       │              │
│           │                       │                       │              │
│  ┌────────▼─────────────────────────────────────────────────────────┐   │
│  │                    Claude Module                                  │   │
│  │                                                                   │   │
│  │  ┌─────────────────────────────────────────────────────────────┐ │   │
│  │  │                  ClaudeEditorPlugin                         │ │   │
│  │  │  - Lifecycle management                                     │ │   │
│  │  │  - Editor settings registration                             │ │   │
│  │  │  - Notification handling                                    │ │   │
│  │  └───────────────────────┬─────────────────────────────────────┘ │   │
│  │                          │                                       │   │
│  │  ┌───────────────────────▼─────────────────────────────────────┐ │   │
│  │  │                     ClaudeDock                              │ │   │
│  │  │  - Chat UI                                                  │ │   │
│  │  │  - Action preview panel                                     │ │   │
│  │  │  - Context options                                          │ │   │
│  │  └───────────────────────┬─────────────────────────────────────┘ │   │
│  │                          │                                       │   │
│  │         ┌────────────────┼────────────────┐                      │   │
│  │         │                │                │                      │   │
│  │         ▼                ▼                ▼                      │   │
│  │  ┌────────────┐  ┌──────────────┐  ┌─────────────────┐          │   │
│  │  │ClaudeClient│  │ClaudeScene   │  │ClaudeAction     │          │   │
│  │  │            │  │Serializer    │  │Executor         │          │   │
│  │  │- HTTP API  │  │              │  │                 │          │   │
│  │  │- Streaming │  │- Scene→JSON  │  │- Undo/Redo     │          │   │
│  │  │- Auth      │  │- Selection   │  │- Validation    │          │   │
│  │  └────────────┘  └──────────────┘  └─────────────────┘          │   │
│  │                                            │                     │   │
│  │                                            ▼                     │   │
│  │                                   ┌─────────────────┐            │   │
│  │                                   │ClaudeAction     │            │   │
│  │                                   │Parser           │            │   │
│  │                                   │                 │            │   │
│  │                                   │- JSON parsing   │            │   │
│  │                                   │- Action creation│            │   │
│  │                                   └─────────────────┘            │   │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ┌──────────────────┐    ┌──────────────────┐    ┌──────────────────┐   │
│  │EditorUndoRedo    │    │  EditorSelection │    │ EditorFileSystem │   │
│  │Manager           │    │                  │    │                  │   │
│  └──────────────────┘    └──────────────────┘    └──────────────────┘   │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

## Layer Responsibilities

### Editor Layer

| Component | Responsibility |
|-----------|----------------|
| `ClaudeEditorPlugin` | Plugin lifecycle, settings registration, notification routing |
| `ClaudeDock` | All UI elements: chat display, input, action preview, context options |

### API Layer

| Component | Responsibility |
|-----------|----------------|
| `ClaudeClient` | HTTP communication, streaming, authentication, rate limiting |
| `ClaudeSceneSerializer` | Convert scene tree to JSON for Claude's context |
| `ClaudePromptBuilder` | Construct system prompts with context |

### Action Layer

| Component | Responsibility |
|-----------|----------------|
| `ClaudeAction` | Data structure representing a single operation |
| `ClaudeActionParser` | Parse Claude's response into action objects |
| `ClaudeActionExecutor` | Execute actions with undo/redo integration |

## Data Flow

### Request Flow

```
User Input (Dock)
       │
       ▼
ClaudeDock::_on_send_pressed()
       │
       ├──► ClaudeSceneSerializer::serialize_scene()
       │           │
       │           ▼
       │    Scene JSON Context
       │           │
       ├───────────┘
       │
       ▼
ClaudePromptBuilder::build_system_prompt()
       │
       ▼
ClaudeClient::send_message_streaming()
       │
       ▼
HTTP POST to api.anthropic.com
```

### Response Flow

```
HTTP Response (streaming chunks)
       │
       ▼
ClaudeClient::_on_request_completed()
       │
       ├──► emit_signal("response_chunk")  ──► ClaudeDock (display)
       │
       ▼
ClaudeActionParser::parse_response()
       │
       ▼
TypedArray<ClaudeAction>
       │
       ▼
ClaudeDock::_display_pending_actions()
       │
       ▼
User clicks "Apply"
       │
       ▼
ClaudeActionExecutor::execute_action_batch()
       │
       ▼
EditorUndoRedoManager (undo/redo stack)
```

## Integration Points

### EditorInterface Usage

```cpp
// Get current scene
Node *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();

// Get selected nodes
EditorSelection *selection = EditorInterface::get_singleton()->get_selection();
TypedArray<Node> selected = selection->get_selected_nodes();

// Get undo/redo manager
EditorUndoRedoManager *undo_redo = EditorInterface::get_singleton()->get_editor_undo_redo();
```

### EditorPlugin Notifications

The plugin receives these notifications:

| Method | When Called |
|--------|-------------|
| `notify_scene_changed()` | Scene tree structure changes |
| `notify_scene_closed()` | A scene is closed |
| `notify_resource_saved()` | Any resource is saved |
| `notify_main_screen_changed()` | Switching between 2D/3D/Script |

### EditorDock Integration

The dock integrates with:

| System | Integration |
|--------|-------------|
| Dock Manager | Registered via `EditorDockManager::add_dock()` |
| Layout Persistence | Saves/loads position via `save_layout_to_config()` |
| Shortcuts | Keyboard shortcut to focus dock |

## Class Inheritance

```
Object
├── RefCounted
│   ├── ClaudeClient
│   ├── ClaudeSceneSerializer
│   ├── ClaudePromptBuilder
│   ├── ClaudeAction
│   ├── ClaudeActionParser
│   └── ClaudeActionExecutor
│
├── Node
│   └── EditorPlugin
│       └── ClaudeEditorPlugin
│
└── Control
    └── MarginContainer
        └── EditorDock
            └── ClaudeDock
```

## Threading Model

```
┌─────────────────────────────────────────────────────────────────┐
│                         Main Thread                              │
│                                                                  │
│  ClaudeDock ──► ClaudeClient ──► HTTPRequest (use_threads=true) │
│      ▲                                │                          │
│      │                                ▼                          │
│      │                        ┌───────────────┐                  │
│      │                        │ Worker Thread │                  │
│      │                        │               │                  │
│      │                        │ HTTP I/O      │                  │
│      │                        └───────┬───────┘                  │
│      │                                │                          │
│      │         call_deferred()        │                          │
│      └────────────────────────────────┘                          │
│                                                                  │
│  All UI updates and scene modifications on main thread           │
└─────────────────────────────────────────────────────────────────┘
```

## Memory Management

| Object Type | Ownership |
|-------------|-----------|
| `ClaudeDock` | Owned by EditorDockManager |
| `ClaudeEditorPlugin` | Owned by EditorNode plugin system |
| `ClaudeClient` | RefCounted, held by ClaudeDock |
| `ClaudeAction` | RefCounted, temporary during execution |
| `HTTPRequest` | Node child of ClaudeClient, auto-freed |
