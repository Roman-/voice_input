# Voice Input Application (C++/Qt) – Requirements & Specifications

**Table of Contents**  
1. [Overview & Objective](#overview--objective)  
2. [Scope & Constraints](#scope--constraints)  
3. [System Requirements](#system-requirements)  
4. [Functional Requirements](#functional-requirements)  
   - [4.1 Recording & File Handling](#41-recording--file-handling)  
   - [4.2 Real-time Visualizations](#42-real-time-visualizations)  
   - [4.3 Transcription Integration (OpenAI Whisper)](#43-transcription-integration-openai-whisper)  
   - [4.4 Error Handling & Logging](#44-error-handling--logging)  
5. [UI/UX Requirements (Qt)](#uiux-requirements-qt)  
6. [Implementation Guidelines](#implementation-guidelines)  
7. [Appendix / Additional Notes](#appendix--additional-notes)

---

## 1. Overview & Objective

The “Voice Input Application” will capture audio from the user’s microphone, transcribe it via **OpenAI Whisper**, and present the transcribed text. The implementation will be in **C++** using **Qt** for the GUI layer to address:

- **Faster Startup**: Reduce or eliminate the noticeable delay at launch.  
- **Better Performance**: Gain low-level control if needed (e.g., concurrency, minimal overhead).  

This specification outlines the required features, expected behaviors, and constraints. It also provides high-level guidance for the C++ engineer to avoid pitfalls experienced in the Python prototype.

---

## 2. Scope & Constraints

1. **Platform**:  
   - Primarily **Ubuntu 22.04** or similar Linux environment.  
   - The system must have **Qt** installed (version 5.15).  
   - **FFmpeg** is expected to be installed system-wide for encoding to M4A (AAC).  

2. **Dependencies**:  
   - **OpenAI** Whisper is accessed via the OpenAI REST API. The application must rely on the user’s environment variable `OPENAI_API_KEY`.  
   - If the `OPENAI_API_KEY` variable is missing or invalid, the application shall present a friendly error message.  

3. **Constraints**:  
   - **Minimize Startup Lag**: The UI should appear instantly or near-instantly. Delayed or asynchronous initialization of audio features is acceptable if it can reduce perceived launch time.  

4. **Audio**:  
   - The recorded audio shall be **AAC**-encoded and saved as **`/tmp/voice_input.m4a`**.  
   - There is a **35 MB** file size limit. If the limit is exceeded, recording stops automatically and is *not* sent to STT.  

---

## 3. System Requirements

1. **Operating System**: Ubuntu 22.04 (or a closely compatible distribution).  
2. **Libraries/Tools**:  
   - Qt 5 for the GUI.  
   - FFmpeg for real-time audio capture and encoding.  
   - A C++ compiler and build system (e.g., **CMake** or **qmake**).  

3. **Environment Variable**:  
   - `OPENAI_API_KEY` must be set for transcription to function.  

---

## 4. Functional Requirements

### 4.1 Recording & File Handling

1. **Automatic Recording**  
   - When the application starts, it should _quickly_ show the main window.  
   - Recording should begin immediately or after a brief, clearly indicated “initialization” step.  

2. **File Format**  
   - The recorded file is always in `.m4a` format with **AAC** encoding.  
   - The file is saved to **`/tmp/voice_input.m4a`**.  

3. **File Size Limit**  
   - If the recording file size reaches **35 MB**, stop recording automatically.  
   - Do **not** send the file to the transcription API in this scenario. Display an appropriate message to the user.  

4. **File Cleanup**  
   - On application startup, remove any leftover `/tmp/voice_input.m4a` and `/tmp/voice_input.txt` from previous runs.  
   - No further cleanup is required after exit.  

5. **Text Output**  
   - On successful transcription, save the resulting text to **`/tmp/voice_input.txt`**

### 4.2 Real-time Visualizations

1. **Volume Meter**  
   - Display an indicator (e.g., a horizontal progress bar) reflecting the current microphone volume level in real-time.  
   - An RMS-based or peak-based approach is acceptable.  

2. **Duration & File Size**  
   - Display the current recording duration (in seconds) as time elapses.  
   - Continuously display the updated file size (e.g., in bytes or MB).  

### 4.3 Transcription Integration (OpenAI Whisper)

1. **API Key**  
   - Read from `OPENAI_API_KEY`. If not present, show a UI error and prompt to retry or quit.  

2. **API Parameters**  
   - Model: the official “latest” Whisper model name. (Currently `whisper-1` or as updated by OpenAI’s docs.)  
   - Temperature: `0.1`  
   - Language: `en`  
   - Response format: `text`  

3. **Transcription Flow**  
   - On “Send” or pressing **Enter**:  
     1. Stop recording.  
     2. Upload the `.m4a` file to OpenAI’s Whisper.  
     3. Upon success, save text to `/tmp/voice_input.txt`, and exit.  
     4. If an error occurs (network, bad key, etc.), show an error message and provide a **“Submit Again”** button to retry the same file once more.  
     5. If the second attempt also fails, exit gracefully.  

### 4.4 Error Handling & Logging

1. **Error Visibility**  
   - Show high-level user-friendly messages in the GUI.  
   - Provide detailed error logs to `stderr` or a console for troubleshooting.  

2. **Retries**  
   - Users get exactly one chance to re-submit the same file if a transcription request fails.  

3. **Escape Key / Cancel**  
   - Immediately cancels everything (discard the audio file and any partial state) and closes the application.  

---

## 5. UI/UX Requirements (Qt)

1. **GUI Layout**  
   - **Volume Bar**: A clear visual (e.g., QProgressBar) indicating volume.  
   - **Status/Info** area showing duration (in seconds) and file size.  
   - **Send** button (and Enter key) stops recording and initiates STT.  
   - **Stop** button stops recording without sending.  
   - **Submit Again** button appears only after a failed transcription attempt.  
   - **Error/Status messages** displayed as needed.  

2. **Responsiveness & Concurrency**  
   - The main window should appear quickly.  
   - If needed, heavy tasks (e.g., setting up FFmpeg, establishing network connections) could be initialized in a background thread, but ensure that once the user speaks, audio is indeed being recorded.  
   - The UI must remain responsive during recording.  

---

## 6. Implementation Guidelines

Below are some optional hints/notes for the C++ developer to avoid known pitfalls:

1. **Instant Launch**  
   - Show the **QMainWindow** (or **QWidget**) first. If initialization for FFmpeg or other libraries takes time, consider deferring or doing it asynchronously so the user sees the UI immediately.  
   - Alternatively, you can partially initialize the audio capture in a background thread and show the main window instantly. Just be sure to handle any “recording not ready yet” states gracefully in the UI.  

2. **FFmpeg Command**  
   - A typical FFmpeg command for AAC on Linux with PulseAudio might be:  
     ```bash
     ffmpeg -y -f pulse -i default -acodec aac -b:a 128k /tmp/voice_input.m4a
     ```  
   - You can launch it via `QProcess` in C++.  
   - Redirect or discard stderr/stdout to avoid spamming the console, unless you need to parse ffmpeg logs in real-time.  

3. **Volume Meter**  
   - You can either:  
     - Run a small **separate** audio capture in parallel (e.g., via **QtMultimedia** or **PortAudio** or **ALSA** APIs) to compute RMS.  
     - Or parse FFmpeg’s real-time logs if you run FFmpeg with a more verbose level (though that’s more complex).  
   - Updating the volume bar every ~100–200 ms is usually sufficient.  

4. **File Size Monitoring**  
   - Periodically (e.g., a QTimer firing every 200 ms) check the size of `/tmp/voice_input.m4a` with standard file I/O.  
   - If it reaches **35 MB**, stop the FFmpeg process and display a message (“Max size reached”).  

5. **Transcription**  
   - Use a standard **HTTP** library (e.g., Qt Network, libcurl) to POST the file to OpenAI.  
   - Make sure to set the appropriate multipart/form-data boundaries, model, temperature, language, etc.  
   - Parse the plain text response on success.  
   - If an error is returned, parse the JSON error body (if any) and present a user-friendly message.  

6. **Clipboard**  
   - For Qt 5 or 6, you can use `QApplication::clipboard()->setText(...)`.  

7. **One-time Retry**  
   - If STT fails, enable a **“Submit Again”** button. If that fails again, exit.  
   - Provide advanced error logs (e.g., network error, HTTP status code, etc.) to the console or some debug log.  

8. **Cleanup**  
   - On launch, remove `/tmp/voice_input.m4a` and `/tmp/voice_input.txt` if they exist.  
   - On exit, you may leave the files or remove them—**the specification only requires** that we remove them at next launch.  

---

## 7. Appendix / Additional Notes

1. **Performance Considerations**  
   - C++/Qt is expected to have less overhead than the Python variant. However, **FFmpeg** invocation time is usually the same across languages. If you need “record from the first millisecond,” you might consider spawning FFmpeg **just before** showing the UI, or maintain a small buffer in memory if that’s critical.  
   - Asynchronous calls to OpenAI can be handled via Qt’s `QNetworkAccessManager` or a dedicated HTTP client library.  

2. **Internationalization**  
   - If you want multi-language support, consider letting the user specify the `language` parameter in the UI or environment variable.  

3. **Security**  
   - The `OPENAI_API_KEY` is stored in an environment variable. Do not log the key or store it in plain text.  
   - For best security, consider masking the key in debug logs.  

4. **Future Expandability**  
   - The same architecture can be extended to implement advanced features (e.g., multi-language switching, advanced error reporting).  
