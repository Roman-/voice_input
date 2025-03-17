#include "whisperapi.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QHttpPart>
#include <QMimeDatabase>
#include <QDebug>

WhisperApi::WhisperApi(QObject *parent)
    : QObject(parent)
{
    connect(&m_manager, &QNetworkAccessManager::finished,
            this, &WhisperApi::onFinished);
}

void WhisperApi::transcribe(const QString &filePath)
{
    QFileInfo fi(filePath);
    if (!fi.exists() || fi.size() == 0) {
        emit transcriptionError("File does not exist or is empty: " + filePath);
        return;
    }

    QFile *file = new QFile(filePath, this);
    if (!file->open(QIODevice::ReadOnly)) {
        emit transcriptionError("Failed to open file for reading: " + filePath);
        file->deleteLater();
        return;
    }

    // Prepare the multipart form data
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    multiPart->setParent(this); // ensure deletion

    // File part
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QString("form-data; name=\"file\"; filename=\"stt-recording.m4a\"")));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("audio/m4a"));
    filePart.setBodyDevice(file);

    // Once the multiPart is sent, it takes ownership of 'file'
    file->setParent(multiPart);
    multiPart->append(filePart);

    // Model part
    {
        QHttpPart modelPart;
        modelPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                            QVariant("form-data; name=\"model\""));
        modelPart.setBody(m_model.toUtf8());
        multiPart->append(modelPart);
    }

    // Temperature
    {
        QHttpPart temperaturePart;
        temperaturePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                            QVariant("form-data; name=\"temperature\""));
        temperaturePart.setBody(QByteArray::number(m_temperature, 'f', 2));
        multiPart->append(temperaturePart);
    }

    // Language
    {
        QHttpPart languagePart;
        languagePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                            QVariant("form-data; name=\"language\""));
        languagePart.setBody(m_language.toUtf8());
        multiPart->append(languagePart);
    }

    // The official endpoint for whisper transcription
    //   https://api.openai.com/v1/audio/transcriptions
    QUrl url("https://api.openai.com/v1/audio/transcriptions");
    QNetworkRequest request(url);

    // Set headers
    QString headerValue = QString("Bearer %1").arg(m_apiKey);
    request.setRawHeader("Authorization", headerValue.toUtf8());
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = m_manager.post(request, multiPart);
    // multiPart will be deleted automatically when reply is finished
}

void WhisperApi::onFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        // Some error occurred
        QString errMsg = reply->errorString();
        // Attempt to parse any JSON error body
        QByteArray data = reply->readAll();
        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
        if (!doc.isNull()) {
            QJsonObject obj = doc.object();
            if (obj.contains("error")) {
                QJsonObject errObj = obj.value("error").toObject();
                if (errObj.contains("message")) {
                    errMsg = errObj.value("message").toString();
                }
            }
        }
        emit transcriptionError(errMsg);
        return;
    }

    // On success, we should get back plain text (by default) or JSON depending on the endpoint parameters.
    QByteArray data = reply->readAll();

    // If the response is text, we can just emit that text.
    // If you set "response_format=json", you'd parse JSON.
    // This example assumes "response_format=text" or the default text response.
    QString transcriptText = QString::fromUtf8(data).trimmed();
    emit transcriptionSuccess(transcriptText);
}
