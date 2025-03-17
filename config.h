#ifndef CONFIG_H
#define CONFIG_H

#include <QString>

namespace Config {
    // File paths
    static const QString RECORDING_PATH = "/tmp/stt-recording.m4a";
    static const QString TRANSCRIPTION_PATH = "/tmp/stt-transcription.txt";
    
    // OpenAI API settings
    static const QString WHISPER_MODEL = "whisper-1";
    static const double WHISPER_TEMPERATURE = 0.1;
    static const QString WHISPER_LANGUAGE = "en";
    
    // Recording settings
    static const QString AUDIO_FORMAT = "aac";
    static const QString AUDIO_BITRATE = "128k";
    static const qint64 MAX_FILE_SIZE_BYTES = 35 * 1024 * 1024; // 35 MB
    
    // UI settings
    static const int UI_WIDTH = 400;
    static const int UI_HEIGHT = 200;
    static const int UI_UPDATE_INTERVAL_MS = 500;
    static const int VOLUME_UPDATE_INTERVAL_MS = 200;
    static const int EXIT_DELAY_MS = 2000;
}

#endif // CONFIG_H