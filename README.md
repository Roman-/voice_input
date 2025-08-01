# Roman's voice input

Qt-based voice input app with MP3 encoding (LAME), PortAudio input, and automatic transcription using Groq's Whisper API.

## ⚙️ Build Instructions

### Prerequisites

```bash
sudo apt install cmake qtbase5-dev libportaudio2 libmp3lame-dev pkg-config
```

### Build Steps

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

## 🧠 Environment Requirements

Set your Groq API key (required for transcription):

```bash
export GROQ_API_KEY=your_key_here
```

## 🚀 Usage

Run the application:

```bash
./romans_voice_input
```

To start recording in the background and trigger via signal:

```bash
kill -SIGUSR1 $(pidof romans_voice_input)
```

To stop recording and transcribe, press `Enter` or `Space` in the window.

The results will be copied to the clipboard and the application will simulate pressing `Ctrl+V` to paste the transcription.
Additionally, the the transcription will be saved to the output file.

## 📁 Output Files

| Path                                | Description                    |
|-------------------------------------|--------------------------------|
| `/tmp/voice_input_recording.mp3`    | Audio output file              |
| `/tmp/voice_input_transcription.txt`| Transcription result           |
| `/tmp/voice_input_status.txt`       | Current status indicator       |
| `/tmp/voice_input_lock.pid`         | Lock file for singleton check  |