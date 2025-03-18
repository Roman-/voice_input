#include <QtTest/QtTest>
#include <QProcess>
#include <QFileInfo>
#include "config/config.h"

class TestFileVerification : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Remove any existing test file
        QFile::remove(OUTPUT_FILE_PATH);
    }
    
    void cleanupTestCase()
    {
        // Clean up after tests
        QFile::remove(OUTPUT_FILE_PATH);
    }
    
    void testRecordedFileFormat()
    {
        // Run the recorder with a very short timeout
        QProcess recorder;
        recorder.start("../build/audio_recorder", QStringList() << "--timeout" << "1000");
        QVERIFY(recorder.waitForStarted(3000));
        
        // Wait for timeout + extra time for shutdown
        QVERIFY(recorder.waitForFinished(3000));
        
        // Check exit code
        QVERIFY(recorder.exitCode() == 0);
        
        // Check if file exists
        QFileInfo fileInfo(OUTPUT_FILE_PATH);
        QVERIFY(fileInfo.exists());
        QVERIFY(fileInfo.size() > 0);
        qDebug() << "Generated file size:" << fileInfo.size() << "bytes";
        
        // Check if file has valid AAC/M4A structure
        QFile file(OUTPUT_FILE_PATH);
        QVERIFY(file.open(QIODevice::ReadOnly));
        
        // Read the first bytes to check for AAC/ADTS signature
        QByteArray header = file.read(8);
        file.close();
        
        // ADTS files start with 0xFFF...
        if (header.size() >= 2) {
            quint8 byte1 = static_cast<quint8>(header[0]);
            quint8 byte2 = static_cast<quint8>(header[1]);
            
            // Check for ADTS sync word (0xFFF)
            bool hasADTSHeader = (byte1 == 0xFF) && ((byte2 & 0xF0) == 0xF0);
            QVERIFY2(hasADTSHeader, "File doesn't have valid ADTS header");
        } else {
            QFAIL("File too small to have valid header");
        }
    }
    
    void testTimeoutFeature()
    {
        // Test different timeout values to ensure they work
        QElapsedTimer timer;
        
        // Run with 500ms timeout
        timer.start();
        QProcess recorder;
        recorder.start("../build/audio_recorder", QStringList() << "--timeout" << "500");
        recorder.waitForFinished(2000);
        qint64 elapsed = timer.elapsed();
        
        // Check that it terminated within a reasonable time (500ms + some overhead)
        QVERIFY(elapsed >= 500);
        QVERIFY(elapsed < 1500); // Allow some extra time for startup/shutdown
        
        // Check file was created
        QFileInfo fileInfo(OUTPUT_FILE_PATH);
        QVERIFY(fileInfo.exists());
    }
};

QTEST_MAIN(TestFileVerification)
#include "test_file_verification.moc"