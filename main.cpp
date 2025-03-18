#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QTimer>
#include <csignal>

#include "config/config.h"
#include "core/audiorecorder.h"
#include "ui/mainwindow.h"

// Global pointer to AudioRecorder for signal handling
static AudioRecorder* g_audioRecorder = nullptr;

static void signalHandler(int sig)
{
    qInfo() << "[INFO] Received termination signal:" << sig;
    if (g_audioRecorder) {
        g_audioRecorder->stopRecording();
    }
    QCoreApplication::quit();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Configure logging format for simplicity
    // (Alternatively, configure qInstallMessageHandler for advanced logging)
    qSetMessagePattern("[%{type}] %{message}");

    qInfo() << "[INFO] Application started";

    // Remove any leftover output file from previous runs
    QFile leftoverFile(OUTPUT_FILE_PATH);
    if (leftoverFile.exists()) {
        leftoverFile.remove();
        qInfo() << "[DEBUG] Removed leftover file:" << OUTPUT_FILE_PATH;
    }

    // Parse command line arguments
    QCommandLineParser parser;
    parser.setApplicationDescription("Audio Recorder Application");
    parser.addHelpOption();

    QCommandLineOption timeoutOption(QStringList() << "t" << "timeout",
                                     "Stop recording after <milliseconds> timeout.",
                                     "milliseconds");
    parser.addOption(timeoutOption);

    parser.process(app);

    int timeoutMs = DEFAULT_TIMEOUT;
    if (parser.isSet(timeoutOption)) {
        bool ok = false;
        int val = parser.value(timeoutOption).toInt(&ok);
        if (ok && val > 0) {
            timeoutMs = val;
        }
    }

    // Create the AudioRecorder
    AudioRecorder recorder;
    g_audioRecorder = &recorder; // For signalHandler access

    // Connect aboutToQuit for graceful cleanup
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&](){
        recorder.stopRecording();
    });

    // Create main window (UI) and pass a pointer to the recorder
    MainWindow window(&recorder);
    window.show();
    
    // Start recording asynchronously after the event loop starts (completely decouple UI startup)
    QTimer::singleShot(0, [&recorder](){
        recorder.startRecording();
    });

    // If timeout was specified, stop after the given period
    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, [&](){
            qInfo() << "[INFO] Timeout reached:" << timeoutMs << "ms";
            recorder.stopRecording();
            QApplication::quit();
        });
    }

    // Install signal handlers for SIGINT, SIGTERM
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    return app.exec();
}
