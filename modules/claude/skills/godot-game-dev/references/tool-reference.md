# MCP Tool Reference

Complete API reference for all 42 tools exposed by the Claude MCP module.

## Conventions

### Response Format

All tools return JSON with a common structure:

**Success:**
```json
{ "success": true, "message": "Human-readable description", ...tool-specific fields... }
```

**Error:**
```json
{ "success": false, "error": "Human-readable error message" }
```

The MCP server wraps these into MCP content blocks:
```json
{ "content": [{ "type": "text", "text": "<JSON>" }] }
```

For `godot_capture_screenshot`, an additional `image` content block is prepended.

### Path Rules

- **Node paths** must start with `/root/` (e.g., `/root/Main/Player`).
- **Script paths** must start with `res://`, end with `.gd`, and cannot contain `..` or hidden segments (`/.`). Max 256 characters.
- **Resource types** must be in the allowed resource types list (see [resource-types.md](resource-types.md)).

### Runtime Tool Prerequisites

Tools prefixed with `godot_get_runtime_*`, `godot_capture_screenshot`, `godot_runtime_camera_control`, and `godot_get_runtime_camera_info` require an active game instance (started via `godot_run_scene`). They communicate with the running game through Godot's debugger protocol.

---

## Type Coercion

When setting properties via `godot_set_property` or `godot_add_node` (with the `properties` parameter), JSON values are automatically coerced to match the target Godot type.

### Dictionary to Godot Type

| Target Type | JSON Format | Keys | Defaults |
|---|---|---|---|
| `Vector2` | `{"x": 1.0, "y": 2.0}` | `x`, `y` | `0.0` |
| `Vector2i` | `{"x": 1, "y": 2}` | `x`, `y` | `0` |
| `Vector3` | `{"x": 1.0, "y": 2.0, "z": 3.0}` | `x`, `y`, `z` | `0.0` |
| `Vector3i` | `{"x": 1, "y": 2, "z": 3}` | `x`, `y`, `z` | `0` |
| `Vector4` | `{"x": 1.0, "y": 2.0, "z": 3.0, "w": 4.0}` | `x`, `y`, `z`, `w` | `0.0` |
| `Color` | `{"r": 1.0, "g": 0.5, "b": 0.0, "a": 1.0}` | `r`, `g`, `b`, `a` | `1.0` |
| `Rect2` | `{"x": 0, "y": 0, "width": 100, "height": 50}` | `x`, `y`, `width`, `height` | `0.0` |
| `Rect2i` | `{"x": 0, "y": 0, "width": 100, "height": 50}` | `x`, `y`, `width`, `height` | `0` |

### Resource Instantiation

| JSON Format | Behavior |
|---|---|
| `"BoxMesh"` (string) | Instantiate resource with default properties |
| `{"_type": "BoxMesh", "size": {"x": 2, "y": 2, "z": 2}}` | Instantiate resource and set properties (recursively coerced) |

Resource types must be in the allowed list. The `_type` key is required for dictionary-form instantiation.

### Numeric Conversions

`float` to `int` and `int` to `float` conversions are applied automatically.

### Passthrough

If no coercion rule matches, the value is passed through unchanged.

---

## Scene Tools

### godot_get_scene_tree

Get the current scene tree structure as JSON.

**Parameters:** None

**Returns:**

| Field | Type | Description |
|---|---|---|
| `scene` | object/null | Serialized scene hierarchy, or `null` if no scene open |
| `scene_path` | string | Scene file path (e.g., `res://main.tscn`) |

**Notes:** Read-only. Works even if no scene is open (returns `null` scene).

---

### godot_add_node

Add a new node to the scene tree.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `parent_path` | string | Yes | Path to parent node (e.g., `/root/Main`) |
| `node_type` | string | Yes | Godot node class name (e.g., `CharacterBody3D`) |
| `node_name` | string | Yes | Name for the new node |
| `properties` | object | No | Initial property values (type-coerced) |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `node_path` | string | Full path of the created node |

**Errors:**
- `"Node path is empty"` / `"Node path must start with /root/"`
- `"Unknown node type: <type>"` / `"Type is not a Node: <type>"`
- `"Parent node not found: <path>"`
- `"Failed to instantiate: <type>"`

**Notes:** Supports undo/redo. If `node_name` is empty, defaults to `node_type`. Properties are coerced to match existing property types.

---

### godot_remove_node

Remove a node from the scene tree.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `node_path` | string | Yes | Path to the node to remove |

**Errors:**
- `"Node not found: <path>"`
- `"Cannot remove scene root node"`
- `"Node has no parent"`

**Notes:** Supports undo/redo. Cannot remove the scene root node.

---

### godot_set_property

Set a property on a node.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `node_path` | string | Yes | Path to the node |
| `property` | string | Yes | Property name |
| `value` | any | Yes | New property value (JSON, type-coerced) |

**Errors:**
- `"Node not found: <path>"`
- `"Property name is empty"`

**Notes:** Supports undo/redo. The value is coerced to match the property's current type. For initially-null Object properties (e.g., `MeshInstance3D.mesh`), the property list is inspected to detect the expected type, enabling resource instantiation.

---

### godot_get_property

Get a property value from a node.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `node_path` | string | Yes | Path to the node |
| `property` | string | Yes | Property name |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `property` | string | The property name |
| `value` | any | The property value |
| `type` | string | Godot type name (e.g., `Vector3`, `float`, `String`) |

**Notes:** Read-only.

---

## Scene Persistence Tools

### godot_save_scene

Save the current scene to disk. Optionally specify a new path for "save as".

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `path` | string | No | Path for "save as" (e.g., `res://levels/level1.tscn`). If empty, saves to the current scene's existing path. |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `path` | string | The path the scene was saved to |

**Errors:**
- `"No scene is currently open"`
- `"Scene has no file path. Provide a 'path' parameter for the initial save."` — scene was never saved before and no path given
- Scene path validation errors (must be `res://`, `.tscn`/`.scn` extension, no `..`)

**Notes:** For new scenes that have never been saved, `path` is required. For existing scenes, omitting `path` saves to the current location.

---

### godot_new_scene

Create a new empty scene with a typed root node and make it the active edited scene.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `root_type` | string | Yes | Node type for the scene root (e.g., `Node3D`, `Node2D`, `Control`) |
| `root_name` | string | No | Name for the root node (defaults to `root_type`) |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `root_type` | string | The node type used |
| `root_name` | string | The root node name |

**Errors:**
- Node type validation errors (must exist in ClassDB, must be a Node subclass)
- `"Failed to instantiate: <type>"`

**Notes:** Creates a new editor tab with the scene. The scene is unsaved — use `godot_save_scene` with a path to persist it.

---

### godot_open_scene

Open an existing scene file in the editor.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `path` | string | Yes | Resource path to the scene file (e.g., `res://main.tscn`) |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `path` | string | The opened scene path |

**Errors:**
- Scene path validation errors
- `"Scene file not found: <path>"`

**Notes:** If the scene is already open, it switches to that tab. If there are unsaved changes in the current scene, Godot may prompt the user.

---

### godot_instance_scene

Instance a PackedScene as a child of a node in the current scene. This is Godot's composition/prefab pattern.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `scene_path` | string | Yes | Resource path to the scene to instance (e.g., `res://enemies/enemy.tscn`) |
| `parent_path` | string | Yes | Path to parent node in the current scene |
| `node_name` | string | No | Name for the instanced node (defaults to the scene's root node name) |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `node_path` | string | Full path of the instanced node |
| `scene_path` | string | The source scene path |

**Errors:**
- Scene path validation errors
- `"Scene file not found: <path>"`
- `"Parent node not found: <path>"`
- `"Failed to load scene: <path>"`
- `"Failed to instantiate scene: <path>"`

**Notes:** Supports undo/redo. The instanced scene appears as a single node in the scene tree (expandable). Internal nodes of the instance are not directly editable — modify the source scene instead, or make it "editable children" in the editor.

---

## Signal Tools

### godot_connect_signal

Connect a signal from one node to a method on another node.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `source_path` | string | Yes | Path to the node that emits the signal |
| `signal_name` | string | Yes | Name of the signal (e.g., `pressed`, `body_entered`) |
| `target_path` | string | Yes | Path to the node that receives the signal |
| `method_name` | string | Yes | Method to call on the target node |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `source` | string | Source node path |
| `signal` | string | Signal name |
| `target` | string | Target node path |
| `method` | string | Method name |

**Errors:**
- Node path validation errors (for both source and target)
- `"Source node not found: <path>"`
- `"Target node not found: <path>"`
- `"Signal '<name>' not found on <path>"`
- `"Signal '<name>' is already connected to <path>::<method>"`

**Notes:** Supports undo/redo. Signal connections are persisted in `.tscn` files when the scene is saved. The target method does not need to exist yet (it can be defined in a script attached later), but the signal must exist on the source node.

---

### godot_disconnect_signal

Disconnect a signal connection between two nodes.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `source_path` | string | Yes | Path to the node that emits the signal |
| `signal_name` | string | Yes | Name of the signal |
| `target_path` | string | Yes | Path to the node that receives the signal |
| `method_name` | string | Yes | Method name on the target |

**Errors:**
- Node path validation errors
- `"Source node not found: <path>"`
- `"Target node not found: <path>"`
- `"No connection from <source>::<signal> to <target>::<method>"`

**Notes:** Supports undo/redo.

---

## Project Settings Tool

### godot_project_settings

Get, set, or list project settings.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `get`, `set`, or `list` |
| `setting` | string | For get/set | Setting path (e.g., `display/window/size/viewport_width`) |
| `value` | any | For set | New value for the setting |
| `prefix` | string | For list | Filter prefix (e.g., `display/window`, `input/`). If empty, lists common settings. |

**Returns (vary by action):**

**get:** `setting`, `value`, `type`

**set:** `setting`, `value`, optional `note` (warning if restart may be required)

**list:** `settings` (array of `{name, type, value}`), `count`, `prefix`

**Errors:**
- `"Missing required 'action' parameter. Use: 'get', 'set', or 'list'"`
- `"Setting not found: <name>"` (for get)
- `"'set' action requires 'value' parameter"`
- `"Unknown action: <action>"`

**Notes:** Values are coerced to match existing setting types. Changes to rendering/window settings may require an editor restart.

---

## Script Tools

### godot_create_script

Create a new GDScript file.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `path` | string | Yes | Resource path (must start with `res://`) |
| `content` | string | Yes | Script content |
| `attach_to` | string | No | Node path to attach script to |

**Returns:** `path`, optional `attached_to`

**Errors:**
- `"Path must start with res://"`
- `"Path cannot contain parent traversal (..)"`
- `"Cannot create hidden files"`
- `"Script must have .gd extension"`
- `"Path too long"` (max 256 characters)
- `"Script already exists: <path>"`

**Notes:** Supports undo/redo. Creates parent directories automatically. Triggers filesystem rescan.

---

### godot_read_script

Read the content of a script file.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `path` | string | Yes | Resource path to the script |

**Returns:** `path`, `content`

**Errors:**
- `"Script not found: <path>"`
- `"Cannot read: <path>"`

---

### godot_modify_script

Modify an existing script file.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `path` | string | Yes | Resource path to the script |
| `content` | string | Yes | New script content |

**Errors:**
- `"Script not found: <path>"`

**Notes:** Supports undo/redo. Old content is preserved for undo. Triggers filesystem rescan.

---

### godot_validate_script

Validate a GDScript file and return compilation errors/warnings without running the game.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `path` | string | Yes | Resource path to the script (e.g., `res://scripts/player.gd`) |

**Returns:** `path`, `valid` (bool), `errors` (array of `{line, column, message, path}`), `warnings` (array of `{start_line, end_line, code, message}`). Optional `depended_errors` (dict keyed by file path) when dependency errors exist.

**Notes:** Read-only. Uses the same validation pipeline as the editor's Script Editor.

---

## Selection Tools

### godot_get_selected_nodes

Get the currently selected nodes in the editor.

**Parameters:** None

**Returns:** `selected` (array of node path strings), `count`

---

### godot_select_nodes

Select nodes in the editor.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `node_paths` | array | Yes | Array of node paths to select |

**Returns:** `count`

**Notes:** Clears current selection first. Silently skips unresolvable paths.

---

## Execution Tools

### godot_run_scene

Run the current scene or a specific scene.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `scene_path` | string | No | Scene path; uses current scene if empty |

---

### godot_stop_scene

Stop the running scene.

**Parameters:** None

---

## Runtime Tools

### godot_get_runtime_scene_tree

Get the scene tree from the currently running game instance.

**Parameters:** None

**Returns:** `running` (bool), `scene` (object/null)

Each node in the tree contains: `name`, `type`, `id` (int64), `scene_file_path`, `children` (array).

**Errors:**
- `"Debugger session not active"`
- `"Timeout waiting for runtime scene tree. Game may not be responding to debugger."` (2s timeout)

---

### godot_get_runtime_output

Get output/log messages from the running game.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `limit` | integer | No | Maximum messages to return (default 100) |
| `since_timestamp` | number | No | Only return messages after this Unix timestamp |

**Returns:** `running` (bool), `message_count` (int), `messages` (array with `type`, `text`, `timestamp`)

**Notes:** Messages are returned newest-first. Buffer holds up to 1000 messages. Buffer clears when a new game session starts.

---

### godot_get_runtime_errors

Get structured runtime errors/warnings from the running game with source locations and call stacks.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `limit` | integer | No | Maximum errors to return (default 50) |
| `since_timestamp` | number | No | Only return errors after this Unix timestamp |
| `severity` | string | No | Filter: `all` (default), `error`, or `warning` |
| `include_callstack` | boolean | No | Include call stack frames (default: true) |

**Returns:** `running` (bool), `error_count` (int), `errors` (array with `severity`, `timestamp`, `time`, `source_file`, `source_line`, `source_func`, `error`, `error_description`, and optional `callstack`)

**Notes:** Errors are returned newest-first. Shows the same data as the editor's Errors tab. The error buffer persists after the game stops.

---

### godot_capture_screenshot

Capture a screenshot from the running game viewport.

**Parameters:** None

**Returns:** `width`, `height`. Response includes an MCP `image` content block with PNG data (base64-encoded).

**Errors:**
- `"No game running"`
- `"Screenshot capture timed out"` (5s timeout)

---

### godot_runtime_camera_control

Control the debug camera in a running game.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `enable`, `disable`, `move`, `look_at`, or `reset` |
| `camera_type` | string | No | `3d` (default) or `2d` |
| `position` | object | No | `{x, y, z}` for 3D or `{x, y}` for 2D |
| `rotation_degrees` | object | No | Euler rotation `{x, y, z}` in degrees (3D only) |
| `target` | object | No | Look-at target `{x, y, z}` (3D `look_at` action) |
| `from` | object | No | Camera position for `look_at` `{x, y, z}` |
| `fov` | number | No | Field of view in degrees (3D, default 75) |
| `zoom` | number | No | Zoom level (2D, default 1.0) |

**Notes:**
- Camera override must be enabled before `move` or `look_at` will have visible effect.
- 3D rotation uses YXZ Euler order.
- 2D does **not** support `look_at`.

---

### godot_get_runtime_camera_info

Get current camera state from the running game.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `camera_type` | string | No | `3d` (default) or `2d` |

**Returns:** `camera_type`, `override_enabled` (bool), `note`

**Notes:** Informational only. Reports override state but does not return the camera transform.

---

## Input Tools

### godot_input_map

Manage input actions and bindings. Add/remove actions, bind keys/buttons/axes for player controls.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `list`, `add_action`, `remove_action`, `add_binding`, or `remove_binding` |
| `action_name` | string | For mutations | Input action name (e.g., `move_forward`) |
| `deadzone` | number | No | Deadzone (default: 0.5). Used with `add_action`. |
| `binding` | object | For bindings | `{type, key/button/axis}`. Types: `key`, `mouse_button`, `joypad_button`, `joypad_motion` |

**Returns (list):** `actions` (array of `{name, deadzone, bindings}`), `count`

**Returns (mutations):** `action_name`, `binding_type` (if applicable)

**Notes:** Skips built-in `ui_*` actions in list. Changes persist to `project.godot`. All mutations support undo/redo.

---

## Introspection Tools

### godot_get_class_info

Query ClassDB for a Godot class: properties, methods, signals, enums, and inheritance chain. Read-only.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `class_name` | string | Yes | Godot class name (e.g., `CharacterBody3D`) |
| `include_properties` | bool | No | Include property list (default: true) |
| `include_methods` | bool | No | Include method list (default: false) |
| `include_signals` | bool | No | Include signal list (default: true) |
| `include_enums` | bool | No | Include enum definitions (default: false) |
| `inherited` | bool | No | Include inherited members (default: false) |

**Returns:** `class_name`, `inheritance` (chain), `properties`, `methods`, `signals`, `enums` (depending on flags)

**Notes:** Does not require a scene to be open. Set `inherited=false` (default) to see only the class's own members.

### godot_get_node_info

Full inspector for a single node: all properties with current values, script info, children summary.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `node_path` | string | Yes | Path to the node (e.g., `/root/Main/Player`) |
| `include_properties` | bool | No | Include all properties with current values (default: true) |
| `include_methods` | bool | No | Include method list (default: false) |
| `include_signals` | bool | No | Include signals and connections (default: false) |

**Returns:** `node_path`, `class`, `name`, `script_path`, `children` (array), `child_count`, `properties` (with values)

**Notes:** Unlike `godot_get_property` (one property), this returns all editor-visible properties at once. Object-type properties are serialized as `{class, path}`.

---

## Batch Operations

### godot_set_properties_batch

Set multiple properties across nodes in one call with a single undo action. Ctrl+Z reverts all changes together.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `operations` | array | Yes | Array of `{node_path, property, value}` objects |

**Returns:** `results` (per-operation), `succeeded`, `failed`

**Notes:** All successful operations share a single undo action. Failed operations are reported individually in results but don't block successful ones. Type coercion works the same as `godot_set_property`.

---

## Resource Tools

### godot_project_files

List project files with resource type metadata, trigger a filesystem rescan, or run import diagnostics.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `list` (browse files), `scan` (trigger filesystem rescan), or `diagnostics` (scan for invalid imports) |
| `path` | string | No | Directory path for `list` (default: `res://`) |
| `recursive` | bool | No | List files recursively (default: false) |
| `extensions` | array | No | Filter by extensions, e.g., `["tscn", "gd"]` |

**Returns (list):** `path`, `files` (array of `{path, type}`), `file_count`, `subdirectories`

**Returns (diagnostics):** `invalid_imports` (array), `total_files` (int), `invalid_count` (int)

**Notes:** Use `scan` after creating files externally to make them visible in the editor. The `diagnostics` action scans the entire project for broken import references.

---

## 3D Tools

### godot_bake_navigation

Trigger navigation mesh baking on a NavigationRegion3D. Required for AI pathfinding.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `node_path` | string | Yes | Path to a NavigationRegion3D node |

**Returns:** `node_path`

**Notes:** The node must have a NavigationMesh resource assigned. Baking is synchronous. Save the scene to persist the baked mesh.

---

## Animation Tools

### godot_create_animation

Create an animation with tracks and keyframes on an AnimationPlayer/AnimationMixer.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `node_path` | string | Yes | Path to an AnimationPlayer/AnimationMixer node |
| `animation_name` | string | Yes | Name for the animation (e.g., `idle`, `walk`) |
| `length` | number | Yes | Length in seconds |
| `library_name` | string | No | Library name (default: `""` for default library) |
| `loop_mode` | string | No | `none` (default), `linear`, or `ping_pong` |
| `tracks` | array | No | Track definitions: `{type, path, keys}` |

**Track types:** `value`, `position_3d`, `rotation_3d`, `scale_3d`, `blend_shape`, `method`, `bezier`

**Returns:** `animation_name`, `library`, `length`, `loop_mode`, `tracks_added`

**Notes:** Supports undo/redo. Creates library automatically if needed. Rotation values without `w` are interpreted as Euler degrees.

### godot_get_animation_info

Inspect animations: library list, animation details, track summaries, state machine info.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `node_path` | string | Yes | Path to an AnimationPlayer/AnimationMixer/AnimationTree |
| `animation_name` | string | No | Get detailed track/keyframe info for a specific animation |

**Returns:** `node_path`, `class`, `libraries` (with animations). For AnimationTree: `tree_root_type`, `state_machine` info.

**Notes:** Without `animation_name`, returns summary of all animations. With it, includes full track and keyframe data.

---

## Editor Tools

### godot_get_editor_log

Get editor-level log messages from the Output panel (startup messages, tool script output, `ERR_PRINT`/`WARN_PRINT` from editor context).

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `limit` | integer | No | Maximum messages to return (default 100) |
| `types` | array | No | Filter by types: `std`, `error`, `warning`, `editor`, `std_rich`. Returns all if empty. |

**Returns:** `message_count` (int), `messages` (array with `type`, `text`, `count`)

**Notes:** Read-only. Messages are returned newest-first. Reads from the editor's Output panel, not the running game.

---

### godot_editor_screenshot

Capture a screenshot from the editor viewport (3D or 2D) or the running game.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `source` | string | No | `3d` (default), `2d`, or `game` |
| `viewport_idx` | integer | No | 3D viewport index 0-3 (default 0) |
| `scale` | number | No | Downscale factor 0.1-1.0 (default 0.5) |

**Returns:** `width`, `height`, `source`, plus an MCP `image` content block (base64 PNG).

---

### godot_editor_viewport_camera

Control the 3D editor viewport camera.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `move`, `orbit`, `look_at`, `focus`, `set_view`, or `get_state` |
| `viewport_idx` | integer | No | 3D viewport index 0-3 (default 0) |
| `position` | object | No | Focal point `{x, y, z}` (for `move`) |
| `distance` | number | No | Distance from focal point |
| `x_rotation` | number | No | Pitch in radians |
| `y_rotation` | number | No | Yaw in radians |
| `target` | object | No | Look-at target `{x, y, z}` |
| `from` | object | No | Camera position `{x, y, z}` (for `look_at`) |
| `view` | string | No | Preset: `front`, `back`, `left`, `right`, `top`, `bottom` |
| `orthogonal` | boolean | No | Toggle orthogonal projection |
| `fov_scale` | number | No | FOV scale 0.1-2.5 |

**Notes:** `focus` centers on editor selection. `get_state` returns current camera state.

---

### godot_editor_control

Control editor workspace: switch panels, set 3D display mode, toggle grid.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `switch_panel`, `set_display_mode`, `toggle_grid`, or `get_state` |
| `panel` | string | No | `2D`, `3D`, `Script`, or `AssetLib` |
| `display_mode` | string | No | `normal`, `wireframe`, `overdraw`, `lighting`, or `unshaded` |
| `viewport_idx` | integer | No | Which 3D viewport (default 0) |
| `grid` | boolean | No | Grid visibility |

---

### godot_canvas_view

Control the 2D canvas editor: pan, zoom, center, snap.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `get_state`, `center_at`, `zoom`, `pan`, `focus`, or `set_snap` |
| `position` | object | No | World position `{x, y}` |
| `zoom` | number | No | Zoom level |
| `grid_snap` | boolean | No | Enable grid snapping |
| `smart_snap` | boolean | No | Enable smart snapping |
| `grid_step` | object | No | Grid step `{x, y}` |

---

### godot_editor_state

Get comprehensive editor state in one call.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `include_3d` | boolean | No | Include 3D viewport state (default true) |
| `include_2d` | boolean | No | Include 2D canvas state (default true) |

**Returns:** `active_panel`, `open_scenes`, `current_scene`, `selected_nodes`, `viewport_3d`, `canvas_2d`

**Notes:** Read-only. Combines multiple queries into one efficient call.

---

## Transform Tools

### godot_transform_nodes

Apply relative transforms to multiple nodes with undo/redo.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `translate`, `rotate`, `scale`, or `set_transform` |
| `node_paths` | array | Yes | Node paths to transform |
| `value` | object | No | Delta vector `{x, y, z}` |
| `local` | boolean | No | Local space (default false) |
| `transform` | object | No | Full transform `{origin, rotation, scale}` (for `set_transform`) |

**Notes:** For `scale`, missing components default to 1.0. For `translate`/`rotate`, missing components default to 0.0.

---

## Scene Operation Tools

### godot_scene_operations

Hierarchy operations: duplicate, reparent, visibility, lock, groups. All with undo/redo.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `duplicate`, `reparent`, `set_visible`, `toggle_lock`, or `group` |
| `node_paths` | array | Yes | Target node paths |
| `new_parent` | string | No | New parent path (for `reparent`) |
| `visible` | boolean | No | Visibility (for `set_visible`) |
| `group_name` | string | No | Group name (for `group` — toggles membership) |
| `offset` | object | No | Position offset `{x,y,z}` for duplicates |

**Notes:** `toggle_lock` toggles `_edit_lock_` meta. `group` toggles group membership.
