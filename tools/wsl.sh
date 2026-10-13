#!/bin/sh
# Run a command in the WSL Ubuntu distro from the repo root (Windows host without a C compiler): tools/wsl.sh 'cmd'
exec wsl -d Ubuntu -- bash -lc "cd \"\$(wslpath '$(pwd -W 2>/dev/null || pwd)')\" && $1"
