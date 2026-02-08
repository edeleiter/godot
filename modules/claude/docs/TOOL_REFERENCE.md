# MCP Tool Reference

Complete API reference for all 24 tools exposed by the Claude MCP module.

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
