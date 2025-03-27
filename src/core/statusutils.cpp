#include "statusutils.h"
#include "config/config.h"

#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QProcess>

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
    notifyI3Blocks();
    return true;
}

void clearFileStatus()
{
    QFile statusFile(STATUS_FILE_PATH);
    if (statusFile.exists()) {
        if (statusFile.remove()) {
            qDebug() << "Status file removed:" << STATUS_FILE_PATH;
        } else {
            qWarning() << "Failed to remove status file:" << STATUS_FILE_PATH;
        }
    }
}

void notifyI3Blocks() {
    QProcess::execute("pkill", {"-RTMIN+2", "i3blocks"}); // pkill -RTMIN+2 i3blocks
}

void copyTranscriptionToClipboard(bool andPressCtrlV) {
    QString command = QString("tr -d '\\n' < %1 | xclip -i -sel c").arg(TRANSCRIPTION_OUTPUT_PATH);
    if (andPressCtrlV) {
        command += " && xdotool key ctrl+v";
    }

    int exitCode = QProcess::execute("/bin/sh", {"-c", command});
    if (exitCode != 0) {
        qWarning() << "Failed to copy transcription to clipboard. Exit code:" << exitCode;
    } else {
        qDebug() << "Transcription copied to clipboard"
                 << (andPressCtrlV ? "and Ctrl+V simulated." : ".");
    }
}

QColor getLanguageColor(const QString& languageCode) {
    if (languageCode.toLower() == "en") {
        return QColor("#5CAAFF"); // Blue
    } else {
        return QColor("#FF6B6B"); // default to Red
    }
}
