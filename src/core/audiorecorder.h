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
#include <fdk-aac/aacenc_lib.h>

class AudioRecorder : public QObject
{
    Q_OBJECT
public:
    explicit AudioRecorder(QObject* parent = nullptr);
    ~AudioRecorder();

    bool startRecording();
    void stopRecording();

    // For UI: volume level, file size, etc.
    float currentVolumeLevel() const;
    qint64 fileSize() const;
    qint64 elapsedMs() const;

signals:
    void volumeChanged(float newVolume);
    void recordingStopped();
    void recordingStarted();

private:
    bool initializePortAudio();
    void finalizePortAudio();
    bool initializeAACEncoder();
    void finalizeAACEncoder();
    bool writeMP4Header();
    bool finalizeMP4File();
    QByteArray encodeToAAC(const short* inputBuffer, int inputSize);

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
    float           m_currentVolume;
    QFuture<void>   m_initFuture;
    
    // AAC encoding
    HANDLE_AACENCODER   m_aacEncoder;
    bool                m_aacInitialized;
    QByteArray          m_encodedData;
    QBuffer             m_dataBuffer; // For intermediate processing
};

#endif // AUDIORECORDER_H
