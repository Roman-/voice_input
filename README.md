# The Voice Input Application
C++/Qt-based tool that records audio, transcribes it using OpenAI Whisper, and saves the results to file.

Designed for Ubuntu 22.04, it ensures low-latency startup, efficient recording, and seamless integration with OpenAI's API while handling errors and file management automatically.

## Usage

```
voice_input [options]
```

Options:
- `--send` or `-s`: Send recording to OpenAI for transcription. If not provided, the recording is only saved to disk without transcription.

## Features

- Records audio to a uniquely named file in `~/Documents/voice_input/`
- Outputs the saved file path to stdout after recording
- Verifies that the recorded file exists before exiting
- Only sends to OpenAI Whisper if `--send` argument is provided

See SPEC.md for requirements