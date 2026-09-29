#!/usr/bin/env bash
# Run an app. Usage: ./run.sh <gl|vk> [technique] [-- extra args]
#   ./run.sh vk 08_shadow_mapping
# Set CFG=release to run the release build.
set -euo pipefail
cd "$(dirname "$0")"

api="${1:-}"
case "$api" in
  gl|vk) shift ;;
  *) echo "usage: $0 <gl|vk> [technique]" >&2; exit 1 ;;
esac

# Validation layers are found through the SDK's VK_LAYER_PATH.
if [[ -z "${VULKAN_SDK:-}" ]]; then
  sdk_env=$(ls -d ~/vulkan-sdk/*/setup-env.sh 2>/dev/null | sort -V | tail -1 || true)
  # setup-env.sh reads $1 unguarded, so relax -u while sourcing it (with no args).
  [[ -n "$sdk_env" ]] && { set +u; source "$sdk_env" ""; set -u; }
fi

exe="build/${CFG:-debug}/glint_$api"
[[ -x "$exe" ]] || { echo "$exe not built — run ./build.sh first" >&2; exit 1; }
exec "$exe" "$@"
