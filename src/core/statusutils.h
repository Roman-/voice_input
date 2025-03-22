#ifndef STATUSUTILS_H
#define STATUSUTILS_H

#include <QString>

// Status values
constexpr auto STATUS_READY = "ready";
constexpr auto STATUS_BUSY = "busy";
constexpr auto STATUS_ERROR = "error";

// Set the application's status in the status file
bool setFileStatus(const QString& status, const QString& errorMessage = QString());

// Clear the status file (usually on app exit)
void clearFileStatus();

void notifyI3Blocks();

void copyTranscriptionToClipboard(bool andPressCtrlV);

#endif // STATUSUTILS_H