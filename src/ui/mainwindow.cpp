#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDebug>
#include <QPalette>
#include <QColor>
#include <QProgressBar>
#include <QKeyEvent>
#include <QApplication>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>

#include "core/audiorecorder.h"
#include "core/transcriptionservice.h"
#include "core/transcriptionfactory.h"
#include "config/config.h"

MainWindow::MainWindow(AudioRecorder* recorder, QWidget* parent)
    : QMainWindow(parent),
      m_recorder(recorder),
      m_transcriptionService(TranscriptionFactory::createTranscriptionService(this)),
      m_statusLabel(new QLabel(this)),
      m_volumeLabel(new QLabel(this)),
      m_transcriptionLabel(new QLabel(this)),
      m_transcribeButton(new QPushButton("Transcribe Recording", this)),
      m_hasApiKey(false)
{
    // Set window properties
    setWindowTitle("Audio Recorder");
    resize(400, 320);  // Increased size to accommodate transcription UI
    
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
    
    // Add transcription UI elements
    setupTranscriptionUI();
    layout->addSpacing(15);
    layout->addWidget(m_transcriptionLabel);
    layout->addWidget(m_transcribeButton);

    setCentralWidget(central);

    // Set colors for dark mode initializing state
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(30, 30, 40));        // Dark blue-gray background
    pal.setColor(QPalette::WindowText, QColor(220, 220, 220)); // Light gray text
    pal.setColor(QPalette::Text, QColor(220, 220, 220));       // Light gray text for widgets
    
    // Apply palette to window and labels directly
    setPalette(pal);
    m_statusLabel->setPalette(pal);
    m_volumeLabel->setPalette(pal);
    m_transcriptionLabel->setPalette(pal);
    
    // Set status text style to be bold and larger with distinctive color
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 12pt; color: #5CAAFF;");
    
    // Show initializing state immediately
    m_statusLabel->setText("Initializing... (Press Enter/Space to save, Esc to cancel)");
    m_volumeLabel->setText("Starting audio device...");

    // Connect signals from recorder
    connect(m_recorder, &AudioRecorder::volumeChanged, this, &MainWindow::onVolumeChanged);
    connect(m_recorder, &AudioRecorder::recordingStopped, this, &MainWindow::onRecordingStopped);
    connect(m_recorder, &AudioRecorder::recordingStarted, this, &MainWindow::onRecordingStarted);
    connect(m_recorder, &AudioRecorder::audioDeviceReady, this, &MainWindow::onAudioDeviceReady);
    
    // Connect transcription signals
    connect(m_transcribeButton, &QPushButton::clicked, this, &MainWindow::onTranscribeButtonClicked);
    connect(m_transcriptionService, &TranscriptionService::transcriptionCompleted, 
            this, &MainWindow::onTranscriptionCompleted);
    connect(m_transcriptionService, &TranscriptionService::transcriptionFailed, 
            this, &MainWindow::onTranscriptionFailed);
    connect(m_transcriptionService, &TranscriptionService::transcriptionProgress, 
            this, &MainWindow::onTranscriptionProgress);

    // Periodically update UI for elapsed time and file size
    m_updateTimer.setInterval(500); // 0.5 seconds
    connect(&m_updateTimer, &QTimer::timeout, this, &MainWindow::updateUI);
    m_updateTimer.start();
    
    // Check for API key
    m_hasApiKey = m_transcriptionService->hasApiKey();
    if (!m_hasApiKey) {
        m_transcriptionLabel->setStyleSheet("color: #FF6B6B;");
        m_transcriptionLabel->setText("NO API KEY - Set OPENAI_API_KEY environment variable");
    }
    
    // Clean up any leftover transcription file
    QFile leftoverTranscriptionFile(TRANSCRIPTION_OUTPUT_PATH);
    if (leftoverTranscriptionFile.exists()) {
        leftoverTranscriptionFile.remove();
        qDebug() << "Removed leftover transcription file:" << TRANSCRIPTION_OUTPUT_PATH;
    }
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
        qInfo() << "First audio data received, volume:" << volume;
    }
    
    // Calculate percentage for display (more intuitive for users)
    int volumePercentage = static_cast<int>(volume * 100.0f);
    
    // Only update if we have a significant volume level (reduces noise in display)
    static float lastVolume = 0.0f;
    if (qAbs(volume - lastVolume) > 0.005f) {
        // Update volume display to show percentage
        m_volumeLabel->setText(QString("Volume: %1%").arg(volumePercentage));
        updateVolumeBar(volume);
        lastVolume = volume;
    }
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
        
        // Color gradient from green to yellow to red - dark mode colors
        QColor color;
        if (i < segments * 0.6) {            // First 60% - Bright Green
            color = QColor(0, 230, 118);
        } else if (i < segments * 0.8) {     // Next 20% - Bright Yellow
            color = QColor(255, 214, 0);
        } else {                             // Last 20% - Bright Red
            color = QColor(255, 82, 82);
        }
        
        // Set the bar color via stylesheet with dark mode styling
        QString style = QString("QProgressBar { background: #222; border: 1px solid #333; border-radius: 2px; } "
                               "QProgressBar::chunk { background-color: %1; }")
                        .arg(color.name());
        bar->setStyleSheet(style);
        
        m_volumeBarLayout->addWidget(bar);
    }
}

void MainWindow::updateVolumeBar(float volume)
{
    // Skip processing very low volumes (reduces noise in the display)
    if (volume < VOLUME_MIN_THRESHOLD) {
        volume = 0.0f;
    }
    
    // Scale volume with a curve to make small volumes more visible
    // Using stronger log scale based on config parameters
    float scaledVolume;
    if (volume <= 0.0f) {
        scaledVolume = 0.0f;
    } else {
        // Enhanced log transformation to boost low values
        scaledVolume = (log10f(1.0f + volume * (VOLUME_LOG_BASE - 1.0f)) / log10f(VOLUME_LOG_BASE)) * 100.0f;
    }
    
    // Ensure scaledVolume is within 0-100 range
    scaledVolume = qBound(0.0f, scaledVolume, 100.0f);
    
    // Log volume levels periodically for debugging
    static QElapsedTimer logTimer;
    if (!logTimer.isValid() || logTimer.elapsed() > 5000) { // Log every 5 seconds
        qDebug() << "Raw volume:" << volume << "Scaled volume:" << scaledVolume;
        logTimer.start();
    }
    
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
                float segmentVolume = scaledVolume - prevThreshold;
                if (segmentVolume > 0) {
                    int pct = qBound(0, static_cast<int>((segmentVolume * 100) / segmentRange), 100);
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
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 12pt; color: #4CFF64;");
    
    // Change background to indicate recording has stopped
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(40, 40, 40));        // Dark gray for completed state
    pal.setColor(QPalette::WindowText, QColor(200, 200, 200)); // Light gray text
    pal.setColor(QPalette::Text, QColor(200, 200, 200));       // Light gray text for widgets
    setPalette(pal);
    m_statusLabel->setPalette(pal);
    m_volumeLabel->setPalette(pal);
    
    // Enable transcription button if we have a valid recording and API key
    QFile recordingFile(OUTPUT_FILE_PATH);
    if (recordingFile.exists() && m_hasApiKey) {
        m_transcribeButton->setEnabled(true);
        m_transcriptionLabel->setText("Ready to transcribe recording");
        m_transcriptionLabel->setStyleSheet("color: #4CFF64;");
    } else if (!m_hasApiKey) {
        m_transcriptionLabel->setText("NO API KEY - Transcription unavailable");
        m_transcriptionLabel->setStyleSheet("color: #FF6B6B;");
    } else {
        m_transcriptionLabel->setText("Recording file not found");
        m_transcriptionLabel->setStyleSheet("color: #FF6B6B;");
    }
    
    // Add a note about pressing Esc or Enter/Space to close
    QTimer::singleShot(1000, this, [this]() {
        m_volumeLabel->setText("Press Enter/Space to save and exit, or Esc to cancel");
    });
}

void MainWindow::onRecordingStarted()
{
    // Update UI when recording initialization starts
    m_statusLabel->setText("Initializing audio system...");
    m_volumeLabel->setText("Audio device initializing...");
}

void MainWindow::onAudioDeviceReady()
{
    // Update UI when audio device is fully ready and recording is actually happening
    m_statusLabel->setText("Recording in progress... (Press Enter/Space to save, Esc to cancel)");
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 12pt; color: #4CFF64;");
    m_volumeLabel->setText("Audio device ready - recording in progress");
    
    // Change background to indicate active recording
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(25, 40, 25));        // Dark green for active recording
    pal.setColor(QPalette::WindowText, QColor(220, 220, 220)); // Light gray text
    pal.setColor(QPalette::Text, QColor(220, 220, 220));       // Light gray text for widgets
    setPalette(pal);
    
    qInfo() << "Audio device is fully initialized and recording has started";
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
        
        // Cancel transcription if in progress
        if (m_transcriptionService && m_transcriptionService->isTranscribing()) {
            m_transcriptionService->cancelTranscription();
        }
        
        // Remove the output files
        QFile outputFile(OUTPUT_FILE_PATH);
        if (outputFile.exists()) {
            outputFile.remove();
            qInfo() << "[INFO] Output file removed:" << OUTPUT_FILE_PATH;
        }
        
        QFile transcriptionFile(TRANSCRIPTION_OUTPUT_PATH);
        if (transcriptionFile.exists()) {
            transcriptionFile.remove();
            qInfo() << "[INFO] Transcription file removed:" << TRANSCRIPTION_OUTPUT_PATH;
        }
        
        // Update UI
        m_statusLabel->setText("Recording canceled.");
        m_statusLabel->setStyleSheet("font-weight: bold; font-size: 12pt; color: #FF6B6B;");
        
        // Wait briefly to show status, then quit application
        QTimer::singleShot(500, []() {
            QApplication::quit();
        });
    }
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
        // Enter or Space key pressed - stop recording, save the file, and exit
        qInfo() << "[INFO] Enter/Space key pressed - stopping recording and saving";
        
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

void MainWindow::setupTranscriptionUI()
{
    // Configure transcription button
    m_transcribeButton->setEnabled(false);
    m_transcribeButton->setStyleSheet("QPushButton { "
                                     "  background-color: #4CAF50; "
                                     "  color: white; "
                                     "  padding: 8px; "
                                     "  border: none; "
                                     "  border-radius: 4px; "
                                     "} "
                                     "QPushButton:disabled { "
                                     "  background-color: #777777; "
                                     "} "
                                     "QPushButton:hover:!disabled { "
                                     "  background-color: #45a049; "
                                     "}");
    
    // Configure transcription status label
    m_transcriptionLabel->setStyleSheet("font-size: 10pt;");
    m_transcriptionLabel->setText("Complete recording to enable transcription");
    m_transcriptionLabel->setAlignment(Qt::AlignCenter);
}

void MainWindow::onTranscribeButtonClicked()
{
    if (!m_transcriptionService) return;
    
    // Check if the recording file exists
    QFile recordingFile(OUTPUT_FILE_PATH);
    if (!recordingFile.exists()) {
        m_transcriptionLabel->setText("Error: Recording file not found");
        m_transcriptionLabel->setStyleSheet("color: #FF6B6B;");
        return;
    }
    
    // Start the transcription process
    m_transcribeButton->setEnabled(false);
    m_transcriptionLabel->setStyleSheet("color: #5CAAFF;");
    m_transcriptionLabel->setText("Starting transcription process...");
    
    m_transcriptionService->transcribeAudio(OUTPUT_FILE_PATH);
}

void MainWindow::onTranscriptionCompleted(const QString& transcribedText)
{
    // Update UI with success message
    m_transcriptionLabel->setStyleSheet("color: #4CFF64;");
    
    // Truncate the text if too long for display
    QString displayText = transcribedText;
    if (displayText.length() > 100) {
        displayText = displayText.left(97) + "...";
    }
    
    m_transcriptionLabel->setText(QString("Transcription complete: \"%1\"").arg(displayText));
    
    // Re-enable the transcribe button
    m_transcribeButton->setEnabled(true);
    
    // Show a dialog with the full text
    QMessageBox msgBox;
    msgBox.setWindowTitle("Transcription Complete");
    msgBox.setText("Speech transcription completed successfully.");
    msgBox.setDetailedText(transcribedText);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();
}

void MainWindow::onTranscriptionFailed(const QString& errorMessage)
{
    // Update UI with error message
    m_transcriptionLabel->setStyleSheet("color: #FF6B6B;");
    m_transcriptionLabel->setText(QString("Transcription failed: %1").arg(errorMessage));
    
    // Re-enable the transcribe button
    m_transcribeButton->setEnabled(true);
}

void MainWindow::onTranscriptionProgress(const QString& status)
{
    // Update UI with progress status
    m_transcriptionLabel->setStyleSheet("color: #5CAAFF;");
    m_transcriptionLabel->setText(status);
}
