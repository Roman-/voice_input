#include "statusutils.h"
#include "config/config.h"

#include <QFile>
#include <QTextStream>
#include <QDebug>

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