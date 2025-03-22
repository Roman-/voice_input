#ifndef TRANSCRIPTIONSERVICE_H
#define TRANSCRIPTIONSERVICE_H

#include <QObject>

class TranscriptionService : public QObject
{
    Q_OBJECT
public:
    explicit TranscriptionService(QObject* parent = nullptr) : QObject(parent) {}
    ~TranscriptionService() override = default;

    // Start transcription of the given audio file
    virtual void transcribeAudio(const QString& audioFilePath, const QString& language) = 0;
    
    // Cancel ongoing transcription
    virtual void cancelTranscription() = 0;
    
    // Check if transcription is in progress
    virtual bool isTranscribing() const = 0;
    
    // Get last error message
    virtual QString lastError() const = 0;
    
    // Check if the API key is available
    virtual bool hasApiKey() const = 0;
    
    // Refresh API key from environment (used when retrying)
    virtual void refreshApiKey() = 0;

signals:
    // Emitted when transcription completes successfully
    void transcriptionCompleted(const QString& transcribedText);
    
    // Emitted when transcription fails
    void transcriptionFailed(const QString& errorMessage);
    
    // Progress information
    void transcriptionProgress(const QString& status);
};

#endif // TRANSCRIPTIONSERVICE_H