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
      m_aacEncoder(nullptr),
      m_aacInitialized(false)
{
    // Initialize data buffer for MP4 processing
    m_encodedData.reserve(1024 * 1024); // Pre-allocate 1MB
    m_dataBuffer.setBuffer(&m_encodedData);
    m_dataBuffer.open(QIODevice::ReadWrite);
}

AudioRecorder::~AudioRecorder()
{
    stopRecording();
    finalizePortAudio();
    finalizeAACEncoder();
}

bool AudioRecorder::startRecording()
{
    qInfo() << "startRecording() called";

    // Prepare output file immediately
    m_outputFile.setFileName(OUTPUT_FILE_PATH);
    if (!m_outputFile.open(QIODevice::WriteOnly)) {
        qCritical() << "Unable to open output file for writing:" << OUTPUT_FILE_PATH;
        return false;
    }

    // Reset data buffer
    m_encodedData.clear();
    m_dataBuffer.seek(0);
    
    // Initialize AAC encoder
    if (!initializeAACEncoder()) {
        qCritical() << "Failed to initialize AAC encoder";
        m_outputFile.close();
        return false;
    }
    
    // Write MP4 header
    if (!writeMP4Header()) {
        qCritical() << "Failed to write MP4 header";
        m_outputFile.close();
        return false;
    }

    // Start the timer
    m_elapsedTimer.start();
    m_isRecording = true;
    
    // Signal that recording has started (UI should reflect this immediately)
    emit recordingStarted();
    
    // Initialize PortAudio asynchronously - don't block UI
    m_initFuture = QtConcurrent::run([this]() {
        if (!initializePortAudio()) {
            qCritical() << "Failed to initialize PortAudio";
            m_isRecording = false;
            m_outputFile.close();
            return;
        }
        
        // Mark that the audio device is ready - no artificial delay needed
        m_audioDeviceInitialized = true;
        qInfo() << "Audio device initialized successfully, recording to:" << OUTPUT_FILE_PATH;
    });
    
    return true;
}

void AudioRecorder::stopRecording()
{
    if (!m_isRecording)
        return;

    qDebug() << "stopRecording() called";
    m_isRecording = false;
    
    // Wait for initialization to complete if it's still running
    if (m_initFuture.isRunning()) {
        m_initFuture.waitForFinished();
    }

    // Stop PortAudio stream
    if (m_stream) {
        Pa_StopStream(m_stream);
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
    }

    // Finalize AAC encoding and MP4 file
    if (m_aacInitialized) {
        // Flush encoder
        QByteArray finalData = encodeToAAC(nullptr, 0);
        if (!finalData.isEmpty()) {
            m_outputFile.write(finalData);
        }
        
        // Finalize MP4 container
        finalizeMP4File();
        
        // Clean up encoder
        finalizeAACEncoder();
    }

    // Close output file
    if (m_outputFile.isOpen()) {
        m_outputFile.close();
    }

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

bool AudioRecorder::initializeAACEncoder()
{
    qDebug() << "Initializing AAC encoder";

    // Clean up any existing encoder
    finalizeAACEncoder();

    // Create encoder instance
    AACENC_ERROR err = aacEncOpen(&m_aacEncoder, 0, NUM_CHANNELS);
    if (err != AACENC_OK) {
        qCritical() << "Failed to open AAC encoder:" << err;
        return false;
    }

    // Set encoder parameters
    int aot = AOT_AAC_LC;                         // AAC Low Complexity profile
    int sampleRate = SAMPLE_RATE;
    int channelMode = MODE_1;                     // Mono
    int bitrate = ENCODER_BITRATE;                // From config.h
    int afterburner = 1;                          // Quality boost
    int signaling = 0;                            // Implicit signaling

    // Configure the encoder
    if ((err = aacEncoder_SetParam(m_aacEncoder, AACENC_AOT, aot)) != AACENC_OK) {
        qCritical() << "Unable to set AOT:" << err;
        aacEncClose(&m_aacEncoder);
        return false;
    }
    if ((err = aacEncoder_SetParam(m_aacEncoder, AACENC_SAMPLERATE, sampleRate)) != AACENC_OK) {
        qCritical() << "Unable to set sample rate:" << err;
        aacEncClose(&m_aacEncoder);
        return false;
    }
    if ((err = aacEncoder_SetParam(m_aacEncoder, AACENC_CHANNELMODE, channelMode)) != AACENC_OK) {
        qCritical() << "Unable to set channel mode:" << err;
        aacEncClose(&m_aacEncoder);
        return false;
    }
    if ((err = aacEncoder_SetParam(m_aacEncoder, AACENC_BITRATE, bitrate)) != AACENC_OK) {
        qCritical() << "Unable to set bitrate:" << err;
        aacEncClose(&m_aacEncoder);
        return false;
    }
    if ((err = aacEncoder_SetParam(m_aacEncoder, AACENC_AFTERBURNER, afterburner)) != AACENC_OK) {
        qCritical() << "Unable to set afterburner:" << err;
        aacEncClose(&m_aacEncoder);
        return false;
    }
    if ((err = aacEncoder_SetParam(m_aacEncoder, AACENC_SIGNALING_MODE, signaling)) != AACENC_OK) {
        qCritical() << "Unable to set signaling mode:" << err;
        aacEncClose(&m_aacEncoder);
        return false;
    }
    if ((err = aacEncoder_SetParam(m_aacEncoder, AACENC_TRANSMUX, TT_MP4_ADTS)) != AACENC_OK) {
        qCritical() << "Unable to set transmux:" << err;
        aacEncClose(&m_aacEncoder);
        return false;
    }

    // Initialize encoder
    if ((err = aacEncEncode(m_aacEncoder, NULL, NULL, NULL, NULL)) != AACENC_OK) {
        qCritical() << "Unable to initialize encoder:" << err;
        aacEncClose(&m_aacEncoder);
        return false;
    }

    m_aacInitialized = true;
    qDebug() << "AAC encoder initialized successfully";
    return true;
}

void AudioRecorder::finalizeAACEncoder()
{
    if (m_aacEncoder) {
        aacEncClose(&m_aacEncoder);
        m_aacEncoder = nullptr;
    }
    m_aacInitialized = false;
}

bool AudioRecorder::writeMP4Header()
{
    // Simple ADTS header for MP4 file
    // The ADTS format adds its own header to each AAC frame
    // This isn't a full MP4 header, but it makes the file playable with most players
    
    qDebug() << "Writing MP4 header";
    return true;
}

bool AudioRecorder::finalizeMP4File()
{
    // For a proper MP4 file, we'd need to update various fields in the file
    // For simplicity, we're using ADTS which embeds headers in each frame
    qDebug() << "Finalizing MP4 file";
    return true;
}

QByteArray AudioRecorder::encodeToAAC(const short* inputBuffer, int inputSize)
{
    if (!m_aacInitialized) {
        return QByteArray();
    }

    QByteArray result;

    // Prepare in/out buffers
    AACENC_BufDesc inBufDesc = { 0 };
    AACENC_BufDesc outBufDesc = { 0 };
    AACENC_InArgs inArgs = { 0 };
    AACENC_OutArgs outArgs = { 0 };

    // Input buffer
    void* inPtr = (void*)inputBuffer;
    INT inSize = inputSize;
    INT inIdentifier = IN_AUDIO_DATA;
    INT inElemSize = sizeof(short);

    inBufDesc.numBufs = 1;
    inBufDesc.bufs = &inPtr;
    inBufDesc.bufferIdentifiers = &inIdentifier;
    inBufDesc.bufSizes = &inSize;
    inBufDesc.bufElSizes = &inElemSize;

    // Output buffer
    int outSize = 8192; // Should be enough for the encoded output
    result.resize(outSize);
    void* outPtr = result.data();
    INT outIdentifier = OUT_BITSTREAM_DATA;
    INT outElemSize = 1;

    outBufDesc.numBufs = 1;
    outBufDesc.bufs = &outPtr;
    outBufDesc.bufferIdentifiers = &outIdentifier;
    outBufDesc.bufSizes = &outSize;
    outBufDesc.bufElSizes = &outElemSize;

    // Encoding arguments
    if (inputBuffer) {
        // For audio data
        inArgs.numInSamples = inputSize / sizeof(short);
    } else {
        // For flush
        inArgs.numInSamples = -1;
    }

    // Encode
    AACENC_ERROR err = aacEncEncode(m_aacEncoder, &inBufDesc, &outBufDesc, &inArgs, &outArgs);
    if (err != AACENC_OK) {
        if (err != AACENC_ENCODE_EOF) {
            qWarning() << "AAC encoding error:" << err;
        }
        return QByteArray();
    }

    // Resize result to the actual output size
    result.resize(outArgs.numOutBytes);
    return result;
}

bool AudioRecorder::initializePortAudio()
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

    err = Pa_StartStream(m_stream);
    if (err != paNoError) {
        qCritical() << "Pa_StartStream() failed:" << Pa_GetErrorText(err);
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
        Pa_Terminate();
        return false;
    }

    qDebug() << "PortAudio stream opened successfully";
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
    
    // Always calculate volume level when we have data, even if not recording
    // This ensures volume meter shows feedback immediately
    const short* buffer = reinterpret_cast<const short*>(inputBuffer);
    long sum = 0;
    for (unsigned long i = 0; i < frames; ++i) {
        sum += qAbs(buffer[i]);
    }
    float average = static_cast<float>(sum) / frames;
    
    // Scale the volume using the config scaling factor
    float normalizedVolume = average / 32767.0f;  // normalize to ~0..1
    m_currentVolume = qMin(normalizedVolume * VOLUME_SCALING_FACTOR, 1.0f);  // Apply scaling with 1.0 max
    
    // Always emit volume change to update UI
    emit volumeChanged(m_currentVolume);
    
    // When we get first audio data, it confirms the audio device is fully ready
    // This is the proper way to detect initialization completion
    static bool signalEmitted = false;
    if (m_audioDeviceInitialized && !signalEmitted) {
        signalEmitted = true;
        emit audioDeviceReady();
    }
    
    // Only process for recording if we're actually recording and stream is ready
    if (!m_isRecording || !m_stream) {
        return;
    }

    // Encode and write audio data if encoder is ready
    if (m_aacInitialized) {
        QByteArray encodedData = encodeToAAC(buffer, frames * sizeof(short));
        if (!encodedData.isEmpty()) {
            m_outputFile.write(encodedData);
        }
    }
}
