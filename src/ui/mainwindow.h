#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include <QHBoxLayout>
#include <QPushButton>

class AudioRecorder;
class TranscriptionService;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(AudioRecorder* recorder, QWidget* parent = nullptr);
    ~MainWindow() = default;
    
    // Cancel any ongoing transcription - used by signal handler
    void cancelTranscription();

private slots:
    void updateUI();
    void onVolumeChanged(float volume);
    void onRecordingStopped();
    void onRecordingStarted();
    void onAudioDeviceReady();
    
    // Transcription slots
    void onTranscribeButtonClicked();
    void onTranscriptionCompleted(const QString& transcribedText);
    void onTranscriptionFailed(const QString& errorMessage);
    void onTranscriptionProgress(const QString& status);

protected:
    // Override key press event to handle Enter/Escape keys
    void keyPressEvent(QKeyEvent* event) override;

private:
    void createVolumeBar();
    void updateVolumeBar(float volume);
    void setupTranscriptionUI();

private:
    AudioRecorder* m_recorder;
    TranscriptionService* m_transcriptionService;
    QLabel*        m_statusLabel;
    QLabel*        m_volumeLabel;
    QLabel*        m_transcriptionLabel;
    QTimer         m_updateTimer;
    QWidget*       m_volumeBar;
    QHBoxLayout*   m_volumeBarLayout;
    QPushButton*   m_transcribeButton;
    bool           m_hasApiKey;
    QTimer         m_autoCloseTimer;
    int            m_autoCloseSeconds;
};

#endif // MAINWINDOW_H
