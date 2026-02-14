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
- **Resource types** must be in the allowed resource types list (see below).

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

## Allowed Resource Types

Only these types can be instantiated via type coercion. Organized by category:

### Primitive Meshes

`BoxMesh`, `SphereMesh`, `CylinderMesh`, `CapsuleMesh`, `PlaneMesh`, `PrismMesh`, `TorusMesh`, `PointMesh`, `QuadMesh`, `TextMesh`

### Materials

`StandardMaterial3D`, `ORMMaterial3D`, `ShaderMaterial`

### Shapes (3D)

`BoxShape3D`, `SphereShape3D`, `CapsuleShape3D`, `CylinderShape3D`, `ConvexPolygonShape3D`, `ConcavePolygonShape3D`, `WorldBoundaryShape3D`, `HeightMapShape3D`, `SeparationRayShape3D`

### Shapes (2D)

`RectangleShape2D`, `CircleShape2D`, `CapsuleShape2D`, `ConvexPolygonShape2D`, `ConcavePolygonShape2D`, `SegmentShape2D`, `SeparationRayShape2D`, `WorldBoundaryShape2D`

### Animation

`Animation`, `AnimationLibrary`, `AnimationNodeStateMachine`, `AnimationNodeAnimation`, `AnimationNodeBlendTree`, `AnimationNodeBlendSpace1D`, `AnimationNodeBlendSpace2D`

### Navigation

`NavigationMesh`

### Noise

`FastNoiseLite`

### Shaders

`Shader`, `ShaderInclude`

### Theme

`Theme`

### Input Events

`InputEventKey`, `InputEventMouseButton`, `InputEventJoypadButton`, `InputEventJoypadMotion`

### Styles and UI

`StyleBoxFlat`, `StyleBoxLine`, `StyleBoxEmpty`, `LabelSettings`, `FontVariation`

### Environment and Sky

`Environment`, `Sky`, `ProceduralSkyMaterial`, `PanoramaSkyMaterial`, `PhysicalSkyMaterial`

### Other

`Gradient`, `Curve`, `Curve2D`, `Curve3D`, `PhysicsMaterial`

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

Connect a signal from one node to a method on another node. The primary Godot event/messaging pattern.

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
- `"Signal '<name>' not found on <path>"` — signal doesn't exist on the source node
- `"Signal '<name>' is already connected to <path>::<method>"` — duplicate connection

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
- `"No connection from <source>::<signal> to <target>::<method>"` — connection doesn't exist

**Notes:** Supports undo/redo.

---

## Project Settings Tool

### godot_project_settings

Get, set, or list project settings (window size, physics, input map, rendering, etc.).

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `get`, `set`, or `list` |
| `setting` | string | For get/set | Setting path (e.g., `display/window/size/viewport_width`) |
| `value` | any | For set | New value for the setting |
| `prefix` | string | For list | Filter prefix (e.g., `display/window`, `input/`). If empty, lists common settings. |

**Returns (vary by action):**

**get:**

| Field | Type | Description |
|---|---|---|
| `setting` | string | Setting path |
| `value` | any | Current value |
| `type` | string | Godot type name |

**set:**

| Field | Type | Description |
|---|---|---|
| `setting` | string | Setting path |
| `value` | any | New value |
| `note` | string | (Optional) Warning if restart may be required |

**list:**

| Field | Type | Description |
|---|---|---|
| `settings` | array | Array of `{name, type, value}` objects |
| `count` | int | Number of settings returned |
| `prefix` | string | The filter used |

**Errors:**
- `"Missing required 'action' parameter. Use: 'get', 'set', or 'list'"`
- `"Setting not found: <name>"` (for get)
- `"'set' action requires 'value' parameter"`
- `"Unknown action: <action>"`

**Notes:** Values are coerced to match existing setting types. Changes to rendering/window settings may require an editor restart. Settings are saved to `project.godot` immediately. Common settings categories when listing without a prefix: `application/config/`, `display/window/size/`, `physics/2d/`, `physics/3d/`, `rendering/renderer/`, `rendering/environment/`, `input/`.

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

**Returns:**

| Field | Type | Description |
|---|---|---|
| `path` | string | The script path |
| `attached_to` | string | Node path (only if `attach_to` was specified) |

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

**Returns:**

| Field | Type | Description |
|---|---|---|
| `path` | string | The script path |
| `content` | string | Full text content of the script |

**Errors:**
- `"Script not found: <path>"`
- `"Cannot read: <path>"`
- Same path validation errors as `godot_create_script`

**Notes:** Read-only.

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
- Same path validation errors as `godot_create_script`

**Notes:** Supports undo/redo. Old content is preserved for undo. Triggers filesystem rescan.

---

### godot_validate_script

Validate a GDScript file and return compilation errors/warnings without running the game.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `path` | string | Yes | Resource path to the script (e.g., `res://scripts/player.gd`) |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `path` | string | The script path |
| `valid` | bool | Whether the script compiled without errors |
| `errors` | array | Errors in this script: `{line, column, message, path}` |
| `warnings` | array | Warnings: `{start_line, end_line, code, message}` |
| `depended_errors` | dict | (Optional) Errors in dependencies, keyed by file path |

**Errors:**
- Same path validation errors as `godot_create_script`
- `"Script not found: <path>"`
- `"Cannot read: <path>"`
- `"No script language found for extension: <ext>"`

**Notes:** Read-only. Uses the same validation pipeline as the editor's Script Editor. The `depended_errors` field only appears when errors exist in files that the validated script depends on (e.g., a parent class or autoloaded script).

---

## Selection Tools

### godot_get_selected_nodes

Get the currently selected nodes in the editor.

**Parameters:** None

**Returns:**

| Field | Type | Description |
|---|---|---|
| `selected` | array | Array of node path strings |
| `count` | int | Number of selected nodes |

**Notes:** Read-only. Returns empty array if nothing is selected.

---

### godot_select_nodes

Select nodes in the editor.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `node_paths` | array | Yes | Array of node paths to select |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `count` | int | Number of actually selected nodes |

**Notes:** Clears current selection first. Silently skips unresolvable paths.

---

## Execution Tools

### godot_run_scene

Run the current scene or a specific scene.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `scene_path` | string | No | Scene path; uses current scene if empty |

**Notes:** If `scene_path` is empty, runs the current scene. Otherwise runs the specified scene.

---

### godot_stop_scene

Stop the running scene.

**Parameters:** None

---

## Runtime Tools

### godot_get_runtime_scene_tree

Get the scene tree from the currently running game instance.

**Parameters:** None

**Returns:**

| Field | Type | Description |
|---|---|---|
| `running` | bool | Whether a game is running |
| `scene` | object/null | Hierarchical scene tree, or `null` if no game running |

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

**Returns:**

| Field | Type | Description |
|---|---|---|
| `running` | bool | Whether a game is running |
| `message_count` | int | Number of returned messages |
| `messages` | array | Messages, each with `type` (`log`/`warning`/`error`), `text`, and `timestamp` |

**Notes:** Messages are returned newest-first. Buffer holds up to 1000 messages; oldest are trimmed. Buffer clears when a new game session starts.

---

### godot_get_runtime_errors

Get structured runtime errors/warnings from the running game with source locations and call stacks. Shows the same data as the editor's Errors tab.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `limit` | integer | No | Maximum number of errors to return (default 50) |
| `since_timestamp` | number | No | Only return errors after this Unix timestamp |
| `severity` | string | No | Filter by severity: `all` (default), `error`, or `warning` |
| `include_callstack` | boolean | No | Include call stack frames (default: true) |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `running` | bool | Whether a game is running |
| `error_count` | int | Number of returned errors |
| `errors` | array | Error objects (see below) |

**Error object fields:**

| Field | Type | Description |
|---|---|---|
| `severity` | string | `error` or `warning` |
| `timestamp` | number | Unix timestamp |
| `time` | string | Human-readable time string |
| `source_file` | string | Source file path |
| `source_line` | int | Line number in source file |
| `source_func` | string | Function name |
| `error` | string | Error type/category |
| `error_description` | string | Detailed error message |
| `callstack` | array | (If `include_callstack` is true) Stack frames: `{file, function, line}` |

**Notes:** Errors are returned newest-first. Unlike `godot_get_runtime_output` which captures print/log messages, this tool captures structured errors from Godot's debugger protocol — the same data shown in the editor's Errors tab. The error buffer persists even after the game stops, so you can retrieve errors from the last run.

---

### godot_capture_screenshot

Capture a screenshot from the running game viewport.

**Parameters:** None

**Returns:**

| Field | Type | Description |
|---|---|---|
| `width` | int | Pixel width |
| `height` | int | Pixel height |

The response includes an MCP `image` content block with the PNG data (base64-encoded).

**Errors:**
- `"No game running"`
- `"GameViewDebugger not available"`
- `"Failed to register screenshot callback. Is the game running?"`
- `"Screenshot capture timed out"` (5s timeout)

---

### godot_runtime_camera_control

Control the debug camera in a running game. Enable camera override, move, look at targets, or reset.

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

**Returns (vary by action):**
- **enable/disable/reset:** Confirmation message
- **move (3D):** `position`, `rotation_degrees`, `fov`
- **move (2D):** `offset`, `zoom`
- **look_at (3D):** `position`, `target`, `rotation_degrees`, `fov`

**Errors:**
- `"No game running. Start a scene first with godot_run_scene."`
- `"Missing required 'action' parameter. Use: enable, disable, move, look_at, or reset"`
- `"Invalid camera_type: <type>. Use '3d' or '2d'"`
- `"Debugger session not active"`
- `"'look_at' action requires 'target' parameter"`
- `"Unknown 3D camera action: <action>"`
- `"Unknown 2D camera action: <action>. 2D cameras support: enable, disable, reset, move"`

**Notes:**
- 3D rotation uses YXZ Euler order; degrees are converted to radians internally.
- 3D `look_at` uses up vector `(0, 1, 0)` and returns the computed rotation.
- 2D does **not** support `look_at`.
- Camera override must be enabled before `move` or `look_at` will have visible effect.

---

### godot_get_runtime_camera_info

Get current camera state from the running game.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `camera_type` | string | No | `3d` (default) or `2d` |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `camera_type` | string | `3d` or `2d` |
| `override_enabled` | bool | Whether camera override is active |
| `note` | string | Human-readable description of the override state |

**Errors:**
- `"No game running. Start a scene first with godot_run_scene."`
- `"Debugger session not active"`

**Notes:** Informational only. Reports override state but does not return the camera transform.

---

## Input Tools

### godot_input_map

Manage input actions and bindings. Add/remove actions, bind keys/buttons/axes for player controls. All mutations support undo/redo.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `list`, `add_action`, `remove_action`, `add_binding`, or `remove_binding` |
| `action_name` | string | No | Input action name (e.g., `move_forward`). Required for all actions except `list`. |
| `deadzone` | number | No | Deadzone for the action (default: 0.5). Used with `add_action`. |
| `binding` | object | No | Binding definition. Required for `add_binding`/`remove_binding`. |

**Binding object fields:**

| Field | Type | Description |
|---|---|---|
| `type` | string | `key`, `mouse_button`, `joypad_button`, or `joypad_motion` |
| `key` | string | Key name for `key` type (e.g., `W`, `Space`, `Escape`) |
| `button` | int | Button index for `mouse_button` (1=left, 2=right, 3=middle) or `joypad_button` (0=A, 1=B, 2=X, 3=Y) |
| `axis` | int | Axis index for `joypad_motion` (0=LeftX, 1=LeftY, 2=RightX, 3=RightY) |
| `axis_value` | float | Axis direction for `joypad_motion` (-1.0 or 1.0) |

**Returns (list):**

| Field | Type | Description |
|---|---|---|
| `actions` | array | Array of action objects with `name`, `deadzone`, and `bindings` |
| `count` | int | Number of actions listed |

**Errors:**
- `"Action already exists: <name>"` — duplicate `add_action`
- `"Action not found: <name>"` — unknown action name
- `"Unknown key: <name>"` — invalid key name for binding
- `"No matching binding found"` — `remove_binding` couldn't find a match

**Notes:** The `list` action skips built-in `ui_*` actions. Changes are persisted to `project.godot` immediately. All mutations (add/remove action, add/remove binding) are undoable via Ctrl+Z.

---

## Introspection Tools

### godot_get_class_info

Query ClassDB for a Godot class: properties, methods, signals, enums, and inheritance chain. Read-only.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `class_name` | string | Yes | Godot class name (e.g., `CharacterBody3D`, `GPUParticles3D`) |
| `include_properties` | bool | No | Include property list (default: true) |
| `include_methods` | bool | No | Include method list (default: false) |
| `include_signals` | bool | No | Include signal list (default: true) |
| `include_enums` | bool | No | Include enum definitions (default: false) |
| `inherited` | bool | No | Include inherited members (default: false) |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `class_name` | string | The queried class name |
| `inheritance` | array | Inheritance chain from class to Object |
| `properties` | array | Property list (if requested): `{name, type, hint}` |
| `methods` | array | Method list (if requested): `{name, return_type, arguments}` |
| `signals` | array | Signal list (if requested): `{name, arguments}` |
| `enums` | dict | Enum definitions (if requested): `{enum_name: {constant: value}}` |

**Errors:**
- `"Unknown class: <name>"` — class not registered in ClassDB

**Notes:** Read-only introspection. Does not require a scene to be open. Useful for discovering available properties before using `godot_set_property`. Set `inherited=false` (default) to see only the class's own members.

### godot_get_node_info

Full inspector for a single node: all properties with current values, script info, signals, and children summary. Returns everything in one call.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `node_path` | string | Yes | Path to the node (e.g., `/root/Main/Player`) |
| `include_properties` | bool | No | Include all properties with current values (default: true) |
| `include_methods` | bool | No | Include method list (default: false) |
| `include_signals` | bool | No | Include signals and connections (default: false) |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `node_path` | string | Resolved node path |
| `class` | string | Node class name |
| `name` | string | Node name |
| `script_path` | string | Attached script path (if any) |
| `children` | array | Child nodes: `{name, class}` |
| `child_count` | int | Number of children |
| `properties` | array | Properties with values: `{name, type, value, category}` |
| `methods` | array | Methods (if requested) |
| `signals` | array | Signals with connections (if requested) |

**Errors:**
- `"Node not found: <path>"` — invalid node path
- Standard node path validation errors

**Notes:** Unlike `godot_get_property` (which reads one property), this returns all editor-visible properties at once. Object-type properties are serialized as `{class, path}` or a string representation.

---

## Batch Operations

### godot_set_properties_batch

Set multiple properties across nodes in one call with a single undo action. Ctrl+Z reverts all changes together.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `operations` | array | Yes | Array of operation objects |

**Operation object fields:**

| Field | Type | Description |
|---|---|---|
| `node_path` | string | Path to target node |
| `property` | string | Property name to set |
| `value` | any | Value to set (supports type coercion) |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `results` | array | Per-operation results: `{node_path, property, success, error}` |
| `succeeded` | int | Number of successful operations |
| `failed` | int | Number of failed operations |

**Errors:**
- `"No operations provided"` — empty operations array
- Individual operation errors are reported in the `results` array, not as top-level errors

**Notes:** All successful operations are grouped into a single undo action. If some operations fail (e.g., node not found), the successful ones still apply. Type coercion works the same as `godot_set_property`.

---

## Resource Tools

### godot_project_files

List project files with resource type metadata, trigger a filesystem rescan after external file changes, or run import diagnostics.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `list` (browse files), `scan` (trigger filesystem rescan), or `diagnostics` (scan for invalid imports) |
| `path` | string | No | Directory path for `list` (default: `res://`) |
| `recursive` | bool | No | List files recursively (default: false) |
| `extensions` | array | No | Filter by file extensions, e.g., `["tscn", "gd", "tres"]` |

**Returns (list):**

| Field | Type | Description |
|---|---|---|
| `path` | string | Directory path listed |
| `files` | array | File objects: `{path, type}` |
| `file_count` | int | Number of files returned |
| `subdirectories` | array | Immediate subdirectory paths |

**Returns (scan):** Success message confirming rescan was triggered.

**Returns (diagnostics):**

| Field | Type | Description |
|---|---|---|
| `invalid_imports` | array | Files with invalid imports |
| `total_files` | int | Total files scanned |
| `invalid_count` | int | Number of files with invalid imports |

**Errors:**
- `"Directory not found: <path>"` — path doesn't exist in project
- `"Editor filesystem not ready"` — filesystem not yet initialized
- `"Unknown action: <action>. Use: 'list', 'scan', or 'diagnostics'"`
- Standard resource path validation errors

**Notes:** The `list` action always includes immediate subdirectories for navigation, even when filtering by extension. Use `scan` after creating files externally (e.g., via Claude Code's file tools) to make them visible in the editor. The `diagnostics` action scans the entire project for files with broken import references.

---

## 3D Tools

### godot_bake_navigation

Trigger navigation mesh baking on a NavigationRegion3D. Required for AI pathfinding to work.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `node_path` | string | Yes | Path to a NavigationRegion3D node |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `node_path` | string | Path to the baked node |

**Errors:**
- `"Node is not a NavigationRegion3D: <path>"` — wrong node type
- `"NavigationRegion3D has no NavigationMesh resource"` — set a NavigationMesh first
- `"Navigation mesh is already being baked"` — bake already in progress

**Notes:** The node must have a NavigationMesh resource assigned before baking. Baking is synchronous (blocks until complete). The baked mesh is stored in-memory; save the scene to persist it.

---

## Animation Tools

### godot_create_animation

Create an animation with tracks and keyframes on an AnimationPlayer or AnimationMixer. Supports position/rotation/scale/value/method/blend_shape tracks.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `node_path` | string | Yes | Path to an AnimationPlayer or AnimationMixer node |
| `animation_name` | string | Yes | Name for the new animation (e.g., `idle`, `walk`) |
| `length` | number | Yes | Animation length in seconds |
| `library_name` | string | No | Animation library name (default: `""` for the default library) |
| `loop_mode` | string | No | `none` (default), `linear`, or `ping_pong` |
| `tracks` | array | No | Array of track definitions |

**Track definition fields:**

| Field | Type | Description |
|---|---|---|
| `type` | string | `value`, `position_3d`, `rotation_3d`, `scale_3d`, `blend_shape`, `method`, or `bezier` |
| `path` | string | Node path relative to the AnimationMixer (e.g., `MeshInstance3D:position`) |
| `keys` | array | Keyframe array: `{time, value}` (format depends on track type) |

**Keyframe value formats by track type:**

| Track Type | Value Format |
|---|---|
| `position_3d`, `scale_3d` | `{"x": 0, "y": 0, "z": 0}` |
| `rotation_3d` | `{"x": 0, "y": 90, "z": 0}` (degrees) or `{"x": 0, "y": 0, "z": 0, "w": 1}` (quaternion) |
| `blend_shape` | float (0.0 to 1.0) |
| `method` | `{"method": "func_name", "args": [...]}` |
| `value` | Any Variant (auto-coerced) |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `animation_name` | string | Name of created animation |
| `library` | string | Library name |
| `length` | number | Animation length |
| `loop_mode` | string | Loop mode |
| `tracks_added` | int | Number of tracks successfully added |

**Errors:**
- `"Node is not an AnimationPlayer or AnimationMixer"` — wrong node type
- `"Animation '<name>' already exists in library '<lib>'"` — duplicate name
- `"Unknown track type: <type>"` — invalid track type string

**Notes:** Supports undo/redo. If the specified library doesn't exist, it will be created automatically. Rotation values without a `w` component are interpreted as Euler degrees and converted to quaternions internally.

### godot_get_animation_info

Inspect animations on an AnimationPlayer, AnimationMixer, or AnimationTree: library list, animation details, track summaries, and state machine info.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `node_path` | string | Yes | Path to an AnimationPlayer, AnimationMixer, or AnimationTree node |
| `animation_name` | string | No | Get detailed track/keyframe info for a specific animation |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `node_path` | string | Resolved node path |
| `class` | string | Node class name |
| `libraries` | array | Animation libraries with their animations |
| `tree_root_type` | string | (AnimationTree only) Root node type |
| `state_machine` | object | (AnimationTree with state machine) States and transitions |

**Library entry fields:**

| Field | Type | Description |
|---|---|---|
| `name` | string | Library name (`""` for default) |
| `animations` | array | Animation summaries: `{name, length, loop_mode, track_count}` |
| `animation_count` | int | Number of animations in the library |

**Detailed animation fields (when `animation_name` is specified):**

| Field | Type | Description |
|---|---|---|
| `tracks` | array | Track details: `{index, type, path, key_count, enabled, keys}` |
| `keys` | array | Keyframe data: `{time, value}` (format varies by track type) |

**Errors:**
- `"Node is not an AnimationPlayer/AnimationMixer/AnimationTree"` — wrong node type

**Notes:** Without `animation_name`, returns a summary of all libraries and animations. With `animation_name`, includes full track and keyframe data for that specific animation. For AnimationTree nodes, also reports the state machine structure (states, transitions, switch modes).

---

## Editor Tools

### godot_get_editor_log

Get editor-level log messages from the Output panel (startup messages, tool script output, `ERR_PRINT`/`WARN_PRINT` from editor context).

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `limit` | integer | No | Maximum number of messages to return (default 100) |
| `types` | array | No | Filter by message types: `std`, `error`, `warning`, `editor`, `std_rich`. Returns all types if empty. |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `message_count` | int | Number of returned messages |
| `messages` | array | Messages, each with `type`, `text`, and `count` (repeat count) |

**Message type values:**

| Type | Description |
|---|---|
| `std` | Standard output messages |
| `error` | Error messages |
| `warning` | Warning messages |
| `editor` | Editor-specific messages |
| `std_rich` | Rich text (BBCode) messages |

**Errors:**
- `"EditorLog not available"` — editor log subsystem not initialized

**Notes:** Read-only. Messages are returned newest-first. Unlike `godot_get_runtime_output` (which captures messages from the running game), this tool reads from the editor's own Output panel — including startup messages, import notifications, and messages from `@tool` scripts running in the editor.

---

### godot_editor_screenshot

Capture a screenshot from the editor viewport (3D or 2D) or the running game. Returns a base64 PNG image.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `source` | string | No | Screenshot source: `3d` (default), `2d`, or `game` |
| `viewport_idx` | integer | No | 3D viewport index 0-3 (default 0) |
| `scale` | number | No | Downscale factor 0.1-1.0 (default 0.5 = half resolution) |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `width` | int | Pixel width |
| `height` | int | Pixel height |
| `source` | string | Which viewport was captured |

The response includes an MCP `image` content block with the PNG data (base64-encoded).

**Notes:** Read-only. For `game` source, a game must be running. For `3d`/`2d`, captures the editor viewport directly.

---

### godot_editor_viewport_camera

Control the 3D editor viewport camera. Move, orbit, look at targets, focus on selection, or set preset views.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `move`, `orbit`, `look_at`, `focus`, `set_view`, or `get_state` |
| `viewport_idx` | integer | No | 3D viewport index 0-3 (default 0) |
| `position` | object | No | Camera focal point `{x, y, z}` (for `move`) |
| `distance` | number | No | Distance from focal point (for `move`, `orbit`) |
| `x_rotation` | number | No | Pitch in radians (for `move`, `orbit`) |
| `y_rotation` | number | No | Yaw in radians (for `move`, `orbit`) |
| `target` | object | No | Look-at target `{x, y, z}` (for `look_at`) |
| `from` | object | No | Camera position `{x, y, z}` (for `look_at`) |
| `view` | string | No | View preset: `front`, `back`, `left`, `right`, `top`, `bottom` (for `set_view`) |
| `orthogonal` | boolean | No | Toggle orthogonal projection |
| `fov_scale` | number | No | FOV scale 0.1-2.5 |

**Notes:** `get_state` returns the current camera position, rotation, distance, and projection mode. `focus` centers the camera on the current editor selection.

---

### godot_editor_control

Control editor workspace: switch panels, set 3D display mode, toggle grid visibility.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `switch_panel`, `set_display_mode`, `toggle_grid`, or `get_state` |
| `panel` | string | No | `2D`, `3D`, `Script`, or `AssetLib` (for `switch_panel`) |
| `display_mode` | string | No | `normal`, `wireframe`, `overdraw`, `lighting`, or `unshaded` (for `set_display_mode`) |
| `viewport_idx` | integer | No | Which 3D viewport to affect (default 0) |
| `grid` | boolean | No | Grid visibility (for `toggle_grid`) |

**Notes:** `get_state` returns the current active panel, display mode, and grid state.

---

### godot_canvas_view

Control the 2D canvas editor: pan, zoom, center on points, focus selection, configure snapping.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `get_state`, `center_at`, `zoom`, `pan`, `focus`, or `set_snap` |
| `position` | object | No | World position `{x, y}` (for `center_at`, `pan`) |
| `zoom` | number | No | Zoom level (for `zoom`) |
| `grid_snap` | boolean | No | Enable grid snapping (for `set_snap`) |
| `smart_snap` | boolean | No | Enable smart snapping (for `set_snap`) |
| `grid_step` | object | No | Grid step size `{x, y}` (for `set_snap`) |

**Notes:** `get_state` returns current zoom, scroll offset, and snap settings. `focus` centers on the current editor selection.

---

### godot_editor_state

Get comprehensive editor state in one call: viewport cameras, snap settings, scene info, selected nodes.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `include_3d` | boolean | No | Include 3D viewport state (default true) |
| `include_2d` | boolean | No | Include 2D canvas state (default true) |

**Returns:**

| Field | Type | Description |
|---|---|---|
| `active_panel` | string | Currently active editor panel |
| `open_scenes` | array | List of open scene paths |
| `current_scene` | string | Path of the currently edited scene |
| `selected_nodes` | array | Currently selected node paths |
| `viewport_3d` | object | 3D viewport state (if `include_3d`) |
| `canvas_2d` | object | 2D canvas state (if `include_2d`) |

**Notes:** Read-only. Combines information from multiple sources into a single call for efficiency.

---

## Transform Tools

### godot_transform_nodes

Apply relative transforms to nodes (translate, rotate, scale by delta). Works on both Node3D and Node2D with undo/redo.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `translate`, `rotate`, `scale`, or `set_transform` |
| `node_paths` | array | Yes | Node paths to transform |
| `value` | object | No | Delta vector `{x, y, z}` for translate/rotate/scale |
| `local` | boolean | No | Apply in local space (default false = global) |
| `transform` | object | No | Full transform for `set_transform`: `{origin: {x,y,z}, rotation: {x,y,z}, scale: {x,y,z}}` |

**Notes:** All operations support undo/redo. For `scale`, missing components default to 1.0 (identity). For `translate` and `rotate`, missing components default to 0.0.

---

## Scene Operation Tools

### godot_scene_operations

Hierarchy operations: duplicate nodes, reparent, set visibility, toggle editor lock, manage groups. All with undo/redo.

**Parameters:**

| Name | Type | Required | Description |
|---|---|---|---|
| `action` | string | Yes | `duplicate`, `reparent`, `set_visible`, `toggle_lock`, or `group` |
| `node_paths` | array | Yes | Target node paths |
| `new_parent` | string | No | New parent path (for `reparent`) |
| `visible` | boolean | No | Visibility state (for `set_visible`) |
| `group_name` | string | No | Group name (for `group` — toggles membership) |
| `offset` | object | No | Position offset `{x,y,z}` for duplicated nodes |

**Notes:** All operations support undo/redo. `toggle_lock` toggles the `_edit_lock_` meta on each node. `group` toggles group membership — nodes already in the group are removed, others are added.
