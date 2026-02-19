#!/bin/bash
set -e

# Build Godot Editor with .NET/C# support — DEV BUILD (debug symbols + extra checks)
# Usage: ./build_dotnet_editor_dev.sh [--clean] [platform] [jobs]
#   --clean:  run scons -c first to remove stale object files
#   platform: windows (default), linuxbsd, macos
#   jobs:     parallel compile jobs (default: NUMBER_OF_PROCESSORS)

CLEAN=false
POSITIONAL=()
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=true ;;
        *) POSITIONAL+=("$arg") ;;
    esac
done
PLATFORM="${POSITIONAL[0]:-windows}"
JOBS="${POSITIONAL[1]:-${NUMBER_OF_PROCESSORS:-4}}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="${SCRIPT_DIR}/.venv"
VENV_PYTHON="${VENV_DIR}/Scripts/python.exe"

echo "=== Building Godot .NET Editor (DEV) for ${PLATFORM} ==="

# Ensure .venv exists in the repo, create with uv if missing
if [[ ! -d "${VENV_DIR}" ]]; then
    echo "--- Creating .venv in repo with uv ---"
    uv venv "${VENV_DIR}"
    uv pip install scons --python "${VENV_PYTHON}"
fi

# Step 0 (optional): Clean stale build artifacts
if [[ "${CLEAN}" == true ]]; then
    echo ""
    echo "--- Step 0: Cleaning previous build artifacts (scons -c) ---"
    CLEAN_ARGS="platform=${PLATFORM} module_mono_enabled=yes target=editor dev_build=yes"
    if [[ "${PLATFORM}" == "windows" ]]; then
        CLEAN_ARGS="${CLEAN_ARGS} d3d12=no"
    fi
    uv run --python "${VENV_PYTHON}" scons -c ${CLEAN_ARGS}
fi

# Step 1: Build the editor binary with mono enabled (dev build)
echo ""
echo "--- Step 1/4: Building editor binary (platform=${PLATFORM}, mono=yes, dev_build=yes) ---"
SCONS_ARGS="platform=${PLATFORM} module_mono_enabled=yes target=editor num_jobs=${JOBS} dev_build=yes scu_build=yes"
if [[ "${PLATFORM}" == "windows" ]]; then
    SCONS_ARGS="${SCONS_ARGS} d3d12=no"
fi
if command -v ccache &>/dev/null; then
    echo "  (ccache detected — enabling compiler cache)"
    SCONS_ARGS="${SCONS_ARGS} cpp_compiler_launcher=ccache"
fi
uv run --python "${VENV_PYTHON}" scons ${SCONS_ARGS}

# Determine the binary name (dev builds add .dev, mono builds add .mono suffix)
ARCH="x86_64"
BIN="${SCRIPT_DIR}/bin/godot.${PLATFORM}.editor.dev.${ARCH}.mono"
if [[ "${PLATFORM}" == "windows" ]]; then
    BIN="${BIN}.exe"
fi

if [[ ! -f "${BIN}" ]]; then
    echo "ERROR: Expected binary not found at ${BIN}"
    exit 1
fi

# Step 2: Generate C# glue/bindings
echo ""
echo "--- Step 2/4: Generating C# glue bindings ---"
"${BIN}" --headless --generate-mono-glue "${SCRIPT_DIR}/modules/mono/glue"

# Step 3: Build C# assemblies
echo ""
echo "--- Step 3/4: Building C# assemblies ---"
NUPKG_DIR="${SCRIPT_DIR}/bin/GodotSharp/Tools/nupkgs"
uv run --python "${VENV_PYTHON}" python "${SCRIPT_DIR}/modules/mono/build_scripts/build_assemblies.py" --godot-output-dir "${SCRIPT_DIR}/bin" \
    --push-nupkgs-local "${NUPKG_DIR}"

# Step 4: Build demo project (if present)
if [[ -f "${SCRIPT_DIR}/demo/demo.csproj" ]]; then
    echo ""
    echo "--- Step 4/4: Building demo C# project ---"
    # Flush stale NuGet cache so source generators from the freshly-built SDK are used
    dotnet nuget locals all --clear
    # Wipe stale obj to force fresh NuGet restore + source generator discovery
    rm -rf "${SCRIPT_DIR}/demo/.godot/mono/temp/obj"
    dotnet build "${SCRIPT_DIR}/demo/demo.csproj"

    # Verify the output DLL exists with the name Godot expects (must match project.godot assembly_name).
    DEMO_DLL="${SCRIPT_DIR}/demo/.godot/mono/temp/bin/Debug/RTDemo.dll"
    if [[ ! -f "${DEMO_DLL}" ]]; then
        echo "ERROR: Expected demo assembly not found at ${DEMO_DLL}"
        echo "  The assembly name in demo.csproj (<AssemblyName>) must match project.godot (dotnet/project/assembly_name)."
        exit 1
    fi
fi

echo ""
echo "=== DEV Build complete! ==="
echo "Run the editor with: ${BIN}"
