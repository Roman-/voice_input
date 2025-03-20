#include "transcriptionfactory.h"
#include "openaitranscriptionservice.h"
#include "mocktranscriptionservice.h"
#include <QProcessEnvironment>
#include <QDebug>

TranscriptionService* TranscriptionFactory::createTranscriptionService(QObject* parent)
{
    // Get the API key from environment
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString apiKey = env.value("OPENAI_API_KEY");
    
    // If API key is "test", use the mock implementation
    if (apiKey == "test") {
        qDebug() << "Using mock transcription service (OPENAI_API_KEY=test)";
        return new MockTranscriptionService(parent);
    } else {
        // Otherwise, use the real implementation
        qDebug() << "Using real OpenAI transcription service";
        return new OpenAiTranscriptionService(parent);
    }
}