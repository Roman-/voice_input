# Voice Input Recorder

**macOS only** - Audio recording and transcription tool with global hotkey support.

## Installation

### Prerequisites

```bash
# Install dependencies via Homebrew
brew install qt portaudio lame cmake
```

### Build

```bash
mkdir -p build
cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
```

### Run

```bash
# Launch from terminal to see logs
./build/romans_voice_input.app/Contents/MacOS/romans_voice_input
```

## Usage

### Starting/Stopping Recording

- **Global Hotkey**: `Option+Space` to start/stop recording
- **Signal**: `kill -SIGUSR1 <PID>` to trigger recording  
- **Menu Bar**: System tray icon for microphone selection and controls

### Window Visibility

The application provides flexible window visibility control:

- **"Always Show Window" checkbox** in the system tray menu (checked by default)
  - When **checked**: Window is always visible in top-right corner, showing recording status and volume meter
  - When **unchecked**: Window never appears, tray icon color provides visual feedback
- Window accepts keyboard input when visible (press `Esc` to cancel recording)
- Closing the window (clicking X) automatically unchecks "Always Show Window"
- When not recording, window shows "Ready - waiting for signal" status
- Tray icon color indicates state: grey (ready), red (recording), yellow (processing)

### Recording Workflow

1. Launch the application (window visible by default in top-right corner)
2. Press `Option+Space` or send SIGUSR1 to start recording
3. Speak your text (window shows volume meter if visible)
4. Press `Option+Space` again to stop recording
5. Wait for transcription (status updates shown if window visible)
6. Transcribed text automatically pastes into the active application
7. Window remains visible showing "Ready - waiting for signal"

## Configuration

Set environment variable for API key:
```bash
export GROQ_API_KEY="your-api-key-here"
# or
export OPENAI_API_KEY="your-api-key-here"
```

Edit `src/config/config.h` to configure API endpoint, model, and audio settings.

---

## Implementation Details

### Simplified Window Management

The application uses a **user-controlled visibility model** with no complex show/hide logic:

**Key Implementation:**
- Window flags: `Qt::Window | Qt::WindowStaysOnTopHint`
- Single source of truth: `m_alwaysShowWindow` boolean controlled by tray menu checkbox
- Window positioned once at startup (top-right corner), never repositioned
- No `activateWindow()`, `raise()`, or focus manipulation calls
- Window accepts keyboard input when visible (normal Qt behavior)

**Window States:**
- **Always Visible**: Window shows recording status, volume meter, and "Ready" state
- **Always Hidden**: Recording works normally, tray icon provides visual feedback
- Volume bar automatically hidden when not recording

**Benefits:**
- Simple, predictable behavior
- User controls window visibility via tray menu
- No focus stealing issues
- Works reliably across macOS versions
- Keyboard shortcuts (Esc) work when window is visible

**Files Modified:**
- `src/ui/mainwindow.h`: Added `m_showWindowAction` and `m_alwaysShowWindow`
- `src/ui/mainwindow.cpp`: Removed all dynamic show/hide logic, simplified event handlers
- `main.cpp`: Removed show() calls from signal handler

