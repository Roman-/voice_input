# Transcription Test Instructions

This document explains how to run the transcription test that verifies the functionality of the speech-to-text feature using OpenAI's Whisper API.

## Prerequisites

1. **API Key**: You must have an OpenAI API key set as an environment variable named `OPENAI_API_KEY`.

2. **Test Audio File**: The test requires a file called `hello_world_fixed.mp4` in the project's root directory. This file should contain an audio recording of someone saying "hello world" (words can be in any order, case, or with punctuation). The file must be in a format directly supported by the Whisper API (MP4 format in our case).

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

- The test will be skipped if the API key is not available
- The test will fail if the transcription doesn't contain both "hello" and "world"
- A timeout of 120 seconds (2 minutes) is set for the transcription to complete
- The test is case-insensitive, so any capitalization of "hello" and "world" will pass
- The output file is cleaned up at the end of the test
- The test requires a successful API call to pass

## Real API Testing

For testing with the real API, ensure that:
1. Your API key has valid permissions for the Whisper API
2. The audio file is in a format supported by the Whisper API (flac, m4a, mp3, mp4, mpeg, mpga, oga, ogg, wav, webm)
3. The audio file contains clear speech saying "hello world"

If the API call fails, the test will output detailed error information including the HTTP status code and response body. This information can be helpful for debugging issues with the API integration.

## Creating a Compatible Test File

If you're having issues with your audio file format, you can convert it to a compatible format using FFmpeg:

```bash
# Convert any audio file to MP4 format supported by Whisper API
ffmpeg -i your_input_file.mp3 -c:a aac -f mp4 hello_world_fixed.mp4

# If your original file is in MP3 format, you need to convert it to MP4 container
ffmpeg -i hello_world.mp3 -c:a aac -f mp4 hello_world_fixed.mp4
```

Common issues with audio files:
- Raw MP3 files may not be recognized by the API in some cases
- Files must have proper container formats (MP4, MP3, etc.) not just the raw audio data
- The Whisper API requires specific formats listed in their documentation