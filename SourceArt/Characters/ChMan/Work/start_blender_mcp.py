"""Start the existing official Blender MCP bridge without changing preferences."""
import importlib.util
import sys

addon_path = r'C:\Users\I\Documents\Codex\2026-10-24\new-chat\work\blender-mcp\official-addon'
spec = importlib.util.spec_from_file_location('chman_blmcp', addon_path + r'\__init__.py', submodule_search_locations=[addon_path])
addon = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = addon
spec.loader.exec_module(addon)
from chman_blmcp import mcp_to_blender_server, execute_blocking
mcp_to_blender_server.start('127.0.0.1', 9876)
execute_blocking.run()
