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

// Global pointers for signal handling
static AudioRecorder* g_audioRecorder = nullptr;
static MainWindow* g_mainWindow = nullptr;

static void signalHandler(int sig)
{
    qInfo() << "[INFO] Received termination signal:" << sig;
    if (g_audioRecorder) {
        g_audioRecorder->stopRecording();
    }
    
    // Cancel any transcription in progress
    if (g_mainWindow) {
        g_mainWindow->cancelTranscription();
    }
    
    // Use the canceled exit code for signal interruptions
    qInfo() << "Setting application exit code to:" << APP_EXIT_FAILURE_CANCELED << "(CANCELED)";
    exit(APP_EXIT_FAILURE_CANCELED);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Configure logging format for simplicity
    // (Alternatively, configure qInstallMessageHandler for advanced logging)
    qSetMessagePattern("[%{type}] %{message}");

    qInfo() << "[INFO] Application started";

    // Remove any leftover files from previous runs
    QFile leftoverAudioFile(OUTPUT_FILE_PATH);
    if (leftoverAudioFile.exists()) {
        leftoverAudioFile.remove();
        qInfo() << "[DEBUG] Removed leftover audio file:" << OUTPUT_FILE_PATH;
    }
    
    QFile leftoverTranscriptionFile(TRANSCRIPTION_OUTPUT_PATH);
    if (leftoverTranscriptionFile.exists()) {
        leftoverTranscriptionFile.remove();
        qInfo() << "[DEBUG] Removed leftover transcription file:" << TRANSCRIPTION_OUTPUT_PATH;
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
        
        // Set the application exit code based on MainWindow's exit code
        if (g_mainWindow) {
            qInfo() << "Setting application exit code to:" << g_mainWindow->exitCode();
            app.exit(g_mainWindow->exitCode());
        }
    });

    // Create and prepare recorder immediately
    recorder.startRecording();
    
    // Create main window (UI) and pass a pointer to the recorder
    MainWindow window(&recorder);
    g_mainWindow = &window;  // For signalHandler access
    window.show();

    // If timeout was specified, stop recording after the given period
    // But don't quit immediately - we need to allow transcription to complete
    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, [&](){
            qInfo() << "[INFO] Timeout reached:" << timeoutMs << "ms";
            recorder.stopRecording();
            // Don't quit - let the transcription process complete
            // QApplication::exit() will be called after transcription with the appropriate exit code
        });
    }

    // Install signal handlers for SIGINT, SIGTERM
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    return app.exec();
}
