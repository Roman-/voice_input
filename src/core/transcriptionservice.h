#ifndef TRANSCRIPTIONSERVICE_H
#define TRANSCRIPTIONSERVICE_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

class TranscriptionService : public QObject
{
    Q_OBJECT
public:
    explicit TranscriptionService(QObject* parent = nullptr);
    ~TranscriptionService();

    // Start transcription of the given audio file
    void transcribeAudio(const QString& audioFilePath);
    
    // Cancel ongoing transcription
    void cancelTranscription();
    
    // Check if transcription is in progress
    bool isTranscribing() const;
    
    // Get last error message
    QString lastError() const;
    
    // Check if the API key is available
    bool hasApiKey() const;

signals:
    // Emitted when transcription completes successfully
    void transcriptionCompleted(const QString& transcribedText);
    
    // Emitted when transcription fails
    void transcriptionFailed(const QString& errorMessage);
    
    // Progress information
    void transcriptionProgress(const QString& status);

private slots:
    void handleNetworkReply(QNetworkReply* reply);
    void onUploadProgress(qint64 bytesSent, qint64 bytesTotal);

private:
    QNetworkAccessManager* m_networkManager;
    QNetworkReply* m_currentReply;
    QString m_lastError;
    bool m_isTranscribing;
    QString m_apiKey;
};

#endif // TRANSCRIPTIONSERVICE_H