#ifndef CONFIG_H
#define CONFIG_H

constexpr auto OUTPUT_FILE_PATH = "/tmp/audio_recording.m4a";
constexpr int DEFAULT_TIMEOUT = 0;           // No timeout by default
constexpr int SAMPLE_RATE = 44100;           // CD-quality sample rate
constexpr int NUM_CHANNELS = 1;              // Mono
constexpr int ENCODER_BITRATE = 128000;      // 128 kbps AAC (conceptual)

// Volume Visualization Settings
constexpr float VOLUME_SCALING_FACTOR = 5.0f;  // Amplify volume for better visualization
constexpr float VOLUME_LOG_BASE = 20.0f;       // Higher values make small sounds more visible
constexpr float VOLUME_MIN_THRESHOLD = 0.001f; // Minimum volume to register any display

#endif // CONFIG_H
