#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: install.sh [--system | --user] [--no-apt] [--non-interactive]

Install the optional CPU Whisper runtime in a dedicated Python virtual
environment. Model weights are never downloaded by this script.
USAGE
}

system_install=false
install_system_dependencies=true
non_interactive=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --system)
      system_install=true
      shift
      ;;
    --user)
      system_install=false
      shift
      ;;
    --no-apt)
      install_system_dependencies=false
      shift
      ;;
    --non-interactive)
      non_interactive=true
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ "$system_install" == true && "${EUID:-$(id -u)}" -ne 0 ]]; then
  echo "--system requires root privileges." >&2
  exit 2
fi

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
requirements_file="$script_dir/requirements-cpu.txt"
if [[ ! -f "$requirements_file" ]]; then
  echo "The packaged requirements file is missing: $requirements_file" >&2
  exit 1
fi

if [[ "$system_install" == true ]]; then
  runtime_root="/opt/obs-whisperbleep/runtime-venv"
else
  data_root="${XDG_DATA_HOME:-$HOME/.local/share}"
  runtime_root="$data_root/Sythos/OBS-WhisperBleep/runtime-venv"
fi

if [[ "$install_system_dependencies" == true && "${EUID:-$(id -u)}" -eq 0 && \
      -x "$(command -v apt-get || true)" ]]; then
  export DEBIAN_FRONTEND=noninteractive
  apt-get update
  apt-get install --yes --no-install-recommends \
    ca-certificates ffmpeg python3 python3-pip python3-venv
fi

python_command=""
for candidate in python3.11 python3; do
  if command -v "$candidate" >/dev/null 2>&1; then
    major_minor="$($candidate -c 'import sys; print(f"{sys.version_info[0]}.{sys.version_info[1]}")')"
    if [[ "$major_minor" =~ ^(3\.(1[1-9]|[2-9][0-9]))$ ]]; then
      python_command="$candidate"
      break
    fi
  fi
done

if [[ -z "$python_command" ]]; then
  echo "Python 3.11 or newer is required. Install python3 and python3-venv, then retry." >&2
  exit 1
fi

mkdir -p "$(dirname -- "$runtime_root")"
if [[ ! -x "$runtime_root/bin/python" ]]; then
  "$python_command" -m venv "$runtime_root"
fi

venv_python="$runtime_root/bin/python"
"$venv_python" -m pip install --disable-pip-version-check --upgrade pip
"$venv_python" -m pip install --disable-pip-version-check \
  --index-url https://download.pytorch.org/whl/cpu \
  --extra-index-url https://pypi.org/simple \
  --requirement "$requirements_file"

printf '%s\n' "$venv_python" > "$runtime_root/python-path.txt"
chmod 0644 "$runtime_root/python-path.txt"

if [[ "$system_install" == true ]]; then
  chmod -R a+rX "$runtime_root"
fi
printf 'Installed the CPU Whisper runtime at %s\n' "$runtime_root"
printf '%s\n' 'Download and verify a Whisper model through the OBS plugin before enabling censorship.'
