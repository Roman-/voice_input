#include "audiorecorder.h"
#include <QDebug>
#include <QFileInfo>
#include <QDateTime>

#include "config/config.h"

AudioRecorder::AudioRecorder(QObject* parent)
    : QObject(parent),
      m_stream(nullptr),
      m_isRecording(false),
      m_audioDeviceInitialized(false),
      m_currentVolume(0.0f),
      m_lameGlobal(nullptr),
      m_mp3Initialized(false)
{
    // Initialize data buffer for MP3 processing
    m_encodedData.reserve(1024 * 1024); // Pre-allocate 1MB
    m_dataBuffer.setBuffer(&m_encodedData);
    m_dataBuffer.open(QIODevice::ReadWrite);
}

AudioRecorder::~AudioRecorder()
{
    stopRecording();
    finalizePortAudio();
    finalizeMP3Encoder();
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
    
    // Check if audio system is initialized
    if (!m_audioDeviceInitialized) {
        qCritical() << "Cannot start recording - audio system not initialized";
        return false;
    }

    // Make sure the audio stream is active
    if (!isAudioStreamActive()) {
        if (!resumeAudioStream()) {
            qCritical() << "Failed to resume audio stream for recording";
            return false;
        }
    }

    // Prepare output file immediately
    m_outputFile.setFileName(OUTPUT_FILE_PATH);
    if (!m_outputFile.open(QIODevice::WriteOnly)) {
        qCritical() << "Unable to open output file for writing:" << OUTPUT_FILE_PATH;
        return false;
    }

    // Reset data buffer
    m_encodedData.clear();
    m_dataBuffer.seek(0);
    
    // Initialize MP3 encoder
    if (!initializeMP3Encoder()) {
        qCritical() << "Failed to initialize MP3 encoder";
        m_outputFile.close();
        return false;
    }

    // Start the timer
    m_elapsedTimer.start();
    m_isRecording = true;
    
    // Make sure volume is reset on new recording (emit zero volume to reset bar)
    m_currentVolume = 0.0f;
    emit volumeChanged(m_currentVolume);
    
    // Signal that recording has started (UI should reflect this immediately)
    emit recordingStarted();
    qInfo() << "Recording started, writing to:" << OUTPUT_FILE_PATH;
    
    return true;
}

void AudioRecorder::stopRecording()
{
    if (!m_isRecording)
        return;

    qDebug() << "stopRecording() called";
    
    // Pause the audio stream first to prevent new data from being processed
    if (isAudioStreamActive()) {
        pauseAudioStream();
    }
    
    // Use mutex to ensure no audio processing is happening during finalization
    QMutexLocker locker(&m_dataMutex);
    m_isRecording = false;

    // Finalize MP3 encoding
    if (m_mp3Initialized) {
        try {
            // Flush encoder with proper error handling
            QByteArray finalData = encodeToMP3(nullptr, 0);
            if (!finalData.isEmpty()) {
                m_outputFile.write(finalData);
            }
            
            // Clean up encoder
            finalizeMP3Encoder();
        } catch (const std::exception& e) {
            qWarning() << "Exception during MP3 finalization:" << e.what();
        }
    }

    // Close output file
    if (m_outputFile.isOpen()) {
        m_outputFile.close();
    }

    // Reset volume to zero now that recording has stopped
    m_currentVolume = 0.0f;
    emit volumeChanged(m_currentVolume);

    // Verify file was created and has content
    QFileInfo fileInfo(OUTPUT_FILE_PATH);
    if (fileInfo.exists() && fileInfo.size() > 0) {
        qInfo() << "Recording stopped, file saved successfully to:" << OUTPUT_FILE_PATH 
                << "Size:" << fileInfo.size() << "bytes";
    } else {
        qWarning() << "Output file may be missing or empty:" << OUTPUT_FILE_PATH;
    }

    emit recordingStopped();
}

float AudioRecorder::currentVolumeLevel() const
{
    return m_currentVolume;
}

qint64 AudioRecorder::fileSize() const
{
    return m_outputFile.size();
}

qint64 AudioRecorder::elapsedMs() const
{
    return m_elapsedTimer.elapsed();
}

bool AudioRecorder::initializeMP3Encoder()
{
    qDebug() << "Initializing MP3 encoder";

    // Clean up any existing encoder
    finalizeMP3Encoder();

    // Create encoder instance
    m_lameGlobal = lame_init();
    if (!m_lameGlobal) {
        qCritical() << "Failed to initialize LAME MP3 encoder";
        return false;
    }

    // Set encoder parameters
    lame_set_num_channels(m_lameGlobal, NUM_CHANNELS);
    lame_set_in_samplerate(m_lameGlobal, SAMPLE_RATE);
    lame_set_brate(m_lameGlobal, ENCODER_BITRATE / 1000); // LAME uses kbps
    lame_set_quality(m_lameGlobal, 2); // 0=best, 9=worst
    lame_set_mode(m_lameGlobal, NUM_CHANNELS == 1 ? MONO : STEREO);
    
    // Initialize the encoder
    if (lame_init_params(m_lameGlobal) < 0) {
        qCritical() << "Failed to initialize LAME parameters";
        lame_close(m_lameGlobal);
        m_lameGlobal = nullptr;
        return false;
    }

    m_mp3Initialized = true;
    qDebug() << "MP3 encoder initialized successfully";
    return true;
}

void AudioRecorder::finalizeMP3Encoder()
{
    if (m_lameGlobal) {
        lame_close(m_lameGlobal);
        m_lameGlobal = nullptr;
    }
    m_mp3Initialized = false;
}

QByteArray AudioRecorder::encodeToMP3(const short* inputBuffer, int inputSize)
{
    if (!m_mp3Initialized || !m_lameGlobal) {
        return QByteArray();
    }

    QByteArray result;
    int numSamples = 0;
    
    if (inputBuffer) {
        // Regular encoding
        numSamples = inputSize / sizeof(short);
    } else {
        // Flush encoder (end of stream)
        numSamples = 0;
    }

    // MP3 buffer needs to be 1.25x + 7200 bytes larger than the PCM data
    int mp3BufferSize = numSamples * 1.25 + 7200;
    result.resize(mp3BufferSize);
    
    int bytesEncoded = 0;
    
    if (numSamples > 0) {
        // Encode audio samples
        bytesEncoded = lame_encode_buffer(
            m_lameGlobal,
            inputBuffer,      // left channel (mono = only channel)
            nullptr,          // right channel (unused for mono)
            numSamples,
            reinterpret_cast<unsigned char*>(result.data()),
            result.size()
        );
    } else {
        // Flush remaining MP3 data - use safer flush_nogap instead of regular flush
        bytesEncoded = lame_encode_flush_nogap(
            m_lameGlobal,
            reinterpret_cast<unsigned char*>(result.data()),
            result.size()
        );
    }
    
    if (bytesEncoded < 0) {
        qWarning() << "MP3 encoding error:" << bytesEncoded;
        return QByteArray();
    } else if (bytesEncoded == 0) {
        // No bytes to encode, return empty array
        return QByteArray();
    }
    
    // Safety check before resizing
    if (bytesEncoded > mp3BufferSize) {
        qWarning() << "MP3 encoding buffer overflow, limiting output";
        bytesEncoded = mp3BufferSize;
    }
    
    // Resize to actual encoded size
    result.resize(bytesEncoded);
    return result;
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
    }

    // Open default stream with input channels, no output channels
    err = Pa_OpenDefaultStream(&m_stream,
                               NUM_CHANNELS,
                               0,
                               paInt16,
                               SAMPLE_RATE,
                               256,
                               &AudioRecorder::audioCallback,
                               this);
    if (err != paNoError) {
        qCritical() << "Pa_OpenDefaultStream() failed:" << Pa_GetErrorText(err);
        Pa_Terminate();
        return false;
    }

    // Only start the stream if requested
    if (startStreamImmediately) {
        err = Pa_StartStream(m_stream);
        if (err != paNoError) {
            qCritical() << "Pa_StartStream() failed:" << Pa_GetErrorText(err);
            Pa_CloseStream(m_stream);
            m_stream = nullptr;
            Pa_Terminate();
            return false;
        }
        qDebug() << "PortAudio stream opened and started successfully";
    } else {
        qDebug() << "PortAudio stream opened successfully but not started (paused)";
    }
    
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
    QMutexLocker locker(&m_dataMutex);
    
    // Just return if we don't have valid input buffer (no audio data)
    if (!inputBuffer) {
        return;
    }
    
    // Calculate volume level from audio data
    const short* buffer = reinterpret_cast<const short*>(inputBuffer);
    long sum = 0;
    for (unsigned long i = 0; i < frames; ++i) {
        sum += qAbs(buffer[i]);
    }
    float average = static_cast<float>(sum) / frames;
    
    // Scale the volume using the config scaling factor
    float normalizedVolume = average / 32767.0f;  // normalize to ~0..1
    m_currentVolume = qMin(normalizedVolume * VOLUME_SCALING_FACTOR, 1.0f);  // Apply scaling with 1.0 max
    
    // Only emit volume changes if we're recording
    if (m_isRecording) {
        emit volumeChanged(m_currentVolume);
        
        // Log volume levels periodically for debugging
        static QElapsedTimer logTimer;
        if (!logTimer.isValid() || logTimer.elapsed() > 5000) { // Log every 5 seconds
            qDebug() << "Raw volume:" << normalizedVolume << "Scaled volume:" << m_currentVolume;
            logTimer.start();
        }
    }
    
    // Only process for recording if we're actually recording and stream is ready
    if (!m_isRecording || !m_stream) {
        return;
    }

    // Encode and write audio data if encoder is ready
    if (m_mp3Initialized) {
        try {
            QByteArray encodedData = encodeToMP3(buffer, frames * sizeof(short));
            if (!encodedData.isEmpty()) {
                if (!m_outputFile.write(encodedData)) {
                    qWarning() << "Failed to write MP3 data to file:" << m_outputFile.errorString();
                }
            }
        } catch (const std::exception& e) {
            qWarning() << "Exception during MP3 encoding:" << e.what();
        }
    }
}
