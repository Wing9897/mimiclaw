#!/bin/sh
# Convert image to RGB565 C headers for MimiClaw LCD
# Generates both portrait (172x320) and landscape (320x172)
# Usage: ./convert.sh your_image.png [name]
# Example: ./convert.sh banner.png logo

if [ -z "$1" ]; then
    echo "Usage: ./convert.sh <image_file> [name]"
    echo "Example: ./convert.sh banner.png logo"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
python3 "$SCRIPT_DIR/convert.py" "$@"
