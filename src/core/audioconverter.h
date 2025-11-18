#ifndef AUDIOCONVERTER_H
#define AUDIOCONVERTER_H

#include <QObject>
#include <QString>

class QFile;

// LAME is optional on macOS
// Only include if LAME_INCLUDE_DIR is defined (set by CMake if found)
#ifdef LAME_INCLUDE_DIR
#include <lame/lame.h>
#endif

class AudioConverter : public QObject
{
    Q_OBJECT

public:
    explicit AudioConverter(QObject* parent = nullptr);
    ~AudioConverter();

    // Convert WAV file to MP3
    // Returns true on success, false on failure
    bool convertWavToMp3(const QString& wavFilePath, const QString& mp3FilePath);

    // Check if LAME encoder is available
    bool isLameAvailable() const;

signals:
    // Emitted when conversion starts
    void conversionStarted();
    
    // Emitted when conversion completes successfully
    void conversionCompleted(const QString& mp3Path);
    
    // Emitted when conversion fails
    void conversionFailed(const QString& errorMessage);

private:
#ifdef LAME_INCLUDE_DIR
    // Initialize LAME encoder
    bool initializeLame(int sampleRate, int numChannels, int bitrate);
    
    // Finalize LAME encoder
    void finalizeLame();
    
    // Parse WAV header and extract audio parameters
    struct WavInfo {
        uint32_t sampleRate;
        uint16_t numChannels;
        uint32_t dataSize;
        double durationSeconds;
    };
    bool parseWavHeader(QFile& wavFile, WavInfo& info);
    
    lame_global_flags* m_lameGlobal;
    bool m_lameInitialized;
#endif
};

#endif // AUDIOCONVERTER_H

