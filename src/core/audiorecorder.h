#ifndef AUDIORECORDER_H
#define AUDIORECORDER_H

#include <QObject>
#include <QFile>
#include <QElapsedTimer>
#include <QMutex>
#include <QFuture>
#include <QtConcurrent>
#include <QBuffer>
#include <portaudio.h>
#include <lame/lame.h>

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
    bool initializeMP3Encoder();
    void finalizeMP3Encoder();
    QByteArray encodeToMP3(const short* inputBuffer, int inputSize);

    static int audioCallback( const void *inputBuffer,
                              void *outputBuffer,
                              unsigned long framesPerBuffer,
                              const PaStreamCallbackTimeInfo* timeInfo,
                              PaStreamCallbackFlags statusFlags,
                              void *userData );

    void handleAudioData(const void* inputBuffer, unsigned long frames);

private:
    // PortAudio
    PaStream*       m_stream;
    
    // File output
    QFile           m_outputFile;
    QElapsedTimer   m_elapsedTimer;
    QMutex          m_dataMutex;
    
    // State
    bool            m_isRecording;
    bool            m_audioDeviceInitialized;
    float           m_currentVolume;
    QFuture<void>   m_initFuture;
    
    // MP3 encoding
    lame_global_flags* m_lameGlobal;
    bool               m_mp3Initialized;
    QByteArray         m_encodedData;
    QBuffer            m_dataBuffer; // For intermediate processing
};

#endif // AUDIORECORDER_H
