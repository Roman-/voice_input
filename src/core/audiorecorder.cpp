#include "audiorecorder.h"
#include <QDebug>
#include <QFileInfo>
#include <QDateTime>

#include "config/config.h"

AudioRecorder::AudioRecorder(QObject* parent)
    : QObject(parent),
      m_stream(nullptr),
      m_isRecording(false),
      m_currentVolume(0.0f)
{
}

AudioRecorder::~AudioRecorder()
{
    stopRecording();
    finalizePortAudio();
}

bool AudioRecorder::startRecording()
{
    qInfo() << "[INFO] startRecording() called";

    if (!initializePortAudio()) {
        qCritical() << "[ERROR] Failed to initialize PortAudio";
        return false;
    }

    m_outputFile.setFileName(OUTPUT_FILE_PATH);
    if (!m_outputFile.open(QIODevice::WriteOnly)) {
        qCritical() << "[ERROR] Unable to open output file for writing:" << OUTPUT_FILE_PATH;
        return false;
    }

    // (Here you'd initialize your AAC encoder with libfdk-aac or similar)
    // For now, we'll just pretend we directly write raw data for demonstration.

    // Start the timer
    m_elapsedTimer.start();
    m_isRecording = true;
    qInfo() << "[INFO] Recording started:" << OUTPUT_FILE_PATH;
    return true;
}

void AudioRecorder::stopRecording()
{
    if (!m_isRecording)
        return;

    qInfo() << "[DEBUG] stopRecording() called";
    m_isRecording = false;

    // Stop PortAudio stream
    if (m_stream) {
        Pa_StopStream(m_stream);
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
    }

    // Close output file
    if (m_outputFile.isOpen()) {
        m_outputFile.close();
    }

    // (Here you'd finalize/flush the AAC encoder)
    qInfo() << "[INFO] Recording stopped, file saved successfully";

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

bool AudioRecorder::initializePortAudio()
{
    qInfo() << "[DEBUG] Initializing PortAudio";

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        qCritical() << "[ERROR] Pa_Initialize() failed:" << Pa_GetErrorText(err);
        return false;
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
        qCritical() << "[ERROR] Pa_OpenDefaultStream() failed:" << Pa_GetErrorText(err);
        Pa_Terminate();
        return false;
    }

    err = Pa_StartStream(m_stream);
    if (err != paNoError) {
        qCritical() << "[ERROR] Pa_StartStream() failed:" << Pa_GetErrorText(err);
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
        Pa_Terminate();
        return false;
    }

    qInfo() << "[DEBUG] PortAudio stream opened successfully";
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
    if (!m_isRecording || !inputBuffer) {
        return;
    }

    // Basic volume level calculation for demonstration
    const short* buffer = reinterpret_cast<const short*>(inputBuffer);
    long sum = 0;
    for (unsigned long i = 0; i < frames; ++i) {
        sum += qAbs(buffer[i]);
    }
    float average = static_cast<float>(sum) / frames;
    m_currentVolume = average / 32767.0f;  // normalized ~0..1

    // Emit volumeChanged if desired
    emit volumeChanged(m_currentVolume);

    // Write raw data directly to file (placeholder)
    // In reality, you'd feed the data to your AAC encoder, then write the encoded frames.
    m_outputFile.write(reinterpret_cast<const char*>(inputBuffer),
                       frames * sizeof(short));
}
