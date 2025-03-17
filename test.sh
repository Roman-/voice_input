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

# 2. Remove any existing temp files
echo "Cleaning temporary files..."
rm -f /tmp/stt-recording.m4a
rm -f /tmp/stt-transcription.txt

# 3. Run with auto-stop after 1 second (1000ms)
echo "Running app with 1 second auto-stop..."
# Use timeout to ensure the test doesn't hang
timeout 5s ./build_test/VoiceInputApp --force-stop-after 1 &
APP_PID=$!

# 4. Wait for the app to finish (should auto-stop after 1 second)
echo "Waiting for auto-stop..."
sleep 3

# Check if the app is still running
if ps -p $APP_PID > /dev/null; then
    echo "ERROR: App still running after auto-stop time elapsed"
    kill $APP_PID
    exit 1
fi

# 5. Check that the audio file exists and has content
echo "Verifying output..."
if [ ! -f "/tmp/stt-recording.m4a" ]; then
    echo "ERROR: Audio file was not created"
    exit 1
fi

# Check file size (should be > 0)
FILE_SIZE=$(stat -c%s "/tmp/stt-recording.m4a")
if [ "$FILE_SIZE" -eq 0 ]; then
    echo "ERROR: Audio file is empty"
    exit 1
fi

echo "File size: $FILE_SIZE bytes"
echo "PASSED: Test completed successfully"