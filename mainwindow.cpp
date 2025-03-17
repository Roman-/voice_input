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

MainWindow::MainWindow(bool sendToOpenAI, QWidget *parent)
    : QMainWindow(parent),
      m_sendToOpenAI(sendToOpenAI)
{
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
    resize(400, 200);

    // Timer to update recording stats (file size, duration)
    connect(&m_updateTimer, &QTimer::timeout, this, &MainWindow::updateRecordingStats);
    m_updateTimer.start(500);

    // Timer to update volume level
    connect(&m_volumeTimer, &QTimer::timeout, this, &MainWindow::updateVolumeLevel);
    m_volumeTimer.start(200);
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

    // Create a unique output file path using timestamp
    QString docsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString voiceInputDir = docsPath + "/voice_input";
    QDir().mkpath(voiceInputDir); // Create the directory if it doesn't exist
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    m_outputFilePath = QString("%1/recording_%2.m4a").arg(voiceInputDir).arg(timestamp);
    
    // Store a copy in tmp for possible OpenAI transcription
    QString tmpPath = "/tmp/voice_input.m4a";
    
    // Build ffmpeg command
    // -y = overwrite output
    // -f pulse -i default: use PulseAudio default input device
    // -acodec aac -b:a 128k
    QString program = "ffmpeg";
    QStringList arguments;
    arguments << "-y"
              << "-f" << "pulse"
              << "-i" << "default"
              << "-acodec" << "aac"
              << "-b:a" << "128k"
              << tmpPath;

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

    // Stop the ffmpeg process
    m_ffmpegProcess.terminate();
    // Give it a moment to shutdown gracefully
    if (!m_ffmpegProcess.waitForFinished(2000)) {
        m_ffmpegProcess.kill();
        m_ffmpegProcess.waitForFinished();
    }
    m_isRecording = false;
    
    // Copy the file from tmp to the final destination
    QFile::copy("/tmp/voice_input.m4a", m_outputFilePath);
    
    // Check if the file exists and output the path
    QFile outputFile(m_outputFilePath);
    if (outputFile.exists()) {
        qDebug() << "Recording saved to:" << m_outputFilePath;
    } else {
        qDebug() << "ERROR: Failed to save recording to:" << m_outputFilePath;
    }

    if (sendToSTT && m_sendToOpenAI) {
        m_statusLabel->setText("Uploading to OpenAI Whisper...");
        m_sendRequested = true;
        sendForTranscription();
    } else {
        // Always show the saved file path in status bar
        m_statusLabel->setText(QString("Recording saved to: %1").arg(m_outputFilePath));
        
        // If no transcription requested/allowed, exit after a delay
        if (!sendToSTT || !m_sendToOpenAI) {
            QTimer::singleShot(2000, qApp, &QCoreApplication::quit);
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
    QFile file("/tmp/voice_input.m4a");
    if (file.exists()) {
        m_currentFileSize = file.size();
        if (m_currentFileSize >= MAX_FILE_SIZE_BYTES) {
            // Stop the recording and show error
            stopRecording(false); // do not send
            showError("Max file size (35 MB) reached. Transcription canceled.");
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
    if (!m_audioDevice || !m_audioInput) return;

    // We can read some data from m_audioDevice to get an approximate level
    QByteArray buffer = m_audioDevice->readAll();
    if (buffer.isEmpty())
        return;

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
    m_volumeBar->setValue(level);
}

void MainWindow::sendForTranscription()
{
    // If file is missing or empty, just error out
    QFileInfo fi("/tmp/voice_input.m4a");
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
    api->setModel("whisper-1");
    api->setTemperature(0.1);
    api->setLanguage("en");

    // Perform the transcription
    api->transcribe("/tmp/voice_input.m4a");
}

void MainWindow::onTranscriptionSuccess(const QString &text)
{
    // Get directory path from m_outputFilePath
    QFileInfo fileInfo(m_outputFilePath);
    QString dir = fileInfo.absolutePath();
    QString baseName = fileInfo.baseName();
    
    // Save transcription next to the recording
    QString transcriptionPath = QString("%1/%2.txt").arg(dir).arg(baseName);
    QFile outFile(transcriptionPath);
    if (outFile.open(QFile::WriteOnly | QFile::Truncate)) {
        outFile.write(text.toUtf8());
        outFile.close();
    }
    
    // Also save to /tmp for compatibility
    QFile tmpFile("/tmp/voice_input.txt");
    if (tmpFile.open(QFile::WriteOnly | QFile::Truncate)) {
        tmpFile.write(text.toUtf8());
        tmpFile.close();
    }

    // Check if transcription file exists and output the path
    QFile transFile(transcriptionPath);
    if (transFile.exists()) {
        qDebug() << "Transcription saved to:" << transcriptionPath;
    } else {
        qDebug() << "ERROR: Failed to save transcription to:" << transcriptionPath;
    }

    m_statusLabel->setText(QString("Transcription success. Saved to: %1").arg(transcriptionPath));
    // Optionally, place text on clipboard:
    // QApplication::clipboard()->setText(text);

    // Exit after a short delay
    QTimer::singleShot(2000, qApp, &QCoreApplication::quit);
}

void MainWindow::onTranscriptionError(const QString &errorMsg)
{
    if (!m_retryUsed) {
        // Offer a retry
        m_retryButton->show();
        showError(QString("Transcription failed: %1. You may retry.").arg(errorMsg));
    } else {
        // Already retried once
        showError(QString("Transcription failed again: %1. Exiting...").arg(errorMsg));
        QTimer::singleShot(2000, qApp, &QCoreApplication::quit);
    }
}

void MainWindow::retryTranscription()
{
    if (m_retryUsed) return; // only allow once

    m_retryUsed = true;
    m_retryButton->hide();
    m_statusLabel->setText("Retrying transcription...");
    sendForTranscription();
}

void MainWindow::showError(const QString &message)
{
    m_statusLabel->setText(message);
    qWarning() << message; // log to stderr
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        // Immediately cancel and quit
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
        // "Send" hotkey
        stopRecording(true);
    } else {
        QMainWindow::keyPressEvent(event);
    }
}
