#include "audiorecorder.h"
#include "audioconverter.h"
#include "formatutils.h"
#include <QDebug>
#include <QFileInfo>
#include <QDateTime>
#include <QThread>
#include <cmath>
#include <cstring>
#include <array>

#include "config/config.h"

// WAV header structure
#pragma pack(push, 1)
struct WavHeader {
    char riff[4] = {'R','I','F','F'};
    uint32_t fileSizeMinus8;
    char wave[4] = {'W','A','V','E'};
    char fmt[4]  = {'f','m','t',' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1; // PCM
    uint16_t numChannels = 1;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample = 16;
    char data[4] = {'d','a','t','a'};
    uint32_t dataSize;
};
#pragma pack(pop)

AudioRecorder::AudioRecorder(QObject* parent)
    : QObject(parent),
      m_stream(nullptr),
      m_isRecording(false),
      m_workerShouldStop(false),
      m_isCanceled(false),
      m_audioDeviceInitialized(false),
      m_currentVolume(0.0f),
      m_sampleRate(SAMPLE_RATE),
      m_pcmBytesWritten(0),
      m_ringBuffer(nullptr),
      m_volumePollTimer(nullptr),
      m_audioConverter(new AudioConverter(this))
{
    // Connect AudioConverter signals to AudioRecorder signals
    connect(m_audioConverter, &AudioConverter::conversionStarted, this, &AudioRecorder::conversionStarted);
    connect(m_audioConverter, &AudioConverter::conversionCompleted, this, &AudioRecorder::conversionCompleted);
    connect(m_audioConverter, &AudioConverter::conversionFailed, this, &AudioRecorder::conversionFailed);
    
    // Create volume polling timer (can't emit signals from callback thread)
    m_volumePollTimer = new QTimer(this);
    m_volumePollTimer->setInterval(50); // Poll every 50ms
    connect(m_volumePollTimer, &QTimer::timeout, this, [this]() {
        if (m_isRecording.load()) {
            float vol = m_currentVolume.load(std::memory_order_relaxed);
            emit volumeChanged(vol);
        }
    });
}

AudioRecorder::~AudioRecorder()
{
    stopRecording();
    
    // Wait for worker thread to finish
    if (m_workerFuture.isRunning()) {
        m_workerShouldStop = true;
        m_workerFuture.waitForFinished();
    }
    
    // Free ring buffer
    if (m_ringBuffer) {
        delete m_ringBuffer;
        m_ringBuffer = nullptr;
    }
    
    finalizePortAudio();
}

bool AudioRecorder::initializeAudioSystem()
{
    qInfo() << "Initializing audio system";
    
    // Initialize PortAudio but don't start the stream yet
    if (!initializePortAudio(false)) {
        qCritical() << "Failed to initialize PortAudio";
        return false;
    }
    
    // Mark that the audio device is ready
    m_audioDeviceInitialized = true;
    qInfo() << "Audio system initialized successfully";
    emit audioDeviceReady();
    
    return true;
}

bool AudioRecorder::pauseAudioStream()
{
    if (!m_stream || !m_audioDeviceInitialized) {
        return false;
    }
    
    PaError err = Pa_StopStream(m_stream);
    if (err != paNoError) {
        qWarning() << "Failed to pause audio stream:" << Pa_GetErrorText(err);
        return false;
    }
    
    qInfo() << "Audio stream paused - no longer listening to microphone";
    return true;
}

bool AudioRecorder::resumeAudioStream()
{
    if (!m_stream || !m_audioDeviceInitialized) {
        return false;
    }
    
    // Only resume if the stream is not already active
    if (!Pa_IsStreamActive(m_stream)) {
        PaError err = Pa_StartStream(m_stream);
        if (err != paNoError) {
            qWarning() << "Failed to resume audio stream:" << Pa_GetErrorText(err);
            return false;
        }
        
        qInfo() << "Audio stream resumed - now listening to microphone";
    }
    
    return true;
}

bool AudioRecorder::isAudioStreamActive() const
{
    if (!m_stream) {
        return false;
    }
    
    return Pa_IsStreamActive(m_stream) == 1;
}

bool AudioRecorder::startRecording()
{
    qInfo() << "startRecording() called";
    
    // Reset canceled flag
    m_isCanceled = false;
    
    // Check if audio system is initialized
    if (!m_audioDeviceInitialized) {
        qCritical() << "Cannot start recording - audio system not initialized";
        return false;
    }

    // Make sure the audio stream is active (on macOS it should always be running for pre-roll)
    if (!isAudioStreamActive()) {
        if (!resumeAudioStream()) {
            qCritical() << "Failed to resume audio stream for recording";
            return false;
        }
    }

    // Prepare output file immediately (use WAV path for recording)
    m_outputFile.setFileName(OUTPUT_FILE_PATH_WAV);
    if (!m_outputFile.open(QIODevice::WriteOnly)) {
        qCritical() << "Unable to open output file for writing:" << OUTPUT_FILE_PATH_WAV;
        return false;
    }

    // Write WAV header and start worker thread
    m_pcmBytesWritten = 0;
    writeWavHeader(m_outputFile, static_cast<uint32_t>(m_sampleRate));
    
    // Start worker thread to read from ring buffer and write to file
    m_workerShouldStop = false;
    m_isRecording = true;
    m_workerFuture = QtConcurrent::run([this]() { this->workerThreadFunction(); });
    
    qInfo() << "WAV recording started with worker thread";

    // Start the timer
    m_elapsedTimer.start();
    
    // Make sure volume is reset on new recording (emit zero volume to reset bar)
    m_currentVolume = 0.0f;
    emit volumeChanged(m_currentVolume);
    
    // Start volume polling timer
    if (m_volumePollTimer) {
        m_volumePollTimer->start();
    }
    
    // Signal that recording has started (UI should reflect this immediately)
    emit recordingStarted();
    qInfo() << "Recording started, writing to:" << OUTPUT_FILE_PATH_WAV;
    
    return true;
}

void AudioRecorder::stopRecording()
{
    if (!m_isRecording.load())
        return;

    qDebug() << "stopRecording() called";
    
    // Stop worker thread and patch WAV header
    m_isRecording = false;
    m_workerShouldStop = true;
    
    // Wait for worker thread to finish
    if (m_workerFuture.isRunning()) {
        m_workerFuture.waitForFinished();
    }
    
    // Patch WAV header with actual data size
    if (m_outputFile.isOpen()) {
        patchWavSizes(m_outputFile, m_pcmBytesWritten);
        m_outputFile.close();
    }
    
    qInfo() << "WAV recording stopped, wrote" << m_pcmBytesWritten << "bytes";

    // Reset volume to zero now that recording has stopped
    m_currentVolume = 0.0f;
    emit volumeChanged(m_currentVolume);
    
    // Stop volume polling timer
    if (m_volumePollTimer) {
        m_volumePollTimer->stop();
    }

    // Verify WAV file was created and has content
    QFileInfo wavFileInfo(OUTPUT_FILE_PATH_WAV);
    if (wavFileInfo.exists() && wavFileInfo.size() > 0) {
        qInfo() << "Recording stopped, WAV file saved successfully to:" << OUTPUT_FILE_PATH_WAV 
                << "Size:" << formatFileSize(wavFileInfo.size());
        
        // Convert WAV to MP3 only if recording was not canceled
        if (!m_isCanceled.load()) {
            if (m_audioConverter && m_audioConverter->isLameAvailable()) {
                qInfo() << "Starting WAV to MP3 conversion...";
                bool conversionSuccess = m_audioConverter->convertWavToMp3(OUTPUT_FILE_PATH_WAV, OUTPUT_FILE_PATH);
                
                if (conversionSuccess) {
                    // Delete temporary WAV file after successful conversion
                    QFile wavFile(OUTPUT_FILE_PATH_WAV);
                    if (wavFile.remove()) {
                        qInfo() << "Temporary WAV file deleted successfully";
                    } else {
                        qWarning() << "Failed to delete temporary WAV file:" << wavFile.errorString();
                    }
                } else {
                    qWarning() << "MP3 conversion failed, keeping WAV file";
                }
            } else {
                qWarning() << "LAME encoder not available, skipping MP3 conversion. WAV file saved at:" << OUTPUT_FILE_PATH_WAV;
            }
        } else {
            qInfo() << "Recording was canceled, skipping MP3 conversion";
            // Delete WAV file when canceled
            QFile wavFile(OUTPUT_FILE_PATH_WAV);
            if (wavFile.remove()) {
                qInfo() << "WAV file deleted after cancellation";
            }
        }
    } else {
        qWarning() << "WAV file may be missing or empty:" << OUTPUT_FILE_PATH_WAV;
    }

    emit recordingStopped();
}

void AudioRecorder::cancelRecording()
{
    // Mark as canceled and stop recording
    m_isCanceled = true;
    stopRecording();
}

float AudioRecorder::currentVolumeLevel() const
{
    // Volume is stored atomically
    return m_currentVolume.load(std::memory_order_relaxed);
}

qint64 AudioRecorder::fileSize() const
{
    return m_outputFile.size();
}

qint64 AudioRecorder::elapsedMs() const
{
    return m_elapsedTimer.elapsed();
}

QString AudioRecorder::getOutputFilePath() const
{
    // Check if MP3 file exists (preferred)
    QFileInfo mp3FileInfo(OUTPUT_FILE_PATH);
    if (mp3FileInfo.exists() && mp3FileInfo.size() > 0) {
        return OUTPUT_FILE_PATH;
    }
    
    // Fall back to WAV file if MP3 doesn't exist (LAME not available or conversion failed)
    QFileInfo wavFileInfo(OUTPUT_FILE_PATH_WAV);
    if (wavFileInfo.exists() && wavFileInfo.size() > 0) {
        return OUTPUT_FILE_PATH_WAV;
    }
    
    // Neither file exists, return MP3 path as default
    return OUTPUT_FILE_PATH;
}

bool AudioRecorder::initializePortAudio(bool startStreamImmediately)
{
    qDebug() << "Initializing PortAudio";

    // Try to terminate any prior instances first, for safety
    Pa_Terminate();
    
    // Initialize PortAudio library
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        qCritical() << "Pa_Initialize() failed:" << Pa_GetErrorText(err);
        return false;
    }
    
    // Ensure we have at least one input device
    int numDevices = Pa_GetDeviceCount();
    if (numDevices < 1) {
        qCritical() << "No audio devices found!";
        Pa_Terminate();
        return false;
    }
    
    // Find the default input device
    int defaultInputDevice = Pa_GetDefaultInputDevice();
    if (defaultInputDevice == paNoDevice) {
        qCritical() << "No default input device!";
        Pa_Terminate();
        return false;
    }
    
    // Log device info
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(defaultInputDevice);
    if (deviceInfo) {
        qInfo() << "Using input device:" << deviceInfo->name 
                << "with" << deviceInfo->maxInputChannels << "channels";
        m_sampleRate = deviceInfo->defaultSampleRate;
    } else {
        m_sampleRate = SAMPLE_RATE;
    }

    // Use Pa_OpenStream with suggestedLatency for low latency
    PaStreamParameters in;
    in.device = defaultInputDevice;
    in.channelCount = NUM_CHANNELS;
    in.sampleFormat = paInt16;
    in.suggestedLatency = deviceInfo ? deviceInfo->defaultLowInputLatency : 0.01;
    in.hostApiSpecificStreamInfo = nullptr;

    err = Pa_OpenStream(&m_stream,
                        &in,
                        nullptr,
                        m_sampleRate,
                        256, // frames per buffer
                        paClipOff,
                        &AudioRecorder::audioCallback,
                        this);
    if (err != paNoError) {
        qCritical() << "Pa_OpenStream() failed:" << Pa_GetErrorText(err);
        Pa_Terminate();
        return false;
    }
    
    // Initialize ring buffer for real-time safe audio capture
    // Allocate enough for ~500ms of pre-roll at the sample rate
    size_t ringBufferFrames = static_cast<size_t>(m_sampleRate * 0.5); // 500ms
    size_t ringBufferSize = ringBufferFrames * sizeof(int16_t) * NUM_CHANNELS;
    m_ringBuffer = new LockFreeRingBuffer(ringBufferSize);
    
    qInfo() << "Ring buffer initialized:" << ringBufferSize << "bytes for" << ringBufferFrames << "frames";

    // Always start the stream (for pre-roll)
    err = Pa_StartStream(m_stream);
    if (err != paNoError) {
        qCritical() << "Pa_StartStream() failed:" << Pa_GetErrorText(err);
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
        if (m_ringBuffer) {
            delete m_ringBuffer;
            m_ringBuffer = nullptr;
        }
        Pa_Terminate();
        return false;
    }
    
    qDebug() << "PortAudio stream opened and started successfully";
    return true;
}

void AudioRecorder::finalizePortAudio()
{
    // In case something is still open, ensure it's properly closed.
    if (m_stream) {
        Pa_StopStream(m_stream);
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
    }
    Pa_Terminate();
    
    // Free ring buffer
    if (m_ringBuffer) {
        delete m_ringBuffer;
        m_ringBuffer = nullptr;
    }
}

int AudioRecorder::audioCallback( const void *inputBuffer,
                                  void * /*outputBuffer*/,
                                  unsigned long framesPerBuffer,
                                  const PaStreamCallbackTimeInfo* /*timeInfo*/,
                                  PaStreamCallbackFlags /*statusFlags*/,
                                  void *userData )
{
    AudioRecorder* recorder = reinterpret_cast<AudioRecorder*>(userData);
    recorder->handleAudioData(inputBuffer, framesPerBuffer);
    return paContinue;
}

void AudioRecorder::handleAudioData(const void* inputBuffer, unsigned long frames)
{
    // Real-time safe callback - no mutex, no I/O, no Qt signals
    // Just return if we don't have valid input buffer (no audio data)
    if (!inputBuffer) {
        return;
    }
    
    const int16_t* buffer = static_cast<const int16_t*>(inputBuffer);
    
    // Write to ring buffer (lock-free, real-time safe)
    if (m_ringBuffer) {
        size_t written = m_ringBuffer->write(buffer, sizeof(int16_t), frames);
        if (written < frames) {
            // Ring buffer overflow - this should be rare with proper sizing
            // Just continue, we'll lose some samples
        }
    }
    
    // Calculate volume (cheap operation, atomic store)
    float rms = 0.0f;
    for (unsigned long i = 0; i < frames; ++i) {
        float s = buffer[i] / 32768.0f;
        rms += s * s;
    }
    rms = std::sqrt(rms / frames);
    m_currentVolume.store(qMin(rms * VOLUME_SCALING_FACTOR, 1.0f), std::memory_order_relaxed);
    
    // Note: We can't emit signals from the callback thread safely
    // Volume updates will be polled by the UI thread
}

void AudioRecorder::writeWavHeader(QFile& file, uint32_t sampleRate, uint32_t totalPcmBytesEstimate)
{
    WavHeader h;
    h.sampleRate = sampleRate;
    h.byteRate = sampleRate * 2 /* bytes per sample */ * 1 /* ch */;
    h.blockAlign = 2;
    h.dataSize = totalPcmBytesEstimate;
    h.fileSizeMinus8 = 36 + h.dataSize;
    file.write(reinterpret_cast<const char*>(&h), sizeof(h));
}

void AudioRecorder::patchWavSizes(QFile& file, uint32_t dataBytes)
{
    file.seek(4);
    uint32_t fileSizeMinus8 = 36 + dataBytes;
    file.write(reinterpret_cast<const char*>(&fileSizeMinus8), 4);
    file.seek(40);
    file.write(reinterpret_cast<const char*>(&dataBytes), 4);
}

void AudioRecorder::workerThreadFunction()
{
    std::array<int16_t, 4096> tmp{};
    
    while (!m_workerShouldStop.load() && m_isRecording.load()) {
        if (!m_ringBuffer) {
            QThread::msleep(10);
            continue;
        }
        
        size_t available = m_ringBuffer->getReadAvailable();
        
        if (available > 0) {
            size_t toRead = qMin(available / sizeof(int16_t), tmp.size());
            size_t got = m_ringBuffer->read(tmp.data(), sizeof(int16_t), toRead);
            
            if (got > 0 && m_outputFile.isOpen()) {
                qint64 bytesWritten = m_outputFile.write(reinterpret_cast<const char*>(tmp.data()), 
                                                         got * sizeof(int16_t));
                if (bytesWritten > 0) {
                    m_pcmBytesWritten += bytesWritten;
                } else {
                    qWarning() << "Failed to write PCM data to file:" << m_outputFile.errorString();
                }
            }
        } else {
            QThread::msleep(2);
        }
    }
}
