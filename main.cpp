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
    qInfo() << "[INFO] Received signal:" << sig;
    
    // Handle SIGUSR1 (user signal 1) to show window and start recording
    if (sig == SIGUSR1) {
        if (g_mainWindow && !g_mainWindow->isVisible()) {
            // Ensure any existing recording is stopped
            if (g_audioRecorder && g_audioRecorder->isRecording()) {
                g_audioRecorder->stopRecording();
            }
            
            // Clean up any previous files
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
            
            // Show window
            g_mainWindow->show();
            
            // Start a new recording immediately - audio system is already initialized
            if (g_audioRecorder) {
                g_audioRecorder->startRecording();
            }
            
            return;
        }
    }
    
    // Handle termination signals
    if (sig == SIGINT || sig == SIGTERM) {
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

    // Parse command line arguments
    QCommandLineParser parser;
    parser.setApplicationDescription("Audio Recorder Application");
    parser.addHelpOption();

    QCommandLineOption timeoutOption(QStringList() << "t" << "timeout",
                                     "Stop recording after <milliseconds> timeout.",
                                     "milliseconds");
    parser.addOption(timeoutOption);
    
    QCommandLineOption startVisibleOption(QStringList() << "s" << "start-visible",
                                         "Start with the window visible and begin recording immediately.");
    parser.addOption(startVisibleOption);

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
    
    // Clean up any leftover files
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
    
    // Initialize the audio system once at startup
    qInfo() << "[INFO] Initializing audio system...";
    if (!recorder.initializeAudioSystem()) {
        qCritical() << "[ERROR] Failed to initialize audio system";
        return APP_EXIT_FAILURE_GENERAL;
    }
    qInfo() << "[INFO] Audio system initialized successfully";
    
    // Create main window (UI) and pass a pointer to the recorder
    MainWindow window(&recorder);
    g_mainWindow = &window;  // For signalHandler access
    
    // Determine if we should start visible or hidden
    bool startVisible = parser.isSet(startVisibleOption);
    
    if (startVisible) {
        // Start with window visible and begin recording
        window.show();
        recorder.startRecording();
        
        // If timeout was specified, stop recording after the given period
        if (timeoutMs > 0) {
            QTimer::singleShot(timeoutMs, [&](){
                qInfo() << "[INFO] Timeout reached:" << timeoutMs << "ms";
                recorder.stopRecording();
                // Don't quit - let the transcription process complete
            });
        }
    } else {
        // Start with window hidden - make sure audio stream is paused
        recorder.pauseAudioStream();
        qInfo() << "[INFO] Starting in background mode with microphone paused."
                << "To show window and begin recording:\n```\nkill -SIGUSR1"
                << QCoreApplication::applicationPid() << "\n```";
    }

    // Install signal handlers for SIGINT, SIGTERM, and SIGUSR1
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGUSR1, signalHandler);

    return app.exec();
}
