#ifndef WHISPERAPI_H
#define WHISPERAPI_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHttpMultiPart>

/**
 * A small helper class to handle uploading the .m4a file
 * to OpenAI Whisper endpoint, parse the response, and emit signals.
 */
class WhisperApi : public QObject
{
    Q_OBJECT
public:
    explicit WhisperApi(QObject *parent = nullptr);

    void setApiKey(const QString &apiKey) { m_apiKey = apiKey; }
    void setModel(const QString &model) { m_model = model; }
    void setTemperature(double temperature) { m_temperature = temperature; }
    void setLanguage(const QString &lang) { m_language = lang; }

public slots:
    void transcribe(const QString &filePath);

signals:
    void transcriptionSuccess(const QString &text);
    void transcriptionError(const QString &errorMsg);

private slots:
    void onFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager m_manager;
    QString m_apiKey;
    QString m_model;
    double  m_temperature;
    QString m_language;
};

#endif // WHISPERAPI_H
