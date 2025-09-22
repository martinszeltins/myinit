#!/bin/bash

# Check if a C source file is provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 <source.c>"
    echo "Example: $0 myinit.c"
    exit 1
fi

SOURCE_FILE="$1"

# Check if the source file exists
if [ ! -f "$SOURCE_FILE" ]; then
    echo "Error: Source file '$SOURCE_FILE' not found"
    exit 1
fi

# Extract filename without extension for the output binary
BASENAME=$(basename "$SOURCE_FILE" .c)

# Compile the C program
echo "Compiling $SOURCE_FILE..."
gcc -static -O2 -Wall -o "$BASENAME" "$SOURCE_FILE"

# Check if compilation was successful
if [ $? -eq 0 ]; then
    echo "Compilation successful! Executable: $BASENAME"
else
    echo "Compilation failed!"
    exit 1
fi
