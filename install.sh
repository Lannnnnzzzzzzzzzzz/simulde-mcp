#!/usr/bin/env bash
# install.sh — Installer Linux untuk SimulIDE 1.1.0 + MCP Server (port)
#
# Script ini menginstall dependensi build lalu meng-compile SimulIDE 1.1.0
# terpatch MCP dari source di folder simulide-1.1.0/ (sebelah script ini).
# Khusus distro keluarga Debian/Ubuntu. Cara pakai:
#
#   chmod +x install.sh
#   ./install.sh            # jumlah job make = jumlah core CPU
#   JOBS=1 ./install.sh     # batasi job (berguna di mesin RAM kecil)
#
# Pintu masuk build adalah simulide-1.1.0/build_XX/SimulIDE_Build.pro
# (bukan SimulIDE.pro di root source — file itu hanya di-include olehnya).
# Daftar paket Qt di bawah persis seperti yang tercatat di PORTING.md.

set -euo pipefail

log()  { printf '\n==> %s\n' "$*"; }
fail() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$SCRIPT_DIR/simulide-1.1.0"
BUILD_DIR="$SRC_DIR/build_XX"
BIN_DIR="$BUILD_DIR/executables/SimulIDE_1.1.0-SR1"
BIN="$BIN_DIR/simulide"
JOBS="${JOBS:-$(nproc)}"

# ---------------------------------------------------------------- cek distro
log "Tahap 1/4: Memeriksa distro"
[ -f /etc/os-release ] || fail "File /etc/os-release tidak ditemukan; script ini hanya untuk keluarga Debian/Ubuntu."
# shellcheck disable=SC1091
. /etc/os-release
ID_LOWER="$(printf '%s %s' "${ID:-}" "${ID_LIKE:-}" | tr '[:upper:]' '[:lower:]')"
case "$ID_LOWER" in
    *debian*|*ubuntu*) ;;
    *) fail "Distro terdeteksi: ${PRETTY_NAME:-tidak dikenal}. Script ini hanya mendukung keluarga Debian/Ubuntu." ;;
esac
echo "Distro OK: ${PRETTY_NAME:-$ID}"

[ -d "$SRC_DIR" ] || fail "Folder source tidak ditemukan: $SRC_DIR (pastikan install.sh dijalankan dari root paket ini)."
[ -f "$BUILD_DIR/SimulIDE_Build.pro" ] || fail "File proyek tidak ditemukan: $BUILD_DIR/SimulIDE_Build.pro"

# ------------------------------------------------------------- hak akses apt
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    command -v sudo >/dev/null 2>&1 || fail "Script butuh sudo untuk langkah apt, tapi sudo tidak terinstall. Jalankan sebagai root atau install sudo dulu."
    SUDO="sudo"
fi

# ------------------------------------------------------------- install paket
log "Tahap 2/4: Menginstall dependensi build (compiler + Qt 5 dev)"
PKGS=(
    build-essential
    qtbase5-dev
    libqt5svg5-dev
    libqt5serialport5-dev
    qtmultimedia5-dev
    qttools5-dev-tools
)
$SUDO apt-get update
$SUDO apt-get install -y "${PKGS[@]}" || fail "Instalasi paket gagal. Periksa koneksi/repositori apt, lalu jalankan ulang script ini."

command -v qmake >/dev/null 2>&1 || fail "qmake tidak ditemukan setelah instalasi paket — build tidak bisa dilanjutkan."
command -v make  >/dev/null 2>&1 || fail "make tidak ditemukan setelah instalasi paket — build tidak bisa dilanjutkan."
echo "qmake: $(qmake -v | tr '\n' ' ')"

# --------------------------------------------------------------------- build
log "Tahap 3/4: Meng-compile SimulIDE (qmake release, make -j$JOBS)"
echo "Catatan: build pertama bisa belasan menit. Di mesin RAM kecil gunakan JOBS=1."
cd "$BUILD_DIR"
qmake CONFIG+=release SimulIDE_Build.pro || fail "qmake gagal. Lihat pesan error qmake di atas."
make -j"$JOBS" || fail "Compile gagal di tahap make. Jalankan ulang dengan JOBS=1 untuk pesan error yang lebih berurutan dan pemakaian RAM lebih kecil."

# --------------------------------------------------------------- hasil akhir
log "Tahap 4/4: Memeriksa hasil build"
[ -x "$BIN" ] || fail "Binary hasil build tidak ditemukan di lokasi yang diharapkan: $BIN"

cat <<EOF

============================================================
BUILD BERHASIL
============================================================
Binary SimulIDE 1.1.0 + MCP:
  $BIN

Menjalankan (GUI biasa):
  cd "$BIN_DIR"
  ./simulide

Menjalankan dengan MCP server aktif (endpoint http://127.0.0.1:8723/mcp):
  cd "$BIN_DIR"
  SIMULIDE_MCP=1 ./simulide
  # port lain bila 8723 bentrok: SIMULIDE_MCP=1 SIMULIDE_MCP_PORT=8724 ./simulide

Menghubungkan Claude Code:
  claude mcp add --transport http simulide1 http://127.0.0.1:8723/mcp

Server MCP hanya mengikat 127.0.0.1 — klien dan SimulIDE wajib
di mesin yang sama. Detail teknis: PORTING.md.
EOF
