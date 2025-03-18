#include <QtTest/QtTest>
#include "core/audiorecorder.h"
#include "config/config.h"

class TestAudioRecorder : public QObject
{
    Q_OBJECT

private slots:
    void testInitializeAndStop()
    {
        AudioRecorder recorder;
        QVERIFY(!recorder.currentVolumeLevel()); // Should start at 0
        QVERIFY(recorder.startRecording());
        QVERIFY(recorder.currentVolumeLevel() == 0.0f); // no real data yet
        recorder.stopRecording();
    }
    
    void testFileCreation()
    {
        // Remove any existing file first
        QFile leftoverFile(OUTPUT_FILE_PATH);
        if (leftoverFile.exists()) {
            leftoverFile.remove();
        }
        
        // Start recording for a short time
        AudioRecorder recorder;
        QVERIFY(recorder.startRecording());
        
        // Sleep to let it record for a moment
        QTest::qWait(500);
        
        // Stop recording
        recorder.stopRecording();
        
        // Verify the file exists and is not empty
        QFileInfo fileInfo(OUTPUT_FILE_PATH);
        QVERIFY(fileInfo.exists());
        QVERIFY(fileInfo.size() > 0);
        
        // Check file has proper ADTS header
        QFile outputFile(OUTPUT_FILE_PATH);
        QVERIFY(outputFile.open(QIODevice::ReadOnly));
        QByteArray fileStart = outputFile.read(7); // ADTS header is 7 bytes
        outputFile.close();
        
        // Check for ADTS sync word (0xFFF) in the first 2 bytes
        if (fileStart.size() >= 2) {
            QVERIFY((static_cast<unsigned char>(fileStart[0]) & 0xFF) == 0xFF);
            QVERIFY((static_cast<unsigned char>(fileStart[1]) & 0xF0) == 0xF0);
        }
    }
};

// QTest macro to include the test's main function
QTEST_MAIN(TestAudioRecorder)
#include "test_audiorecorder.moc"
