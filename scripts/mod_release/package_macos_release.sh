#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  package_macos_release.sh [options]

Description:
  Stages a macOS mod release directory under dist/ by copying Barony.app and
  editor.app from an existing build output, bundling non-system dylib
  dependencies into each app's Contents/Frameworks directory, adding the
  packaged mod README/changelogs, writing missing-dependency reports plus a
  SHA256 manifest, and creating a zip archive with ditto.

Options:
  --barony-app <path>   Path to Barony/barony.app (default: auto-detect)
  --editor-app <path>   Path to editor.app (default: auto-detect)
  --output-root <dir>   Output root (default: dist)
  --label <label>       Package label (default: timestamp)
  --skip-editor         Do not package editor.app
  --force               Overwrite an existing staged directory/zip
  -h, --help            Show this help
EOF
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"

barony_app=""
editor_app=""
output_root="dist"
label="$(date +%Y%m%d-%H%M%S)"
skip_editor=0
force=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --barony-app)
      barony_app="${2:-}"
      shift 2
      ;;
    --editor-app)
      editor_app="${2:-}"
      shift 2
      ;;
    --output-root)
      output_root="${2:-}"
      shift 2
      ;;
    --label)
      label="${2:-}"
      shift 2
      ;;
    --skip-editor)
      skip_editor=1
      shift
      ;;
    --force)
      force=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

resolve_full_path() {
  local path="$1"
  local dir=""
  if [[ -z "$path" ]]; then
    echo ""
    return
  fi
  if [[ "$path" = /* ]]; then
    dir="$(cd "$(dirname "$path")" && pwd)"
  else
    dir="$(cd "$repo_root" && cd "$(dirname "$path")" && pwd)"
  fi
  printf '%s/%s\n' "$dir" "$(basename "$path")"
}

find_first_existing_path() {
  local candidate=""
  for candidate in "$@"; do
    if [[ -e "$repo_root/$candidate" ]]; then
      printf '%s\n' "$(resolve_full_path "$candidate")"
      return 0
    fi
  done
  return 1
}

remove_path_if_exists() {
  local path="$1"
  if [[ -e "$path" ]]; then
    rm -rf "$path"
  fi
}

append_unique_line() {
  local file="$1"
  local line="$2"
  touch "$file"
  if ! grep -Fqx -- "$line" "$file" 2>/dev/null; then
    printf '%s\n' "$line" >> "$file"
  fi
}

is_macho_file() {
  local path="$1"
  file -b "$path" 2>/dev/null | grep -q "Mach-O"
}

is_system_dependency() {
  local dep="$1"
  case "$dep" in
    /System/Library/*|/usr/lib/*)
      return 0
      ;;
  esac
  return 1
}

binary_install_name() {
  local binary="$1"
  otool -D "$binary" 2>/dev/null | sed -n '2p' || true
}

normalize_runtime_path() {
  local binary="$1"
  local path="$2"
  local bundle_root="${binary%%/Contents/*}"
  local executable_dir="$bundle_root/Contents/MacOS"

  case "$path" in
    @loader_path/*)
      printf '%s/%s\n' "$(dirname "$binary")" "${path#@loader_path/}"
      ;;
    @executable_path/*)
      printf '%s/%s\n' "$executable_dir" "${path#@executable_path/}"
      ;;
    *)
      printf '%s\n' "$path"
      ;;
  esac
}

rpaths_for_binary() {
  local binary="$1"
  local raw_path=""

  while IFS= read -r raw_path; do
    [[ -n "$raw_path" ]] || continue
    normalize_runtime_path "$binary" "$raw_path"
  done < <(
    otool -l "$binary" | awk '
      $1 == "cmd" && $2 == "LC_RPATH" { capture = 1; next }
      capture && $1 == "path" { print $2; capture = 0 }
    '
  )
}

resolve_dependency_source() {
  local binary="$1"
  local dep="$2"
  local candidate=""
  local rpath=""

  case "$dep" in
    /*)
      [[ -f "$dep" ]] && printf '%s\n' "$dep"
      return 0
      ;;
    @loader_path/*|@executable_path/*)
      candidate="$(normalize_runtime_path "$binary" "$dep")"
      [[ -f "$candidate" ]] && printf '%s\n' "$candidate"
      return 0
      ;;
    @rpath/*)
      while IFS= read -r rpath; do
        [[ -n "$rpath" ]] || continue
        candidate="${rpath%/}/${dep#@rpath/}"
        if [[ -f "$candidate" ]]; then
          printf '%s\n' "$candidate"
          return 0
        fi
      done < <(rpaths_for_binary "$binary")
      return 0
      ;;
  esac

  return 0
}

bundle_needs_loader_path_dependency() {
  local bundle="$1"
  local dep_name="$2"
  local binary=""
  local dep=""

  while IFS= read -r binary; do
    is_macho_file "$binary" || continue
    while IFS= read -r dep; do
      [[ -n "$dep" ]] || continue
      if [[ "$dep" == "@loader_path/$dep_name" ]]; then
        return 0
      fi
    done < <(otool -L "$binary" | tail -n +2 | awk '{print $1}')
  done < <(find "$bundle/Contents" -type f | sort)

  return 1
}

ensure_known_loader_path_dependencies() {
  local bundle="$1"
  local macos_dir="$bundle/Contents/MacOS"
  local steam_api_source="$repo_root/deps/steamworks/sdk/redistributable_bin/osx/libsteam_api.dylib"
  local steam_api_dest="$macos_dir/libsteam_api.dylib"

  mkdir -p "$macos_dir"

  if bundle_needs_loader_path_dependency "$bundle" "libsteam_api.dylib" && [[ ! -f "$steam_api_dest" ]]; then
    [[ -f "$steam_api_source" ]] || {
      echo "Missing Steam redistributable: $steam_api_source" >&2
      exit 1
    }
    cp -fL "$steam_api_source" "$steam_api_dest"
    chmod u+w "$steam_api_dest" || true
  fi
}

ensure_overlay_resource_symlinks() {
  local bundle="$1"
  local macos_dir="$bundle/Contents/MacOS"
  local resource_entry=""
  local resource_entries=(
    books
    data
    fonts
    gamecontrollerdb.txt
    images
    items
    lang
    maps
    models
    music
    npcnames-female.txt
    npcnames-male.txt
    playernames-female.txt
    playernames-male.txt
    sound
    steam_appid.txt
    themes
  )

  mkdir -p "$macos_dir"

  for resource_entry in "${resource_entries[@]}"; do
    if [[ -e "$macos_dir/$resource_entry" || -L "$macos_dir/$resource_entry" ]]; then
      continue
    fi
    ln -s "../Resources/$resource_entry" "$macos_dir/$resource_entry"
  done
}

normalize_bundle_binary_id() {
  local binary="$1"
  local current_id=""
  local normalized_id=""

  current_id="$(binary_install_name "$binary")"
  [[ -n "$current_id" ]] || return 0

  case "$binary" in
    */Contents/Frameworks/*|*/Contents/MacOS/*)
      normalized_id="@loader_path/$(basename "$binary")"
      ;;
    *)
      return 0
      ;;
  esac

  if [[ "$current_id" != "$normalized_id" ]]; then
    chmod u+w "$binary" || true
    install_name_tool -id "$normalized_id" "$binary"
  fi
}

bundle_dependency_reference() {
  local binary="$1"
  local dep_name="$2"
  case "$binary" in
    */Contents/Frameworks/*)
      printf '@loader_path/%s\n' "$dep_name"
      ;;
    *)
      printf '@loader_path/../Frameworks/%s\n' "$dep_name"
      ;;
  esac
}

source_path_for_binary() {
  local map_file="$1"
  local binary="$2"
  awk -F '\t' -v target="$binary" '$1 == target { print $2; exit }' "$map_file"
}

initial_macho_queue() {
  local bundle="$1"
  local queue_file="$2"
  local source_map_file="$3"
  local file_path=""
  while IFS= read -r file_path; do
    if is_macho_file "$file_path"; then
      append_unique_line "$queue_file" "$file_path"
      printf '%s\t%s\n' "$file_path" "$file_path" >> "$source_map_file"
    fi
  done < <(find "$bundle/Contents" -type f | sort)
}

write_missing_deps_report() {
  local bundle="$1"
  local report_path="$2"
  local tmp_report
  tmp_report="$(mktemp)"
  local binary=""
  local binary_id=""
  local dep=""
  local resolved=""
  : > "$tmp_report"

  while IFS= read -r binary; do
    is_macho_file "$binary" || continue
    binary_id="$(binary_install_name "$binary")"
    while IFS= read -r dep; do
      [[ -n "$dep" ]] || continue
      [[ -n "$binary_id" && "$dep" == "$binary_id" ]] && continue
      is_system_dependency "$dep" && continue
      resolved="$(resolve_dependency_source "$binary" "$dep")"
      if [[ -z "$resolved" || "$resolved" != "$bundle/"* ]]; then
        printf '%s -> %s\n' "${binary#$bundle/}" "$dep" >> "$tmp_report"
      fi
    done < <(otool -L "$binary" | tail -n +2 | awk '{print $1}')
  done < <(find "$bundle/Contents" -type f | sort)

  if [[ -s "$tmp_report" ]]; then
    sort -u "$tmp_report" > "$report_path"
  else
    printf 'No external non-system dependencies detected.\n' > "$report_path"
  fi
  rm -f "$tmp_report"
}

bundle_non_system_dylibs() {
  local bundle="$1"
  local report_name="$2"
  local frameworks_dir="$bundle/Contents/Frameworks"
  local queue_file
  local processed_file
  local source_map_file
  local binary=""
  local scan_binary=""
  local binary_id=""
  local dep=""
  local dep_source=""
  local dep_name=""
  local dest=""
  local replacement=""

  mkdir -p "$frameworks_dir"
  queue_file="$(mktemp)"
  processed_file="$(mktemp)"
  source_map_file="$(mktemp)"

  initial_macho_queue "$bundle" "$queue_file" "$source_map_file"

  while true; do
    binary="$(grep -Fvx -f "$processed_file" "$queue_file" | head -n 1 || true)"
    [[ -n "$binary" ]] || break
    append_unique_line "$processed_file" "$binary"
    normalize_bundle_binary_id "$binary"
    scan_binary="$(source_path_for_binary "$source_map_file" "$binary")"
    [[ -n "$scan_binary" ]] || scan_binary="$binary"
    binary_id="$(binary_install_name "$scan_binary")"

    while IFS= read -r dep; do
      [[ -n "$dep" ]] || continue
      [[ -n "$binary_id" && "$dep" == "$binary_id" ]] && continue
      is_system_dependency "$dep" && continue
      dep_source="$(resolve_dependency_source "$scan_binary" "$dep")"
      [[ -n "$dep_source" ]] || continue
      if [[ "$dep_source" == "$bundle/"* ]]; then
        continue
      fi

      dep_name="$(basename "$dep_source")"
      dest="$frameworks_dir/$dep_name"
      if [[ ! -e "$dest" ]]; then
        cp -fL "$dep_source" "$dest"
        chmod u+w "$dest" || true
        if is_macho_file "$dest"; then
          normalize_bundle_binary_id "$dest"
          append_unique_line "$queue_file" "$dest"
          printf '%s\t%s\n' "$dest" "$dep_source" >> "$source_map_file"
        fi
      fi

      replacement="$(bundle_dependency_reference "$binary" "$dep_name")"
      if [[ "$dep" != "$replacement" ]]; then
        chmod u+w "$binary" || true
        install_name_tool -change "$dep" "$replacement" "$binary"
      fi
    done < <(otool -L "$scan_binary" | tail -n +2 | awk '{print $1}')
  done

  rm -f "$queue_file" "$processed_file" "$source_map_file"
  write_missing_deps_report "$bundle" "$report_name"
}

ad_hoc_codesign_bundle() {
  local bundle="$1"
  codesign --force --deep --sign - "$bundle" >/dev/null
}

write_sha256_sums() {
  local package_dir="$1"
  local hash_path="$package_dir/SHA256SUMS.txt"
  local rel=""
  (
    cd "$package_dir"
    find . -type f ! -name 'SHA256SUMS.txt' ! -name '.DS_Store' | sort | while IFS= read -r rel; do
      local_hash="$(shasum -a 256 "$rel" | awk '{print $1}')"
      rel="${rel#./}"
      printf '%s *%s\n' "$local_hash" "$rel"
    done > "$hash_path"
  )
}

readme_source="$repo_root/docs/mod_release/README.txt"
changelog_source="$repo_root/docs/mod_release/mod-changelog.txt"
detailed_changelog_source="$repo_root/docs/mod_release/changelog_v5.0.2.md"

[[ -f "$readme_source" ]] || { echo "Missing release document: $readme_source" >&2; exit 1; }
[[ -f "$changelog_source" ]] || { echo "Missing release document: $changelog_source" >&2; exit 1; }
[[ -f "$detailed_changelog_source" ]] || { echo "Missing release document: $detailed_changelog_source" >&2; exit 1; }

if [[ -z "$barony_app" ]]; then
  barony_app="$(find_first_existing_path \
    "build-mac/barony.app" \
    "build-mac/Barony.app" \
    "build-mac-all/barony.app" \
    "build-mac-all/Barony.app")" || true
else
  barony_app="$(resolve_full_path "$barony_app")"
fi

if [[ -z "$barony_app" || ! -d "$barony_app" ]]; then
  echo "Barony app bundle not found. Pass --barony-app <path>." >&2
  exit 1
fi

if [[ "$skip_editor" -eq 0 ]]; then
  if [[ -z "$editor_app" ]]; then
    editor_app="$(find_first_existing_path \
      "build-mac/editor.app" \
      "build-mac-all/editor.app")" || true
  else
    editor_app="$(resolve_full_path "$editor_app")"
  fi
  if [[ -z "$editor_app" || ! -d "$editor_app" ]]; then
    echo "editor.app not found. Pass --editor-app <path> or use --skip-editor." >&2
    exit 1
  fi
fi

output_root="$(resolve_full_path "$output_root")"
mkdir -p "$output_root"

package_name="barony-macos-release-$label"
package_dir="$output_root/$package_name"
zip_path="$output_root/$package_name.zip"

if [[ -e "$package_dir" || -e "$zip_path" ]]; then
  if [[ "$force" -ne 1 ]]; then
    echo "Package output already exists. Use --force to overwrite: $package_dir" >&2
    exit 1
  fi
  remove_path_if_exists "$package_dir"
  rm -f "$zip_path"
fi

mkdir -p "$package_dir"
cp -f "$readme_source" "$package_dir/README.txt"
cp -f "$changelog_source" "$package_dir/mod-changelog.txt"
cp -f "$detailed_changelog_source" "$package_dir/changelog_v5.0.2.md"

ditto "$barony_app" "$package_dir/Barony.app"
ensure_known_loader_path_dependencies "$package_dir/Barony.app"
ensure_overlay_resource_symlinks "$package_dir/Barony.app"
bundle_non_system_dylibs "$package_dir/Barony.app" "$package_dir/barony-missing-deps.txt"
ad_hoc_codesign_bundle "$package_dir/Barony.app"

if [[ "$skip_editor" -eq 0 ]]; then
  ditto "$editor_app" "$package_dir/editor.app"
  ensure_known_loader_path_dependencies "$package_dir/editor.app"
  ensure_overlay_resource_symlinks "$package_dir/editor.app"
  bundle_non_system_dylibs "$package_dir/editor.app" "$package_dir/editor-missing-deps.txt"
  ad_hoc_codesign_bundle "$package_dir/editor.app"
fi

find "$package_dir" -name '.DS_Store' -delete
write_sha256_sums "$package_dir"

ditto -c -k --sequesterRsrc --keepParent "$package_dir" "$zip_path"

echo "Packaged macOS release:"
echo "  package_dir=$package_dir"
echo "  zip_path=$zip_path"
