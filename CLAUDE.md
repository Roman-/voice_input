# How to Verify Application Changes

## Building the Application
```bash
cd build
cmake ..
make -j16
```

## Testing the Application
1. **Run Unit Tests**:
   ```bash
   cd build
   ./test_recorder       # Tests audio recorder core functionality
   ./test_file_verification  # Tests file format and timeout feature
   ```

2. **Manual Testing**:
   ```bash
   cd build
   ./audio_recorder --timeout 3000   # Records for 3 seconds
   ```

3. **Verify Recorded File**:
   ```bash
   # Check file format
   file /tmp/audio_recording.mp3  # Should show: "MPEG MPEG-1 Audio Layer 3, mono, 44.1 kHz"
   
   # Play recorded file
   vlc /tmp/audio_recording.mp3

   # View file details
   hexdump -C /tmp/audio_recording.mp3 | head -20  # Should start with FF (MP3 frame sync or ID3 tag)
   ```
