#ifndef TRANSCRIPTIONFACTORY_H
#define TRANSCRIPTIONFACTORY_H

#include "transcriptionservice.h"

class TranscriptionFactory
{
public:
    // Creates and returns the appropriate transcription service
    // based on environment variables
    static TranscriptionService* createTranscriptionService(QObject* parent = nullptr);
};

#endif // TRANSCRIPTIONFACTORY_H