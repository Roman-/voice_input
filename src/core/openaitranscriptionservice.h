#ifndef OPENAITRANSCRIPTIONSERVICE_H
#define OPENAITRANSCRIPTIONSERVICE_H

#include "transcriptionservice.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

class OpenAiTranscriptionService : public TranscriptionService
{
    Q_OBJECT
public:
    explicit OpenAiTranscriptionService(QObject* parent = nullptr);
    ~OpenAiTranscriptionService() override;

    // Start transcription of the given audio file
    void transcribeAudio(const QString& audioFilePath, const QString& language) override;
    
    // Cancel ongoing transcription
    void cancelTranscription() override;
    
    // Check if transcription is in progress
    bool isTranscribing() const override;
    
    // Get last error message
    QString lastError() const override;
    
    // Check if the API key is available
    bool hasApiKey() const override;
    
    // Refresh API key from environment (used when retrying)
    void refreshApiKey() override;

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

#endif // OPENAITRANSCRIPTIONSERVICE_H