## TL;DR

* **Reuse your Qt/PortAudio codebase** on macOS, but make 4 focused changes:

    1. **Keep the audio stream warm** at login (launchd). Either keep it running with a small ring buffer (“pre‑roll”) or keep it opened/paused so `Pa_StartStream` is instant.
    2. **Move MP3 encoding out of the PortAudio callback** (no locks / no file I/O in the callback). Write raw PCM to a ring buffer in the callback; encode on a worker thread, or better yet **skip MP3** and upload **WAV/PCM** to Groq for even lower end‑to‑end latency.
    3. **Replace Linux‑only bits** (i3blocks, pkill) with a **menu bar status item + global hotkey** (QHotkey) and `launchd` to start at login. Keep the SIGUSR1 path too—it works on macOS if you run from Terminal.
    4. Add **Info.plist** keys for microphone permission and (optionally) request **Accessibility** for simulating ⌘V.

* If you want a smaller native stack, the other “most reliable” option is a **Swift + AVAudioEngine** menu bar app that’s always resident, with a ring buffer and pre‑roll. But you don’t *need* to rewrite—your Qt design ports nicely and gives you parity with Linux.

* A **Python** solution can be instant **only if** you keep it resident at login (rumps menu bar app or launchd + PyObjC). Spinning a new Python process on every keypress will keep biting you with cold‑start time.

---

## Why reuse your Qt code?

* **PortAudio on macOS** sits on Core Audio and is extremely low‑latency.
* Your code already does the “initialize once, run in background” pattern.
* Qt’s `QNetworkAccessManager` and your multipart code work the same.
* You get one codebase for Linux + mac.

The only place I’d tighten things is the **audio callback**: right now it takes a mutex, logs, encodes MP3, and writes to disk *inside* the callback. That’s risky for dropouts. On macOS (and in general), the callback must be near‑zero work: push bytes into a lock‑free ring buffer, signal a worker thread, and return.

---

## Architecture (macOS)

**Process model**

* A **LaunchAgent** starts your app at login.
* App runs as a **menu bar (NSStatusItem)** or a hidden agent (LSUIElement) with optional window like you have.
* A **global hotkey** (e.g., ⌥Space) toggles “capture now”. You can also keep your SIGUSR1 path for CLI control.

**Audio path (instant start)**

* **Initialize PortAudio** on launch.
* **Open the input stream** at the preferred device sample rate.
* **Start the stream** and continuously write to a **ring buffer** (or keep it opened/paused and call `Pa_StartStream` on demand; starting is typically ~sub‑10ms on modern macs, but pre‑roll makes it truly zero‑miss).
* Maintain **~250–500 ms pre‑roll** so you never miss the first syllable. When the hotkey is pressed, you copy pre‑roll + live audio to the “active” buffer/file.

**Encoding / upload**

* In the **worker thread**, read PCM from the ring buffer:

    * **Fastest path**: write a tiny **WAV** header + PCM and POST it to Groq (they accept WAV/PCM—no need to pay the MP3 encode latency).
    * If you keep MP3: encode on the worker thread (never in the callback). On M4 Max the cost is small, but removing it simplifies the hot path.

**Clipboard + paste**

* Copy transcription to the **NSPasteboard** (or fallback to `pbcopy`).
* To **simulate ⌘V** you must have Accessibility permission. Detect via `AXIsProcessTrustedWithOptions` and prompt once; use Quartz to post key events. (Or skip synthetic keystrokes entirely and just leave the text on the clipboard.)

---

## Minimal, targeted changes to your Qt code

### 1) Build on macOS (Homebrew + CMake)

```bash
# Deps
brew install cmake qt@6 portaudio lame pkg-config

# Build
export PATH="/opt/homebrew/opt/qt@6/bin:$PATH"
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)" ..
make -j
```

In CMake (high level):

```cmake
find_package(Qt6 COMPONENTS Core Gui Widgets Network REQUIRED)
find_path(PORTAUDIO_INCLUDE_DIR portaudio.h PATHS /opt/homebrew/include)
find_library(PORTAUDIO_LIBRARY portaudio PATHS /opt/homebrew/lib)
find_library(MP3LAME_LIBRARY mp3lame PATHS /opt/homebrew/lib)
include_directories(${PORTAUDIO_INCLUDE_DIR})
target_link_libraries(romans_voice_input PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network ${PORTAUDIO_LIBRARY} ${MP3LAME_LIBRARY})
set_target_properties(romans_voice_input PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/mac/Info.plist"
)
```

### 2) Info.plist (permissions + optional menu‑bar‑only)

`mac/Info.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Roman Voice Input</string>
  <key>CFBundleIdentifier</key><string>com.roman.voiceinput</string>
  <key>CFBundleVersion</key><string>1.0</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>

  <!-- Mic permission prompt -->
  <key>NSMicrophoneUsageDescription</key>
  <string>This app records short snippets to transcribe your speech.</string>

  <!-- Run as a background agent (no Dock icon). Comment out to show a standard app -->
  <key>LSUIElement</key><true/>
</dict>
</plist>
```

If you plan to synthesize ⌘V, you’ll also request Accessibility at runtime (no Info.plist key is required for that; you call `AXIsProcessTrustedWithOptions`).

### 3) Launch at login (LaunchAgent)

`~/Library/LaunchAgents/com.roman.voiceinput.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist ...>
<plist version="1.0">
<dict>
  <key>Label</key> <string>com.roman.voiceinput</string>
  <key>ProgramArguments</key>
  <array>
    <string>/Applications/Roman Voice Input.app/Contents/MacOS/romans_voice_input</string>
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ProcessType</key><string>Interactive</string>
  <key>EnvironmentVariables</key>
  <dict>
    <key>GROQ_API_KEY</key><string>YOUR_KEY_HERE</string>
    <key>RECORDER_AUTO_CLOSE_ON_ERROR_AFTER</key><string>0</string>
  </dict>
  <key>StandardOutPath</key><string>/tmp/roman_voiceinput.out</string>
  <key>StandardErrorPath</key><string>/tmp/roman_voiceinput.err</string>
</dict>
</plist>
```

Load it:

```bash
launchctl unload ~/Library/LaunchAgents/com.roman.voiceinput.plist 2>/dev/null || true
launchctl load ~/Library/LaunchAgents/com.roman.voiceinput.plist
```

You can still trigger with signals if you keep your existing SIGUSR1 path:

```bash
kill -USR1 $(pgrep romans_voice_input)
```

### 4) Global hotkey in Qt (QHotkey)

Add dependency: `QHotkey` (header‑only if you prefer). Example:

```cpp
#include <QHotkey>
// in MainWindow ctor
auto hk = new QHotkey(QKeySequence("Alt+Space"), true, this);
connect(hk, &QHotkey::activated, this, [this](){
    // bring window forward (if you show one)
    this->show();
    // same start logic you have in SIGUSR1 path
    if (m_recorder->isRecording()) return;
    m_recorder->startRecording();
    setFileStatus(STATUS_BUSY);
});
```

### 5) Make the callback “real‑time safe” with a ring buffer

**Do not** take a `QMutex`, encode MP3, or write `QFile` in the callback thread. Use PortAudio’s ring buffer (`pa_ringbuffer.h`) or a tiny lock‑free SPSC ring.

**Callback (very fast):**

```cpp
// Pseudocode inside audioCallback
auto* in = static_cast<const int16_t*>(inputBuffer);
PaUtil_WriteRingBuffer(&m_rb, in, framesPerBuffer);  // returns immediately
// Optionally compute a quick RMS and stash in an atomic<float> m_pendingVolume
return paContinue;
```

**Worker thread (QtConcurrent or QThread):**

```cpp
// On startRecording: open output file (WAV preferred) and start worker.
// Worker loop: repeatedly read from ring -> append to file or memory buffer.
// On stopRecording: flush, close file; if you used PCM, you already have a valid WAV; 
// if you still prefer MP3, encode here (not in callback).
```

**Why WAV instead of MP3?**

* Faster: no encoder work on the hot path.
* For short utterances, network cost is minor (e.g. 3s mono 44.1kHz 16‑bit ≈ 258 KB).
* Groq’s Whisper accepts WAV/PCM just fine.

**If you keep MP3**

* Keep LAME on a worker thread with a small queue.
* Keep `lame_encode_buffer_interleaved` off the callback.
* Keep file I/O off the callback.

### 6) Lower‑latency device open on macOS

Switch `Pa_OpenDefaultStream` to `Pa_OpenStream` so you can pass `suggestedLatency`:

```cpp
PaStreamParameters in;
in.device = Pa_GetDefaultInputDevice();
const PaDeviceInfo* dev = Pa_GetDeviceInfo(in.device);
in.channelCount = 1;
in.sampleFormat = paInt16;
in.suggestedLatency = dev->defaultLowInputLatency; // key for low latency
in.hostApiSpecificStreamInfo = nullptr;

double sampleRate = dev->defaultSampleRate; // avoid resampling if you can

PaError err = Pa_OpenStream(&m_stream, &in, nullptr, sampleRate,
                            256 /* frames */, paClipOff, &AudioRecorder::audioCallback, this);
```

Keep the stream **open** for the life of the app. Either:

* **Started**: write to a circular buffer at all times (gives you pre‑roll); or
* **Paused**: call `Pa_StartStream(m_stream)` on hotkey (still quick).

### 7) Replace Linux clipboard/paste with macOS calls

**Copy only** (no extra permission required):

```cpp
#include <QProcess>
QProcess::execute("/bin/sh", {"-c", QString("tr -d '\\n' < %1 | pbcopy").arg(TRANSCRIPTION_OUTPUT_PATH)});
```

**Simulate ⌘V** (requires Accessibility permission):

* At startup, call `AXIsProcessTrustedWithOptions` to prompt once.
* Post two events:

```cpp
CGEventRef cmdDown = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)0x37 /* cmd */, true);
CGEventSetFlags(cmdDown, kCGEventFlagMaskCommand);
CGEventRef vDown = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)9 /* v key */, true);
CGEventSetFlags(vDown, kCGEventFlagMaskCommand);
CGEventRef vUp   = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)9, false);
CGEventSetFlags(vUp, kCGEventFlagMaskCommand);
CGEventRef cmdUp = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)0x37, false);

CGEventPost(kCGHIDEventTap, cmdDown);
CGEventPost(kCGHIDEventTap, vDown);
CGEventPost(kCGHIDEventTap, vUp);
CGEventPost(kCGHIDEventTap, cmdUp);
CFRelease(cmdDown); CFRelease(vDown); CFRelease(vUp); CFRelease(cmdUp);
```

If you’d rather avoid Accessibility entirely, just leave the transcription on the clipboard and optionally bring the target app to front (AppleScript) for the user to paste.

---

## Small but important quality tweaks

* **Pre‑roll**: keep 250–500 ms in memory so you don’t chop first words.
* **Silence auto‑stop** (optional): if no signal for N ms after starting, stop automatically and transcribe.
* **Sample rate**: prefer the device’s default (often 48k on Macs) to skip resampling. The Groq API doesn’t mind.
* **App Nap / Energy**: an active audio stream prevents nap; if you ever switch to paused mode, consider `NSProcessInfo.beginActivity` to avoid sleeps during short post‑record encoding/uploads.
* **Permissions**: first run shows the microphone prompt; for synthetic paste you’ll get the Accessibility prompt on first attempt.

---

## If you *really* want Swift instead of Qt

This is the “native” equivalent and is also rock‑solid:

* **AVAudioEngine** with an input node **tapOnBus** writing to a ring buffer.
* **NSStatusItem** menu bar app (LSUIElement) with a global hotkey (RegisterEventHotKey).
* **URLSession** multipart POST to Groq.
* Use **AVAudioFile** to write WAV during capture (or memory + manual header).
* Same pre‑roll, same launchd agent.

You’ll get excellent latency, but you’d be duplicating code you already have.

---

## Could Python be “instant”?

Yes—**if it’s always resident** and pre‑initializes audio + hotkey. A lightweight recipe:

* **rumps** or **pyobjc** for a menu bar app + hotkey.
* **sounddevice** (PortAudio) for input with an always‑open stream writing into **queue.Queue** (or `collections.deque`).
* On hotkey, start a capture session that reads from the existing stream & queue; on stop, write WAV and POST with `requests`.
* Use `pbcopy` for clipboard, optional osascript for ⌘V.

**Do not** spawn Python only when you press the key; interpreter, module import, and PortAudio/NumPy init will blow your budget.

Python is fine if you’re okay with:

* extra packaging friction,
* global hotkeys needing Accessibility, and
* slightly more GC variability.
  For “most fast and reliable”, Qt/C++ or Swift wins.

---

## What to change in *your* repo (checklist)

1. **macOS build**: add Info.plist, Homebrew instructions, and CMake tweaks above.
2. **Permissions**: NSMicrophoneUsageDescription. (Accessibility only if you synthesize ⌘V.)
3. **Launch at login**: add LaunchAgent plist and docs.
4. **Global hotkey**: add QHotkey (keep SIGUSR1 too).
5. **Audio**:

    * switch to `Pa_OpenStream` with `suggestedLatency = defaultLowInputLatency`,
    * keep the stream open, and
    * **replace callback work** with a ring buffer + worker (WAV path preferred).
6. **Networking**: your `OpenAiTranscriptionService` is fine; just accept `.wav`.
7. **Status/Tray**: replace i3blocks with menu‑bar text/icon (optional; you can also keep files in `/tmp` as your external “protocol”).
8. **Clipboard**: use `pbcopy` and (optional) Quartz for ⌘V.

---

## A tiny code sketch (WAV path + ring buffer)

**WAV header helper** (once on start):

```cpp
#pragma pack(push, 1)
struct WavHeader {
  char riff[4] = {'R','I','F','F'};
  uint32_t fileSizeMinus8;
  char wave[4] = {'W','A','V','E'};
  char fmt[4]  = {'f','m','t',' '};
  uint32_t fmtSize = 16;
  uint16_t audioFormat = 1; // PCM
  uint16_t numChannels = 1;
  uint32_t sampleRate;
  uint32_t byteRate;
  uint16_t blockAlign;
  uint16_t bitsPerSample = 16;
  char data[4] = {'d','a','t','a'};
  uint32_t dataSize;
};
#pragma pack(pop)

inline void writeWavHeader(QFile& f, uint32_t sr, uint32_t totalPcmBytesEstimate = 0) {
  WavHeader h;
  h.sampleRate = sr;
  h.byteRate = sr * 2 /* bytes per sample */ * 1 /* ch */;
  h.blockAlign = 2;
  h.dataSize = totalPcmBytesEstimate;
  h.fileSizeMinus8 = 36 + h.dataSize;
  f.write(reinterpret_cast<const char*>(&h), sizeof(h));
}

inline void patchWavSizes(QFile& f, uint32_t dataBytes) {
  f.seek(4);  uint32_t fileSizeMinus8 = 36 + dataBytes; f.write(reinterpret_cast<char*>(&fileSizeMinus8), 4);
  f.seek(40); f.write(reinterpret_cast<char*>(&dataBytes), 4);
}
```

**Callback (no locks, no I/O):**

```cpp
static int audioCallback(const void* input, void*, unsigned long frames,
                         const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* user) {
  auto* self = reinterpret_cast<AudioRecorder*>(user);
  if (!input) return paContinue;
  const auto* in = static_cast<const int16_t*>(input);
  // write to ring buffer
  PaUtil_WriteRingBuffer(&self->m_rb, in, frames);
  // cheap volume
  float rms = 0.f; for (unsigned long i=0;i<frames;i++) { float s = in[i] / 32768.f; rms += s*s; }
  rms = sqrtf(rms/frames);
  self->m_volumeAtomic.store(rms, std::memory_order_relaxed);
  return paContinue;
}
```

**Worker loop (QtConcurrent):**

```cpp
// on startRecording():
m_pcmBytesWritten = 0;
m_outputFile.setFileName(OUTPUT_FILE_PATH.replace(".mp3",".wav"));
m_outputFile.open(QIODevice::WriteOnly);
writeWavHeader(m_outputFile, m_sampleRate); // placeholder sizes

m_worker = QtConcurrent::run([this](){
  std::array<int16_t, 4096> tmp{};
  while (m_isRecording.load()) {
    ring_count_t got = PaUtil_ReadRingBuffer(&m_rb, tmp.data(), tmp.size());
    if (got>0) {
      m_outputFile.write(reinterpret_cast<char*>(tmp.data()), got * sizeof(int16_t));
      m_pcmBytesWritten += got * sizeof(int16_t);
    } else {
      QThread::msleep(2);
    }
  }
});
```

**On stop:**

```cpp
m_isRecording.store(false);
m_worker.waitForFinished();
patchWavSizes(m_outputFile, m_pcmBytesWritten);
m_outputFile.close();
```

Then POST the `.wav` to Groq exactly like you already do (your code already lets the server detect content type; the filename extension will be `.wav` now).

---

## Final recommendation

* **Best speed & reliability today** for your case: **keep Qt/PortAudio**, apply the callback/ring‑buffer fix, switch to **WAV** for recording/transcribe, add **launchd + global hotkey + Info.plist**. You’ll get instant capture on a MacBook Pro M4 Max with no noticeable warm‑up.

* If you ever want to go “Mac‑native” later, a small Swift menu bar app with AVAudioEngine mirrors the same architecture and is equally snappy—but it’s not necessary to reach your goal.

If you want, I can produce a small patch set against your repo (CMake + Info.plist + ring buffer worker + hotkey + launchd plist) to make this concrete.