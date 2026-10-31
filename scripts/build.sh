#!/usr/bin/env bash
# Compila o LinOSD.
#   ./scripts/build.sh [--debug] [--with-32bit] [--clean] [-j N]
#
#   build/    aplicativo + hooks 64-bit
#   build32/  hooks 32-bit (opcional, para jogos 32-bit/Wine 32-bit)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
type=Release
with32=0
clean=0
jobs="$(nproc 2>/dev/null || echo 4)"

while [ $# -gt 0 ]; do
    case "$1" in
        --debug) type=Debug ;;
        --with-32bit) with32=1 ;;
        --clean) clean=1 ;;
        -j) jobs="$2"; shift ;;
        -j*) jobs="${1#-j}" ;;
        -h|--help) sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "opção desconhecida: $1" >&2; exit 1 ;;
    esac
    shift
done

cd "$ROOT"
if [ $clean = 1 ]; then rm -rf build build32; fi

echo ">> [64-bit] configurando ($type)"
cmake -S . -B build -DCMAKE_BUILD_TYPE="$type"
echo ">> [64-bit] compilando"
cmake --build build -j "$jobs"

if [ $with32 = 1 ]; then
    echo ">> [32-bit] compilando hooks"
    cmake -S . -B build32 -DCMAKE_BUILD_TYPE="$type" -DLINOSD_HOOK_ONLY=ON \
        -DCMAKE_CXX_FLAGS="-m32" -DCMAKE_SHARED_LINKER_FLAGS="-m32" -DCMAKE_EXE_LINKER_FLAGS="-m32"
    cmake --build build32 -j "$jobs"
fi

echo
echo ">> Build concluído:"
ls -1 build/linosd build/liblinosd_vk.so build/liblinosd_gl.so
[ $with32 = 1 ] && ls -1 build32/liblinosd_vk.so build32/liblinosd_gl.so
echo
echo "Testar sem instalar:  ./build/linosd"
echo "Instalar (Debian/Ubuntu/Mint): ./scripts/install.sh        (gera o .deb e instala com o apt)"
echo "Só para o usuário:             ./scripts/install.sh --user (em ~/.local)"
