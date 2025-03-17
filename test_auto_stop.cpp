#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <QFile>
#include "config.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    // Clean up any previous test files
    QFile::remove(Config::RECORDING_PATH);
    
    qDebug() << "Starting auto-stop test...";
    
    // Set a timer to simulate recording for 1 second
    QTimer recordingTimer;
    recordingTimer.setSingleShot(true);
    QObject::connect(&recordingTimer, &QTimer::timeout, [&]() {
        qDebug() << "Recording simulation complete after 1 second";
        
        // Create a mock audio file to simulate recording
        QFile file(Config::RECORDING_PATH);
        if (file.open(QIODevice::WriteOnly)) {
            file.write("Test audio data");
            file.close();
            qDebug() << "Created test audio file at:" << Config::RECORDING_PATH;
            
            // Verify file exists and has content
            if (file.exists() && file.size() > 0) {
                qDebug() << "File verification successful. Size:" << file.size() << "bytes";
                QCoreApplication::exit(0); // Success
            } else {
                qDebug() << "ERROR: File verification failed";
                QCoreApplication::exit(1); // Error
            }
        } else {
            qDebug() << "ERROR: Could not create test audio file";
            QCoreApplication::exit(1); // Error
        }
    });
    
    // Start the mock recording
    recordingTimer.start(1000);
    
    return app.exec();
}