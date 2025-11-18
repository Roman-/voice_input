#ifndef TIMINGTRACKER_H
#define TIMINGTRACKER_H

#include <QString>
#include <QElapsedTimer>
#include <QVector>

struct TimingStage {
    QString name;
    qint64 durationMs;
};

class TimingTracker
{
public:
    TimingTracker();

    void start(const QString& label);
    void markStage(const QString& stageName);
    void setStageDuration(const QString& stageName, qint64 durationMs);
    QString summary() const;
    qint64 totalDurationMs() const;
    void reset();

private:
    QString m_label;
    QElapsedTimer m_timer;
    QVector<TimingStage> m_stages;
    qint64 m_lastMarkMs;
};

#endif // TIMINGTRACKER_H
