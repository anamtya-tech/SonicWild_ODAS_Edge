#!/usr/bin/env bash
# setup_runtime.sh — Instantiate ODAS runtime config files from templates.
#
# Usage:
#   ./scripts/setup_runtime.sh [--odas-dir PATH] [--output-dir PATH]
#
# Defaults:
#   --odas-dir    Directory of this repo (auto-detected)
#   --output-dir  ~/sodas  (conventional working directory)
#
# The working directory is separate from the repo so that generated classifier
# logs, raw session recordings and live JSON files are never accidentally
# committed.  Set ODAS_WORKING_DIR to override ~/sodas.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"

ODAS_DIR="${ODAS_DIR:-$REPO_DIR}"
CHATAK_GUI_DIR="${CHATAK_GUI_DIR:-$HOME/ChatakGUI}"
OUTPUT_DIR="${ODAS_WORKING_DIR:-$HOME/sodas}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --odas-dir)   ODAS_DIR="$2";   shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

echo "=== ODAS runtime config setup ==="
echo "  Repo dir    : $ODAS_DIR"
echo "  GUI dir     : $CHATAK_GUI_DIR"
echo "  Output dir  : $OUTPUT_DIR"
echo ""

mkdir -p "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR/ClassifierLogs"   # ODAS writes .bin and session JSON here

TEMPLATE_DIR="$REPO_DIR/config/runtime"

for template in "$TEMPLATE_DIR"/*.cfg.template; do
    filename="$(basename "${template%.template}")"
    dest="$OUTPUT_DIR/$filename"
    sed \
        "s|\${ODAS_DIR}|$ODAS_DIR|g; \
         s|\${CHATAK_GUI_DIR}|$CHATAK_GUI_DIR|g" \
        "$template" > "$dest"
    echo "  Written: $dest"
done

echo ""
echo "Done. To run ODAS:"
echo "  cd $ODAS_DIR/build"
echo "  ./bin/odaslive -c $OUTPUT_DIR/local_socket.cfg"
echo ""
echo "To stream a pre-recorded audio file instead of a live mic:"
echo "  python3 $REPO_DIR/scripts/vm_socket_emit.py \\"
echo "      --audio /path/to/render.raw --port 10000"
