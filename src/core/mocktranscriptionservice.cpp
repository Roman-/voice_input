#include "mocktranscriptionservice.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QRandomGenerator>
#include "config/config.h"

MockTranscriptionService::MockTranscriptionService(QObject* parent)
    : TranscriptionService(parent),
      m_timer(new QTimer(this)),
      m_isTranscribing(false)
{
    // Configure timer to be single-shot
    m_timer->setSingleShot(true);
    
    // Connect timer signal to complete the mock transcription
    connect(m_timer, &QTimer::timeout, this, &MockTranscriptionService::completeTranscription);
}

MockTranscriptionService::~MockTranscriptionService()
{
    cancelTranscription();
}

void MockTranscriptionService::transcribeAudio(const QString& audioFilePath)
{
    // Check if we're already transcribing
    if (m_isTranscribing) {
        cancelTranscription();
    }
    
    // Check if file exists
    QFile fileCheck(audioFilePath);
    if (!fileCheck.exists()) {
        m_lastError = "Audio file does not exist: " + audioFilePath;
        emit transcriptionFailed(m_lastError);
        return;
    }
    
    // Verify it's an MP3 file
    QFileInfo fileInfo(audioFilePath);
    if (fileInfo.suffix().toLower() != "mp3") {
        qWarning() << "Warning: File extension is not mp3, proceeding anyway for mock service";
    }
    
    qDebug() << "Mock transcription service: Simulating transcription for" << audioFilePath;
    
    // Set random delay between 1-3 seconds to simulate network latency
    int delay = QRandomGenerator::global()->bounded(1000, 3000);
    
    // Start the transcription simulation
    m_isTranscribing = true;
    emit transcriptionProgress("Sending audio to mock transcription service...");
    
    // After 30% of the delay, update progress
    QTimer::singleShot(delay * 0.3, this, [this]() {
        if (m_isTranscribing) {
            emit transcriptionProgress("Uploading audio: 50%");
        }
    });
    
    // After 60% of the delay, update progress again
    QTimer::singleShot(delay * 0.6, this, [this]() {
        if (m_isTranscribing) {
            emit transcriptionProgress("Uploading audio: 100%");
        }
    });
    
    // Start the timer to simulate API processing time
    m_timer->start(delay);
}

void MockTranscriptionService::cancelTranscription()
{
    if (m_isTranscribing) {
        if (m_timer->isActive()) {
            m_timer->stop();
        }
        m_isTranscribing = false;
        emit transcriptionProgress("Transcription canceled");
    }
}

bool MockTranscriptionService::isTranscribing() const
{
    return m_isTranscribing;
}

QString MockTranscriptionService::lastError() const
{
    return m_lastError;
}

bool MockTranscriptionService::hasApiKey() const
{
    // Always return true since this is a mock implementation
    return true;
}

void MockTranscriptionService::completeTranscription()
{
    if (!m_isTranscribing) {
        return;
    }
    
    m_isTranscribing = false;
    
    // Create mock JSON response
    QJsonObject jsonResponse;
    jsonResponse["text"] = "Hello, world.";
    
    // Mock successful response
    QString transcribedText = jsonResponse["text"].toString();
    qDebug() << "Mock transcription completed successfully with text:" << transcribedText;
    
    // Save transcription to file
    QFile outputFile(TRANSCRIPTION_OUTPUT_PATH);
    if (outputFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&outputFile);
        out << transcribedText;
        outputFile.close();
        qDebug() << "Mock transcription saved to" << TRANSCRIPTION_OUTPUT_PATH;
    } else {
        qWarning() << "Failed to save mock transcription to file";
    }
    
    emit transcriptionCompleted(transcribedText);
}