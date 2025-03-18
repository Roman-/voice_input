#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QTimer>

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

private:
    AudioRecorder* m_recorder;
    QLabel*        m_statusLabel;
    QLabel*        m_volumeLabel;
    QTimer         m_updateTimer;
};

#endif // MAINWINDOW_H
