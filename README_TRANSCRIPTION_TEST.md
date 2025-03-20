# Transcription Test Instructions

This document explains how to run the transcription test that verifies the functionality of the speech-to-text feature using OpenAI's Whisper API.

## Prerequisites

1. **API Key**: You have two options:
   - **Real API**: Set your OpenAI API key as an environment variable named `OPENAI_API_KEY`
   - **Mock API**: Set the environment variable `OPENAI_API_KEY=test` to use a mock implementation

2. **Test Audio File**: The test requires a file called `hello_world.mp3` in the project's root directory. This file should contain an audio recording of someone saying "hello world" (words can be in any order, case, or with punctuation). The file must be in MP3 format.

## Running the Test

1. **Build the test**:
   ```bash
   cd build_Debug
   cmake ..
   make test_transcription
   ```

2. **Execute the test**:
   ```bash
   # Make sure your OpenAI API key is set
   export OPENAI_API_KEY=your_api_key_here
   
   # Run the test
   ./test_transcription
   ```

3. **Test Results**:
   - The test will output progress information as it runs
   - It verifies that the API key is properly detected from the environment
   - It transcribes the audio file using the real OpenAI API
   - It verifies the transcription contains "hello" and "world"
   - It checks that the transcription is properly saved to the output file

## Notes

- The real API test will be skipped if the API key is not available or if using the mock API
- The mock API test will be run only when OPENAI_API_KEY=test
- The test will fail if the transcription doesn't contain both "hello" and "world" (real API)
- For the mock API, the test checks for the exact response: "Hello, world."
- A timeout of 120 seconds (2 minutes) is set for the real API call
- A timeout of 10 seconds is set for the mock API call
- The test is case-insensitive for the real API
- The output file is cleaned up at the end of the test

## Real API Testing

For testing with the real API, ensure that:
1. Your API key has valid permissions for the Whisper API
2. The audio file is in a format supported by the Whisper API (flac, m4a, mp3, mp4, mpeg, mpga, oga, ogg, wav, webm)
3. The audio file contains clear speech saying "hello world"

If the API call fails, the test will output detailed error information including the HTTP status code and response body. This information can be helpful for debugging issues with the API integration.

## Mock API Testing

The mock API provides a way to test the application without making real API calls:

1. Set the environment variable to exactly "test":
   ```bash
   export OPENAI_API_KEY=test
   ```

2. Run the test or application as normal - it will automatically use the mock implementation

The mock API implementation:
- Always returns a fixed response: `{"text":"Hello, world."}`
- Has a random delay between 1-3 seconds to simulate network latency
- Emits progress signals to test the progress UI
- Does not make any real network requests
- Works with any valid audio file regardless of content (but still checks if the file exists)

## Creating a Compatible Test File

If you're having issues with your audio file format, you can convert it to a compatible format using FFmpeg:

```bash
# Convert any audio file to MP3 format
ffmpeg -i your_input_file.wav -ar 44100 -ac 1 -b:a 128k hello_world.mp3

# If you need to convert between formats
ffmpeg -i hello_world.mp4 -ar 44100 -ac 1 -b:a 128k hello_world.mp3
```

Common issues with audio files:
- Raw MP3 files may not be recognized by the API in some cases
- Files must have proper container formats (MP4, MP3, etc.) not just the raw audio data
- The Whisper API requires specific formats listed in their documentation

## Integrating Mock API in Custom Tests

You can use the mock API in your own tests by setting the API key to "test" before running any code that creates a transcription service:

```cpp
// Set environment variable programmatically
qputenv("OPENAI_API_KEY", "test");

// Create transcription service using factory
TranscriptionService* service = TranscriptionFactory::createTranscriptionService(this);

// Now the service will be a MockTranscriptionService that responds with "Hello, world."
```

This approach allows you to:
- Run tests without real API keys
- Test the application workflow without internet connectivity
- Get consistent, predictable responses
- Avoid consuming API quota during development and testing