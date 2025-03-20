#ifndef MOCKTRANSCRIPTIONSERVICE_H
#define MOCKTRANSCRIPTIONSERVICE_H

#include "transcriptionservice.h"
#include <QTimer>

class MockTranscriptionService : public TranscriptionService
{
    Q_OBJECT
public:
    explicit MockTranscriptionService(QObject* parent = nullptr);
    ~MockTranscriptionService() override;

    // Start transcription of the given audio file
    void transcribeAudio(const QString& audioFilePath) override;
    
    // Cancel ongoing transcription
    void cancelTranscription() override;
    
    // Check if transcription is in progress
    bool isTranscribing() const override;
    
    // Get last error message
    QString lastError() const override;
    
    // Check if the API key is available
    bool hasApiKey() const override;

private slots:
    void completeTranscription();

private:
    QTimer* m_timer;
    QString m_lastError;
    bool m_isTranscribing;
};

#endif // MOCKTRANSCRIPTIONSERVICE_H