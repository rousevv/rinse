#!/bin/bash
# install.sh - Install rinse to /usr/local/bin

# Strict mode: Exit on error, undefined vars, and pipe failures
set -euo pipefail

# --- Configuration ---
REPO_URL="https://github.com/RousevGH/rinse"
# Create a unique temp directory using the script's PID
TEMP_DIR="/tmp/rinse-install-$$"
INSTALL_DIR="/usr/local/bin"
CONFIG_DIR="$HOME/.config/rinse"

# --- Colors ---
readonly GREEN='\033[0;32m'
readonly RED='\033[0;31m'
readonly NC='\033[0m' # No Color

# --- Helper Functions ---
info() {
    echo -e "${GREEN}=== rinse installer ===${NC}"
    echo ""
}

success() {
    echo -e "${GREEN}✓${NC} $1"
}

error() {
    echo -e "${RED}Error:${NC} $1" >&2
}

# Cleanup function to remove temp files on exit or error
cleanup() {
    if [ -d "$TEMP_DIR" ]; then
        # cd to / to avoid "device or resource busy" errors if we are currently inside the dir
        cd /
        rm -rf "$TEMP_DIR"
    fi
}

# Register the cleanup function to run when the script exits
trap cleanup EXIT

# --- Start Installation ---
info

# Check for required tools
if ! command -v git &> /dev/null; then
    error "git is required but not installed."
    exit 1
fi

if ! command -v g++ &> /dev/null; then
    error "g++ is required but not installed."
    exit 1
fi

# Create temp directory
mkdir -p "$TEMP_DIR"

# Clone repository
echo "[1/5] Cloning repository..."
if git clone --depth 1 "$REPO_URL" "$TEMP_DIR" 2>/dev/null; then
    cd "$TEMP_DIR"
else
    # If clone fails, check if we're running from the repo locally
    if [ -f "rinse.cpp" ] || [ -f "rinse_latest.cpp" ]; then
        echo "    Clone failed, using local directory..."
        TEMP_DIR="."
        cd "$TEMP_DIR"
    else
        error "Failed to clone repository and no local source found."
        exit 1
    fi
fi

# Find the source file
echo "[2/5] Locating source file..."
if [ -f "rinse_latest.cpp" ]; then
    SOURCE_FILE="rinse_latest.cpp"
elif [ -f "rinse.cpp" ]; then
    SOURCE_FILE="rinse.cpp"
else
    error "No source file found (expected rinse.cpp or rinse_latest.cpp)."
    exit 1
fi

echo "    Found: $SOURCE_FILE"

# Build binary
echo "[3/5] Building rinse..."
if g++ -std=c++17 -O3 "$SOURCE_FILE" -o rinse; then
    success "Build successful"
else
    error "Compilation failed."
    exit 1
fi

# Install binary
echo "[4/5] Installing to $INSTALL_DIR..."

# Check if we need sudo
if [ -w "$INSTALL_DIR" ]; then
    # We have write permissions, run normally
    cp rinse "$INSTALL_DIR/rinse"
    chmod +x "$INSTALL_DIR/rinse"
else
    # We do NOT have write permissions, use sudo
    echo "    Root privileges required for installation."
    sudo cp rinse "$INSTALL_DIR/rinse"
    sudo chmod +x "$INSTALL_DIR/rinse"
fi

success "Installed: $INSTALL_DIR/rinse"

# Create config directory
echo "[5/5] Setting up configuration..."
mkdir -p "$CONFIG_DIR"

# Install default config if available
if [ -f "rinse.conf" ]; then
    if [ ! -f "$CONFIG_DIR/rinse.conf" ]; then
        cp rinse.conf "$CONFIG_DIR/rinse.conf"
        success "Created: $CONFIG_DIR/rinse.conf"
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
        success "Created default config: $CONFIG_DIR/rinse.conf"
    fi
fi

echo ""
echo "Installation complete!"
echo ""
echo "Usage:"
echo "  rinse <package>      Install a package"
echo "  rinse update         Update system"
echo "  rinse --help         Show all commands"
echo ""
echo "Config: $CONFIG_DIR/rinse.conf"
