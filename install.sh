#!/bin/bash
# install.sh - Install rinse to /usr/local/bin

set -e

REPO_URL="https://github.com/Rousevv/rinse"
TEMP_DIR="/tmp/rinse-install-$$"
INSTALL_DIR="/usr/local/bin"
CONFIG_DIR="$HOME/.config/rinse"

echo "=== rinse installer ==="
echo ""

# Check for required tools
if ! command -v git &> /dev/null; then
    echo "Error: git is required but not installed"
    exit 1
fi

if ! command -v g++ &> /dev/null; then
    echo "Error: g++ is required but not installed"
    exit 1
fi

# Clone repository
echo "[1/5] Cloning repository..."
git clone --depth 1 "$REPO_URL" "$TEMP_DIR" 2>/dev/null || {
    echo "Error: Failed to clone repository"
    echo "Falling back to local build..."
    
    # If clone fails, check if we're running from the repo
    if [ -f "rinse.cpp" ]; then
        TEMP_DIR="."
    else
        echo "Error: rinse.cpp not found"
        exit 1
    fi
}

cd "$TEMP_DIR"

# Find the source file
echo "[2/5] Locating source file..."
if [ -f "rinse_latest.cpp" ]; then
    SOURCE_FILE="rinse_latest.cpp"
elif [ -f "rinse.cpp" ]; then
    SOURCE_FILE="rinse.cpp"
else
    echo "Error: No source file found (expected rinse.cpp or rinse_latest.cpp)"
    exit 1
fi

echo "    Found: $SOURCE_FILE"

# Build binary
echo "[3/5] Building rinse..."
g++ -std=c++17 -O3 "$SOURCE_FILE" -o rinse || {
    echo "Error: Compilation failed"
    exit 1
}

echo "    Build successful"

# Install binary
echo "[4/5] Installing to $INSTALL_DIR..."
if [ -w "$INSTALL_DIR" ]; then
    cp rinse "$INSTALL_DIR/rinse"
else
    sudo cp rinse "$INSTALL_DIR/rinse"
fi

chmod +x "$INSTALL_DIR/rinse"
echo "    Installed: $INSTALL_DIR/rinse"

# Create config directory
echo "[5/5] Setting up configuration..."
mkdir -p "$CONFIG_DIR"

# Install default config if available
if [ -f "rinse.conf" ]; then
    if [ ! -f "$CONFIG_DIR/rinse.conf" ]; then
        cp rinse.conf "$CONFIG_DIR/rinse.conf"
        echo "    Created: $CONFIG_DIR/rinse.conf"
    else
        echo "    Config already exists, skipping"
    fi
else
    # Create minimal default config
    if [ ! -f "$CONFIG_DIR/rinse.conf" ]; then
        cat > "$CONFIG_DIR/rinse.conf" << 'EOF'
# rinse configuration file
# Lines starting with # are comments

# Keep build files after AUR installation
keep_build = false

# Send desktop notifications
notify = true

# Default time threshold for outdated packages
outdated_time = 6m
EOF
        echo "    Created default config: $CONFIG_DIR/rinse.conf"
    fi
fi

# Cleanup
if [ "$TEMP_DIR" != "." ]; then
    cd /
    rm -rf "$TEMP_DIR"
fi

echo ""
echo "✓ Installation complete!"
echo ""
echo "Usage:"
echo "  rinse <package>      Install a package"
echo "  rinse update         Update system"
echo "  rinse --help         Show all commands"
echo ""
echo "Config: $CONFIG_DIR/rinse.conf"
