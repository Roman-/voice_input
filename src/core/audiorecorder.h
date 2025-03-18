#ifndef AUDIORECORDER_H
#define AUDIORECORDER_H

#include <QObject>
#include <QFile>
#include <QElapsedTimer>
#include <QMutex>
#include <QFuture>
#include <QtConcurrent>
#include <portaudio.h>

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

private:
    bool initializePortAudio();
    void finalizePortAudio();

    static int audioCallback( const void *inputBuffer,
                              void *outputBuffer,
                              unsigned long framesPerBuffer,
                              const PaStreamCallbackTimeInfo* timeInfo,
                              PaStreamCallbackFlags statusFlags,
                              void *userData );

    void handleAudioData(const void* inputBuffer, unsigned long frames);

private:
    PaStream*       m_stream;
    QFile           m_outputFile;
    QElapsedTimer   m_elapsedTimer;
    QMutex          m_dataMutex;
    bool            m_isRecording;
    float           m_currentVolume;
    QFuture<void>   m_initFuture;
};

#endif // AUDIORECORDER_H
