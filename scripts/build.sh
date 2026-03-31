#!/usr/bin/env bash
# Build standalone-example for Move (ARM64)
#
# Automatically uses Docker for cross-compilation if needed.
# Set CROSS_PREFIX to skip Docker (e.g., for native ARM builds).
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="standalone-example-builder"

# Check if we need Docker
if [ -z "$CROSS_PREFIX" ] && [ ! -f "/.dockerenv" ]; then
    echo "=== Standalone Example Build (via Docker) ==="
    echo ""

    # Build Docker image if needed
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        echo "Building Docker image (first time only)..."
        docker build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$REPO_ROOT"
        echo ""
    fi

    # Run build inside container
    echo "Running build..."
    docker run --rm \
        -v "$REPO_ROOT:/build" \
        -u "$(id -u):$(id -g)" \
        -w /build \
        "$IMAGE_NAME" \
        ./scripts/build.sh

    echo ""
    echo "=== Done ==="
    exit 0
fi

# === Actual build (runs in Docker or with cross-compiler) ===
CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"

cd "$REPO_ROOT"

echo "=== Building Standalone Example ==="
echo "Cross prefix: $CROSS_PREFIX"

# Create build directories
mkdir -p build
mkdir -p dist/standalone-example

# Compile standalone binary
echo "Compiling standalone binary..."
${CROSS_PREFIX}gcc -g -O3 \
    src/dsp/standalone.c \
    -o build/standalone \
    -Isrc -Isrc/lib \
    -lm

# Copy files to dist (use cat to avoid ExtFS deallocation issues with Docker)
echo "Packaging..."
cat src/module.json > dist/standalone-example/module.json
cat build/standalone > dist/standalone-example/standalone
chmod +x dist/standalone-example/standalone

# Create tarball for release
cd dist
tar -czvf standalone-example-module.tar.gz standalone-example/
cd ..

echo ""
echo "=== Build Complete ==="
echo "Output: dist/standalone-example/"
echo "Tarball: dist/standalone-example-module.tar.gz"
echo ""
echo "To install on Move:"
echo "  ./scripts/install.sh"
