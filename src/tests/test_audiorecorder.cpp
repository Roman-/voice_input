#include <QtTest/QtTest>
#include "src/core/audiorecorder.h"

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
};

// QTest macro to include the test's main function
QTEST_MAIN(TestAudioRecorder)
#include "test_audiorecorder.moc"
