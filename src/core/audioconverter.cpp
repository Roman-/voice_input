#include "audioconverter.h"
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QElapsedTimer>
#include <cmath>
#include "config/config.h"

#ifdef LAME_INCLUDE_DIR
#include <cstring>
#endif

AudioConverter::AudioConverter(QObject* parent)
    : QObject(parent)
#ifdef LAME_INCLUDE_DIR
    , m_lameGlobal(nullptr)
    , m_lameInitialized(false)
#endif
{
}

AudioConverter::~AudioConverter()
{
#ifdef LAME_INCLUDE_DIR
    finalizeLame();
#endif
}

bool AudioConverter::isLameAvailable() const
{
#ifdef LAME_INCLUDE_DIR
    qDebug() << "LAME_INCLUDE_DIR is defined - LAME should be available";
    return true;
#else
    qWarning() << "LAME_INCLUDE_DIR is NOT defined - LAME is not available";
    qWarning() << "Check CMake configuration and rebuild the project";
    return false;
#endif
}

bool AudioConverter::convertWavToMp3(const QString& wavFilePath, const QString& mp3FilePath)
{
    emit conversionStarted();

#ifdef LAME_INCLUDE_DIR
    QElapsedTimer timer;
    timer.start();

    // Check if WAV file exists
    QFileInfo wavInfo(wavFilePath);
    if (!wavInfo.exists()) {
        QString error = QString("WAV file does not exist: %1").arg(wavFilePath);
        qCritical() << error;
        emit conversionFailed(error);
        return false;
    }

    qint64 wavSizeBytes = wavInfo.size();
    double wavSizeKB = wavSizeBytes / 1024.0;
    double wavSizeMB = wavSizeKB / 1024.0;

    qInfo() << "Starting WAV to MP3 conversion:";
    qInfo() << "  Input file:" << wavFilePath;
    qInfo() << "  Input size:" << wavSizeBytes << "bytes (" 
            << (wavSizeMB >= 1.0 ? QString::number(wavSizeMB, 'f', 2) + " MB" : QString::number(wavSizeKB, 'f', 2) + " KB") << ")";

    // Open WAV file
    QFile wavFile(wavFilePath);
    if (!wavFile.open(QIODevice::ReadOnly)) {
        QString error = QString("Failed to open WAV file: %1").arg(wavFile.errorString());
        qCritical() << error;
        emit conversionFailed(error);
        return false;
    }

    // Read WAV header
    QByteArray header = wavFile.read(44); // Standard WAV header is 44 bytes
    if (header.size() < 44) {
        QString error = "WAV file header is too small or invalid";
        qCritical() << error;
        wavFile.close();
        emit conversionFailed(error);
        return false;
    }

    // Parse WAV header to get sample rate, channels, and data size
    uint32_t sampleRate = *reinterpret_cast<const uint32_t*>(header.data() + 24);
    uint16_t numChannels = *reinterpret_cast<const uint16_t*>(header.data() + 22);
    uint32_t dataSize = *reinterpret_cast<const uint32_t*>(header.data() + 40);
    
    // Calculate audio duration
    uint32_t bytesPerSample = 2; // 16-bit = 2 bytes
    uint32_t bytesPerSecond = sampleRate * numChannels * bytesPerSample;
    double durationSeconds = static_cast<double>(dataSize) / bytesPerSecond;

    qInfo() << "  Sample rate:" << sampleRate << "Hz";
    qInfo() << "  Channels:" << numChannels;
    qInfo() << "  Audio duration:" << durationSeconds << "seconds";
    qInfo() << "  MP3 bitrate:" << MP3_BITRATE << "kbps";

    // Initialize LAME encoder
    if (!initializeLame(static_cast<int>(sampleRate), static_cast<int>(numChannels), MP3_BITRATE)) {
        QString error = "Failed to initialize LAME encoder";
        qCritical() << error;
        wavFile.close();
        emit conversionFailed(error);
        return false;
    }

    // Open MP3 output file
    QFile mp3File(mp3FilePath);
    if (!mp3File.open(QIODevice::WriteOnly)) {
        QString error = QString("Failed to open MP3 file for writing: %1").arg(mp3File.errorString());
        qCritical() << error;
        wavFile.close();
        finalizeLame();
        emit conversionFailed(error);
        return false;
    }

    // Read PCM data and encode to MP3
    const int PCM_SIZE = 8192; // Buffer size for PCM samples
    const int MP3_SIZE = 7200; // Buffer size for MP3 output (LAME recommendation)
    
    int16_t pcmBuffer[PCM_SIZE];
    unsigned char mp3Buffer[MP3_SIZE];
    
    qint64 totalPcmRead = 0;
    qint64 totalMp3Written = 0;

    // Skip WAV header (already read)
    wavFile.seek(44);

    while (!wavFile.atEnd()) {
        // Read PCM data
        qint64 bytesRead = wavFile.read(reinterpret_cast<char*>(pcmBuffer), PCM_SIZE * sizeof(int16_t));
        if (bytesRead <= 0) {
            break;
        }

        int samplesRead = static_cast<int>(bytesRead / sizeof(int16_t));
        totalPcmRead += bytesRead;

        // Encode to MP3
        int mp3BytesEncoded = 0;
        
        if (numChannels == 1) {
            // Mono encoding
            mp3BytesEncoded = lame_encode_buffer(
                m_lameGlobal,
                pcmBuffer,
                nullptr, // No right channel for mono
                samplesRead,
                mp3Buffer,
                MP3_SIZE
            );
        } else {
            // Stereo encoding (interleaved)
            int16_t* leftChannel = pcmBuffer;
            int16_t* rightChannel = pcmBuffer + 1;
            int samplesPerChannel = samplesRead / 2;
            
            mp3BytesEncoded = lame_encode_buffer_interleaved(
                m_lameGlobal,
                pcmBuffer,
                samplesPerChannel,
                mp3Buffer,
                MP3_SIZE
            );
        }

        if (mp3BytesEncoded < 0) {
            QString error = QString("LAME encoding error: %1").arg(mp3BytesEncoded);
            qCritical() << error;
            wavFile.close();
            mp3File.close();
            finalizeLame();
            emit conversionFailed(error);
            return false;
        }

        // Write MP3 data to file
        if (mp3BytesEncoded > 0) {
            qint64 bytesWritten = mp3File.write(reinterpret_cast<const char*>(mp3Buffer), mp3BytesEncoded);
            if (bytesWritten != mp3BytesEncoded) {
                QString error = QString("Failed to write MP3 data: %1").arg(mp3File.errorString());
                qCritical() << error;
                wavFile.close();
                mp3File.close();
                finalizeLame();
                emit conversionFailed(error);
                return false;
            }
            totalMp3Written += bytesWritten;
        }
    }

    // Flush remaining MP3 data
    int mp3BytesFlushed = lame_encode_flush(m_lameGlobal, mp3Buffer, MP3_SIZE);
    if (mp3BytesFlushed > 0) {
        qint64 bytesWritten = mp3File.write(reinterpret_cast<const char*>(mp3Buffer), mp3BytesFlushed);
        if (bytesWritten != mp3BytesFlushed) {
            QString error = QString("Failed to write final MP3 data: %1").arg(mp3File.errorString());
            qCritical() << error;
            wavFile.close();
            mp3File.close();
            finalizeLame();
            emit conversionFailed(error);
            return false;
        }
        totalMp3Written += bytesWritten;
    }

    // Close files
    wavFile.close();
    mp3File.close();
    finalizeLame();

    // Calculate conversion metrics
    qint64 conversionTimeMs = timer.elapsed();
    double mp3SizeKB = totalMp3Written / 1024.0;
    double mp3SizeMB = mp3SizeKB / 1024.0;
    double compressionRatio = static_cast<double>(wavSizeBytes) / totalMp3Written;

    qInfo() << "WAV to MP3 conversion completed:";
    qInfo() << "  Output file:" << mp3FilePath;
    qInfo() << "  Output size:" << totalMp3Written << "bytes (" 
            << (mp3SizeMB >= 1.0 ? QString::number(mp3SizeMB, 'f', 2) + " MB" : QString::number(mp3SizeKB, 'f', 2) + " KB") << ")";
    qInfo() << "  Conversion time:" << conversionTimeMs << "ms (" << (conversionTimeMs / 1000.0) << "seconds)";
    qInfo() << "  Compression ratio:" << QString::number(compressionRatio, 'f', 2) << ":1";
    qInfo() << "  Audio duration:" << durationSeconds << "seconds";
    qInfo() << "  Input size:" << wavSizeBytes << "bytes, Output size:" << totalMp3Written << "bytes";

    emit conversionCompleted(mp3FilePath);
    return true;

#else
    QString error = "LAME encoder not available. Please install LAME library.";
    qCritical() << error;
    emit conversionFailed(error);
    return false;
#endif
}

#ifdef LAME_INCLUDE_DIR
bool AudioConverter::initializeLame(int sampleRate, int numChannels, int bitrate)
{
    if (m_lameInitialized) {
        finalizeLame();
    }

    m_lameGlobal = lame_init();
    if (!m_lameGlobal) {
        qCritical() << "Failed to initialize LAME encoder";
        return false;
    }

    // Set LAME parameters
    lame_set_in_samplerate(m_lameGlobal, sampleRate);
    lame_set_num_channels(m_lameGlobal, numChannels);
    lame_set_brate(m_lameGlobal, bitrate);
    lame_set_mode(m_lameGlobal, numChannels == 1 ? MONO : STEREO);
    lame_set_quality(m_lameGlobal, 2); // Quality setting (0-9, 2 is good balance)
    lame_set_VBR(m_lameGlobal, vbr_off); // Use CBR (constant bitrate)
    
    // Initialize encoder
    if (lame_init_params(m_lameGlobal) < 0) {
        qCritical() << "Failed to initialize LAME parameters";
        lame_close(m_lameGlobal);
        m_lameGlobal = nullptr;
        return false;
    }

    m_lameInitialized = true;
    return true;
}

void AudioConverter::finalizeLame()
{
    if (m_lameGlobal) {
        lame_close(m_lameGlobal);
        m_lameGlobal = nullptr;
        m_lameInitialized = false;
    }
}
#endif

