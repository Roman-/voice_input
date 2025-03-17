#include <QApplication>
#include <QFile>
#include <QDebug>
#include <QCommandLineParser>
#include "mainwindow.h"
#include "config.h"

static void cleanupOldFiles()
{
    // Remove leftover files from previous sessions (old paths for backwards compatibility)
    QFile::remove("/tmp/voice_input.m4a");
    QFile::remove("/tmp/voice_input.txt");
    
    // Remove current path files too
    QFile::remove(Config::RECORDING_PATH);
    QFile::remove(Config::TRANSCRIPTION_PATH);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("voice_input");
    app.setApplicationVersion("1.0");

    // Set up command line parsing
    QCommandLineParser parser;
    parser.setApplicationDescription("Voice input application");
    parser.addHelpOption();
    parser.addVersionOption();

    // Add --send option
    QCommandLineOption sendOption(QStringList() << "s" << "send", "Send recording to OpenAI for transcription");
    parser.addOption(sendOption);
    
    // Add --force-stop-after option (in seconds)
    QCommandLineOption forceStopOption(
        QStringList() << "f" << "force-stop-after", 
        "Automatically stop recording after specified time (in seconds)",
        "seconds", "0");
    parser.addOption(forceStopOption);

    // Process the command line arguments
    parser.process(app);
    bool sendToOpenAI = parser.isSet(sendOption);
    
    // Get the force stop time in milliseconds (converting from seconds)
    bool ok;
    qint64 forceStopAfterSeconds = parser.value(forceStopOption).toLongLong(&ok);
    qint64 forceStopAfterMs = ok ? forceStopAfterSeconds * 1000 : 0;

    // Clean up old temporary files from previous runs
    cleanupOldFiles();

    MainWindow w(sendToOpenAI, forceStopAfterMs);
    w.show();

    return app.exec();
}
