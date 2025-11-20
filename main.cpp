#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QTimer>
#include <QProcess>
#include <csignal>
#include <mutex>
#include <atomic>

#include "config/config.h"
#include "core/audiorecorder.h"
#include "core/statusutils.h"
#include "ui/mainwindow.h"

// Global pointers for signal handling
static AudioRecorder* g_audioRecorder = nullptr;
static MainWindow* g_mainWindow = nullptr;
static QApplication* g_app = nullptr;
static std::atomic<bool> g_cleanupInProgress{false};
static std::atomic<bool> g_exitRequested{false};

namespace {

void removeApplicationFiles()
{
    for (const auto& f : QStringList{OUTPUT_FILE_PATH,
                                     TRANSCRIPTION_OUTPUT_PATH,
                                     STATUS_FILE_PATH,
                                     LOCK_FILE_PATH}) {
        QFile file(f);
        if (file.exists() && file.remove()) {
            qDebug() << "Removed file:" << f;
        }
    }
}

void cleanupApplication(AudioRecorder* recorder, MainWindow* window)
{
    // Guard against multiple cleanup calls
    bool expected = false;
    if (!g_cleanupInProgress.compare_exchange_strong(expected, true)) {
        return; // Cleanup already in progress
    }
    
    if (recorder) {
        recorder->stopRecording();
    }
    if (window) {
        window->cancelTranscription();
    }
    removeApplicationFiles();
    notifyI3Blocks();
}

} // namespace

static void signalHandler(int sig)
{
    qInfo() << "Received signal:" << sig;

    // Handle SIGUSR1 (user signal 1) to start/stop recording
    if (sig == SIGUSR1 && g_audioRecorder) {
        qInfo() << "SIGUSR1 received - recording:" << g_audioRecorder->isRecording();
        
        // Toggle recording state (regardless of window visibility)
        if (g_audioRecorder->isRecording()) {
            // Stop recording
            qInfo() << "SIGUSR1 received - stopping recording";
            g_audioRecorder->stopRecording();
        } else {
            // Start recording
            qInfo() << "SIGUSR1 received - starting recording";
            
            // Clean up any previous files just before starting new recording
            for (const auto& f : QStringList{OUTPUT_FILE_PATH, TRANSCRIPTION_OUTPUT_PATH}) {
                QFile file(f);
                if (file.exists() && file.remove()) {
                    qDebug() << "Removed previous file:" << f;
                }
            }

            // Start a new recording immediately - audio system is already initialized
            g_audioRecorder->startRecording();
            // Set status to busy
            setFileStatus(STATUS_BUSY);
        }

        return;
    }
    
    // Handle termination signals
    if (sig == SIGINT || sig == SIGTERM) {
        // Guard against multiple exit requests
        bool expected = false;
        if (!g_exitRequested.compare_exchange_strong(expected, true)) {
            return; // Exit already requested
        }
        
        // Set exit code in MainWindow if available
        if (g_mainWindow) {
            g_mainWindow->setExitCode(APP_EXIT_FAILURE_CANCELED);
        }
        
        // Request quit - this is thread-safe and will be processed in main thread
        if (g_app) {
            // Use QTimer::singleShot to safely post exit to event loop
            const int exitCode = APP_EXIT_FAILURE_CANCELED;
            QTimer::singleShot(0, g_app, [exitCode]() {
                cleanupApplication(g_audioRecorder, g_mainWindow);
                if (g_app) {
                    g_app->exit(exitCode);
                }
            });
        } else {
            // Fallback if app not initialized yet
            cleanupApplication(g_audioRecorder, g_mainWindow);
            exit(APP_EXIT_FAILURE_CANCELED);
        }
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    g_app = &app; // Store app pointer for signal handler
    qSetMessagePattern("[%{time hh:mm:ss.zzz}] [%{type}] %{message}");

    qInfo() << "Application started";
    
    // Set initial status to "ready"
    if (!setFileStatus(STATUS_READY)) {
        qCritical() << "Failed to set initial status to" << STATUS_FILE_PATH;
        return APP_EXIT_FAILURE_GENERAL;
    }

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
                // Check if process is still running
                // On macOS, use kill -0 to check if process exists
                QProcess checkProcess;
                checkProcess.start("kill", {"-0", QString::number(pid)});
                checkProcess.waitForFinished();
                bool processRunning = (checkProcess.exitCode() == 0);
                
                if (processRunning) {
                    // Instance is already running - send SIGUSR1 to activate it
                    qInfo() << "Another instance is already running with PID:" << pid;
                    qInfo() << "Sending SIGUSR1 to activate existing instance...";
                    
                    QProcess signalProcess;
                    signalProcess.start("kill", {"-SIGUSR1", QString::number(pid)});
                    signalProcess.waitForFinished();
                    
                    if (signalProcess.exitCode() == 0) {
                        qInfo() << "Successfully sent activation signal to existing instance";
                        return 0; // Exit successfully
                    } else {
                        qWarning() << "[WARNING] Failed to send signal to PID" << pid 
                                   << "Error:" << signalProcess.errorString();
                        // Fall through to start new instance if signal failed
                    }
                } else {
                    qInfo() << "Found stale lock file. Previous instance (PID:" << pid << ") is no longer running.";
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
        qInfo() << "Created lock file with PID:" << QCoreApplication::applicationPid();
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
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&recorder](){
        cleanupApplication(&recorder, g_mainWindow);
    });
    
    // Clean up any leftover files
    for (const auto& f : QStringList{OUTPUT_FILE_PATH, TRANSCRIPTION_OUTPUT_PATH}) {
        QFile file(f);
        if (file.exists() && file.remove()) {
            qDebug() << "Removed leftover file:" << f;
        }
    }
    
    // Initialize the audio system once at startup
    qInfo() << "Initializing audio system...";
    if (!recorder.initializeAudioSystem()) {
        qCritical() << "[ERROR] Failed to initialize audio system";
        return APP_EXIT_FAILURE_GENERAL;
    }
    qInfo() << "Audio system initialized successfully";
    
    // Create main window (UI) and pass a pointer to the recorder
    MainWindow window(&recorder);
    g_mainWindow = &window;  // For signalHandler access

    // Show window if m_alwaysShowWindow is true (default)
    // User can hide it via tray menu "Always Show Window" checkbox
    window.show();
    
    // Pause audio stream until recording starts
    recorder.pauseAudioStream();
    qInfo() << "Application ready. Window visible by default."
            << "To trigger recording:\n```\nkill -SIGUSR1"
            << QCoreApplication::applicationPid() << "\n```";

    // Install signal handlers for SIGINT, SIGTERM, and SIGUSR1
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGUSR1, signalHandler);

    const int appResult = app.exec();
    const int finalExitCode = g_mainWindow ? g_mainWindow->exitCode() : appResult;
    
    // Only log exit code once at the end
    if (finalExitCode == APP_EXIT_FAILURE_CANCELED) {
        qInfo() << "Application exiting with code:" << finalExitCode << "(CANCELED)";
    } else if (finalExitCode == APP_EXIT_SUCCESS) {
        qInfo() << "Application exiting with code:" << finalExitCode << "(SUCCESS)";
    } else {
        qInfo() << "Application exiting with code:" << finalExitCode;
    }
    
    return finalExitCode;
}

