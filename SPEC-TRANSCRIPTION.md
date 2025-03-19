## **Core Functional Additions**
1. **Transcription Submission**
    - The program must take the recorded audio file (`/tmp/audio_recording.m4a`) and send it to OpenAI’s Whisper API for speech-to-text processing.
    - API key is in the environment variable: "OPENAI_API_KEY". If not, then the status during the recording should include message "NO API KEY"
    - The transcription request should include essential parameters such as language (`en`), model selection (`whisper-1`), and a low temperature (`0.1`).
    - Once processed, the resulting text should be saved as `/tmp/voice_input.txt`.

2. **User Experience Considerations**
    - The UI should clearly indicate when transcription is in progress.
    - The user should be able to see a status update (e.g., “Transcribing… Please wait.”).
    - Upon success, the text should be displayed or easily accessible.
    - A confirmation message should appear when transcription completes.

3. **Reliability & Error Handling**
    - The application should handle common failure scenarios:
        - **Missing API key** → Show a user-friendly message
        - **Network issues** → Display a retry option if the request fails.
        - **HTTP errors (e.g., 400, 401, 500, timeout)** → Provide a clear error message instead of generic failure notifications.
    - The program should not retry failed transcription requests. Instead, display result of sending in console and display button/action for manually retrying sending

4. **Testing & Verification Requirements**
    - Ensure the transcription feature works reliably under different network conditions (e.g., slow or interrupted connections).
    - Verify correct handling of missing API keys or incorrect credentials.
    - Test for proper UI updates during each stage (recording, sending, waiting, completion).
    - Confirm that transcription output is saved correctly and reflects what was spoken.
    - Edge cases should be tested, such as sending an empty or corrupted audio file.

5. **Seamless Integration & Cleanup**
    - Old files (`/tmp/audio_recording.m4a` and `/tmp/voice_input.txt`) should be removed on startup to prevent conflicts.
    - The application must allow canceling transcription at any point without leaving the system in an inconsistent state.
    - The process should be lightweight, ensuring minimal memory and CPU usage, and must not block other UI operations.

6. **Future-Proofing**
    - The implementation should allow easy modifications, such as changing the transcription API or adding support for different languages.
    - The architecture should be modular, ensuring that the transcription logic is decoupled from the UI and recording components.  
