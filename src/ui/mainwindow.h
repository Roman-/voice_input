#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include <QHBoxLayout>
#include <QPushButton>

class AudioRecorder;
class OpenAiTranscriptionService;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(AudioRecorder* recorder, QWidget* parent = nullptr);
    ~MainWindow() = default;
    
    // Cancel any ongoing transcription - used by signal handler
    void cancelTranscription();
    
    // Hide window and prepare for next recording - used after transcription completed
    void hideAndReset();
    
    // Get the current exit code
    int exitCode() const { return m_exitCode; }

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
    void keyPressEvent(QKeyEvent*) override;
    
    // Override close event to handle window close requests
    void closeEvent(QCloseEvent* event) override;
    
    // Override show event to reset UI when window is shown
    void showEvent(QShowEvent* event) override;

private:
    void createVolumeBar();
    void updateVolumeBar(float volume);
    void setupTranscriptionUI();
    void resetUIForNextRecording(); // Resets UI only without removing files

private:
    AudioRecorder* m_recorder;
    OpenAiTranscriptionService* m_transcriptionService;
    QLabel*        m_statusLabel;
    QLabel*        m_transcriptionLabel;
    QTimer         m_updateTimer;
    QWidget*       m_volumeBar;
    QHBoxLayout*   m_volumeBarLayout;
    QPushButton*   m_transcribeButton;
    bool           m_hasApiKey;
    QTimer         m_autoCloseTimer;
    int            m_autoCloseSeconds;
    int            m_exitCode;  // Exit code to use when application terminates
    bool           m_isClosingPermanently;
    bool m_pressCtrlVAfterCopy{true};
};

#endif // MAINWINDOW_H
