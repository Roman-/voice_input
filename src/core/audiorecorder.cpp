#include "audiorecorder.h"
#include "audioconverter.h"
#include "formatutils.h"
#include <QDebug>
#include <QFileInfo>
#include <QDateTime>
#include <QThread>
#include <QMutexLocker>
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
      m_audioConverter(new AudioConverter(this)),
      m_selectedDeviceId(paNoDevice),
      m_currentDeviceId(paNoDevice)
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

QVector<AudioRecorder::AudioInputDevice> AudioRecorder::availableInputDevices() const
{
    QMutexLocker locker(&m_deviceMutex);
    return m_inputDevices;
}

int AudioRecorder::currentInputDeviceId() const
{
    return m_currentDeviceId;
}

QString AudioRecorder::currentInputDeviceName() const
{
    QMutexLocker locker(&m_deviceMutex);
    for (const auto& device : m_inputDevices) {
        if (device.id == m_currentDeviceId) {
            return device.name;
        }
    }
    return QString();
}

bool AudioRecorder::setInputDevice(int deviceId)
{
    if (!m_audioDeviceInitialized) {
        qWarning() << "Cannot switch microphone - audio system not initialized";
        return false;
    }

    if (m_isRecording.load()) {
        qWarning() << "Cannot switch microphone while recording is active";
        return false;
    }

    if (deviceId == m_currentDeviceId) {
        qInfo() << "Requested microphone is already active";
        return true;
    }

    if (!isValidDeviceId(deviceId)) {
        qWarning() << "Invalid microphone id" << deviceId << "requested";
        return false;
    }

    int previousDeviceId = m_currentDeviceId;

    finalizePortAudio();
    m_selectedDeviceId = deviceId;

    if (!initializePortAudio()) {
        qCritical() << "Failed to switch to microphone id" << deviceId << "- attempting to revert";
        if (isValidDeviceId(previousDeviceId)) {
            m_selectedDeviceId = previousDeviceId;
            if (!initializePortAudio()) {
                qCritical() << "Failed to restore previous microphone as well.";
            }
        }
        return false;
    }

    return true;
}

bool AudioRecorder::refreshInputDeviceList()
{
    if (!m_audioDeviceInitialized) {
        return false;
    }
    return refreshAvailableDevices();
}

bool AudioRecorder::startRecording()
{
    qInfo() << "startRecording() called";
    
    m_timingTracker.reset();
    
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
    
    m_timingTracker.start("Recording Pipeline");
    
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
    
    m_timingTracker.markStage("WAV Finalization");
    
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
                qint64 conversionTimeMs = 0;
                bool conversionSuccess = m_audioConverter->convertWavToMp3(OUTPUT_FILE_PATH_WAV, OUTPUT_FILE_PATH, &conversionTimeMs);
                
                if (conversionSuccess) {
                    if (conversionTimeMs > 0) {
                        m_timingTracker.setStageDuration("MP3 Conversion", conversionTimeMs);
                    }
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
                m_timingTracker.setStageDuration("MP3 Conversion (skipped)", 0);
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

    m_timingTracker.markStage("File Cleanup");

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
    Q_UNUSED(startStreamImmediately);
    qDebug() << "Initializing PortAudio";

    // Try to terminate any prior instances first, for safety
    Pa_Terminate();
    
    // Initialize PortAudio library
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        qCritical() << "Pa_Initialize() failed:" << Pa_GetErrorText(err);
        return false;
    }
    
    if (!refreshAvailableDevices()) {
        qCritical() << "No audio input devices available";
        Pa_Terminate();
        return false;
    }

    int selectedDevice = m_selectedDeviceId;
    if (!isValidDeviceId(selectedDevice)) {
        selectedDevice = Pa_GetDefaultInputDevice();
    }

    if (!isValidDeviceId(selectedDevice)) {
        auto devices = availableInputDevices();
        if (!devices.isEmpty()) {
            selectedDevice = devices.first().id;
        }
    }

    if (!isValidDeviceId(selectedDevice)) {
        qCritical() << "Unable to determine a valid input device";
        Pa_Terminate();
        return false;
    }

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(selectedDevice);
    if (!deviceInfo) {
        qCritical() << "Failed to query device info for id" << selectedDevice;
        Pa_Terminate();
        return false;
    }

    qInfo() << "Using input device:" << deviceInfo->name
            << "with" << deviceInfo->maxInputChannels << "channels";

    m_sampleRate = deviceInfo->defaultSampleRate;

    // Use Pa_OpenStream with suggestedLatency for low latency
    PaStreamParameters in;
    in.device = selectedDevice;
    int channelCount = NUM_CHANNELS;
    if (deviceInfo->maxInputChannels > 0) {
        channelCount = qMin(NUM_CHANNELS, deviceInfo->maxInputChannels);
    }
    if (channelCount <= 0) {
        channelCount = 1;
    }
    in.channelCount = channelCount;
    in.sampleFormat = paInt16;
    in.suggestedLatency = deviceInfo->defaultLowInputLatency;
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
    size_t ringBufferFrames = static_cast<size_t>(m_sampleRate * 0.5); // 500ms
    size_t ringBufferSize = ringBufferFrames * sizeof(int16_t) * NUM_CHANNELS;

    if (m_ringBuffer) {
        delete m_ringBuffer;
    }
    m_ringBuffer = new LockFreeRingBuffer(ringBufferSize);

    qInfo() << "Ring buffer initialized:" << ringBufferSize << "bytes for" << ringBufferFrames << "frames";

    // Always start the stream (for pre-roll)
    err = Pa_StartStream(m_stream);
    if (err != paNoError) {
        qCritical() << "Pa_StartStream() failed:" << Pa_GetErrorText(err);
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
        delete m_ringBuffer;
        m_ringBuffer = nullptr;
        Pa_Terminate();
        return false;
    }

    int previousDeviceId = m_currentDeviceId;
    m_currentDeviceId = selectedDevice;
    m_selectedDeviceId = selectedDevice;

    if (previousDeviceId != m_currentDeviceId) {
        emit inputDeviceChanged(m_currentDeviceId, currentInputDeviceName());
    }

    qDebug() << "PortAudio stream opened and started successfully";
    return true;
}

bool AudioRecorder::refreshAvailableDevices()
{
    QVector<AudioInputDevice> devices;
    int numDevices = Pa_GetDeviceCount();

    for (int i = 0; i < numDevices; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info || info->maxInputChannels <= 0) {
            continue;
        }

        AudioInputDevice device;
        device.id = i;
        device.name = QString::fromUtf8(info->name);
        device.maxInputChannels = info->maxInputChannels;
        device.defaultSampleRate = info->defaultSampleRate;
        devices.append(device);
    }

    bool changed = false;
    {
        QMutexLocker locker(&m_deviceMutex);
        if (devices.size() != m_inputDevices.size()) {
            changed = true;
        } else {
            for (int i = 0; i < devices.size(); ++i) {
                const auto& lhs = devices[i];
                const auto& rhs = m_inputDevices[i];
                if (lhs.id != rhs.id ||
                    lhs.name != rhs.name ||
                    lhs.maxInputChannels != rhs.maxInputChannels ||
                    std::abs(lhs.defaultSampleRate - rhs.defaultSampleRate) > 0.001) {
                    changed = true;
                    break;
                }
            }
        }
        m_inputDevices = devices;
    }

    if (changed) {
        emit deviceListChanged();
    }

    return !devices.isEmpty();
}

bool AudioRecorder::isValidDeviceId(int deviceId) const
{
    if (deviceId == paNoDevice) {
        return false;
    }

    QMutexLocker locker(&m_deviceMutex);
    for (const auto& device : m_inputDevices) {
        if (device.id == deviceId) {
            return true;
        }
    }
    return false;
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

    m_currentDeviceId = paNoDevice;
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
