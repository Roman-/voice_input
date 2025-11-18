#include "timingtracker.h"
#include <QDebug>

TimingTracker::TimingTracker()
    : m_lastMarkMs(0)
{
}

void TimingTracker::start(const QString& label)
{
    m_label = label;
    m_stages.clear();
    m_timer.restart();
    m_lastMarkMs = 0;
}

void TimingTracker::markStage(const QString& stageName)
{
    if (!m_timer.isValid()) {
        m_timer.start();
    }

    qint64 now = m_timer.elapsed();
    qint64 duration = now - m_lastMarkMs;
    m_stages.append({stageName, duration});
    m_lastMarkMs = now;
}

void TimingTracker::setStageDuration(const QString& stageName, qint64 durationMs)
{
    m_stages.append({stageName, durationMs});
    m_lastMarkMs += durationMs;
}

QString TimingTracker::summary() const
{
    QString s = QString("Timing summary (%1):\n").arg(m_label);
    qint64 total = 0;
    for (const auto& stage : m_stages) {
        total += stage.durationMs;
        s += QString("  %1: %2 ms (%3 s)\n")
                .arg(stage.name)
                .arg(stage.durationMs)
                .arg(stage.durationMs / 1000.0, 0, 'f', 3);
    }
    s += QString("Total: %1 ms (%2 s)")
            .arg(total)
            .arg(total / 1000.0, 0, 'f', 3);
    return s;
}

qint64 TimingTracker::totalDurationMs() const
{
    qint64 total = 0;
    for (const auto& stage : m_stages) {
        total += stage.durationMs;
    }
    return total;
}

void TimingTracker::reset()
{
    m_label.clear();
    m_stages.clear();
    m_timer.invalidate();
    m_lastMarkMs = 0;
}
