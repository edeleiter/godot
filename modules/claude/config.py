def can_build(env, platform):
    """Return True if this module can be built on the given platform."""
    # Desktop platforms only - need editor for MCP server
    return platform in ["windows", "linuxbsd", "macos"]


def configure(env):
    """Configure the environment for this module."""
    pass


def get_doc_classes():
    """Return list of classes to generate documentation for."""
    return [
        "GodotMCPServer",
        "MCPSceneSerializer",
        "ClaudeMCPDock",
        "ClaudeEditorPlugin",
    ]


def get_doc_path():
    """Return path to documentation XML files."""
    return "doc_classes"
