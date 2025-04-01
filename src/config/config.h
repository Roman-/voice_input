#ifndef CONFIG_H
#define CONFIG_H

constexpr auto OUTPUT_FILE_PATH = "/tmp/voice_input_recording.mp3";
constexpr auto TRANSCRIPTION_OUTPUT_PATH = "/tmp/voice_input_transcription.txt";
constexpr auto LOCK_FILE_PATH = "/tmp/voice_input_lock.pid";
constexpr auto STATUS_FILE_PATH = "/tmp/voice_input_status.txt";
constexpr int DEFAULT_TIMEOUT = 0;           // No timeout by default
constexpr int SAMPLE_RATE = 44100;           // CD-quality sample rate
constexpr int NUM_CHANNELS = 1;              // Mono
constexpr int ENCODER_BITRATE = 128000;      // 128 kbps MP3 encoding

// Volume Visualization Settings
constexpr float VOLUME_SCALING_FACTOR = 5.0f;  // Amplify volume for better visualization
constexpr float VOLUME_LOG_BASE = 20.0f;       // Higher values make small sounds more visible
constexpr float VOLUME_MIN_THRESHOLD = 0.001f; // Minimum volume to register any display

// Base styles with consistent formatting
constexpr auto STYLE_STATUS_NEUTRAL = "font-weight: bold; font-size: 12pt; color: #5CAAFF;";
constexpr auto STYLE_STATUS_SUCCESS = "font-weight: bold; font-size: 12pt; color: #4CFF64;";
constexpr auto STYLE_STATUS_ERROR = "font-weight: bold; font-size: 12pt; color: #FF6B6B;";

// Transcription text styles
constexpr auto STYLE_TRANSCRIPTION_NEUTRAL = "color: #5CAAFF; font-size: 10pt;";
constexpr auto STYLE_TRANSCRIPTION_ERROR = "color: #FF6B6B; font-size: 10pt;";

// Exit Codes
constexpr int APP_EXIT_SUCCESS = 0;             // Successful transcription
constexpr int APP_EXIT_FAILURE_GENERAL = 1;     // General failure
constexpr int APP_EXIT_FAILURE_NO_API_KEY = 2;  // No API key provided
constexpr int APP_EXIT_FAILURE_API_ERROR = 3;   // API returned an error
constexpr int APP_EXIT_FAILURE_CANCELED = 4;    // User canceled operation
constexpr int APP_EXIT_FAILURE_FILE_ERROR = 5;  // File I/O error

#endif // CONFIG_H
