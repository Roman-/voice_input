#ifndef FORMATUTILS_H
#define FORMATUTILS_H

#include <QString>

// Format file size in human-readable format (KB or MB)
// Returns string like "512 KB" or "1.24 MB"
QString formatFileSize(qint64 bytes);

#endif // FORMATUTILS_H

