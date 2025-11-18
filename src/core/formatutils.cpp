#include "formatutils.h"

QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024) {
        return QString::number(bytes) + " B";
    }
    
    double sizeKB = bytes / 1024.0;
    
    if (sizeKB < 1024.0) {
        return QString::number(sizeKB, 'f', 2) + " KB";
    }
    
    double sizeMB = sizeKB / 1024.0;
    return QString::number(sizeMB, 'f', 2) + " MB";
}

