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
   file /tmp/audio_recording.m4a  # Should show: "MPEG ADTS, AAC, v4 LC, 44.1 kHz, monaural"
   
   # Play recorded file
   vlc /tmp/audio_recording.m4a

   # View file details
   hexdump -C /tmp/audio_recording.m4a | head -20  # Should start with FF F1 (ADTS header)
   ```
