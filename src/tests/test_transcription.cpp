#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QFile>
#include <QProcessEnvironment>
#include <QDebug>
#include <iostream>

#include "core/transcriptionservice.h"
#include "core/transcriptionfactory.h"
#include "core/openaitranscriptionservice.h"
#include "core/mocktranscriptionservice.h"
#include "config/config.h"

class TestTranscription : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testApiKeyDetection();
    void testTranscription();
    void testMockTranscription();

private:
    TranscriptionService* transcriptionService;
    bool apiKeyAvailable;
    bool isUsingMock;
    QString testAudioPath;
};

void TestTranscription::initTestCase()
{
    // Create transcription service using the factory
    transcriptionService = TranscriptionFactory::createTranscriptionService(this);
    
    // Check if we're using the mock or real service
    isUsingMock = qobject_cast<MockTranscriptionService*>(transcriptionService) != nullptr;
    
    // Check if API key is available in environment
    apiKeyAvailable = transcriptionService->hasApiKey();
    if (!apiKeyAvailable && !isUsingMock) {
        qWarning() << "OPENAI_API_KEY environment variable not set or invalid. Some tests will be skipped.";
    }
    
    // Use our properly formatted test file
    QStringList possiblePaths;
    possiblePaths << QDir::currentPath() + "/hello_world.mp3"  // Current directory
                  << QDir::currentPath() + "/../hello_world.mp3"; // Parent directory
    
    // Try to find the file
    testAudioPath = "";
    for (const QString& path : possiblePaths) {
        QFile testFile(path);
        if (testFile.exists()) {
            testAudioPath = path;
            qInfo() << "Found test audio file at:" << testAudioPath;
            break;
        }
    }
    
    // Check if any of the paths had the file
    if (testAudioPath.isEmpty()) {
        qWarning() << "Test audio file not found in any of these locations:";
        for (const QString& path : possiblePaths) {
            qWarning() << " -" << path;
        }
        qWarning() << "Make sure hello_world.mp3 exists in the project root directory.";
    }
}

void TestTranscription::cleanupTestCase()
{
    delete transcriptionService;
    
    // Cleanup any transcription output file that might have been created
    QFile outputFile(TRANSCRIPTION_OUTPUT_PATH);
    if (outputFile.exists()) {
        outputFile.remove();
    }
}

void TestTranscription::testApiKeyDetection()
{
    // If we're using the mock service, it should always have an API key
    if (isUsingMock) {
        QVERIFY(transcriptionService->hasApiKey());
        qInfo() << "Mock service always has an API key";
        return;
    }
    
    // For the real service, test that it correctly detects if API key is available
    bool hasKey = transcriptionService->hasApiKey();
    QCOMPARE(hasKey, apiKeyAvailable);
    
    // Check if API key is in environment (directly)
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString apiKey = env.value("OPENAI_API_KEY");
    bool envHasKey = !apiKey.isEmpty();
    QCOMPARE(hasKey, envHasKey);
}

void TestTranscription::testTranscription()
{
    // Skip this test if using mock service or if API key is not available
    if (isUsingMock) {
        QSKIP("Skipping real transcription test - using mock service");
    }
    
    if (!apiKeyAvailable) {
        QSKIP("Skipping real transcription test - no API key available");
    }
    
    // Check if test audio file was found
    if (testAudioPath.isEmpty()) {
        QFAIL("Test audio file not found. Make sure hello_world.mp3 exists in the project root directory.");
    }
    
    // Create signal spy for completion and failure signals
    QSignalSpy completedSpy(transcriptionService, &TranscriptionService::transcriptionCompleted);
    QSignalSpy failedSpy(transcriptionService, &TranscriptionService::transcriptionFailed);
    QSignalSpy progressSpy(transcriptionService, &TranscriptionService::transcriptionProgress);
    
    // Start transcription
    qInfo() << "Starting transcription of test file using REAL OpenAI API:" << testAudioPath;
    qInfo() << "This may take a minute or two to complete...";
    transcriptionService->transcribeAudio(testAudioPath);
    
    // Wait for completion or failure (timeout after 120 seconds for real API call)
    const int maxWaitMs = 120000;  // 2 minutes
    const int checkIntervalMs = 1000;  // 1 second
    int elapsedMs = 0;
    
    while (elapsedMs < maxWaitMs) {
        if (completedSpy.count() > 0 || failedSpy.count() > 0) {
            break;
        }
        QTest::qWait(checkIntervalMs);
        elapsedMs += checkIntervalMs;
        
        // Log progress if available
        if (progressSpy.count() > 0) {
            auto lastProgress = progressSpy.takeLast();
            qInfo() << "Progress:" << lastProgress[0].toString();
        }
    }
    
    // Check if the API call failed
    if (failedSpy.count() > 0) {
        // Get the error message from the signal
        const QList<QVariant>& errorArgs = failedSpy.takeFirst();
        QString errorMessage = errorArgs.at(0).toString();
        
        // Fail the test with a detailed error message
        QFAIL(QString("API call failed: %1").arg(errorMessage).toUtf8().constData());
    }
    
    // Check if transcription completed successfully
    QVERIFY2(completedSpy.count() > 0, "Transcription did not complete within timeout");
    
    // Get the transcribed text from the signal
    const QList<QVariant>& arguments = completedSpy.takeFirst();
    QString transcribedText = arguments.at(0).toString().toLower(); // Convert to lowercase for case-insensitive comparison
    
    qInfo() << "Transcribed text:" << transcribedText;
    
    // Check if the transcription contains the expected words
    QVERIFY2(transcribedText.contains("hello"), "Transcription does not contain 'hello'");
    QVERIFY2(transcribedText.contains("world"), "Transcription does not contain 'world'");
    
    // Check if transcription file was created
    QFile outputFile(TRANSCRIPTION_OUTPUT_PATH);
    QVERIFY2(outputFile.exists(), "Transcription output file was not created");
    
    // Verify the contents of the transcription file
    outputFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QString fileContent = outputFile.readAll().toLower();
    outputFile.close();
    
    QVERIFY2(fileContent.contains("hello"), "Transcription file does not contain 'hello'");
    QVERIFY2(fileContent.contains("world"), "Transcription file does not contain 'world'");
}

void TestTranscription::testMockTranscription()
{
    // Only run this test if we're using the mock service
    if (!isUsingMock) {
        QSKIP("Skipping mock transcription test - not using mock service");
    }
    
    // Check if test audio file was found
    if (testAudioPath.isEmpty()) {
        QFAIL("Test audio file not found. Make sure hello_world.mp3 exists in the project root directory.");
    }
    
    // Create signal spy for completion and failure signals
    QSignalSpy completedSpy(transcriptionService, &TranscriptionService::transcriptionCompleted);
    QSignalSpy failedSpy(transcriptionService, &TranscriptionService::transcriptionFailed);
    QSignalSpy progressSpy(transcriptionService, &TranscriptionService::transcriptionProgress);
    
    // Start transcription
    qInfo() << "Starting transcription of test file using MOCK service:" << testAudioPath;
    transcriptionService->transcribeAudio(testAudioPath);
    
    // Wait for completion or failure (maximum 10 seconds for mock service)
    const int maxWaitMs = 10000;  // 10 seconds
    const int checkIntervalMs = 100;  // 100 milliseconds
    int elapsedMs = 0;
    
    while (elapsedMs < maxWaitMs) {
        if (completedSpy.count() > 0 || failedSpy.count() > 0) {
            break;
        }
        QTest::qWait(checkIntervalMs);
        elapsedMs += checkIntervalMs;
        
        // Log progress if available
        if (progressSpy.count() > 0) {
            auto lastProgress = progressSpy.takeLast();
            qInfo() << "Progress:" << lastProgress[0].toString();
        }
    }
    
    // Check if the mock API call failed
    if (failedSpy.count() > 0) {
        // Get the error message from the signal
        const QList<QVariant>& errorArgs = failedSpy.takeFirst();
        QString errorMessage = errorArgs.at(0).toString();
        
        // Fail the test with a detailed error message
        QFAIL(QString("Mock API call failed: %1").arg(errorMessage).toUtf8().constData());
    }
    
    // Check if transcription completed successfully
    QVERIFY2(completedSpy.count() > 0, "Mock transcription did not complete within timeout");
    
    // Get the transcribed text from the signal
    const QList<QVariant>& arguments = completedSpy.takeFirst();
    QString transcribedText = arguments.at(0).toString();
    
    qInfo() << "Mock transcribed text:" << transcribedText;
    
    // Check if the mock transcription contains the expected text
    QCOMPARE(transcribedText, QString("Hello, world."));
    
    // Check if transcription file was created
    QFile outputFile(TRANSCRIPTION_OUTPUT_PATH);
    QVERIFY2(outputFile.exists(), "Mock transcription output file was not created");
    
    // Verify the contents of the transcription file
    outputFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QString fileContent = outputFile.readAll();
    outputFile.close();
    
    QCOMPARE(fileContent, QString("Hello, world."));
}

QTEST_MAIN(TestTranscription)
#include "test_transcription.moc"