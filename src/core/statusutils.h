#ifndef STATUSUTILS_H
#define STATUSUTILS_H

#include <QString>
#include <QColor>

// Status values
constexpr auto STATUS_READY = "ready";
constexpr auto STATUS_BUSY = "busy";
constexpr auto STATUS_ERROR = "error";

// Set the application's status in the status file
bool setFileStatus(const QString& status, const QString& errorMessage = QString());

void notifyI3Blocks();

QString getCurrentKeyboardLayout();
QString getLanguageBasedOnKeyboardLayout();

void copyTranscriptionToClipboard(bool andPressCtrlV);

// Get color for language indicator based on language code
QColor getLanguageColor(const QString& languageCode);

#endif // STATUSUTILS_H