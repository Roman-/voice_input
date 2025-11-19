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
#include <QFocusEvent>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QAction>
#include <QActionGroup>
#include <QScreen>
#include <QEvent>
#include <QTimer>

#include "core/audiorecorder.h"
#include "core/openaitranscriptionservice.h"
#include "core/statusutils.h"
#include "core/formatutils.h"
#include "config/config.h"

#ifdef __APPLE__
#include <Carbon/Carbon.h>

// Global hotkey handler
static MainWindow* g_mainWindowForHotkey = nullptr;
static EventHotKeyRef g_hotKeyRef = nullptr;

OSStatus hotKeyHandler(EventHandlerCallRef nextHandler, EventRef theEvent, void* userData)
{
    EventHotKeyID hotKeyID;
    GetEventParameter(theEvent, kEventParamDirectObject, typeEventHotKeyID, NULL, sizeof(hotKeyID), NULL, &hotKeyID);
    
    if (g_mainWindowForHotkey) {
        QMetaObject::invokeMethod(g_mainWindowForHotkey, "onGlobalHotkeyActivated", Qt::QueuedConnection);
    }
    
    return noErr;
}
#endif

MainWindow::MainWindow(AudioRecorder* recorder, QWidget* parent)
    : QMainWindow(parent),
      m_recorder(recorder),
      m_transcriptionService(new OpenAiTranscriptionService(this)),
      m_statusLabel(new QLabel(this)),
      m_transcriptionLabel(new QLabel(this)),
      m_transcribeButton(new QPushButton(this)),
      m_hasApiKey(false),
      m_exitCode(APP_EXIT_FAILURE_GENERAL), // Default to failure exit code until successful transcription
      m_isClosingPermanently(false),
      m_trayIcon(nullptr),
      m_trayMenu(nullptr),
      m_finishRecordingAction(nullptr),
      m_cancelRecordingAction(nullptr),
      m_showWindowAction(nullptr),
      m_isUploading(false),
      m_microphoneMenu(nullptr),
      m_microphoneActionGroup(nullptr),
      m_alwaysShowWindow(true)
{
    // Set window properties
    setWindowTitle("🎤 Recording");
    resize(450, 320);
    
    // Simple window flags: Stay on top, allow keyboard input
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    setAutoFillBackground(true);
    
    // Styled with visible border
    setStyleSheet(
        "QMainWindow {"
        "    background-color: rgb(30, 30, 40);"
        "    border: 2px solid rgb(92, 170, 255);"
        "    border-radius: 8px;"
        "}"
    );
    
    // Set initial position in top-right corner (only once)
    QScreen* screen = QApplication::primaryScreen();
    if (screen) {
        QRect screenGeometry = screen->geometry();
        int x = screenGeometry.x() + screenGeometry.width() - 450 - 20;
        int y = screenGeometry.y() + 60;
        move(x, y);
    }
    
    // Basic UI setup
    auto central = new QWidget(this);
    central->setStyleSheet("background-color: rgb(30, 30, 40);");
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
    connect(m_recorder, &AudioRecorder::deviceListChanged, this, &MainWindow::rebuildMicrophoneMenu);
    connect(m_recorder, &AudioRecorder::inputDeviceChanged, this, &MainWindow::onInputDeviceChanged);
    
    // Connect conversion signals
    connect(m_recorder, &AudioRecorder::conversionStarted, this, &MainWindow::onConversionStarted);
    connect(m_recorder, &AudioRecorder::conversionCompleted, this, &MainWindow::onConversionCompleted);
    connect(m_recorder, &AudioRecorder::conversionFailed, this, &MainWindow::onConversionFailed);
    
    // Connect transcription signals
    connect(m_transcribeButton, &QPushButton::clicked, this, &MainWindow::onTranscribeButtonClicked);
    connect(m_transcriptionService, &OpenAiTranscriptionService::transcriptionCompleted, 
            this, &MainWindow::onTranscriptionCompleted);
    connect(m_transcriptionService, &OpenAiTranscriptionService::transcriptionFailed, 
            this, &MainWindow::onTranscriptionFailed);
    connect(m_transcriptionService, &OpenAiTranscriptionService::transcriptionProgress, 
            this, &MainWindow::onTranscriptionProgress);
    
    // Connect signals to update tray icon
    connect(m_recorder, &AudioRecorder::recordingStarted, this, &MainWindow::updateTrayIcon);
    connect(m_recorder, &AudioRecorder::recordingStopped, this, &MainWindow::updateTrayIcon);
    connect(m_transcriptionService, &OpenAiTranscriptionService::transcriptionProgress, 
            this, &MainWindow::updateTrayIcon);
    connect(m_transcriptionService, &OpenAiTranscriptionService::transcriptionCompleted, 
            this, &MainWindow::updateTrayIcon);
    connect(m_transcriptionService, &OpenAiTranscriptionService::transcriptionFailed, 
            this, &MainWindow::updateTrayIcon);

    // Periodically update UI for elapsed time and file size
    m_updateTimer.setInterval(500); // 0.5 seconds
    connect(&m_updateTimer, &QTimer::timeout, this, &MainWindow::updateUI);
    m_updateTimer.start();
    
    // Check for API key
    m_hasApiKey = m_transcriptionService->hasApiKey();
    if (!m_hasApiKey) {
        m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_ERROR);
        m_transcriptionLabel->setText(QString("NO API KEY - Set %1 environment variable").arg(API_KEY_ENV_VARIABLE));
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
    
    // Setup global hotkey (macOS only)
#ifdef __APPLE__
    setupGlobalHotkey();
    setupSystemTrayIcon();
#endif
}

void MainWindow::updateUI()
{
    if (!m_recorder) return;
    
    // Update UI based on recording state
    if (m_recorder->isRecording()) {
        // Show volume bar when recording
        m_volumeBar->setVisible(true);
        
        qint64 size = m_recorder->fileSize();
        qint64 elapsed = m_recorder->elapsedMs();
        
        // Format elapsed time in a more readable format
        int seconds = elapsed / 1000;
        int minutes = seconds / 60;
        seconds %= 60;
        
        // Only update recording info after we have some data (indicates initialization is complete)
        if (size > 0) {
            QString infoText = QString("Recording... %1:%2 | Size: %3")
                    .arg(minutes, 2, 10, QChar('0'))
                    .arg(seconds, 2, 10, QChar('0'))
                    .arg(formatFileSize(size));
            m_statusLabel->setText(infoText);
            
            // No need to change background on first data anymore, 
            // that's handled by onRecordingStarted()
        }
    } else {
        // Hide volume bar when not recording
        m_volumeBar->setVisible(false);
        
        // Show ready status when not recording
        m_statusLabel->setText("Ready - waiting for signal");
        m_statusLabel->setStyleSheet("font-weight: bold; font-size: 12pt; color: #5CAAFF;");
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
    // If recording was canceled, don't do anything (files should already be removed)
    if (m_recorder && m_recorder->isCanceled()) {
        qInfo() << "Recording was canceled, skipping auto-transcription";
        return;
    }
    
    m_uiStageMarked = false;
    
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
    // Use the actual output file path (MP3 if available, WAV otherwise)
    QString outputFilePath = m_recorder ? m_recorder->getOutputFilePath() : OUTPUT_FILE_PATH;
    QFile recordingFile(outputFilePath);
    if (recordingFile.exists() && m_hasApiKey) {
        // Auto-start transcription
        m_transcriptionLabel->setText("Automatically starting transcription...");
        m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_NEUTRAL);
        
        // Make sure the button is hidden during auto-transcription
        m_transcribeButton->setVisible(false);
        
        // Update status for transcription
        m_statusLabel->setText("Please wait while transcription completes...");
        
        onTranscribeButtonClicked();
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
    m_uploadTimerActive = false;
    m_processingTimerActive = false;
    m_finalStatusTimerActive = false;
    m_uiStageMarked = false;

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
        qInfo() << "Escape key pressed - canceling recording";
        
        // Set exit code for cancellation
        m_exitCode = APP_EXIT_FAILURE_CANCELED;
        qInfo() << "Exit code set to" << m_exitCode << "(CANCELED)";
        
        // Cancel recording (will skip MP3 conversion)
        m_recorder->cancelRecording();
        
        // Cancel transcription if in progress
        if (m_transcriptionService && m_transcriptionService->isTranscribing()) {
            m_transcriptionService->cancelTranscription();
        }

        // Remove both WAV and MP3 files (if they exist)
        QFile wavFile(OUTPUT_FILE_PATH_WAV);
        if (wavFile.exists()) {
            wavFile.remove();
            qInfo() << "WAV file removed:" << OUTPUT_FILE_PATH_WAV;
        }
        
        QFile mp3File(OUTPUT_FILE_PATH);
        if (mp3File.exists()) {
            mp3File.remove();
            qInfo() << "MP3 file removed:" << OUTPUT_FILE_PATH;
        }
        
        // Create empty transcription file instead of removing it
        QFile transcriptionFile(TRANSCRIPTION_OUTPUT_PATH);
        if (transcriptionFile.exists()) {
            transcriptionFile.remove();
        }
        if (transcriptionFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            // Just create an empty file
            transcriptionFile.close();
            qInfo() << "Transcription file emptied:" << TRANSCRIPTION_OUTPUT_PATH;
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
        
        // Update tray icon to grey (ready state)
        updateTrayIcon();
        
        // Hide the window
        QTimer::singleShot(200, [this]() {
            hide();
        });
    }
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
        // First check if transcription is in progress
        if (m_transcriptionService && m_transcriptionService->isTranscribing()) {
            // Don't hide if transcription is in progress
            qInfo() << "Enter/Space key pressed - waiting for transcription to complete";
            m_statusLabel->setText("Please wait for transcription to complete...");
            return;
        }
        
        // If recording is still active, stop it and begin the transcription process
        if (m_recorder->isRecording()) {
            qInfo() << "Enter/Space key pressed - stopping recording and saving";
            m_recorder->stopRecording();
            // Don't hide yet - onRecordingStopped will start transcription
            return;
        }
        
        // If we're here, recording is stopped and transcription is done
        // Hide window instead of exiting
        qInfo() << "Enter/Space key pressed - hiding window";
        hideAndReset();
    }
    else if (event->key() == Qt::Key_Q && (event->modifiers() & Qt::ControlModifier)) {
        // Ctrl+Q to actually exit the application
        qInfo() << "Ctrl+Q pressed - exiting application with code:" << m_exitCode;
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
        // User closed window = uncheck "Always Show Window" option
        event->ignore();
        m_alwaysShowWindow = false;
        if (m_showWindowAction) {
            m_showWindowAction->setChecked(false);
        }
        hide();
    }
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    // No positioning, no raise/activate - window positioning is set once in constructor
}

void MainWindow::focusOutEvent(QFocusEvent* event)
{
    // DO NOT try to regain focus - we want to remain non-intrusive
    // The window is visible but should never steal focus from the user's work
    QMainWindow::focusOutEvent(event);
}

void MainWindow::changeEvent(QEvent* event)
{
    // If window is minimized or hidden while recording, bring it back but don't steal focus
    if (event->type() == QEvent::WindowStateChange) {
        if (m_recorder && m_recorder->isRecording()) {
            QTimer::singleShot(100, this, [this]() {
                if (m_recorder && m_recorder->isRecording() && !isVisible()) {
                    show();
                    // DO NOT call raise(), activateWindow(), or setFocus()
                }
            });
        }
    }
    QMainWindow::changeEvent(event);
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
    
    // Only hide if user wants window hidden
    if (!m_alwaysShowWindow) {
        hide();
        qInfo() << "Window hidden (user preference), microphone paused, ready for next signal";
    } else {
        qInfo() << "Window remains visible (user preference), microphone paused, ready for next signal";
    }
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
    
    if (!m_uiStageMarked && m_recorder) {
        m_recorder->timingTracker().markStage("UI Update");
        m_uiStageMarked = true;
    }
    
    // Cancel any auto-close timer when retry is attempted
    if (m_autoCloseTimer.isActive()) {
        m_autoCloseTimer.stop();
        qInfo() << "Auto-close timer canceled due to retry attempt";
    }
    
    // Get the actual output file path (MP3 if available, WAV otherwise)
    QString outputFilePath = m_recorder ? m_recorder->getOutputFilePath() : OUTPUT_FILE_PATH;
    
    // Check if the recording file exists
    QFile recordingFile(outputFilePath);
    if (!recordingFile.exists()) {
        m_transcriptionLabel->setText("Error: Recording file not found");
        m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_ERROR);
        return;
    }
    
    // Start the transcription process
    m_transcribeButton->setEnabled(false);
    m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_NEUTRAL);
    m_transcriptionLabel->setText("Starting transcription process...");
    
    // Reset uploading flag - will be set to true when upload progress starts
    m_isUploading = false;
    
    // Check environment again for API key (might have been updated)
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (env.contains(API_KEY_ENV_VARIABLE) && !env.value(API_KEY_ENV_VARIABLE).isEmpty()) {
        // Refresh the transcription service with the latest API key
        m_transcriptionService->refreshApiKey();
    }

    m_transcriptionService->transcribeAudio(outputFilePath, "en");
}

void MainWindow::onTranscriptionCompleted(const QString& transcribedText)
{
    // Set exit code to success
    m_exitCode = APP_EXIT_SUCCESS;
    
    // Reset uploading flag
    m_isUploading = false;
    
    // Log the transcription result to console
    qInfo() << "Transcription result:\n-----\n" << transcribedText << "\n-----";
    qInfo() << "Exit code set to" << m_exitCode << "(SUCCESS), hiding window immediately";

    // Set status to ready
    setFileStatus(STATUS_READY);
    
    TimingTracker* tracker = m_recorder ? &m_recorder->timingTracker() : nullptr;
    if (tracker) {
        if (m_finalStatusTimerActive) {
            tracker->setStageDuration("Final Status Update", m_finalStatusTimer.elapsed());
            m_finalStatusTimerActive = false;
        }
        qInfo().noquote() << tracker->summary();
        tracker->reset();
    }

    // Hide window immediately after successful transcription
    hideAndReset();

    // Wait for the window to fully hide so focus returns to the previous app,
    // then copy and auto-paste the transcription.
    constexpr int kPasteDelayMs = 50;
    QTimer::singleShot(kPasteDelayMs, this, [this]() {
        copyTranscriptionToClipboard(true);
    });
}

void MainWindow::onTranscriptionFailed(const QString& errorMessage)
{
    // Reset uploading flag
    m_isUploading = false;
    
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
    if (!env.value(API_KEY_ENV_VARIABLE).isEmpty()) {
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
            if (!env.value(API_KEY_ENV_VARIABLE).isEmpty()) {
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
    
    // Track if we're in uploading phase (contains "Uploading") vs processing phase
    bool wasUploading = m_isUploading;
    bool nowUploading = status.contains("Uploading", Qt::CaseInsensitive);
    m_isUploading = nowUploading;
    
    // If uploading, add file size information
    QString displayStatus = status;
    TimingTracker* tracker = m_recorder ? &m_recorder->timingTracker() : nullptr;
    
    if (!wasUploading && nowUploading) {
        m_uploadTimer.restart();
        m_uploadTimerActive = true;
    }
    
    if (status.contains("Processing audio", Qt::CaseInsensitive)) {
        if (m_uploadTimerActive && tracker) {
            tracker->setStageDuration("Transcription Upload", m_uploadTimer.elapsed());
        }
        m_uploadTimerActive = false;
        
        if (!m_processingTimerActive) {
            m_processingTimer.restart();
            m_processingTimerActive = true;
        }
    }
    
    if (status.contains("Response received", Qt::CaseInsensitive)) {
        if (m_processingTimerActive && tracker) {
            tracker->setStageDuration("Transcription Processing", m_processingTimer.elapsed());
        }
        m_processingTimerActive = false;
        
        if (!m_finalStatusTimerActive) {
            m_finalStatusTimer.restart();
            m_finalStatusTimerActive = true;
        }
    }
    
    if (m_isUploading) {
        // Get the actual output file path (MP3 if available, WAV otherwise)
        QString outputFilePath = m_recorder ? m_recorder->getOutputFilePath() : OUTPUT_FILE_PATH;
        QFileInfo fileInfo(outputFilePath);
        if (fileInfo.exists()) {
            QString sizeStr = formatFileSize(fileInfo.size());
            
            // Extract percentage if present, otherwise just show status with size
            if (status.contains("%")) {
                // Status already contains percentage, add size at the beginning
                displayStatus = QString("Uploading audio (%1): %2").arg(sizeStr, status);
            } else {
                displayStatus = QString("Uploading audio (%1)...").arg(sizeStr);
            }
        }
    }
    
    m_transcriptionLabel->setText(displayStatus);
    
    // Hide the retry button during transcription
    m_transcribeButton->setVisible(false);
    
    // Update status label to show transcription progress rather than recording timer
    m_statusLabel->setText("Transcription in Progress");
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 12pt; color: #5CAAFF;");
    
    // Update status file to show busy state
    setFileStatus(STATUS_BUSY);
    
    // Reset volume bar to zero when transcription starts
    updateVolumeBar(0.0f);
    
    // Update tray icon to reflect current phase
    updateTrayIcon();
}

void MainWindow::onConversionStarted()
{
    // Update status label to show conversion in progress
    m_statusLabel->setText("Converting to MP3...");
    m_statusLabel->setStyleSheet(STYLE_STATUS_NEUTRAL);
    
    // Update transcription label to show conversion status
    m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_NEUTRAL);
    m_transcriptionLabel->setText("Converting WAV to MP3...");
    
    qInfo() << "Conversion started - UI updated";
}

void MainWindow::onConversionCompleted(const QString& mp3Path)
{
    // Update status label
    m_statusLabel->setText("Conversion completed");
    m_statusLabel->setStyleSheet(STYLE_STATUS_SUCCESS);
    
    // Update transcription label
    m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_NEUTRAL);
    m_transcriptionLabel->setText("MP3 file ready for transcription");
    
    qInfo() << "Conversion completed - MP3 file ready:" << mp3Path;
}

void MainWindow::onConversionFailed(const QString& errorMessage)
{
    // Update status label to show error
    m_statusLabel->setText("Conversion failed");
    m_statusLabel->setStyleSheet(STYLE_STATUS_ERROR);
    
    // Update transcription label with error message
    m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_ERROR);
    m_transcriptionLabel->setText(QString("Conversion failed: %1").arg(errorMessage));
    
    qWarning() << "Conversion failed:" << errorMessage;
}

void MainWindow::cancelTranscription()
{
    if (m_transcriptionService && m_transcriptionService->isTranscribing()) {
        m_transcriptionService->cancelTranscription();
        // Reset uploading flag
        m_isUploading = false;
    }
}

void MainWindow::setupGlobalHotkey()
{
#ifdef __APPLE__
    g_mainWindowForHotkey = this;
    
    // Register event handler for hotkey events
    EventTypeSpec eventType;
    eventType.eventClass = kEventClassKeyboard;
    eventType.eventKind = kEventHotKeyPressed;
    
    InstallApplicationEventHandler(&hotKeyHandler, 1, &eventType, NULL, NULL);
    
    // Register Alt+Space (Option+Space) hotkey
    EventHotKeyID hotKeyID;
    hotKeyID.signature = 'vrec'; // Voice recorder
    hotKeyID.id = 1;
    
    // Option (Alt) key = 0x3A, Space = 0x31
    OSStatus status = RegisterEventHotKey(0x31, // Space key
                                           optionKey, // Option (Alt) modifier
                                           hotKeyID,
                                           GetApplicationEventTarget(),
                                           0,
                                           &g_hotKeyRef);
    
    if (status == noErr) {
        qInfo() << "Global hotkey registered: Option+Space";
    } else {
        qWarning() << "Failed to register global hotkey. Error:" << status;
    }
#endif
}

void MainWindow::onGlobalHotkeyActivated()
{
    qInfo() << "Global hotkey activated - recording:" << (m_recorder && m_recorder->isRecording());
    
    if (!m_recorder) return;
    
    // Toggle recording state (regardless of window visibility)
    if (m_recorder->isRecording()) {
        // Stop recording
        qInfo() << "Global hotkey activated - stopping recording";
        m_recorder->stopRecording();
    } else {
        // Start recording
        qInfo() << "Global hotkey activated - starting recording";
        
        // Clean up any previous files
        for (const auto& f : QStringList{OUTPUT_FILE_PATH, TRANSCRIPTION_OUTPUT_PATH}) {
            QFile file(f);
            if (file.exists() && file.remove()) {
                qDebug() << "Removed previous file:" << f;
            }
        }
        
        // Start recording
        m_recorder->startRecording();
        setFileStatus(STATUS_BUSY);
    }
}

void MainWindow::setupSystemTrayIcon()
{
    // Check if system tray is available
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qWarning() << "System tray is not available on this system";
        return;
    }
    
    // Create system tray icon
    m_trayIcon = new QSystemTrayIcon(this);
    
    // Create context menu
    m_trayMenu = new QMenu(this);
    
    // Finish Recording action - stops recording and saves (like Enter/Space)
    QAction* finishRecordingAction = new QAction("Finish Recording", this);
    finishRecordingAction->setEnabled(false); // Enabled only when recording
    connect(finishRecordingAction, &QAction::triggered, this, [this]() {
        if (m_recorder && m_recorder->isRecording()) {
            // Stop recording - this will trigger transcription
            // No show/hide manipulation
            m_recorder->stopRecording();
        }
    });
    m_trayMenu->addAction(finishRecordingAction);
    
    // Cancel Recording action - cancels recording and removes files (like Escape)
    QAction* cancelRecordingAction = new QAction("Cancel Recording", this);
    cancelRecordingAction->setEnabled(false); // Enabled only when recording
    connect(cancelRecordingAction, &QAction::triggered, this, [this]() {
        if (m_recorder && m_recorder->isRecording()) {
            // Set exit code for cancellation
            // No show/hide manipulation
            m_exitCode = APP_EXIT_FAILURE_CANCELED;
            qInfo() << "Exit code set to" << m_exitCode << "(CANCELED)";
            
            // Cancel recording (will skip MP3 conversion)
            m_recorder->cancelRecording();
            
            // Cancel transcription if in progress
            if (m_transcriptionService && m_transcriptionService->isTranscribing()) {
                m_transcriptionService->cancelTranscription();
            }

            // Remove both WAV and MP3 files (if they exist)
            QFile wavFile(OUTPUT_FILE_PATH_WAV);
            if (wavFile.exists()) {
                wavFile.remove();
                qInfo() << "WAV file removed:" << OUTPUT_FILE_PATH_WAV;
            }
            
            QFile mp3File(OUTPUT_FILE_PATH);
            if (mp3File.exists()) {
                mp3File.remove();
                qInfo() << "MP3 file removed:" << OUTPUT_FILE_PATH;
            }
            
            // Create empty transcription file instead of removing it
            QFile transcriptionFile(TRANSCRIPTION_OUTPUT_PATH);
            if (transcriptionFile.exists()) {
                transcriptionFile.remove();
            }
            if (transcriptionFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                transcriptionFile.close();
                qInfo() << "Transcription file emptied:" << TRANSCRIPTION_OUTPUT_PATH;
            }
            
            // Set status to ready
            setFileStatus(STATUS_READY);
            
            // Update UI
            m_statusLabel->setText("Recording canceled.");
            m_statusLabel->setStyleSheet(STYLE_STATUS_ERROR);
            
            // Reset transcription label
            m_transcriptionLabel->setStyleSheet(STYLE_TRANSCRIPTION_NEUTRAL);
            m_transcriptionLabel->setText("Ready for transcription");
            m_transcribeButton->setVisible(false);
            
            // Pause the audio stream
            if (m_recorder) {
                m_recorder->pauseAudioStream();
            }
            
            // Update tray icon to grey (ready state)
            updateTrayIcon();
            
            // No hide - window visibility controlled by user preference
        }
    });
    m_trayMenu->addAction(cancelRecordingAction);
    
    m_trayMenu->addSeparator();
    
    // Always Show Window toggle
    m_showWindowAction = new QAction("Always Show Window", this);
    m_showWindowAction->setCheckable(true);
    m_showWindowAction->setChecked(true); // Checked by default
    connect(m_showWindowAction, &QAction::triggered, this, [this](bool checked) {
        m_alwaysShowWindow = checked;
        if (checked) {
            show();
        } else {
            hide();
        }
    });
    m_trayMenu->addAction(m_showWindowAction);
    
    m_trayMenu->addSeparator();
    
    m_microphoneMenu = new QMenu("Microphone", m_trayMenu);
    connect(m_microphoneMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildMicrophoneMenu);
    m_trayMenu->addMenu(m_microphoneMenu);
    rebuildMicrophoneMenu();

    m_trayMenu->addSeparator();
    
    QAction* quitAction = new QAction("Quit", this);
    connect(quitAction, &QAction::triggered, this, [this]() {
        m_isClosingPermanently = true;
        QApplication::exit(m_exitCode);
    });
    m_trayMenu->addAction(quitAction);
    
    m_trayIcon->setContextMenu(m_trayMenu);
    
    // Store references to actions for updating
    m_finishRecordingAction = finishRecordingAction;
    m_cancelRecordingAction = cancelRecordingAction;
    
    // Don't handle tray icon clicks - window is only shown via signal/hotkey
    
    // Set initial icon (idle state)
    updateTrayIcon();
    
    // Show the tray icon
    m_trayIcon->show();
    
    qInfo() << "System tray icon initialized";
}

void MainWindow::rebuildMicrophoneMenu()
{
    if (!m_microphoneMenu) {
        return;
    }

    m_microphoneMenu->clear();

    if (m_microphoneActionGroup) {
        delete m_microphoneActionGroup;
        m_microphoneActionGroup = nullptr;
    }

    m_microphoneActionGroup = new QActionGroup(m_microphoneMenu);
    m_microphoneActionGroup->setExclusive(true);

    if (!m_recorder) {
        QAction* action = m_microphoneMenu->addAction("Recorder unavailable");
        action->setEnabled(false);
        return;
    }

    m_recorder->refreshInputDeviceList();
    const auto devices = m_recorder->availableInputDevices();
    if (devices.isEmpty()) {
        QAction* action = m_microphoneMenu->addAction("No microphones found");
        action->setEnabled(false);
        return;
    }

    bool isRecording = m_recorder->isRecording();
    int currentId = m_recorder->currentInputDeviceId();

    for (const auto& device : devices) {
        QString label = QString("%1 (%2 ch)").arg(device.name.isEmpty() ? QStringLiteral("Unknown") : device.name)
                                             .arg(device.maxInputChannels);
        QAction* action = m_microphoneMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(device.id == currentId);
        action->setData(device.id);
        action->setEnabled(!isRecording);
        connect(action, &QAction::triggered, this, [this, deviceId = device.id]() {
            handleMicrophoneSelection(deviceId);
        });
        m_microphoneActionGroup->addAction(action);
    }

    if (isRecording) {
        QAction* note = m_microphoneMenu->addAction("Stop recording to switch microphones");
        note->setEnabled(false);
    }
}

void MainWindow::handleMicrophoneSelection(int deviceId)
{
    if (!m_recorder) {
        return;
    }

    if (m_recorder->isRecording()) {
        QMessageBox::information(this,
                                 "Cannot Switch Microphone",
                                 "Stop recording before changing microphones.");
        return;
    }

    if (!m_recorder->setInputDevice(deviceId)) {
        QMessageBox::warning(this,
                             "Microphone Switch Failed",
                             "Unable to switch microphones. Please check the log for more details.");
        return;
    }

    QString deviceName = m_recorder->currentInputDeviceName();
    if (!deviceName.isEmpty()) {
        m_statusLabel->setText(QString("Microphone ready: %1").arg(deviceName));
        m_statusLabel->setStyleSheet(STYLE_STATUS_NEUTRAL);
    }
}

void MainWindow::onInputDeviceChanged(int /*deviceId*/, const QString& deviceName)
{
    if (!deviceName.isEmpty() &&
        m_statusLabel &&
        !m_recorder->isRecording() &&
        !(m_transcriptionService && m_transcriptionService->isTranscribing())) {
        m_statusLabel->setText(QString("Microphone ready: %1").arg(deviceName));
        m_statusLabel->setStyleSheet(STYLE_STATUS_NEUTRAL);
    }

    rebuildMicrophoneMenu();
    updateTrayIcon();
}

void MainWindow::updateTrayIcon()
{
    if (!m_trayIcon) return;
    
    QString color;
    QString tooltip;
    QString currentDeviceName;
    if (m_recorder) {
        currentDeviceName = m_recorder->currentInputDeviceName();
    }
    
    // Determine current state
    bool isRecording = m_recorder && m_recorder->isRecording();
    bool isTranscribing = m_transcriptionService && m_transcriptionService->isTranscribing();
    
    // Check status file for error state and busy state
    QFile statusFile(STATUS_FILE_PATH);
    bool hasError = false;
    bool isBusy = false;
    if (statusFile.exists() && statusFile.open(QIODevice::ReadOnly)) {
        QString status = QString::fromUtf8(statusFile.readAll()).trimmed();
        statusFile.close();
        hasError = (status == STATUS_ERROR);
        isBusy = (status == STATUS_BUSY);
    }
    
    // Check if we have a recording file but transcription hasn't started yet
    // This handles the gap between recording stop and transcription start
    bool hasRecordingFile = QFile::exists(OUTPUT_FILE_PATH);
    bool isPostRecordingPreTranscription = hasRecordingFile && !isRecording && !isTranscribing && isBusy;
    
    if (hasError) {
        color = "#FF6B6B"; // Red for error
        tooltip = "Voice Input - Error";
    } else if (isRecording) {
        color = "#FF4444"; // Red for recording
        tooltip = "Voice Input - Recording...";
    } else if (isTranscribing) {
        if (m_isUploading) {
            color = "#FFD700"; // Gold/yellow-ish for uploading (different from processing)
            tooltip = "Voice Input - Uploading...";
        } else {
            color = "#FFA500"; // Orange/yellow for processing (waiting for reply)
            tooltip = "Voice Input - Processing...";
        }
    } else if (isPostRecordingPreTranscription) {
        // Transition state: recording stopped, transcription about to start
        // Use gold color to indicate we're preparing for upload
        color = "#FFD700"; // Gold/yellow-ish (same as uploading, since we're about to upload)
        tooltip = "Voice Input - Preparing...";
    } else {
        color = "#888888"; // Gray only for truly idle/ready state
        tooltip = "Voice Input - Ready";
    }
    
    QIcon icon = createTrayIcon(color);
    m_trayIcon->setIcon(icon);
    if (!currentDeviceName.isEmpty()) {
        tooltip += QString(" (%1)").arg(currentDeviceName);
    }
    m_trayIcon->setToolTip(tooltip);
    
    // Update finish and cancel recording actions state
    if (m_finishRecordingAction) {
        m_finishRecordingAction->setEnabled(isRecording);
    }
    if (m_cancelRecordingAction) {
        m_cancelRecordingAction->setEnabled(isRecording);
    }
}

QIcon MainWindow::createTrayIcon(const QString& color)
{
    // Create a simple circular icon with the specified color
    // macOS menu bar icons are typically 22x22 points, but we'll create a larger one for retina
    QPixmap pixmap(44, 44);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw a circle (microphone representation)
    QColor iconColor(color);
    painter.setBrush(iconColor);
    painter.setPen(Qt::NoPen);
    
    // Draw a simple microphone shape: circle with a line
    // Main circle (microphone body)
    painter.drawEllipse(12, 8, 20, 20);
    
    // Microphone stand (vertical line)
    painter.setPen(QPen(iconColor, 3, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(22, 28, 22, 36);
    
    // Base (horizontal line)
    painter.drawLine(16, 36, 28, 36);
    
    return QIcon(pixmap);
}
