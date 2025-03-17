#include <QApplication>
#include <QFile>
#include <QDebug>
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

    // Clean up old temporary files from previous runs
    cleanupOldFiles();

    MainWindow w;
    w.show();

    return app.exec();
}
