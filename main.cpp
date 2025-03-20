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
    
    // Remove lock file
    QFile lockFile(LOCK_FILE_PATH);
    if (lockFile.exists()) {
        if (lockFile.remove()) {
            qInfo() << "[INFO] Removed lock file:" << LOCK_FILE_PATH;
        } else {
            qWarning() << "[WARNING] Failed to remove lock file:" << LOCK_FILE_PATH;
        }
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

    // Check if an instance is already running by examining the lock file
    QFile lockFile(LOCK_FILE_PATH);
    if (lockFile.exists()) {
        // Lock file exists, check if process is still running
        if (lockFile.open(QIODevice::ReadOnly)) {
            QString pidStr = QString::fromUtf8(lockFile.readAll()).trimmed();
            lockFile.close();
            
            bool conversionOk = false;
            qint64 pid = pidStr.toLongLong(&conversionOk);
            
            if (conversionOk && pid > 0) {
                // On Linux, check if process is running by checking /proc/{pid} directory
                QFileInfo procDir(QString("/proc/%1").arg(pid));
                if (procDir.exists() && procDir.isDir()) {
                    qCritical() << "[ERROR] Another instance is already running with PID:" << pid;
                    qInfo() << "Setting application exit code to:" << APP_EXIT_FAILURE_GENERAL;
                    return APP_EXIT_FAILURE_GENERAL;
                } else {
                    qInfo() << "[INFO] Found stale lock file. Previous instance (PID:" << pid << ") is no longer running.";
                    lockFile.remove();
                }
            } else {
                qInfo() << "[WARNING] Invalid PID in lock file. Removing.";
                lockFile.remove();
            }
        } else {
            qWarning() << "[WARNING] Cannot read lock file. It may be locked by another process.";
            qInfo() << "Setting application exit code to:" << APP_EXIT_FAILURE_GENERAL;
            return APP_EXIT_FAILURE_GENERAL;
        }
    }
    
    // Create a new lock file with current PID
    if (lockFile.open(QIODevice::WriteOnly)) {
        QTextStream stream(&lockFile);
        stream << QCoreApplication::applicationPid();
        lockFile.close();
        qInfo() << "[INFO] Created lock file with PID:" << QCoreApplication::applicationPid();
    } else {
        qCritical() << "[ERROR] Failed to create lock file:" << LOCK_FILE_PATH;
        qInfo() << "Setting application exit code to:" << APP_EXIT_FAILURE_FILE_ERROR;
        return APP_EXIT_FAILURE_FILE_ERROR;
    }

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
        
        // Remove lock file
        QFile lockFile(LOCK_FILE_PATH);
        if (lockFile.exists()) {
            if (lockFile.remove()) {
                qInfo() << "[INFO] Removed lock file:" << LOCK_FILE_PATH;
            } else {
                qWarning() << "[WARNING] Failed to remove lock file:" << LOCK_FILE_PATH;
            }
        }
        
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
