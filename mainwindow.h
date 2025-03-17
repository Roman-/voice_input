#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QTimer>
#include <QDateTime>
#include <QAudioInput>
#include <QAudioFormat>
#include <QAudioDeviceInfo>
#include <QIODevice>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QLineEdit>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void startRecording();
    void stopRecording(bool sendToSTT);
    void onFfmpegFinished(int exitCode, QProcess::ExitStatus exitStatus);

    void updateRecordingStats();  // Timer callback to update file size, duration
    void updateVolumeLevel();     // Timer callback to update volume progress bar

    void sendForTranscription();
    void onTranscriptionSuccess(const QString &text);
    void onTranscriptionError(const QString &errorMsg);

    void retryTranscription(); // one-time retry

private:
    // Widgets
    QLabel      *m_statusLabel;
    QProgressBar *m_volumeBar;
    QPushButton *m_sendButton;
    QPushButton *m_stopButton;
    QPushButton *m_retryButton;

    // FFMPEG recording
    QProcess    m_ffmpegProcess;
    bool        m_isRecording = false;
    qint64      m_recordStartTime = 0;
    qint64      m_currentFileSize = 0;
    QTimer      m_updateTimer; // updates file size & volume
    const qint64 MAX_FILE_SIZE_BYTES = 35 * 1024 * 1024; // 35 MB

    // Volume measurement
    QAudioInput *m_audioInput = nullptr;
    QIODevice   *m_audioDevice = nullptr;
    QTimer      m_volumeTimer;

    // Transcription
    bool        m_retryUsed = false;
    bool        m_sendRequested = false;

    void initUi();
    void initAudioInputForVolume();
    void showError(const QString &message);
};

#endif // MAINWINDOW_H
