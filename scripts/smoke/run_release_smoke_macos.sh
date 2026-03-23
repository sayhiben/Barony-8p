#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  ./scripts/smoke/run_release_smoke_macos.sh [options] [-- <extra smoke_runner args>]

Description:
  Runs the cross-platform release smoke suite on macOS against a smoke-enabled
  local build. The wrapper auto-detects the common Barony smoke build output
  and Steam asset datadir, but both paths may be overridden explicitly.

Options:
  --app <path>        Barony executable path.
  --datadir <path>    Asset datadir passed through to Barony.
  --profile <name>    sanity | release | full (default: release)
  --outdir <path>     Override the top-level suite artifact directory.
  --fail-fast         Stop after the first failing suite step.
  -h, --help          Show this help.
EOF
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"

app=""
datadir="${BARONY_DATADIR:-}"
profile="release"
outdir=""
fail_fast=0
extra_args=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --app)
      app="${2:-}"
      shift 2
      ;;
    --datadir)
      datadir="${2:-}"
      shift 2
      ;;
    --profile)
      profile="${2:-}"
      shift 2
      ;;
    --outdir)
      outdir="${2:-}"
      shift 2
      ;;
    --fail-fast)
      fail_fast=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      extra_args=("$@")
      break
      ;;
    *)
      extra_args+=("$1")
      shift
      ;;
  esac
done

case "$profile" in
  sanity|release|full)
    ;;
  *)
    echo "Unsupported --profile: $profile" >&2
    usage >&2
    exit 1
    ;;
esac

first_existing() {
  local candidate=""
  for candidate in "$@"; do
    if [[ -f "$repo_root/$candidate" ]]; then
      printf '%s\n' "$repo_root/$candidate"
      return 0
    fi
  done
  return 1
}

if [[ -z "$app" ]]; then
  app="$(first_existing \
    "build-mac-smoke/barony.app/Contents/MacOS/Barony" \
    "build-mac-smoke/barony.app/Contents/MacOS/barony" \
    "build-mac-smoke-nosteam/Barony.app/Contents/MacOS/Barony" \
    "build-mac-smoke-steam/barony.app/Contents/MacOS/barony" \
    "build-mac/barony.app/Contents/MacOS/Barony" \
    "build-mac-all/barony.app/Contents/MacOS/Barony" \
    "build-mac-all/Barony.app/Contents/MacOS/Barony" \
  )"
fi

if [[ -z "$datadir" ]]; then
  datadir="$HOME/Library/Application Support/Steam/steamapps/common/Barony/Barony.app/Contents/Resources"
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 was not found in PATH" >&2
  exit 1
fi

if [[ -z "$app" || ! -f "$app" ]]; then
  echo "Could not locate a smoke-ready Barony executable. Pass --app explicitly." >&2
  exit 1
fi
if [[ ! -x "$app" ]]; then
  echo "Barony executable is not executable: $app" >&2
  exit 1
fi
if [[ ! -d "$datadir" ]]; then
  echo "Datadir does not exist: $datadir" >&2
  exit 1
fi

cmd=(python3 "$repo_root/tests/smoke/smoke_runner.py" release-suite
  --app "$app"
  --datadir "$datadir"
  --profile "$profile"
  --platform macos
)

if [[ -n "$outdir" ]]; then
  cmd+=(--outdir "$outdir")
fi
if [[ "$fail_fast" -eq 1 ]]; then
  cmd+=(--fail-fast)
fi
if [[ "${#extra_args[@]}" -gt 0 ]]; then
  cmd+=("${extra_args[@]}")
fi

printf 'Running release smoke suite:\n  '
printf '%q ' "${cmd[@]}"
printf '\n'

cd "$repo_root"
exec "${cmd[@]}"
