#!/bin/bash
set -e

# Run Godot rendering tests (Windows / Git Bash)
# Usage: ./run_rendering_tests.sh [--build] [--dev] [--list] [--all] [--filter PATTERN] [platform] [jobs]
#   --build:          rebuild editor binary with tests=yes before running
#   --dev:            target the dev binary (editor.dev.x86_64.exe)
#   --list:           list matching test cases instead of running them
#   --all:            run ALL tests (not just rendering subset)
#   --filter PATTERN: custom --test-case glob (default: "*ShadowCaching*,*RTSceneManager*,*ShaderPreprocessor*")
#   platform:         windows (default), linuxbsd, macos
#   jobs:             parallel compile jobs used with --build (default: NUMBER_OF_PROCESSORS)

# --- parse flags ---
BUILD=false
DEV=false
LIST=false
FILTER="*ShadowCaching*,*RTSceneManager*,*ShaderPreprocessor*"
NEXT_IS_FILTER=false
POSITIONAL=()

for arg in "$@"; do
    if [[ "${NEXT_IS_FILTER}" == true ]]; then
        FILTER="${arg}"
        NEXT_IS_FILTER=false
        continue
    fi
    case "$arg" in
        --build)  BUILD=true ;;
        --dev)    DEV=true ;;
        --list)   LIST=true ;;
        --all)    FILTER="*" ;;
        --filter) NEXT_IS_FILTER=true ;;
        *) POSITIONAL+=("$arg") ;;
    esac
done

PLATFORM="${POSITIONAL[0]:-windows}"
JOBS="${POSITIONAL[1]:-${NUMBER_OF_PROCESSORS:-4}}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="${SCRIPT_DIR}/.venv"
VENV_PYTHON="${VENV_DIR}/Scripts/python.exe"

# Determine binary name (no .mono suffix — tests don't require C# support)
ARCH="x86_64"
if [[ "${DEV}" == true ]]; then
    BIN="${SCRIPT_DIR}/bin/godot.${PLATFORM}.editor.dev.${ARCH}"
else
    BIN="${SCRIPT_DIR}/bin/godot.${PLATFORM}.editor.${ARCH}"
fi
if [[ "${PLATFORM}" == "windows" ]]; then
    BIN="${BIN}.exe"
fi

echo "=== Godot Rendering Tests (${PLATFORM}) ==="
echo "  Binary: ${BIN}"
echo "  Filter: ${FILTER}"
echo ""

# Total numbered steps depends on whether we are building first
if [[ "${BUILD}" == true ]]; then
    TOTAL_STEPS=2
else
    TOTAL_STEPS=1
fi

# --- Step 1 (optional): Build editor binary with tests=yes ---
if [[ "${BUILD}" == true ]]; then
    # Ensure .venv exists in the repo, create with uv if missing
    if [[ ! -d "${VENV_DIR}" ]]; then
        echo "--- Creating .venv in repo with uv ---"
        uv venv "${VENV_DIR}"
        uv pip install scons --python "${VENV_PYTHON}"
    fi

    echo "--- Step 1/${TOTAL_STEPS}: Building editor binary (platform=${PLATFORM}, tests=yes) ---"
    SCONS_ARGS="platform=${PLATFORM} target=editor tests=yes num_jobs=${JOBS} scu_build=yes"
    if [[ "${DEV}" == true ]]; then
        SCONS_ARGS="${SCONS_ARGS} dev_build=yes"
    fi
    if [[ "${PLATFORM}" == "windows" ]]; then
        SCONS_ARGS="${SCONS_ARGS} d3d12=no"
    fi
    if command -v ccache &>/dev/null; then
        echo "  (ccache detected — enabling compiler cache)"
        SCONS_ARGS="${SCONS_ARGS} cpp_compiler_launcher=ccache"
    fi
    uv run --python "${VENV_PYTHON}" scons ${SCONS_ARGS}
    echo ""
fi

# Validate binary exists before trying to run it
if [[ ! -f "${BIN}" ]]; then
    echo "ERROR: Expected binary not found at ${BIN}"
    if [[ "${BUILD}" == false ]]; then
        echo "  Tip: run with --build to compile first"
    fi
    exit 1
fi

# --- Final step: list or run tests ---
STEP_NUM="${TOTAL_STEPS}"
if [[ "${LIST}" == true ]]; then
    echo "--- Step ${STEP_NUM}/${TOTAL_STEPS}: Listing matching test cases ---"
    "${BIN}" --test --list-test-cases --test-case="${FILTER}"
else
    echo "--- Step ${STEP_NUM}/${TOTAL_STEPS}: Running rendering tests ---"
    "${BIN}" --test --test-case="${FILTER}"
fi

echo ""
echo "=== Rendering tests complete! ==="
echo "Binary: ${BIN}"
