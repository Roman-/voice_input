#include "mainwindow.h"
#include "whisperapi.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QMessageBox>
#include <QCoreApplication>
#include <QAudioFormat>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QApplication>

MainWindow::MainWindow(bool sendToOpenAI, QWidget *parent)
    : QMainWindow(parent),
      m_sendToOpenAI(sendToOpenAI)
{
    // Clean up previous files at startup
    QFile::remove(Config::RECORDING_PATH);
    QFile::remove(Config::TRANSCRIPTION_PATH);
    
    initUi();
    initAudioInputForVolume();

    // Start ffmpeg recording when the UI is ready
    // For minimal perceived lag, we show the window first, then start.
    QTimer::singleShot(100, this, SLOT(startRecording()));
}

MainWindow::~MainWindow()
{
    // If still recording, kill ffmpeg process
    if (m_ffmpegProcess.state() == QProcess::Running) {
        m_ffmpegProcess.kill();
        m_ffmpegProcess.waitForFinished();
    }
    // Cleanup audio
    if (m_audioInput) {
        m_audioInput->stop();
        m_audioDevice->close();
        delete m_audioDevice;
        delete m_audioInput;
    }
}

void MainWindow::initUi()
{
    QWidget *central = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);

    m_statusLabel = new QLabel(tr("Initializing..."), this);
    m_volumeBar   = new QProgressBar(this);
    m_volumeBar->setRange(0, 100);

    m_sendButton  = new QPushButton(tr("Send"), this);
    m_stopButton  = new QPushButton(tr("Stop"), this);
    m_retryButton = new QPushButton(tr("Submit Again"), this);
    m_retryButton->hide(); // only show if there's an error on first attempt
    
    // Set black background for initialization state
    QPalette pal = central->palette();
    pal.setColor(QPalette::Window, QColor(0, 0, 0)); // Black
    central->setAutoFillBackground(true);
    central->setPalette(pal);

    // Connect signals
    connect(m_sendButton, &QPushButton::clicked, [=](){
        stopRecording(true);
    });
    connect(m_stopButton, &QPushButton::clicked, [=](){
        stopRecording(false);
    });
    connect(m_retryButton, &QPushButton::clicked, this, &MainWindow::retryTranscription);

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(m_stopButton);
    buttonLayout->addWidget(m_sendButton);
    buttonLayout->addWidget(m_retryButton);

    mainLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(m_volumeBar);
    mainLayout->addLayout(buttonLayout);

    setCentralWidget(central);
    setWindowTitle(tr("Voice Input Application (Qt)"));
    resize(Config::UI_WIDTH, Config::UI_HEIGHT);

    // Timer to update recording stats (file size, duration)
    connect(&m_updateTimer, &QTimer::timeout, this, &MainWindow::updateRecordingStats);
    m_updateTimer.start(Config::UI_UPDATE_INTERVAL_MS);

    // Timer to update volume level
    connect(&m_volumeTimer, &QTimer::timeout, this, &MainWindow::updateVolumeLevel);
    m_volumeTimer.start(Config::VOLUME_UPDATE_INTERVAL_MS);
}

void MainWindow::initAudioInputForVolume()
{
    // We'll use QAudioInput to measure the microphone level in parallel to ffmpeg
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);

    // Find input device
    QAudioDeviceInfo inputDevice = QAudioDeviceInfo::defaultInputDevice();
    if (!inputDevice.isFormatSupported(format)) {
        showError("Raw audio format not supported by device, cannot measure volume!");
        return;
    }

    m_audioInput = new QAudioInput(inputDevice, format, this);
    m_audioDevice = m_audioInput->start();
    if (!m_audioDevice) {
        showError("Failed to start audio input for volume metering!");
        return;
    }
}

// Called once when UI is shown (delayed 100ms)
void MainWindow::startRecording()
{
    if (m_isRecording)
        return;
    
    // Change background to normal when recording
    setYellowBackground(false);
    
    // Build ffmpeg command
    // -y = overwrite output
    // -f pulse -i default: use PulseAudio default input device
    // -acodec aac -b:a 128k
    QString program = "ffmpeg";
    QStringList arguments;
    arguments << "-y"
              << "-f" << "pulse"
              << "-i" << "default"
              << "-acodec" << Config::AUDIO_FORMAT
              << "-b:a" << Config::AUDIO_BITRATE
              << Config::RECORDING_PATH;

    // Start the process
    m_ffmpegProcess.start(program, arguments);

    if (!m_ffmpegProcess.waitForStarted(2000)) {
        showError("FFmpeg failed to start. Ensure FFmpeg is installed and accessible.");
        return;
    }

    m_isRecording = true;
    m_recordStartTime = QDateTime::currentSecsSinceEpoch();
    
    if (m_sendToOpenAI) {
        m_statusLabel->setText("Recording... Press Send to transcribe or Stop to discard.");
    } else {
        m_statusLabel->setText("Recording... Press Stop to save (will not transcribe).");
    }

    connect(&m_ffmpegProcess, 
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onFfmpegFinished);
}

void MainWindow::stopRecording(bool sendToSTT)
{
    if (!m_isRecording) {
        return;
    }

    // First, stop volume metering to prevent race conditions
    if (m_audioInput) {
        m_audioInput->stop();
    }
    m_volumeTimer.stop();

    // Stop the ffmpeg process
    if (m_ffmpegProcess.state() == QProcess::Running) {
        m_ffmpegProcess.terminate();
        // Give it a moment to shutdown gracefully
        if (!m_ffmpegProcess.waitForFinished(2000)) {
            m_ffmpegProcess.kill();
            m_ffmpegProcess.waitForFinished();
        }
    }
    m_isRecording = false;
    
    // Log the recording path
    qDebug() << "Recording saved to:" << Config::RECORDING_PATH;

    if (sendToSTT && m_sendToOpenAI) {
        m_statusLabel->setText("Uploading to OpenAI Whisper...");
        m_sendRequested = true;
        // Set black background while waiting for OpenAI
        setYellowBackground(true);
        sendForTranscription();
    } else {
        // Show the file path in status bar
        m_statusLabel->setText("Recording saved to: " + Config::RECORDING_PATH);
        
        // If no transcription requested/allowed, exit after a delay
        if (!sendToSTT || !m_sendToOpenAI) {
            QTimer::singleShot(Config::EXIT_DELAY_MS, qApp, &QCoreApplication::quit);
        }
    }
}

void MainWindow::onFfmpegFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode)
    Q_UNUSED(exitStatus)
    // If FFmpeg stops unexpectedly (e.g., user kills it or error),
    // handle any post-processing here if needed.
    // We'll rely on stopRecording() logic mostly.
}

void MainWindow::updateRecordingStats()
{
    if (!m_isRecording) return;

    // Check file size
    QFile file(Config::RECORDING_PATH);
    if (file.exists()) {
        m_currentFileSize = file.size();
        if (m_currentFileSize >= Config::MAX_FILE_SIZE_BYTES) {
            // Stop the recording and show error
            stopRecording(false); // do not send
            showError(QString("Max file size (%1 MB) reached. Transcription canceled.")
                       .arg(Config::MAX_FILE_SIZE_BYTES / (1024 * 1024)));
            return;
        }
    }

    // Calculate duration
    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 elapsed = now - m_recordStartTime;

    QString info = QString("Recording... %1 s, File size: %2 KB")
            .arg(elapsed)
            .arg(m_currentFileSize / 1024);
    m_statusLabel->setText(info);
}

void MainWindow::updateVolumeLevel()
{
    if (!m_isRecording || !m_audioDevice || !m_audioInput) return;

    // We can read some data from m_audioDevice to get an approximate level
    QByteArray buffer;
    
    try {
        buffer = m_audioDevice->readAll();
        if (buffer.isEmpty())
            return;
    } catch (...) {
        qWarning() << "Error reading audio device buffer";
        return;
    }

    // Compute a naive peak from the buffer
    const qint16 *samples = reinterpret_cast<const qint16*>(buffer.constData());
    int sampleCount = buffer.size() / sizeof(qint16);

    qint16 peak = 0;
    for (int i = 0; i < sampleCount; ++i) {
        qint16 val = qAbs(samples[i]);
        if (val > peak) {
            peak = val;
        }
    }

    // Convert peak to a 0-100 range
    // Max 16-bit is 32767, so scale accordingly
    int level = static_cast<int>(100.0 * double(peak) / 32767.0);
    if (m_volumeBar) {
        m_volumeBar->setValue(level);
    }
}

void MainWindow::sendForTranscription()
{
    // If file is missing or empty, just error out
    QFileInfo fi(Config::RECORDING_PATH);
    if (!fi.exists() || fi.size() == 0) {
        showError("Recorded file missing or empty. Cannot transcribe.");
        return;
    }

    // Check OPENAI_API_KEY
    QByteArray apiKey = qgetenv("OPENAI_API_KEY");
    if (apiKey.isEmpty()) {
        showError("Missing OPENAI_API_KEY environment variable.");
        return;
    }

    // Create a WhisperApi instance to handle the network call
    WhisperApi *api = new WhisperApi(this);
    connect(api, &WhisperApi::transcriptionSuccess,
            this, &MainWindow::onTranscriptionSuccess);
    connect(api, &WhisperApi::transcriptionError,
            this, &MainWindow::onTranscriptionError);

    // We set up the request parameters
    api->setApiKey(QString::fromUtf8(apiKey));
    api->setModel(Config::WHISPER_MODEL);
    api->setTemperature(Config::WHISPER_TEMPERATURE);
    api->setLanguage(Config::WHISPER_LANGUAGE);

    // Perform the transcription
    api->transcribe(Config::RECORDING_PATH);
}

void MainWindow::onTranscriptionSuccess(const QString &text)
{
    // Set background back to normal after getting response
    setYellowBackground(false);
    
    // Save transcription to fixed path
    QFile outFile(Config::TRANSCRIPTION_PATH);
    if (outFile.open(QFile::WriteOnly | QFile::Truncate)) {
        outFile.write(text.toUtf8());
        outFile.close();
        qDebug() << "Transcription saved to:" << Config::TRANSCRIPTION_PATH;
    } else {
        qDebug() << "ERROR: Failed to save transcription to:" << Config::TRANSCRIPTION_PATH;
    }

    m_statusLabel->setText("Transcription success. Saved to: " + Config::TRANSCRIPTION_PATH);
    // Optionally, place text on clipboard:
    // QApplication::clipboard()->setText(text);

    // Exit after a short delay
    QTimer::singleShot(Config::EXIT_DELAY_MS, qApp, &QCoreApplication::quit);
}

void MainWindow::onTranscriptionError(const QString &errorMsg)
{
    // Set background back to normal after error
    setYellowBackground(false);
    
    if (!m_retryUsed) {
        // Offer a retry
        m_retryButton->show();
        showError(QString("Transcription failed: %1. You may retry.").arg(errorMsg));
    } else {
        // Already retried once
        showError(QString("Transcription failed again: %1. Exiting...").arg(errorMsg));
        QTimer::singleShot(Config::EXIT_DELAY_MS, qApp, &QCoreApplication::quit);
    }
}

void MainWindow::retryTranscription()
{
    if (m_retryUsed) return; // only allow once

    m_retryUsed = true;
    m_retryButton->hide();
    m_statusLabel->setText("Retrying transcription...");
    // Set yellow background while waiting for OpenAI
    setYellowBackground(true);
    sendForTranscription();
}

void MainWindow::showError(const QString &message)
{
    m_statusLabel->setText(message);
    qWarning() << message; // log to stderr
}

void MainWindow::setYellowBackground(bool yellow)
{
    QWidget *central = centralWidget();
    if (central) {
        QPalette pal = central->palette();
        if (yellow) {
            pal.setColor(QPalette::Window, QColor(0, 0, 0)); // Black
        } else {
            pal.setColor(QPalette::Window, QApplication::palette().color(QPalette::Window)); // Default
        }
        central->setPalette(pal);
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        // Immediately cancel and quit
        m_updateTimer.stop();
        m_volumeTimer.stop();
        
        if (m_ffmpegProcess.state() == QProcess::Running) {
            m_ffmpegProcess.kill();
            m_ffmpegProcess.waitForFinished();
        }
        
        // Properly clean up audio resources before quitting
        if (m_audioInput) {
            m_audioInput->stop();
            if (m_audioDevice) {
                m_audioDevice->close();
            }
        }
        
        // Use a very short delay to allow cleanup to complete
        QTimer::singleShot(10, qApp, &QCoreApplication::quit);
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        // "Send" hotkey - only if recording is active
        if (m_isRecording) {
            stopRecording(true);
        }
    } else {
        QMainWindow::keyPressEvent(event);
    }
}
