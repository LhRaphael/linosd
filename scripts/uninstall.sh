#!/usr/bin/env bash
# Remove o LinOSD.
#
#   ./scripts/uninstall.sh              remove o pacote (apt) e qualquer instalação manual encontrada
#   ./scripts/uninstall.sh --purge      também apaga a configuração (~/.config/linosd)
#   ./scripts/uninstall.sh --prefix DIR remove apenas a instalação manual em DIR
set -euo pipefail

purge=0
prefixes=()
manual_only=0
while [ $# -gt 0 ]; do
    case "$1" in
        --purge) purge=1 ;;
        --user) prefixes=("$HOME/.local"); manual_only=1 ;;
        --system) prefixes=(/usr/local); manual_only=1 ;;
        --prefix) prefixes=("$2"); manual_only=1; shift ;;
        --manual-only) manual_only=1 ;;   # uso interno (install.sh)
        -h|--help) sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "opção desconhecida: $1" >&2; exit 1 ;;
    esac
    shift
done
[ ${#prefixes[@]} -eq 0 ] && prefixes=("$HOME/.local" /usr/local)

SUDO=""
[ "$(id -u)" -ne 0 ] && SUDO=sudo
removed=0

pkill -u "$(id -u)" -x linosd 2>/dev/null || true

# Pacote do sistema
if [ $manual_only = 0 ] && dpkg-query -W -f='${Status}' linosd 2>/dev/null | grep -q "install ok installed"; then
    pkgs=(linosd)
    if dpkg-query -W -f='${Status}' linosd-hooks32:i386 2>/dev/null | grep -q "install ok installed"; then
        pkgs+=(linosd-hooks32:i386)
    fi
    if [ $purge = 1 ]; then action=purge; else action=remove; fi
    echo ">> Removendo os pacotes: ${pkgs[*]}"
    $SUDO apt-get "$action" -y "${pkgs[@]}"
    removed=1
fi

# Instalações manuais
for PREFIX in "${prefixes[@]}"; do
    MANIFEST="$PREFIX/share/linosd/installed-files.txt"
    [ -f "$MANIFEST" ] || continue
    S="$SUDO"
    case "$PREFIX" in "$HOME"/*) S="" ;; esac
    echo ">> Removendo instalação manual em $PREFIX"
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        case "$f" in /etc/*) sudo rm -f "$f" ;; *) $S rm -f "$f" ;; esac
    done < "$MANIFEST"
    $S rm -f "$MANIFEST"
    for d in "$PREFIX/lib/linosd/lib" "$PREFIX/lib/linosd/x86_64" "$PREFIX/lib/linosd/i686" \
             "$PREFIX/lib/linosd" "$PREFIX/share/linosd"; do
        $S rmdir "$d" 2>/dev/null || true
    done
    removed=1
done

if [ $manual_only = 0 ]; then
    # Regra opcional do RAPL (criada por install.sh --enable-cpu-power)
    [ -f /etc/tmpfiles.d/linosd-rapl.conf ] && sudo rm -f /etc/tmpfiles.d/linosd-rapl.conf
    rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/autostart/linosd.desktop"
    rm -f /dev/shm/linosd.* 2>/dev/null || true
    [ $purge = 1 ] && rm -rf "${XDG_CONFIG_HOME:-$HOME/.config}/linosd"
fi

if [ $removed = 1 ]; then echo ">> LinOSD removido."; else echo "Nenhuma instalação do LinOSD encontrada."; fi
