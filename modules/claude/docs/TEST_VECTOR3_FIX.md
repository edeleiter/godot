# Test Plan: Vector3 Property Parsing Fix

## Summary

Fixed a bug where `godot_set_property` with Vector3 values like `{"x": 10, "y": 1, "z": 10}` would report success but the value wouldn't change.

## Changes Made

**File:** `modules/claude/mcp/godot_mcp_server.cpp`

### Change 1: `_make_schema` (lines 491-494)

Only add `type` to JSON Schema if explicitly provided:

```cpp
// Before:
String type = param.get("type", "string");
// ...
prop["type"] = type;

// After:
if (param.has("type")) {
    prop["type"] = param["type"];
}
```

### Change 2: `set_property` value parameter (line 537)

Removed explicit `"type": "string"` from value parameter:

```cpp
// Before:
set_prop_params.push_back(Dictionary{ { "name", "value" }, { "type", "string" }, { "description", "New property value (as JSON)" }, { "required", true } });

// After:
set_prop_params.push_back(Dictionary{ { "name", "value" }, { "description", "New property value (as JSON)" }, { "required", true } });
```

## Why This Fixes the Bug

- The tool schema declaring `value` as `"type": "string"` caused MCP clients to serialize complex values as JSON strings (`"{\"x\": 10, ...}"`) rather than native JSON objects (`{"x": 10, ...}`)
- The `_coerce_value()` function expects a `Variant::DICTIONARY` to convert to Vector3, but was receiving a string instead
- By omitting the type, JSON Schema accepts any value type, allowing MCP clients to send native JSON objects

## Test Steps

### Prerequisites
1. Build the editor with the fix:
   ```bash
   scons platform=windows target=editor -j8
   ```
2. Open Godot editor
3. Load the test scene: `res://example_node_3d.tscn`
4. Start the MCP server from the Claude MCP dock

### Test 1: Set Vector3 Property

```
godot_set_property node_path="/root/Node3D/Ground" property="scale" value={"x": 10, "y": 1, "z": 10}
```

**Expected:** Success message

### Test 2: Verify Value Changed

```
godot_get_property node_path="/root/Node3D/Ground" property="scale"
```

**Expected:** Value should be `(10, 1, 10)`, NOT `(0, 0, 0)` or `(1, 1, 1)`

### Test 3: Visual Verification

1. Run the scene with `godot_run_scene`
2. Capture screenshot with `godot_capture_screenshot`
3. Verify the Ground plane is visibly larger/scaled

### Test 4: Other Types Still Work

Verify resource types still work:

```
godot_set_property node_path="/root/Node3D/MeshInstance3D" property="mesh" value="BoxMesh"
```

**Expected:** Mesh property set to a BoxMesh instance

## Status

- [x] Code changes implemented
- [ ] Build verified
- [ ] Test 1: Set Vector3 property
- [ ] Test 2: Verify value changed
- [ ] Test 3: Visual verification
- [ ] Test 4: Other types still work
