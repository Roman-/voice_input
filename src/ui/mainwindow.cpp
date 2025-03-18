#include "mainwindow.h"
#include <QVBoxLayout>
#include <QDebug>
#include <QPalette>
#include <QColor>

#include "core/audiorecorder.h"

MainWindow::MainWindow(AudioRecorder* recorder, QWidget* parent)
    : QMainWindow(parent),
      m_recorder(recorder),
      m_statusLabel(new QLabel(this)),
      m_volumeLabel(new QLabel(this))
{
    // Basic UI setup
    auto central = new QWidget(this);
    auto layout = new QVBoxLayout(central);

    m_statusLabel->setText("Initializing Audio Recorder...");
    m_volumeLabel->setText("Volume: 0.0");

    layout->addWidget(m_statusLabel);
    layout->addWidget(m_volumeLabel);

    setCentralWidget(central);

    // Indicate initialization in progress with a distinctive background color
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(255, 230, 200));
    setPalette(pal);
    
    // Set normal background after a short delay
    QTimer::singleShot(3000, this, [this]() {
        QPalette pal = palette();
        pal.setColor(QPalette::Window, QColor(Qt::white));
        setPalette(pal);
        m_statusLabel->setText("Ready to record...");
    });

    // Connect signals from recorder
    connect(m_recorder, &AudioRecorder::volumeChanged, this, &MainWindow::onVolumeChanged);
    connect(m_recorder, &AudioRecorder::recordingStopped, this, &MainWindow::onRecordingStopped);

    // Periodically update UI for elapsed time and file size
    m_updateTimer.setInterval(500); // 0.5 seconds
    connect(&m_updateTimer, &QTimer::timeout, this, &MainWindow::updateUI);
    m_updateTimer.start();
}

void MainWindow::updateUI()
{
    if (!m_recorder) return;

    qint64 size = m_recorder->fileSize();
    qint64 elapsed = m_recorder->elapsedMs();
    
    // Only update recording info after we have some data (indicates initialization is complete)
    if (size > 0) {
        QString infoText = QString("Recording... Elapsed: %1 ms | File size: %2 bytes")
                .arg(elapsed)
                .arg(size);
        m_statusLabel->setText(infoText);
        
        // If this is the first data we're seeing, change background to normal
        static bool firstData = true;
        if (firstData) {
            firstData = false;
            QPalette pal = palette();
            pal.setColor(QPalette::Window, QColor(Qt::white));
            setPalette(pal);
        }
    }
}

void MainWindow::onVolumeChanged(float volume)
{
    m_volumeLabel->setText(QString("Volume: %1").arg(volume, 0, 'f', 3));
}

void MainWindow::onRecordingStopped()
{
    m_statusLabel->setText("Recording Stopped. File saved.");
    // Restore background color to default
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(Qt::white));
    setPalette(pal);
}
