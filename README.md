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

Control window visibility via **"Always Show Window"** checkbox in the system tray menu:
- **Checked (default)**: Window always visible in top-right corner showing status, volume meter
- **Unchecked**: Window hidden, tray icon color provides feedback (grey=ready, red=recording, yellow=processing)
- Closing window (X button) automatically unchecks "Always Show Window"

### Recording Workflow

1. Press `Option+Space` or `kill -SIGUSR1 <PID>` to start recording
2. Press again to stop and transcribe
3. Press `Esc` to cancel (only works while recording/transcribing)
4. Transcribed text automatically pastes into active application

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

