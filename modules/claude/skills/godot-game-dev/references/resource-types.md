# Allowed Resource Types

Only these types can be instantiated via type coercion in `godot_set_property` and `godot_add_node` (with the `properties` parameter). Types not on this list will be rejected with a `"Resource type not allowed"` error.

## Primitive Meshes

`BoxMesh`, `SphereMesh`, `CylinderMesh`, `CapsuleMesh`, `PlaneMesh`, `PrismMesh`, `TorusMesh`, `PointMesh`, `QuadMesh`, `TextMesh`

## Materials

`StandardMaterial3D`, `ORMMaterial3D`, `ShaderMaterial`

## Collision Shapes (3D)

`BoxShape3D`, `SphereShape3D`, `CapsuleShape3D`, `CylinderShape3D`, `ConvexPolygonShape3D`, `ConcavePolygonShape3D`, `WorldBoundaryShape3D`, `HeightMapShape3D`, `SeparationRayShape3D`

## Collision Shapes (2D)

`RectangleShape2D`, `CircleShape2D`, `CapsuleShape2D`, `ConvexPolygonShape2D`, `ConcavePolygonShape2D`, `SegmentShape2D`, `SeparationRayShape2D`, `WorldBoundaryShape2D`

## Particles

`ParticleProcessMaterial`

## Animation

`Animation`, `AnimationLibrary`, `AnimationNodeStateMachine`, `AnimationNodeAnimation`, `AnimationNodeBlendTree`, `AnimationNodeBlendSpace1D`, `AnimationNodeBlendSpace2D`

## Navigation

`NavigationMesh`

## Noise

`FastNoiseLite`

## Shaders

`Shader`, `ShaderInclude`

## Theme

`Theme`

## Input Events

`InputEventKey`, `InputEventMouseButton`, `InputEventJoypadButton`, `InputEventJoypadMotion`

## Environment and Sky

`Environment`, `Sky`, `ProceduralSkyMaterial`, `PanoramaSkyMaterial`, `PhysicalSkyMaterial`

## UI Styles

`StyleBoxFlat`, `StyleBoxLine`, `StyleBoxEmpty`, `LabelSettings`, `FontVariation`

## Other

`Gradient`, `Curve`, `Curve2D`, `Curve3D`, `PhysicsMaterial`

---

## Usage Examples

### Simple instantiation (string form)

```jsonc
// Set a mesh property to a default BoxMesh
godot_set_property(node_path="/root/Main/MyMesh", property="mesh", value="BoxMesh")
```

### Instantiation with properties (dictionary form)

```jsonc
// Set a mesh with custom size
godot_set_property(node_path="/root/Main/MyMesh", property="mesh",
  value={"_type": "BoxMesh", "size": {"x": 2, "y": 2, "z": 2}})

// Set a material with color
godot_set_property(node_path="/root/Main/MyMesh", property="material_override",
  value={"_type": "StandardMaterial3D", "albedo_color": {"r": 1, "g": 0, "b": 0, "a": 1}})

// Set collision shape
godot_add_node(parent="/root/Main/Body", node_type="CollisionShape3D", node_name="Shape",
  properties={"shape": {"_type": "CapsuleShape3D"}})

// Set environment with nested sky
godot_set_property(node_path="/root/Main/WorldEnvironment", property="environment",
  value={"_type": "Environment", "background_mode": 2, "sky": {"_type": "Sky",
  "sky_material": {"_type": "ProceduralSkyMaterial"}}})
```

### Inline during node creation

```jsonc
// Add a MeshInstance3D with a sphere mesh in one call
godot_add_node(parent="/root/Main", node_type="MeshInstance3D", node_name="Ball",
  properties={"mesh": {"_type": "SphereMesh", "radius": 0.5, "height": 1.0}})
```

### Navigation mesh

```jsonc
// Set a NavigationMesh on a NavigationRegion3D
godot_set_property(node_path="/root/Main/NavRegion", property="navigation_mesh",
  value={"_type": "NavigationMesh"})
```

### Shader

```jsonc
// Set a shader on a ShaderMaterial
godot_set_property(node_path="/root/Main/MyMesh", property="material_override",
  value={"_type": "ShaderMaterial"})
```

### Theme

```jsonc
// Set a theme on a Control node
godot_set_property(node_path="/root/UI", property="theme",
  value={"_type": "Theme"})
```
