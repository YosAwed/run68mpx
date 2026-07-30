#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd -- "$script_dir/.." && pwd)

usage() {
  printf 'Usage: %s [WAV file]\n' "${0##*/}"
  printf 'No argument defaults to %s/BOM_01.wav\n' "$project_dir"
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
  usage
  exit 0
fi

if (( $# > 1 )); then
  usage >&2
  exit 2
fi

wav_path=${1:-"$project_dir/BOM_01.wav"}
if [[ ! -f "$wav_path" ]]; then
  printf 'WAV file not found: %s\n' "$wav_path" >&2
  exit 1
fi

if command -v afplay >/dev/null 2>&1; then
  exec afplay "$wav_path"
elif command -v aplay >/dev/null 2>&1; then
  exec aplay "$wav_path"
elif command -v paplay >/dev/null 2>&1; then
  exec paplay "$wav_path"
elif command -v ffplay >/dev/null 2>&1; then
  exec ffplay -nodisp -autoexit "$wav_path"
fi

printf 'No supported audio player found (afplay, aplay, paplay, or ffplay).\n' >&2
exit 1
