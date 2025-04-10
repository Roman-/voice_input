#include "mainwindow.h"
#include <QVBoxLayout>
#include <QPalette>
#include <QColor>
#include <QProgressBar>
#include <QKeyEvent>
#include <QApplication>
#include <QMessageBox>
#include <QUrl>
#include <QDir>
#include <QCloseEvent>
#include <QShowEvent>

#include "core/audiorecorder.h"
#include "core/openaitranscriptionservice.h"
#include "core/statusutils.h"
#include "config/config.h"

MainWindow::MainWindow(AudioRecorder* recorder, QWidget* parent)
    : QMainWindow(parent),
      m_recorder(recorder),
      m_transcriptionService(new OpenAiTranscriptionService(this)),
      m_statusLabel(new QLabel(this)),
      m_transcriptionLabel(new QLabel(this)),
      m_transcribeButton(new QPushButton(this)),
      m_hasApiKey(false),
      m_exitCode(APP_EXIT_FAILURE_GENERAL), // Default to failure exit code until successful transcription
      m_isClosingPermanently(false)
{
    // Set window properties
    setWindowTitle("Audio Recorder");
    resize(400, 320);  // Increased size to accommodate transcription UI
    
    // Basic UI setup
    auto central = new QWidget(this);
    auto layout = new QVBoxLayout(central);

    // Configure all labels to be center-aligned
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_transcriptionLabel->setAlignment(Qt::AlignCenter);
    
    m_statusLabel->setText("Starting...");
    
    // Create volume meter
    m_volumeBar = new QWidget(this);
    m_volumeBarLayout = new QHBoxLayout(m_volumeBar);
    m_volumeBarLayout->setContentsMargins(10, 5, 10, 5);
    createVolumeBar();

    layout->addWidget(m_statusLabel);
    layout->addWidget(m_volumeBar);
    
    // Add transcription UI elements
    setupTranscriptionUI();
    layout->addSpacing(15);
    layout->addWidget(m_transcriptionLabel);
    
    // Add button in a centered layout
    auto buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_transcribeButton);
    buttonLayout->addStretch();
    layout->addLayout(buttonLayout);

    setCentralWidget(central);

    // Set colors for dark mode initializing state
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(30, 30, 40));        // Dark blue-gray background
    pal.setColor(QPalette::WindowText, QColor(220, 220, 220)); // Light gray text
    pal.setColor(QPalette::Text, QColor(220, 220, 220));       // Light gray text for widgets
    
    // Apply palette to window and labels directly
    setPalette(pal);
    m_statusLabel->setPalette(pal);
    m_transcriptionLabel->setPalette(pal);
    
    // Set status text style to be bold and larger with distinctive color
    m_statusLabel->setStyleSheet(STYLE_STATUS_NEUTRAL);
    
    // Show initializing state immediately
    m_statusLabel->setText("Initializing... (Press Enter/Space to save, Esc to cancel)");

    // Connect signals from recorder
    connect(m_recorder, &AudioRecorder::volumeChanged, this, &MainWindow::onVolumeChanged);
    connect(m_recorder, &AudioRecorder::recordingStopped, this, &MainWindow::onRecordingStopped);
    connect(m_recorder, &AudioRecorder::recordingStarted, this, &MainWindow::onRecordingStarted);
    connect(m_recorder, &AudioRecorder::audioDeviceReady, this, &MainWindow::onAudioDeviceReady);
    
    // Connect transcription signals
    connect(m_transcribeButton, &QPushButton::clicked, this, &MainWindow::onTranscribeButtonClicked);
    connect(m_transcriptionService, &OpenAiTranscriptionService::transcriptionCompleted, 
            this, &MainWindow::onTranscriptionCompleted);
    connect(m_transcriptionService, &OpenAiTranscriptionService::transcriptionFailed, 
            this, &MainWindow::onTranscriptionFailed);
    connect(m_transcriptionService, &OpenAiTranscriptionService::transcriptionProgress, 
            this, &MainWindow::onTranscriptionProgress);

    // Periodically update UI for elapsed time and file size
    m_updateTimer.setInterval(500); // 0.5 seconds
    connect(&m_updateTimer, &QTimer::timeout, this, &MainWindow::updateUI);
    m_updateTimer.start();
    
    // Check for API key
    m_hasApiKey = m_transcriptionService->hasApiKey();
    if (!m_hasApiKey) {
        m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_ERROR);
        m_transcriptionLabel->setText("NO API KEY - Set OPENAI_API_KEY environment variable");
    }
    
    // Set up auto-close timer for transcription errors
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    bool ok = false;
    m_autoCloseSeconds = env.value("RECORDER_AUTO_CLOSE_ON_ERROR_AFTER", "0").toInt(&ok);
    if (!ok) {
        m_autoCloseSeconds = 0; // Default to no auto-close if invalid value
    }
    
    qDebug() << "Auto-close on error setting:" << m_autoCloseSeconds << "seconds";
    
    m_autoCloseTimer.setSingleShot(true);
    connect(&m_autoCloseTimer, &QTimer::timeout, this, [this]() {
        qInfo() << "Auto-closing application after error with exit code:" << m_exitCode;
        QApplication::exit(m_exitCode);
    });
    
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
    
    // Only update timer and size if recording is still active
    if (m_recorder->isRecording()) {
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
}

void MainWindow::onVolumeChanged(float volume)
{
    // First volume update means audio is now flowing
    static bool firstVolume = true;
    if (firstVolume) {
        firstVolume = false;
        m_statusLabel->setText("Recording in progress... (Press Enter/Space to save, Esc to cancel)");
        qInfo() << "First audio data received, volume:" << volume;
    }
    
    // Only update volume if currently recording
    if (m_recorder && m_recorder->isRecording()) {
        // Only update if we have a significant volume level (reduces noise in display)
        static float lastVolume = 0.0f;
        if (qAbs(volume - lastVolume) > 0.005f) {
            // Just update the volume bar without showing percentage text
            updateVolumeBar(volume);
            lastVolume = volume;
        }
    } else {
        // Not recording, set volume to zero
        updateVolumeBar(0.0f);
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
    m_statusLabel->setStyleSheet(STYLE_STATUS_SUCCESS);
    
    // Change background to indicate recording has stopped
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(40, 40, 40));        // Dark gray for completed state
    pal.setColor(QPalette::WindowText, QColor(200, 200, 200)); // Light gray text
    pal.setColor(QPalette::Text, QColor(200, 200, 200));       // Light gray text for widgets
    setPalette(pal);
    m_statusLabel->setPalette(pal);
    
    // Reset volume bar when recording stops
    updateVolumeBar(0.0f);
    
    // Check for valid recording and API key
    QFile recordingFile(OUTPUT_FILE_PATH);
    if (recordingFile.exists() && m_hasApiKey) {
        // Auto-start transcription
        m_transcriptionLabel->setText("Automatically starting transcription...");
        m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_NEUTRAL);
        
        // Make sure the button is hidden during auto-transcription
        m_transcribeButton->setVisible(false);
        
        // Update status for transcription
        m_statusLabel->setText("Please wait while transcription completes...");
        
        // Slight delay to update UI before starting transcription
        QTimer::singleShot(500, this, [this]() {
            onTranscribeButtonClicked();
        });
    } else if (!m_hasApiKey) {
        m_transcriptionLabel->setText("NO API KEY - Transcription unavailable");
        m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_ERROR);
        
        // Hide the transcribe button since there's no API key
        m_transcribeButton->setVisible(false);
        
        // Update status with instruction
        QTimer::singleShot(1000, this, [this]() {
            m_statusLabel->setText("Press Enter/Space to save and exit, or Esc to cancel");
        });
    } else if (isVisible()) {
        // Only show "Recording file not found" message if we're visible
        // This prevents showing error after cancellation and reopening
        m_transcriptionLabel->setText("Recording file not found");
        m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_ERROR);
        
        // Hide the transcribe button since there's no recording file
        m_transcribeButton->setVisible(false);
        
        // Update status with instruction
        QTimer::singleShot(1000, this, [this]() {
            m_statusLabel->setText("Press Enter/Space to save and exit, or Esc to cancel");
        });
    }
}

void MainWindow::onRecordingStarted()
{
    // Update UI when recording initialization starts
    m_statusLabel->setText("Initializing audio system...");
    
    // Set status to busy
    setFileStatus(STATUS_BUSY);
}

void MainWindow::onAudioDeviceReady()
{
    // Update UI when audio device is fully ready and recording is actually happening
    m_statusLabel->setText("Recording in progress... (Press Enter/Space to save, Esc to cancel)");
    m_statusLabel->setStyleSheet(STYLE_STATUS_SUCCESS);
    
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
        // Escape key pressed - cancel recording and hide window
        qInfo() << "[INFO] Escape key pressed - canceling recording";
        
        // Set exit code for cancellation
        m_exitCode = APP_EXIT_FAILURE_CANCELED;
        qInfo() << "Exit code set to" << m_exitCode << "(CANCELED)";
        
        // Stop recording
        m_recorder->stopRecording();
        
        // Cancel transcription if in progress
        if (m_transcriptionService && m_transcriptionService->isTranscribing()) {
            m_transcriptionService->cancelTranscription();
        }

        // Remove the audio file
        QFile audioFile(OUTPUT_FILE_PATH);
        if (audioFile.exists()) {
            audioFile.remove();
            qInfo() << "[INFO] Audio file removed:" << OUTPUT_FILE_PATH;
        }
        
        // Create empty transcription file instead of removing it
        QFile transcriptionFile(TRANSCRIPTION_OUTPUT_PATH);
        if (transcriptionFile.exists()) {
            transcriptionFile.remove();
        }
        if (transcriptionFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            // Just create an empty file
            transcriptionFile.close();
            qInfo() << "[INFO] Transcription file emptied:" << TRANSCRIPTION_OUTPUT_PATH;
        }
        
        // Set status to ready (not idle)
        setFileStatus(STATUS_READY);
        
        // Update UI
        m_statusLabel->setText("Recording canceled.");
        m_statusLabel->setStyleSheet(STYLE_STATUS_ERROR);
        
        // Also reset transcription label to avoid stale messages on next open
        m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_NEUTRAL);
        m_transcriptionLabel->setText("Ready for transcription");
        m_transcribeButton->setVisible(false);
        
        // Pause the audio stream to stop listening to the microphone
        if (m_recorder) {
            m_recorder->pauseAudioStream();
        }
        
        // Hide the window
        QTimer::singleShot(200, [this]() {
            hide();
        });
    }
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
        // First check if transcription is in progress
        if (m_transcriptionService && m_transcriptionService->isTranscribing()) {
            // Don't hide if transcription is in progress
            qInfo() << "[INFO] Enter/Space key pressed - waiting for transcription to complete";
            m_statusLabel->setText("Please wait for transcription to complete...");
            return;
        }
        
        // If recording is still active, stop it and begin the transcription process
        if (m_recorder->isRecording()) {
            qInfo() << "[INFO] Enter/Space key pressed - stopping recording and saving";
            m_recorder->stopRecording();
            // Don't hide yet - onRecordingStopped will start transcription
            return;
        }
        
        // If we're here, recording is stopped and transcription is done
        // Hide window instead of exiting
        qInfo() << "[INFO] Enter/Space key pressed - hiding window";
        hideAndReset();
    }
    else if (event->key() == Qt::Key_Q && (event->modifiers() & Qt::ControlModifier)) {
        // Ctrl+Q to actually exit the application
        qInfo() << "[INFO] Ctrl+Q pressed - exiting application with code:" << m_exitCode;
        m_isClosingPermanently = true;
        QApplication::exit(m_exitCode);
    }
    else {
        QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_isClosingPermanently) {
        // Allow the close if we're actually exiting
        event->accept();
    } else {
        // Just hide the window instead of closing the application
        event->ignore();
        hideAndReset();
    }
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    
    // Restore the default UI colors
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(30, 30, 40));        // Dark blue-gray background
    pal.setColor(QPalette::WindowText, QColor(220, 220, 220)); // Light gray text
    pal.setColor(QPalette::Text, QColor(220, 220, 220));       // Light gray text for widgets
    setPalette(pal);
    
    // Reset status text style to be bold and larger with distinctive color
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 12pt; color: #5CAAFF;");
    m_statusLabel->setText("Initializing... (Press Enter/Space to save, Esc to cancel)");
    
    // Reset transcription label to avoid stale messages
    m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_NEUTRAL);
    m_transcriptionLabel->setText("Ready for transcription");
    m_transcribeButton->setVisible(false);
    
    // Restart the audio stream if it was paused
    if (m_recorder && !m_recorder->isAudioStreamActive()) {
        m_recorder->resumeAudioStream();
    }
    
    qInfo() << "[INFO] Window is now shown, UI reset";
}

void MainWindow::hideAndReset()
{
    // Stop any ongoing recording
    if (m_recorder && m_recorder->isRecording()) {
        m_recorder->stopRecording();
    }
    
    // Reset UI elements for next use, but don't remove files
    resetUIForNextRecording();
    
    // Pause the audio stream to stop listening to the microphone
    if (m_recorder) {
        m_recorder->pauseAudioStream();
    }
    
    // Hide the window - don't change status when window hides
    hide();
    
    qInfo() << "[INFO] Window hidden, microphone paused, ready for next signal";
}

void MainWindow::resetUIForNextRecording()
{
    // Reset volume display
    updateVolumeBar(0.0f);
    
    // Reset UI state
    m_statusLabel->setText("Ready for next recording.");
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 12pt; color: #5CAAFF;");
    
    // Reset exit code to default
    m_exitCode = APP_EXIT_FAILURE_GENERAL;
}

void MainWindow::setupTranscriptionUI()
{
    // Configure transcription button - initially hidden since transcription is automatic
    m_transcribeButton->setVisible(false);
    m_transcribeButton->setText("Try Again");
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
    m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_NEUTRAL);
    m_transcriptionLabel->setText("Ready for transcription");
}


void MainWindow::onTranscribeButtonClicked()
{
    if (!m_transcriptionService) return;
    
    // Cancel any auto-close timer when retry is attempted
    if (m_autoCloseTimer.isActive()) {
        m_autoCloseTimer.stop();
        qInfo() << "Auto-close timer canceled due to retry attempt";
    }
    
    // Check if the recording file exists
    QFile recordingFile(OUTPUT_FILE_PATH);
    if (!recordingFile.exists()) {
        m_transcriptionLabel->setText("Error: Recording file not found");
        m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_ERROR);
        return;
    }
    
    // Start the transcription process
    m_transcribeButton->setEnabled(false);
    m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_NEUTRAL);
    m_transcriptionLabel->setText("Starting transcription process...");
    
    // Check environment again for API key (might have been updated)
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (env.contains("OPENAI_API_KEY") && !env.value("OPENAI_API_KEY").isEmpty()) {
        // Refresh the transcription service with the latest API key
        m_transcriptionService->refreshApiKey();
    }

    m_transcriptionService->transcribeAudio(OUTPUT_FILE_PATH, "en");
}

void MainWindow::onTranscriptionCompleted(const QString& transcribedText)
{
    // Set exit code to success
    m_exitCode = APP_EXIT_SUCCESS;
    
    // Log the transcription result to console
    qInfo() << "Transcription result:\n-----\n" << transcribedText << "\n-----";
    qInfo() << "Exit code set to" << m_exitCode << "(SUCCESS), hiding window immediately";

    // Set status to ready
    setFileStatus(STATUS_READY);

    // Hide window immediately after successful transcription
    hideAndReset();

    copyTranscriptionToClipboard(m_pressCtrlVAfterCopy);
}

void MainWindow::onTranscriptionFailed(const QString& errorMessage)
{
    // Set appropriate exit code based on the error
    if (errorMessage.contains("API key", Qt::CaseInsensitive) || 
        errorMessage.contains("authentication", Qt::CaseInsensitive)) {
        m_exitCode = APP_EXIT_FAILURE_NO_API_KEY;
        qWarning() << "Exit code set to" << m_exitCode << "(NO_API_KEY)";
    } else if (errorMessage.contains("Network error", Qt::CaseInsensitive)) {
        m_exitCode = APP_EXIT_FAILURE_API_ERROR;
        qWarning() << "Exit code set to" << m_exitCode << "(API_ERROR)";
    } else {
        m_exitCode = APP_EXIT_FAILURE_GENERAL;
        qWarning() << "Exit code set to" << m_exitCode << "(GENERAL_FAILURE)";
    }
    
    // Set status to error with the error message
    setFileStatus(STATUS_ERROR, errorMessage);
    
    // Update UI with error message
    m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_ERROR);
    m_transcriptionLabel->setText(QString("Transcription failed: %1").arg(errorMessage));

    // Update status label to show transcription failed rather than recording timer
    m_statusLabel->setText("Transcription Failed");
    m_statusLabel->setStyleSheet(STYLE_STATUS_ERROR);
    
    // Check environment again for API key (might have been updated)
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!env.value("OPENAI_API_KEY").isEmpty()) {
        // Make the "Try Again" button visible
        m_transcribeButton->setVisible(true);
        m_transcribeButton->setEnabled(true);
        
        // Show a message suggesting retry
        if (m_autoCloseSeconds > 0) {
            m_statusLabel->setText(QString("Click 'Try Again' or wait %1s for auto-close").arg(m_autoCloseSeconds));
        } else {
            m_statusLabel->setText("Click 'Try Again' or press Enter/Space to exit");
        }
    } else {
        // No API key, don't show the retry button
        m_transcribeButton->setVisible(false);
        
        if (m_autoCloseSeconds > 0) {
            m_statusLabel->setText(QString("No API key found - Auto-closing in %1s").arg(m_autoCloseSeconds));
        } else {
            m_statusLabel->setText("No API key found - Press Enter/Space to exit");
        }
    }
    
    // Log the transcription error to console
    qWarning() << "Transcription failed:" << errorMessage;
    
    // Start auto-close timer if enabled
    if (m_autoCloseSeconds > 0) {
        // Start countdown timer for visual feedback
        QTimer* countdownTimer = new QTimer(this);
        countdownTimer->setInterval(1000);
        int remainingSeconds = m_autoCloseSeconds;
        
        connect(countdownTimer, &QTimer::timeout, this, [this, countdownTimer, remainingSeconds, &env]() mutable {
            remainingSeconds--;
            
            if (remainingSeconds <= 0) {
                countdownTimer->stop();
                countdownTimer->deleteLater();
                return;
            }
            
            // Update message with remaining time
            if (!env.value("OPENAI_API_KEY").isEmpty()) {
                m_statusLabel->setText(QString("Click 'Try Again' or wait %1s for auto-close").arg(remainingSeconds));
            } else {
                m_statusLabel->setText(QString("No API key found - Auto-closing in %1s").arg(remainingSeconds));
            }
        });
        
        countdownTimer->start();
        
        // Set the actual close timer
        m_autoCloseTimer.start(m_autoCloseSeconds * 1000);
        qInfo() << "Will auto-close after" << m_autoCloseSeconds << "seconds due to transcription error";
    }
}

void MainWindow::onTranscriptionProgress(const QString& status)
{
    // Update UI with progress status
    m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_NEUTRAL);
    m_transcriptionLabel->setText(status);
    
    // Hide the retry button during transcription
    m_transcribeButton->setVisible(false);
    
    // Update status label to show transcription progress rather than recording timer
    m_statusLabel->setText("Transcription in Progress");
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 12pt; color: #5CAAFF;");
    
    // Update status file to show busy state
    setFileStatus(STATUS_BUSY);
    
    // Reset volume bar to zero when transcription starts
    updateVolumeBar(0.0f);
}

void MainWindow::cancelTranscription()
{
    if (m_transcriptionService && m_transcriptionService->isTranscribing()) {
        m_transcriptionService->cancelTranscription();
    }
}
