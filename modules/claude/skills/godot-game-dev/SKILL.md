---
name: godot-game-dev
description: >
  Use when the user asks to create a Godot scene, add a node, set up a player
  character, wire signals, run the game, take a screenshot, build a level,
  create a script, set project settings, inspect the scene tree, debug the
  running game, save or instance a scene, or any Godot 4.x game development
  task involving the Godot editor MCP tools.
---

# Godot Game Development Skill

You are assisting with Godot 4.x game development through MCP tools that control the Godot editor. You have 42 tools available for scene manipulation, scripting, signals, project settings, runtime inspection, and scene persistence.

## Tool Quick Reference

### Scene Persistence
| Tool | Purpose |
|------|---------|
| `godot_save_scene` | Save current scene (or "save as" with path) |
| `godot_new_scene` | Create empty scene with typed root node |
| `godot_open_scene` | Open existing .tscn/.scn file |
| `godot_instance_scene` | Instance a PackedScene as child (Godot's prefab pattern) |

### Scene Manipulation
| Tool | Purpose |
|------|---------|
| `godot_get_scene_tree` | Read current scene structure |
| `godot_add_node` | Add node with optional initial properties |
| `godot_remove_node` | Remove a node |
| `godot_set_property` | Set property with auto type coercion |
| `godot_get_property` | Read a property value |
| `godot_connect_signal` | Connect signal between nodes |
| `godot_disconnect_signal` | Remove signal connection |

### Scripts
| Tool | Purpose |
|------|---------|
| `godot_create_script` | Create .gd file, optionally attach to node |
| `godot_read_script` | Read script content |
| `godot_modify_script` | Replace script content |
| `godot_validate_script` | Validate script for compilation errors/warnings |

### Project
| Tool | Purpose |
|------|---------|
| `godot_project_settings` | Get/set/list project settings |

### Runtime (requires running game)
| Tool | Purpose |
|------|---------|
| `godot_run_scene` | Run scene |
| `godot_stop_scene` | Stop scene |
| `godot_get_runtime_scene_tree` | Inspect running scene tree |
| `godot_get_runtime_output` | Get print/error output |
| `godot_get_runtime_errors` | Get structured errors with source locations and call stacks |
| `godot_capture_screenshot` | Screenshot the running game |
| `godot_runtime_camera_control` | Move debug camera |
| `godot_get_runtime_camera_info` | Camera state info |

### Selection
| Tool | Purpose |
|------|---------|
| `godot_get_selected_nodes` | Get editor selection |
| `godot_select_nodes` | Set editor selection |

### Input
| Tool | Purpose |
|------|---------|
| `godot_input_map` | Add/remove input actions and key/button bindings |

### Introspection
| Tool | Purpose |
|------|---------|
| `godot_get_class_info` | Query ClassDB for class properties, methods, signals |
| `godot_get_node_info` | Full inspector: all properties with current values |

### Batch Operations
| Tool | Purpose |
|------|---------|
| `godot_set_properties_batch` | Set multiple properties in one undo action |

### Resource
| Tool | Purpose |
|------|---------|
| `godot_project_files` | List project files, trigger filesystem rescan, or run import diagnostics |

### 3D
| Tool | Purpose |
|------|---------|
| `godot_bake_navigation` | Bake navigation mesh for AI pathfinding |

### Animation
| Tool | Purpose |
|------|---------|
| `godot_create_animation` | Create animation with tracks and keyframes |
| `godot_get_animation_info` | Inspect animations, tracks, state machines |

### Transform
| Tool | Purpose |
|------|---------|
| `godot_transform_nodes` | Translate, rotate, scale, or set transform on multiple nodes |

### Scene Operations
| Tool | Purpose |
|------|---------|
| `godot_scene_operations` | Duplicate, reparent, set visibility, toggle lock, manage groups |

### Editor
| Tool | Purpose |
|------|---------|
| `godot_get_editor_log` | Get editor Output panel messages (startup, tool scripts, editor errors) |
| `godot_editor_screenshot` | Capture editor viewport screenshot (3D, 2D, or running game) |
| `godot_editor_viewport_camera` | Control 3D editor viewport camera (move, orbit, look at, focus, preset views) |
| `godot_editor_control` | Switch editor panels, set 3D display mode, toggle grid |
| `godot_canvas_view` | Control 2D canvas editor view (pan, zoom, center, snap settings) |
| `godot_editor_state` | Get comprehensive editor state (viewports, snap settings, scene info) |

---

## Common Property Syntax

Type coercion converts JSON to Godot types automatically:

```jsonc
// Vectors
{"x": 1.0, "y": 2.0}           // Vector2
{"x": 0, "y": 5, "z": -3}      // Vector3

// Colors
{"r": 1.0, "g": 0.5, "b": 0.0, "a": 1.0}  // Color

// Resources (from allowlist)
"BoxMesh"                                      // Simple instantiation
{"_type": "BoxMesh", "size": {"x": 2, "y": 2, "z": 2}}  // With properties

// Nested resources
{"_type": "StandardMaterial3D", "albedo_color": {"r": 1, "g": 0, "b": 0, "a": 1}}
```

### Common Properties by Node Type

**Node3D**: `position`, `rotation_degrees`, `scale`, `visible`
**CharacterBody3D**: All Node3D props + physics via CollisionShape3D child
**MeshInstance3D**: `mesh` (use `{"_type": "BoxMesh"}` etc.)
**CollisionShape3D**: `shape` (use `{"_type": "BoxShape3D"}` etc.)
**Camera3D**: `position`, `rotation_degrees`, `fov`, `current`
**DirectionalLight3D**: `rotation_degrees`, `light_color`, `light_energy`, `shadow_enabled`
**WorldEnvironment**: `environment` (use `{"_type": "Environment", ...}`)
**Label**: `text`, `horizontal_alignment`, `vertical_alignment`
**Button**: `text`, `disabled`
**TextureRect**: `texture`, `stretch_mode`

---

## Recipe: New 3D Project Setup

```
1. Create main scene:        godot_new_scene(root_type="Node3D", root_name="Main")
2. Save it:                  godot_save_scene(path="res://main.tscn")
3. Set project settings:     godot_project_settings(action="set", setting="display/window/size/viewport_width", value=1280)
                             godot_project_settings(action="set", setting="display/window/size/viewport_height", value=720)
                             godot_project_settings(action="set", setting="application/config/name", value="My Game")
4. Add camera:               godot_add_node(parent="/root/Main", node_type="Camera3D", node_name="Camera3D",
                               properties={"position": {"x": 0, "y": 5, "z": 10}, "rotation_degrees": {"x": -20, "y": 0, "z": 0}})
5. Add light:                godot_add_node(parent="/root/Main", node_type="DirectionalLight3D", node_name="Sun",
                               properties={"rotation_degrees": {"x": -45, "y": 30, "z": 0}, "shadow_enabled": true})
6. Add environment:          godot_add_node(parent="/root/Main", node_type="WorldEnvironment", node_name="WorldEnvironment")
                             godot_set_property(node_path="/root/Main/WorldEnvironment", property="environment",
                               value={"_type": "Environment", "background_mode": 2, "sky": {"_type": "Sky",
                               "sky_material": {"_type": "ProceduralSkyMaterial"}}})
7. Save:                     godot_save_scene()
```

---

## Recipe: 3D Player Character

```
1. Create player scene:      godot_new_scene(root_type="CharacterBody3D", root_name="Player")
2. Add collision:            godot_add_node(parent="/root/Player", node_type="CollisionShape3D", node_name="CollisionShape3D",
                               properties={"shape": {"_type": "CapsuleShape3D"}})
3. Add visual mesh:          godot_add_node(parent="/root/Player", node_type="MeshInstance3D", node_name="MeshInstance3D",
                               properties={"mesh": {"_type": "CapsuleMesh"}})
4. Create movement script:   godot_create_script(path="res://player.gd", content=<PLAYER_SCRIPT>, attach_to="/root/Player")
5. Save player scene:        godot_save_scene(path="res://player.tscn")
6. Open main scene:          godot_open_scene(path="res://main.tscn")
7. Instance player:          godot_instance_scene(scene_path="res://player.tscn", parent_path="/root/Main")
8. Save main scene:          godot_save_scene()
```

**Standard player movement script (CharacterBody3D):**
```gdscript
extends CharacterBody3D

const SPEED = 5.0
const JUMP_VELOCITY = 4.5

func _physics_process(delta: float) -> void:
    if not is_on_floor():
        velocity += get_gravity() * delta

    if Input.is_action_just_pressed("ui_accept") and is_on_floor():
        velocity.y = JUMP_VELOCITY

    var input_dir := Input.get_vector("ui_left", "ui_right", "ui_up", "ui_down")
    var direction := (transform.basis * Vector3(input_dir.x, 0, input_dir.y)).normalized()
    if direction:
        velocity.x = direction.x * SPEED
        velocity.z = direction.z * SPEED
    else:
        velocity.x = move_toward(velocity.x, 0, SPEED)
        velocity.z = move_toward(velocity.z, 0, SPEED)

    move_and_slide()
```

---

## Recipe: Level Building with Scene Composition

```
1. Create level scene:       godot_new_scene(root_type="Node3D", root_name="Level1")
2. Add ground:               godot_add_node(parent="/root/Level1", node_type="StaticBody3D", node_name="Ground")
                             godot_add_node(parent="/root/Level1/Ground", node_type="CollisionShape3D", node_name="CollisionShape3D",
                               properties={"shape": {"_type": "WorldBoundaryShape3D"}})
                             godot_add_node(parent="/root/Level1/Ground", node_type="MeshInstance3D", node_name="GroundMesh",
                               properties={"mesh": {"_type": "PlaneMesh", "size": {"x": 50, "y": 50}}})
3. Save level:               godot_save_scene(path="res://levels/level1.tscn")
4. Open main scene:          godot_open_scene(path="res://main.tscn")
5. Instance level:           godot_instance_scene(scene_path="res://levels/level1.tscn", parent_path="/root/Main")
6. Instance player:          godot_instance_scene(scene_path="res://player.tscn", parent_path="/root/Main",
                               node_name="Player")
7. Position player:          godot_set_property(node_path="/root/Main/Player", property="position",
                               value={"x": 0, "y": 1, "z": 0})
8. Save:                     godot_save_scene()
```

---

## Recipe: Signal Wiring

```
// Button pressed -> call method
godot_connect_signal(source_path="/root/UI/StartButton", signal_name="pressed",
                     target_path="/root/UI", method_name="_on_start_pressed")

// Area detection -> call method
godot_connect_signal(source_path="/root/Main/CoinArea", signal_name="body_entered",
                     target_path="/root/Main/GameManager", method_name="_on_coin_collected")

// Timer timeout
godot_connect_signal(source_path="/root/Main/SpawnTimer", signal_name="timeout",
                     target_path="/root/Main/EnemySpawner", method_name="_on_spawn_timer")
```

Common signals: `pressed` (Button), `body_entered`/`body_exited` (Area3D), `timeout` (Timer), `animation_finished` (AnimationPlayer), `value_changed` (Slider/SpinBox).

---

## Recipe: Run-Observe-Fix Loop

```
1. Save scene:               godot_save_scene()
2. Run:                      godot_run_scene()
3. Wait a moment, then:      godot_capture_screenshot()
4. Check for errors:         godot_get_runtime_output(limit=20)
5. Inspect tree if needed:   godot_get_runtime_scene_tree()
6. Stop:                     godot_stop_scene()
7. Fix issues based on output/screenshot
8. Repeat from step 1
```

---

## Godot Architectural Patterns

### Scene Composition (Preferred)
Build reusable pieces as separate scenes, then instance them together. Each scene is self-contained with its own nodes, scripts, and signals.

```
main.tscn
├── Player.tscn (instanced)
├── Level1.tscn (instanced)
│   ├── Ground
│   ├── Obstacles
│   └── Coins
└── UI.tscn (instanced)
```

### Autoloads (Global Singletons)
For game state, score tracking, scene transitions. Set via project settings:
```
godot_project_settings(action="set", setting="autoload/GameState", value="*res://game_state.gd")
```

### Signal Decoupling
Nodes communicate via signals, not direct references. The emitter doesn't know who's listening.

### Groups
Tag nodes for batch operations. Use in scripts: `add_to_group("enemies")`, `get_tree().call_group("enemies", "take_damage", 10)`.

---

## Error Recovery

**"Node not found" errors**: Check the scene tree with `godot_get_scene_tree()`. Paths are case-sensitive and use `/root/SceneName/...` format.

**"Script already exists" error**: Use `godot_modify_script()` instead of `godot_create_script()` for existing files.

**Scene not saving**: New scenes need an explicit path: `godot_save_scene(path="res://scene.tscn")`.

**Properties not taking effect**: Check the property name with `godot_get_property()`. Many properties use snake_case. Resource properties need the `{"_type": "..."}` syntax.

**"Resource type not allowed"**: Only allowlisted types can be created inline. For others, create them in scripts or use file-based resources.

**Instanced scene not editable**: Instanced scenes are read-only by default in Godot. To modify internal nodes, you'd need to make the instance "editable children" or modify the source scene directly.

---

## Additional Resources

- For full tool API docs (parameters, return values, error codes), see [references/tool-reference.md](references/tool-reference.md)
- For allowed resource types, see [references/resource-types.md](references/resource-types.md)
