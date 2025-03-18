#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDebug>
#include <QPalette>
#include <QColor>
#include <QProgressBar>
#include <QKeyEvent>
#include <QApplication>

#include "core/audiorecorder.h"
#include "config/config.h"

MainWindow::MainWindow(AudioRecorder* recorder, QWidget* parent)
    : QMainWindow(parent),
      m_recorder(recorder),
      m_statusLabel(new QLabel(this)),
      m_volumeLabel(new QLabel(this))
{
    // Set window properties
    setWindowTitle("Audio Recorder");
    resize(400, 200);  // Set a reasonable initial size
    
    // Basic UI setup
    auto central = new QWidget(this);
    auto layout = new QVBoxLayout(central);

    m_statusLabel->setText("Starting...");
    m_volumeLabel->setText("Preparing to record...");
    
    // Create volume meter
    m_volumeBar = new QWidget(this);
    m_volumeBarLayout = new QHBoxLayout(m_volumeBar);
    m_volumeBarLayout->setContentsMargins(10, 5, 10, 5);
    createVolumeBar();

    layout->addWidget(m_statusLabel);
    layout->addWidget(m_volumeBar);
    layout->addWidget(m_volumeLabel);

    setCentralWidget(central);

    // Set colors for recording state directly (green background)
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(240, 255, 240));  // Light green for recording
    pal.setColor(QPalette::WindowText, QColor(0, 0, 0));    // Black text
    pal.setColor(QPalette::Text, QColor(0, 0, 0));          // Black text for widgets
    
    // Apply palette to window and labels directly
    setPalette(pal);
    m_statusLabel->setPalette(pal);
    m_volumeLabel->setPalette(pal);
    
    // Set status text style to be bold and larger with distinctive color
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 12pt; color: #006600;");
    
    // Show recording state immediately
    m_statusLabel->setText("Recording in progress... (Press Enter to save, Esc to cancel)");
    m_volumeLabel->setText("Initializing audio device...");

    // Connect signals from recorder
    connect(m_recorder, &AudioRecorder::volumeChanged, this, &MainWindow::onVolumeChanged);
    connect(m_recorder, &AudioRecorder::recordingStopped, this, &MainWindow::onRecordingStopped);
    connect(m_recorder, &AudioRecorder::recordingStarted, this, &MainWindow::onRecordingStarted);

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
    
    // Format elapsed time in a more readable format
    int seconds = elapsed / 1000;
    int minutes = seconds / 60;
    seconds %= 60;
    
    // Format file size in KB
    float sizeKB = static_cast<float>(size) / 1024.0f;
    
    // Only update recording info after we have some data (indicates initialization is complete)
    if (size > 0) {
        QString infoText = QString("Recording... %1:%2 | Size: %3 KB")
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'))
                .arg(sizeKB, 0, 'f', 2);
        m_statusLabel->setText(infoText);
        
            // No need to change background on first data anymore, 
        // that's handled by onRecordingStarted()
    }
}

void MainWindow::onVolumeChanged(float volume)
{
    // First volume update means audio is now flowing
    static bool firstVolume = true;
    if (firstVolume) {
        firstVolume = false;
        m_volumeLabel->setText("Audio capture active");
    }
    
    // Update volume display
    m_volumeLabel->setText(QString("Volume: %1").arg(volume, 0, 'f', 3));
    updateVolumeBar(volume);
}

void MainWindow::createVolumeBar()
{
    // Clear any existing widgets
    QLayoutItem* item;
    while ((item = m_volumeBarLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    
    // Create 20 segments for the volume meter
    const int segments = 20;
    for (int i = 0; i < segments; i++) {
        QProgressBar* bar = new QProgressBar(this);
        bar->setFixedWidth(8);
        bar->setFixedHeight(30);
        bar->setMinimum(0);
        bar->setMaximum(100);
        bar->setValue(0);
        bar->setTextVisible(false);
        
        // Color gradient from green to yellow to red
        QColor color;
        if (i < segments * 0.6) {            // First 60% - Green
            color = QColor(0, 200, 0);
        } else if (i < segments * 0.8) {     // Next 20% - Yellow
            color = QColor(200, 200, 0);
        } else {                             // Last 20% - Red
            color = QColor(200, 0, 0);
        }
        
        // Set the bar color via stylesheet
        QString style = QString("QProgressBar { background: #444; border: 1px solid #222; border-radius: 2px; } "
                               "QProgressBar::chunk { background-color: %1; }")
                        .arg(color.name());
        bar->setStyleSheet(style);
        
        m_volumeBarLayout->addWidget(bar);
    }
}

void MainWindow::updateVolumeBar(float volume)
{
    // Scale volume to our display (0.0-1.0 to 0-100%)
    int scaledVolume = static_cast<int>(volume * 100);
    
    // Update each segment
    for (int i = 0; i < m_volumeBarLayout->count(); i++) {
        QProgressBar* bar = qobject_cast<QProgressBar*>(m_volumeBarLayout->itemAt(i)->widget());
        if (bar) {
            int threshold = (i+1) * (100 / m_volumeBarLayout->count());
            
            // Each bar is either full or empty based on whether the volume reaches its threshold
            if (scaledVolume >= threshold) {
                bar->setValue(100);  // Full
            } else {
                // Calculate partial fill based on how close we are to threshold
                int prevThreshold = i * (100 / m_volumeBarLayout->count());
                int segmentRange = threshold - prevThreshold;
                int segmentVolume = scaledVolume - prevThreshold;
                if (segmentVolume > 0) {
                    int pct = (segmentVolume * 100) / segmentRange;
                    bar->setValue(pct);
                } else {
                    bar->setValue(0);  // Empty
                }
            }
        }
    }
}

void MainWindow::onRecordingStopped()
{
    m_statusLabel->setText("Recording Stopped. File saved.");
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 12pt; color: #006600;");
    
    // Change background to indicate recording has stopped
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(220, 220, 220));  // Slightly darker gray for completed state
    pal.setColor(QPalette::WindowText, QColor(0, 0, 0));    // Black text
    setPalette(pal);
    m_statusLabel->setPalette(pal);
    m_volumeLabel->setPalette(pal);
    
    // Add a note about pressing Esc or Enter to close
    QTimer::singleShot(1000, this, [this]() {
        m_volumeLabel->setText("Press Enter to save and exit, or Esc to cancel");
    });
}

void MainWindow::onRecordingStarted()
{
    // Update volume label when audio device initialization starts
    m_volumeLabel->setText("Audio device initializing...");
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (!m_recorder) {
        QMainWindow::keyPressEvent(event);
        return;
    }
    
    if (event->key() == Qt::Key_Escape) {
        // Escape key pressed - cancel recording and exit without saving
        qInfo() << "[INFO] Escape key pressed - canceling recording";
        
        // Stop recording
        m_recorder->stopRecording();
        
        // Remove the output file
        QFile outputFile(OUTPUT_FILE_PATH);
        if (outputFile.exists()) {
            outputFile.remove();
            qInfo() << "[INFO] Output file removed:" << OUTPUT_FILE_PATH;
        }
        
        // Update UI
        m_statusLabel->setText("Recording canceled.");
        m_statusLabel->setStyleSheet("font-weight: bold; font-size: 12pt; color: #cc0000;");
        
        // Wait briefly to show status, then quit application
        QTimer::singleShot(500, []() {
            QApplication::quit();
        });
    }
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        // Enter key pressed - stop recording, save the file, and exit
        qInfo() << "[INFO] Enter key pressed - stopping recording and saving";
        
        // Stop recording
        m_recorder->stopRecording();
        
        // Wait briefly to show status, then quit application
        QTimer::singleShot(500, []() {
            QApplication::quit();
        });
    }
    else {
        QMainWindow::keyPressEvent(event);
    }
}
