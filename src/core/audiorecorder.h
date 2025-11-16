#ifndef AUDIORECORDER_H
#define AUDIORECORDER_H

#include <QObject>
#include <QFile>
#include <QElapsedTimer>
#include <QMutex>
#include <QFuture>
#include <QtConcurrent>
#include <QBuffer>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <cstring>
#include <portaudio.h>
// LAME is optional on macOS (we use WAV instead)
// Only include if LAME_INCLUDE_DIR is defined (set by CMake if found)
#ifdef LAME_INCLUDE_DIR
#include <lame/lame.h>
#endif

// Simple lock-free ring buffer for macOS
struct LockFreeRingBuffer {
    char* buffer;
    size_t size;
    std::atomic<size_t> writePos;
    std::atomic<size_t> readPos;
    
    LockFreeRingBuffer(size_t bufferSize) : size(bufferSize), writePos(0), readPos(0) {
        buffer = new char[bufferSize];
    }
    
    ~LockFreeRingBuffer() {
        delete[] buffer;
    }
    
    size_t write(const void* data, size_t elementSize, size_t elementCount) {
        size_t totalBytes = elementSize * elementCount;
        size_t wPos = writePos.load(std::memory_order_relaxed);
        size_t rPos = readPos.load(std::memory_order_acquire);
        
        size_t available = (rPos > wPos) ? (rPos - wPos - 1) : (size - wPos + rPos - 1);
        if (available < totalBytes) {
            totalBytes = (available / elementSize) * elementSize; // Round down to element boundary
        }
        
        if (totalBytes == 0) return 0;
        
        size_t firstPart = qMin(totalBytes, size - wPos);
        memcpy(buffer + wPos, data, firstPart);
        if (totalBytes > firstPart) {
            memcpy(buffer, static_cast<const char*>(data) + firstPart, totalBytes - firstPart);
        }
        
        writePos.store((wPos + totalBytes) % size, std::memory_order_release);
        return totalBytes / elementSize;
    }
    
    size_t read(void* data, size_t elementSize, size_t elementCount) {
        size_t totalBytes = elementSize * elementCount;
        size_t wPos = writePos.load(std::memory_order_acquire);
        size_t rPos = readPos.load(std::memory_order_relaxed);
        
        size_t available = (wPos >= rPos) ? (wPos - rPos) : (size - rPos + wPos);
        if (available < totalBytes) {
            totalBytes = (available / elementSize) * elementSize; // Round down to element boundary
        }
        
        if (totalBytes == 0) return 0;
        
        size_t firstPart = qMin(totalBytes, size - rPos);
        memcpy(data, buffer + rPos, firstPart);
        if (totalBytes > firstPart) {
            memcpy(static_cast<char*>(data) + firstPart, buffer, totalBytes - firstPart);
        }
        
        readPos.store((rPos + totalBytes) % size, std::memory_order_release);
        return totalBytes / elementSize;
    }
    
    size_t getReadAvailable() const {
        size_t wPos = writePos.load(std::memory_order_acquire);
        size_t rPos = readPos.load(std::memory_order_relaxed);
        return (wPos >= rPos) ? (wPos - rPos) : (size - rPos + wPos);
    }
};

class AudioRecorder : public QObject
{
    Q_OBJECT
public:
    explicit AudioRecorder(QObject* parent = nullptr);
    ~AudioRecorder();

    // Initialize audio system - called once at startup
    bool initializeAudioSystem();
    
    // Start/stop recording to file
    bool startRecording();
    void stopRecording();
    
    // Pause/resume the audio stream (to avoid listening when not needed)
    bool pauseAudioStream();
    bool resumeAudioStream();

    // For UI: volume level, file size, etc.
    float currentVolumeLevel() const;
    qint64 fileSize() const;
    qint64 elapsedMs() const;
    
    // Check if recording is active
    bool isRecording() const { return m_isRecording; }
    
    // Check if audio system is initialized
    bool isAudioSystemInitialized() const { return m_audioDeviceInitialized; }
    
    // Check if audio stream is active
    bool isAudioStreamActive() const;

signals:
    void volumeChanged(float newVolume);
    void recordingStopped();
    void recordingStarted();
    void audioDeviceReady();

private:
    bool initializePortAudio(bool startStreamImmediately = true);
    void finalizePortAudio();

    static int audioCallback( const void *inputBuffer,
                              void *outputBuffer,
                              unsigned long framesPerBuffer,
                              const PaStreamCallbackTimeInfo* timeInfo,
                              PaStreamCallbackFlags statusFlags,
                              void *userData );

    void handleAudioData(const void* inputBuffer, unsigned long frames);
    
    // WAV file helpers
    void writeWavHeader(QFile& file, uint32_t sampleRate, uint32_t totalPcmBytesEstimate = 0);
    void patchWavSizes(QFile& file, uint32_t dataBytes);
    
    // Worker thread function
    void workerThreadFunction();

private:
    // PortAudio
    PaStream*       m_stream;
    LockFreeRingBuffer* m_ringBuffer;
    
    // File output
    QFile           m_outputFile;
    QElapsedTimer   m_elapsedTimer;
    QMutex          m_dataMutex;
    
    // State
    std::atomic<bool> m_isRecording;
    std::atomic<bool> m_workerShouldStop;
    bool            m_audioDeviceInitialized;
    std::atomic<float> m_currentVolume;
    QFuture<void>   m_initFuture;
    QFuture<void>   m_workerFuture;
    
    // Audio parameters
    double          m_sampleRate;
    uint32_t        m_pcmBytesWritten;
    
    // Volume polling timer (macOS only - can't emit from callback)
    QTimer*         m_volumePollTimer;
    
    // MP3 encoding (optional)
#ifdef LAME_INCLUDE_DIR
    lame_global_flags* m_lameGlobal;
    bool               m_mp3Initialized;
    QByteArray         m_encodedData;
    QBuffer            m_dataBuffer; // For intermediate processing
#endif
};

#endif // AUDIORECORDER_H
