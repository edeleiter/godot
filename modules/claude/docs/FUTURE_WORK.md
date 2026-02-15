# Future Work

Items identified during architectural review of the Claude MCP module (Feb 2026). These are post-merge improvements — none are blockers.

## 1. TCP Authentication for MCP Server

**Priority:** High | **Risk:** Security

The MCP TCP server (`GodotMCPServer`) currently accepts any local connection without authentication. While it only listens on `127.0.0.1`, any local process can connect and issue commands.

**Recommended approach:** Add a shared-secret token handshake. The server generates a random token on startup and writes it to a known file path. The Python bridge reads the token and includes it in the initial handshake. The server rejects connections that don't present the correct token.

**Key files:** `mcp/godot_mcp_server.cpp` (connection accept logic), `bridge/claude_mcp_bridge.py` (client side).

## 2. Extract Output/Error Buffer Management

**Priority:** Low | **Scope:** Refactor

The output and error ring buffer logic (append, trim, filter, serialize) lives directly in `GodotMCPServer`. Extract it into a dedicated `MCPOutputBuffer` helper class to improve separation of concerns and make it independently testable.

**Key files:** `mcp/godot_mcp_server.h` (buffer structs), `mcp/godot_mcp_tools_runtime.cpp` (`_tool_get_runtime_output`, `_tool_get_runtime_errors`, `_on_debugger_output`, `_on_debugger_data`).

## 3. Replace `Vector<T>.remove_at(0)` with Circular Buffer

**Priority:** Low | **Scope:** Performance

Both `output_buffer` and `error_buffer` use `Vector<T>` with `remove_at(0)` to cap size, which is O(n) per removal because it shifts all remaining elements. Replace with a circular buffer (ring buffer) for O(1) enqueue/dequeue.

**Key files:** `mcp/godot_mcp_tools_runtime.cpp` lines near `_on_debugger_output` and `_on_debugger_data` (the `while (buffer.size() > MAX)` patterns).

## 4. Add Idle Timeout for TCP Clients

**Priority:** Medium | **Scope:** Reliability

Connected TCP clients can remain idle indefinitely. Add a configurable idle timeout (e.g., 5 minutes) that disconnects clients with no activity, preventing resource leaks from abandoned connections.

**Key files:** `mcp/godot_mcp_server.cpp` (`_poll` method, per-client state).

## 5. Split `godot_mcp_tools_scene.cpp`

**Priority:** Low | **Scope:** Code organization

At ~1190 lines, `godot_mcp_tools_scene.cpp` is the largest tool file. The transform tools (`_tool_transform_nodes`) and scene operations (`_tool_scene_operations`) are logically distinct from the core scene/property tools and should be split into a separate file (e.g., `godot_mcp_tools_scene_ops.cpp`).

**Key files:** `mcp/godot_mcp_tools_scene.cpp` (lines ~800+).

## 6. Add a `godot_rename_node` Convenience Tool

**Priority:** Low | **Scope:** Feature

Renaming a node currently requires using `set_property` with `name` as the property. A dedicated `godot_rename_node` tool would be more discoverable and could handle edge cases (duplicate name resolution, undo/redo action naming).

**Key files:** `mcp/godot_mcp_tools_schema.cpp` (tool definition), `mcp/godot_mcp_tools_scene.cpp` (implementation).

## 7. Extract Terminal Emulator to Its Own Module

**Priority:** Medium (when adding Linux support) | **Scope:** Architecture

The terminal emulator (`ConPtyProcess`, `AnsiTerminalState`, `ClaudeTerminalDock`) is Windows-only (guarded with `#ifdef WINDOWS_ENABLED`) and bundled inside `modules/claude/`. When Linux PTY support is added, the terminal should be extracted into an independent module (e.g., `modules/terminal/`) to keep the Claude module focused on MCP functionality.

**Key files:** `terminal/con_pty_process.cpp`, `terminal/ansi_terminal_state.cpp`, `editor/claude_terminal_dock.cpp`.

## 8. Add Optional Restrictions on MCP-Modifiable Project Settings

**Priority:** Low | **Scope:** Safety

The `godot_set_project_setting` tool can modify any project setting. Add an optional allowlist/blocklist mechanism (configurable via editor settings or a project-level config) so that teams can restrict which settings are modifiable via MCP.

**Key files:** `mcp/godot_mcp_tools_project.cpp` (`_tool_set_project_setting`), `mcp/godot_mcp_validation.cpp`.
