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

## Checking User Interface Performance
1. **Instant Launch**: UI should appear immediately; ensure there's no delay between launching and UI appearing
2. **UI Responsiveness**: Interface should remain responsive during audio initialization (background)
3. **Exit Commands**: 
   - Press **Enter** or **Space** to save recording and exit
   - Press **Esc** to cancel recording (deletes file) and exit

## Key Improvements Verification
1. **MP3 Encoding**: The output file should be a valid MP3 file format
2. **Instant UI**: Interface should appear instantly, with audio initialization happening in background
3. **Keyboard Controls**: Enter and Escape keys should properly save/cancel recordings
4. **Error Handling**: Check logs for proper initialization and finalization messages

# **Audio Recorder Application (C++/Qt) – Requirements & Specifications**

## **Overview**
The **Audio Recorder Application** captures audio from the microphone and saves it as an **MP3-encoded file**. It is built using **C++ and Qt 5**, prioritizing **instant launch, low overhead, structured design, and flexibility**.  
Instead of relying on **FFmpeg**, the application uses **PortAudio or RtAudio** for **direct audio capture**, allowing **low-latency recording, real-time UI feedback, and precise control** over the process.

---

## **System & Dependencies**

- **Platform**: Linux (tested on Ubuntu 22.04).
- **Dependencies**:
    - **Qt 5** for GUI and process management.
    - **PortAudio or RtAudio** for real-time audio capture.
    - **libmp3lame** for MP3 encoding.
    - **C++ compiler** (CMake/qmake).

---

## **Functional Requirements**

- **Instant Launch**:
    - The UI **must appear immediately** upon startup.
    - **Audio initialization can be asynchronous** but must not delay UI appearance.

- **Audio Capture**:
    - The application **records directly using PortAudio/RtAudio** instead of spawning an external process (FFmpeg).
    - Captured audio is encoded using **libfdk-aac** (or another AAC encoder) and written **directly to a file**.
    - **No intermediate file storage** (e.g., raw WAV) should be used unless strictly necessary.

- **Recording Stop Conditions**:
    - **Manual Stop**: Pressing **Enter** stops recording, saves the file, and exits.
    - **Cancel Operation**: Pressing **Esc** cancels recording and exits without saving.
    - **Timeout Stop**: If the user passes a timeout value via `--timeout <milliseconds>`, the program **stops recording gracefully after the given duration**, then saves the file.

- **Configurable Parameters**:
    - Configurations are defined in **`config.h`** (no external config files like `.json` or `.ini`).
    - Example `config.h`:
      ```cpp
      #ifndef CONFIG_H
      #define CONFIG_H
  
      constexpr auto OUTPUT_FILE_PATH = "/tmp/audio_recording.mp3";
      constexpr int DEFAULT_TIMEOUT = 0;  // Default: no timeout
      constexpr int SAMPLE_RATE = 44100;  // Standard CD-quality sample rate
      constexpr int NUM_CHANNELS = 1;     // Mono recording
      constexpr int ENCODER_BITRATE = 128000; // 128 kbps MP3 encoding
  
      #endif // CONFIG_H
      ```

- **File Cleanup**:
    - On **startup**, any leftover `/tmp/audio_recording.mp3` should be removed.
    - **On exit**, the program does **not** remove any files—just gracefully stops the recording process.

- **Real-time UI Feedback**:
    - **Volume Meter**: Displays microphone input levels dynamically.
    - **Recording Info**: Shows elapsed time and file size.
    - **Background Change During Initialization**: The UI background must visually indicate when initialization is in progress.

- **Logging**:
    - The program must **log every single step and error**, regardless of console spam.
    - **Timers must not trigger logs** (logs should only be event-driven).
    - **Logging Format**:
        - `[DEBUG]` for every step.
        - `[INFO]` for major events (e.g., recording start/stop, file saved).
        - `[ERROR]` for unexpected failures.
    - Example logs:
      ```bash
      [INFO] Application started  
      [DEBUG] Initializing audio capture  
      [DEBUG] PortAudio stream opened successfully  
      [INFO] Recording started: /tmp/audio_recording.mp3  
      [INFO] Recording stopped, file saved successfully  
      [ERROR] Audio device unavailable!  
      ```

- **Graceful Exit Handling**:
    - The program must **always exit cleanly**, including cases where an external signal (SIGTERM, SIGINT) is sent.
    - Ensure **PortAudio/RtAudio streams are properly closed**.
    - Handle termination signals (`SIGINT`, `SIGTERM`) gracefully without leaving corrupted files or zombie processes.
    - Example:
      ```cpp
      QObject::connect(qApp, &QCoreApplication::aboutToQuit, [&]() {
          stopRecording();
      });
      ```

---

## **Implementation Guidelines**

### **Audio Capture**
- **Use PortAudio or RtAudio** instead of FFmpeg.
- **Configure an audio buffer** for low-latency real-time recording.
- **Use a worker thread for audio processing** to keep the UI responsive.
- Example of **PortAudio stream initialization**:
  ```cpp
  PaStream *stream;
  Pa_OpenDefaultStream(&stream, 1, 0, paInt16, SAMPLE_RATE, 256, audioCallback, nullptr);
  Pa_StartStream(stream);
  ```

### **Timeout Argument Handling**
- The program **must accept an optional timeout argument**:
  ```bash
  ./audio_recorder --timeout 5000  # Stop recording after 5 seconds
  ```
- The timeout must:
    - Start a **QTimer** to track elapsed time.
    - **Stop recording when time is up** while ensuring that audio data is written and finalized.
- Example implementation:
  ```cpp
  QTimer::singleShot(timeoutValue, [&]() {
      stopRecording();
  });
  ```

### **File Writing**
- **Write directly to the output file** without creating temporary audio buffers.
- **Verify the file** after stopping the recording:
  ```cpp
  if (QFile::exists(OUTPUT_FILE_PATH) && QFileInfo(OUTPUT_FILE_PATH).size() > 0) {
      qInfo() << "[INFO] Recording saved successfully.";
  } else {
      qWarning() << "[WARNING] Recording file may be corrupted!";
  }
  ```

### **Threading & UI Responsiveness**
- **Audio processing should run in a separate thread** from the UI.
- The **Qt main thread should handle UI updates only**.
- Example threading approach:
  ```cpp
  QFuture<void> future = QtConcurrent::run(startRecording);
  ```

### **Graceful Termination**
- The program **must handle process termination signals**:
    - If killed via `SIGINT`, `SIGTERM`, or a crash, it **must finalize the audio file properly**.
    - **Ensure no zombie processes remain** (i.e., clean up PortAudio streams properly).
    - Example **handling SIGTERM**:
      ```cpp
      void handleSignal(int signal) {
          qInfo() << "[INFO] Received termination signal: " << signal;
          stopRecording();
          QCoreApplication::quit();
      }
      ```

---

## **Development & Project Structure**

- **Test-Driven Development (TDD)**:
    - The project **must be structured for easy expansion and testing**.
    - Core components must have **unit tests**.
    - Use **Qt Test Framework** for Qt-specific testing.

- **Modular Codebase**:
    - The project must be **structured for extendability**, with dedicated directories for potential future features.
    - Example directory structure:
      ```
      /src
        /core      # Core application logic
        /ui        # UI components
        /tests     # Unit and integration tests
        /config    # Configuration files
      ```  