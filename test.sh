#!/bin/bash
set -e  # Exit immediately if a command exits with a non-zero status

# Change to the directory where the script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

echo "=== Testing Voice Input Application ==="
echo "Working directory: $(pwd)"

# 1. Build the project
echo "Building project..."
mkdir -p build_test
cd build_test
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -- -j$(nproc)
cd ..

# 2. Run the headless test for auto-stop functionality

# Clean up before testing
echo "Cleaning temporary files..."
rm -f /tmp/stt-recording.m4a
rm -f /tmp/stt-transcription.txt

# Run the dedicated test executable
echo "Running auto-stop test..."
timeout 5s ./build_test/test_auto_stop

# 3. Verify file exists after test
echo "Verifying test results..."
if [ ! -f "/tmp/stt-recording.m4a" ]; then
    echo "ERROR: Test did not create the audio file"
    exit 1
fi

FILE_SIZE=$(stat -c%s "/tmp/stt-recording.m4a")
if [ "$FILE_SIZE" -eq 0 ]; then
    echo "ERROR: Test file is empty"
    exit 1
fi
echo "Auto-stop test file size: $FILE_SIZE bytes"

# 4. Test manual cleanup
echo "Testing cleanup functionality..."
rm -f /tmp/stt-recording.m4a

# 5. Test success message
echo "PASSED: Test completed successfully"