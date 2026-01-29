#!/usr/bin/env python3
"""
Stdio-to-TCP bridge for Godot MCP Server.

Bridges Claude Code's stdio-based MCP transport to the Godot editor's
TCP-based MCP server. Reads JSON-RPC messages from stdin, forwards them
to the TCP socket, and writes responses back to stdout.

Usage:
    python claude_mcp_bridge.py [--port PORT] [--host HOST]

Configure in Claude Code's MCP settings:
    {
        "mcpServers": {
            "godot": {
                "command": "python",
                "args": ["path/to/claude_mcp_bridge.py", "--port", "6009"]
            }
        }
    }
"""

import argparse
import socket
import sys
import threading


def reader_thread(sock):
    """Read newline-delimited JSON from the TCP socket and write to stdout."""
    buf = b""
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                break
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                decoded = line.decode("utf-8").strip()
                if decoded:
                    sys.stdout.write(decoded + "\n")
                    sys.stdout.flush()
    except (ConnectionError, OSError):
        pass
    sys.exit(0)


def main():
    parser = argparse.ArgumentParser(description="Godot MCP stdio-to-TCP bridge")
    parser.add_argument("--port", type=int, default=6009, help="TCP port (default: 6009)")
    parser.add_argument("--host", default="127.0.0.1", help="Host (default: 127.0.0.1)")
    args = parser.parse_args()

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((args.host, args.port))
    except ConnectionError as e:
        sys.stderr.write(f"Failed to connect to Godot MCP server at {args.host}:{args.port}: {e}\n")
        sys.stderr.write("Make sure the Godot editor is running with the Claude MCP plugin active.\n")
        sys.exit(1)

    # Start reader thread for TCP -> stdout.
    t = threading.Thread(target=reader_thread, args=(sock,), daemon=True)
    t.start()

    # Main thread: stdin -> TCP.
    try:
        for line in sys.stdin:
            line = line.strip()
            if line:
                sock.sendall((line + "\n").encode("utf-8"))
    except (ConnectionError, OSError, KeyboardInterrupt):
        pass
    finally:
        sock.close()


if __name__ == "__main__":
    main()
