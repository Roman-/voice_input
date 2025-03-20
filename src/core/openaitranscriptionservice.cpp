#include "openaitranscriptionservice.h"
#include <QHttpMultiPart>
#include <QNetworkRequest>
#include <QProcess>
#include <QDebug>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QTimer>
#include <QFileInfo>
#include "config/config.h"

OpenAiTranscriptionService::OpenAiTranscriptionService(QObject* parent)
    : TranscriptionService(parent),
      m_networkManager(new QNetworkAccessManager(this)),
      m_currentReply(nullptr),
      m_isTranscribing(false)
{
    // Retrieve API key from environment variable
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    m_apiKey = env.value("OPENAI_API_KEY");
    
    // Connect network signals
    connect(m_networkManager, &QNetworkAccessManager::finished, 
            this, &OpenAiTranscriptionService::handleNetworkReply);
}

OpenAiTranscriptionService::~OpenAiTranscriptionService()
{
    cancelTranscription();
}

void OpenAiTranscriptionService::transcribeAudio(const QString& audioFilePath)
{
    // Check if we're already transcribing
    if (m_isTranscribing) {
        cancelTranscription();
    }
    
    // Check for API key
    if (!hasApiKey()) {
        m_lastError = "OpenAI API key not found in environment variable OPENAI_API_KEY";
        emit transcriptionFailed(m_lastError);
        return;
    }
    
    // Check if file exists
    QFile fileCheck(audioFilePath);
    if (!fileCheck.exists()) {
        m_lastError = "Audio file does not exist: " + audioFilePath;
        emit transcriptionFailed(m_lastError);
        return;
    }
    
    // Get file info to check the MIME type
    QFileInfo fileInfo(audioFilePath);
    qDebug() << "File extension:" << fileInfo.suffix();
    
    // Verify it's an MP3 file
    if (fileInfo.suffix().toLower() != "mp3") {
        qWarning() << "Warning: File extension is not mp3, may not be recognized by the API";
    }
    
    // Create a file object that will be owned by the multipart
    QFile* audioFilePtr = new QFile(audioFilePath);
    if (!audioFilePtr->open(QIODevice::ReadOnly)) {
        m_lastError = "Could not open audio file: " + audioFilePtr->errorString();
        delete audioFilePtr;
        emit transcriptionFailed(m_lastError);
        return;
    }
    
    // Prepare multipart request for file upload
    QHttpMultiPart* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    
    // Add the file part
    QHttpPart filePart;
    // Don't specify a content type, let the server detect it
    // filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("audio/mpeg"));
    
    // Keep the original file extension
    QString filename = "audio." + fileInfo.suffix();
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader, 
                      QVariant(QString("form-data; name=\"file\"; filename=\"%1\"").arg(filename)));
    
    filePart.setBodyDevice(audioFilePtr);
    audioFilePtr->setParent(multiPart); // QHttpMultiPart takes ownership
    
    qDebug() << "Sending file with filename:" << filename;
    multiPart->append(filePart);
    
    // Add model parameter
    QHttpPart modelPart;
    modelPart.setHeader(QNetworkRequest::ContentDispositionHeader, 
                        QVariant("form-data; name=\"model\""));
    modelPart.setBody("whisper-1");
    multiPart->append(modelPart);
    
    // Add language parameter
    QHttpPart languagePart;
    languagePart.setHeader(QNetworkRequest::ContentDispositionHeader, 
                          QVariant("form-data; name=\"language\""));
    languagePart.setBody("en");
    multiPart->append(languagePart);
    
    // Add temperature parameter
    QHttpPart temperaturePart;
    temperaturePart.setHeader(QNetworkRequest::ContentDispositionHeader, 
                             QVariant("form-data; name=\"temperature\""));
    temperaturePart.setBody("0.1");
    multiPart->append(temperaturePart);
    
    // Setup the request
    QUrl url("https://api.openai.com/v1/audio/transcriptions");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());
    
    // Set timeout to prevent hanging requests
    request.setTransferTimeout(60000); // 60 seconds timeout
    
    // Add debug output to understand the request being sent
    qDebug() << "Sending audio file:" << audioFilePath << "with size:" << QFileInfo(audioFilePath).size() << "bytes";
    qDebug() << "Using API key starting with:" << m_apiKey.left(5) + "..." << "(length:" << m_apiKey.length() << ")";
    
    // Send the request
    m_isTranscribing = true;
    emit transcriptionProgress("Sending audio to transcription service...");
    
    // Create a new network manager for each request to avoid issues
    if (m_networkManager) {
        m_networkManager->deleteLater();
    }
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &OpenAiTranscriptionService::handleNetworkReply);
    
    m_currentReply = m_networkManager->post(request, multiPart);
    multiPart->setParent(m_currentReply); // QNetworkReply takes ownership
    
    // Connect to progress signals
    connect(m_currentReply, &QNetworkReply::uploadProgress, this, &OpenAiTranscriptionService::onUploadProgress);
}

void OpenAiTranscriptionService::cancelTranscription()
{
    if (m_isTranscribing) {
        if (m_currentReply) {
            m_currentReply->abort();
            m_currentReply->deleteLater();
            m_currentReply = nullptr;
        }
        m_isTranscribing = false;
        emit transcriptionProgress("Transcription canceled");
    }
}

bool OpenAiTranscriptionService::isTranscribing() const
{
    return m_isTranscribing;
}

QString OpenAiTranscriptionService::lastError() const
{
    return m_lastError;
}

bool OpenAiTranscriptionService::hasApiKey() const
{
    return !m_apiKey.isEmpty();
}

void OpenAiTranscriptionService::onUploadProgress(qint64 bytesSent, qint64 bytesTotal)
{
    if (bytesTotal > 0) {
        int percentage = static_cast<int>((bytesSent * 100) / bytesTotal);
        emit transcriptionProgress(QString("Uploading audio: %1%").arg(percentage));
    }
}

void OpenAiTranscriptionService::handleNetworkReply(QNetworkReply* reply)
{
    if (reply != m_currentReply) {
        // Not our current reply, ignore it
        return;
    }
    
    m_isTranscribing = false;
    
    // Check for network errors
    if (reply->error() != QNetworkReply::NoError) {
        m_lastError = QString("Network error: %1").arg(reply->errorString());
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        
        // Read response data even for errors
        QByteArray responseData = reply->readAll();
        
        qWarning() << "Transcription failed:" << m_lastError << "HTTP status:" << httpStatus;
        qWarning() << "Response body:" << responseData;
        
        emit transcriptionFailed(m_lastError);
        reply->deleteLater();
        m_currentReply = nullptr;
        return;
    }
    
    // Read response data
    QByteArray responseData = reply->readAll();
    
    // Parse JSON response
    QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
    if (jsonDoc.isNull() || !jsonDoc.isObject()) {
        m_lastError = "Invalid response from transcription service";
        qWarning() << "Transcription failed:" << m_lastError;
        emit transcriptionFailed(m_lastError);
        reply->deleteLater();
        m_currentReply = nullptr;
        return;
    }
    
    QJsonObject jsonObj = jsonDoc.object();
    
    // Check for API error response
    if (jsonObj.contains("error")) {
        QJsonObject errorObj = jsonObj["error"].toObject();
        m_lastError = QString("API error: %1").arg(errorObj["message"].toString());
        qWarning() << "Transcription failed:" << m_lastError;
        emit transcriptionFailed(m_lastError);
        reply->deleteLater();
        m_currentReply = nullptr;
        return;
    }
    
    // Extract transcription text
    if (jsonObj.contains("text")) {
        QString transcribedText = jsonObj["text"].toString();
        qInfo() << "Transcription completed successfully";
        
        // Save transcription to file
        QFile outputFile(TRANSCRIPTION_OUTPUT_PATH);
        if (outputFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&outputFile);
            out << transcribedText;
            outputFile.close();
            qInfo() << "Transcription saved to" << TRANSCRIPTION_OUTPUT_PATH;
        } else {
            qWarning() << "Failed to save transcription to file";
        }
        
        emit transcriptionCompleted(transcribedText);
    } else {
        m_lastError = "No transcription text found in response";
        qWarning() << "Transcription failed:" << m_lastError;
        qWarning() << "Response received:" << responseData;
        emit transcriptionFailed(m_lastError);
    }
    
    reply->deleteLater();
    m_currentReply = nullptr;
}