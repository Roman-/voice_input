#include <QApplication>
#include <QFile>
#include <QDebug>
#include <QCommandLineParser>
#include "mainwindow.h"

static void cleanupOldFiles()
{
    // Remove leftover /tmp/voice_input.m4a and /tmp/voice_input.txt if they exist
    QFile::remove("/tmp/voice_input.m4a");
    QFile::remove("/tmp/voice_input.txt");
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

    // Process the command line arguments
    parser.process(app);
    bool sendToOpenAI = parser.isSet(sendOption);

    // Clean up old temporary files from previous runs
    cleanupOldFiles();

    MainWindow w(sendToOpenAI);
    w.show();

    return app.exec();
}
