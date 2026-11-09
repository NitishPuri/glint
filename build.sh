#!/usr/bin/env bash
# Configure + build. Usage: ./build.sh [debug|release|clang-debug]   (default: debug)
# Each is a preset in CMakePresets.json (compiler + build type), built into build/<preset>.
set -euo pipefail
cd "$(dirname "$0")"

cfg="${1:-debug}"
case "$cfg" in
  debug|release|clang-debug) ;;
  *) echo "usage: $0 [debug|release|clang-debug]" >&2; exit 1 ;;
esac

# glslc + validation layers come from the LunarG SDK, not apt.
if [[ -z "${VULKAN_SDK:-}" ]]; then
  sdk_env=$(ls -d ~/vulkan-sdk/*/setup-env.sh 2>/dev/null | sort -V | tail -1 || true)
  # setup-env.sh reads $1 unguarded, so relax -u while sourcing it (with no args).
  [[ -n "$sdk_env" ]] && { set +u; source "$sdk_env" ""; set -u; }
fi

cmake --preset "$cfg"
cmake --build --preset "$cfg"
