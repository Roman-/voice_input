#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include <QHBoxLayout>

class AudioRecorder;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(AudioRecorder* recorder, QWidget* parent = nullptr);
    ~MainWindow() = default;

private slots:
    void updateUI();
    void onVolumeChanged(float volume);
    void onRecordingStopped();
    void onRecordingStarted();
    void onAudioDeviceReady();

protected:
    // Override key press event to handle Enter/Escape keys
    void keyPressEvent(QKeyEvent* event) override;

private:
    void createVolumeBar();
    void updateVolumeBar(float volume);

private:
    AudioRecorder* m_recorder;
    QLabel*        m_statusLabel;
    QLabel*        m_volumeLabel;
    QTimer         m_updateTimer;
    QWidget*       m_volumeBar;
    QHBoxLayout*   m_volumeBarLayout;
};

#endif // MAINWINDOW_H
