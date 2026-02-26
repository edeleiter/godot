#!/usr/bin/env bash
#
# title         : build_run_dev_editor.sh
# description   : Build the dev editor (C++ + C# assemblies) then launch it.
# usage         : ./build_run_dev_editor.sh
# notes         : Requires Python 3.8+, uv, .NET 8 SDK, and VS Build Tools.
#
./build_dotnet_editor_dev.sh

./bin/godot.windows.editor.dev.x86_64.mono.exe --path demo --editor --accurate-breadcrumbs