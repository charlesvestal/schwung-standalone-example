#!/bin/bash
# Install standalone-example module to Move
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$REPO_ROOT"

if [ ! -d "dist/standalone-example" ]; then
    echo "Error: dist/standalone-example not found. Run ./scripts/build.sh first."
    exit 1
fi

echo "=== Installing Standalone Example ==="

# Deploy to Move - tools subdirectory
echo "Copying module to Move..."
ssh ableton@move.local "mkdir -p /data/UserData/schwung/modules/tools/standalone-example"
scp -r dist/standalone-example/* ableton@move.local:/data/UserData/schwung/modules/tools/standalone-example/

# Set permissions so Module Store can update later
echo "Setting permissions..."
ssh ableton@move.local "chmod -R a+rw /data/UserData/schwung/modules/tools/standalone-example"
ssh ableton@move.local "chmod +x /data/UserData/schwung/modules/tools/standalone-example/standalone"

echo ""
echo "=== Install Complete ==="
echo "Module installed to: /data/UserData/schwung/modules/tools/standalone-example/"
echo ""
echo "Access via Shadow UI: Tools > Standalone Example"
