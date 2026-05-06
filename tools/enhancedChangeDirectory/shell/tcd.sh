# tcd shell wrapper for bash / zsh.
#
# Resolution order for the binary:
#   1. $TCD_BIN if set
#   2. tcd / tcd.exe in this script's parent directory
#   3. `command tcd` on PATH
#
# Install: source this file from your shell rc:
#   source /path/to/tcd.sh

_TCD_SHELL_DIR=""
_tcd_src="${BASH_SOURCE[0]:-$0}"
if [ -n "$_tcd_src" ]; then
    _TCD_SHELL_DIR="$(cd "$(dirname "$_tcd_src")" 2>/dev/null && pwd)"
fi
unset _tcd_src

tcd() {
    local out rc bin=""
    if [ -n "${TCD_BIN:-}" ]; then
        bin="$TCD_BIN"
    elif [ -n "${_TCD_SHELL_DIR:-}" ]; then
        if   [ -x "$_TCD_SHELL_DIR/../tcd" ];     then bin="$_TCD_SHELL_DIR/../tcd"
        elif [ -x "$_TCD_SHELL_DIR/../tcd.exe" ]; then bin="$_TCD_SHELL_DIR/../tcd.exe"
        fi
    fi

    if [ -n "$bin" ]; then
        out=$("$bin" "$@")
        rc=$?
    else
        out=$(command tcd "$@")
        rc=$?
    fi

    if [ "$rc" -ne 0 ]; then
        return "$rc"
    fi
    if [ -n "$out" ]; then
        if [ -d "$out" ]; then
            cd "$out" || return $?
        else
            printf '%s\n' "$out"
        fi
    fi
}
