#include "statusutils.h"
#include "config/config.h"

#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QProcess>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>

bool setFileStatus(const QString& status, const QString& errorMessage)
{
    QFile statusFile(STATUS_FILE_PATH);
    if (!statusFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to open status file for writing:" << STATUS_FILE_PATH;
        return false;
    }

    QTextStream out(&statusFile);
    out << status;
    
    // If this is an error status and there's an error message, add it on the next line
    if (status == STATUS_ERROR && !errorMessage.isEmpty()) {
        out << Qt::endl << errorMessage;
    }
    
    statusFile.close();
    qDebug() << "Status set to:" << status << (errorMessage.isEmpty() ? "" : (" - " + errorMessage));
    return true;
}

void notifyI3Blocks() {
    // Not applicable on macOS
}

void copyTranscriptionToClipboard(bool andPressCtrlV) {
    // Use pbcopy for clipboard
    QString command = QString("tr -d '\\n' < %1 | pbcopy").arg(TRANSCRIPTION_OUTPUT_PATH);
    int exitCode = QProcess::execute("/bin/sh", {"-c", command});
    
    if (exitCode != 0) {
        qWarning() << "Failed to copy transcription to clipboard. Exit code:" << exitCode;
        return;
    }
    
    qDebug() << "Transcription copied to clipboard";
    
    // Simulate ⌘V if requested
    if (andPressCtrlV) {
        // Check Accessibility permission
        bool trusted = AXIsProcessTrusted();
        if (!trusted) {
            qWarning() << "Accessibility permission not granted. Cannot simulate ⌘V.";
            qWarning() << "Please grant Accessibility permission in System Preferences > Security & Privacy > Privacy > Accessibility";
            return;
        }
        
        // Post ⌘V key events using Quartz
        CGEventRef cmdDown = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)0x37 /* cmd */, true);
        CGEventSetFlags(cmdDown, kCGEventFlagMaskCommand);
        
        CGEventRef vDown = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)9 /* v key */, true);
        CGEventSetFlags(vDown, kCGEventFlagMaskCommand);
        
        CGEventRef vUp = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)9, false);
        CGEventSetFlags(vUp, kCGEventFlagMaskCommand);
        
        CGEventRef cmdUp = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)0x37, false);
        CGEventSetFlags(cmdUp, kCGEventFlagMaskCommand);
        
        CGEventPost(kCGHIDEventTap, cmdDown);
        QThread::msleep(10); // Small delay between events
        CGEventPost(kCGHIDEventTap, vDown);
        QThread::msleep(10);
        CGEventPost(kCGHIDEventTap, vUp);
        QThread::msleep(10);
        CGEventPost(kCGHIDEventTap, cmdUp);
        
        CFRelease(cmdDown);
        CFRelease(vDown);
        CFRelease(vUp);
        CFRelease(cmdUp);
        
        qDebug() << "⌘V simulated";
    }
}

