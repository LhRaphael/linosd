#!/usr/bin/env bash
# Remove o LinOSD.
#   ./scripts/uninstall.sh              remove de ~/.local
#   ./scripts/uninstall.sh --system     remove de /usr/local
#   ./scripts/uninstall.sh --prefix DIR
#   Acrescente --purge para apagar também ~/.config/linosd
set -euo pipefail

PREFIX="$HOME/.local"
SUDO=""
purge=0
while [ $# -gt 0 ]; do
    case "$1" in
        --system) PREFIX=/usr/local; [ "$(id -u)" -ne 0 ] && SUDO=sudo ;;
        --prefix) PREFIX="$2"; shift ;;
        --purge) purge=1 ;;
        *) echo "opção desconhecida: $1" >&2; exit 1 ;;
    esac
    shift
done

MANIFEST="$PREFIX/share/linosd/installed-files.txt"
[ -f "$MANIFEST" ] || { echo "Nenhuma instalação encontrada em $PREFIX" >&2; exit 1; }

pkill -u "$(id -u)" -x linosd 2>/dev/null || true

while IFS= read -r f; do
    [ -n "$f" ] || continue
    case "$f" in /etc/*) sudo rm -f "$f" ;; *) $SUDO rm -f "$f" ;; esac
done < "$MANIFEST"
$SUDO rm -f "$MANIFEST"
$SUDO rmdir --ignore-fail-on-non-empty "$PREFIX/lib/linosd/x86_64" "$PREFIX/lib/linosd/i686" \
    "$PREFIX/lib/linosd/lib" "$PREFIX/lib/linosd" "$PREFIX/share/linosd" 2>/dev/null || true
rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/autostart/linosd.desktop"
rm -f /dev/shm/linosd.* 2>/dev/null || true
[ $purge = 1 ] && rm -rf "${XDG_CONFIG_HOME:-$HOME/.config}/linosd"
echo ">> LinOSD removido de $PREFIX"
