#pragma once

#include <QObject>
#include <QProcess>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqml.h>

#include <gst/gst.h>

class QTimer;

class Player : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickItem *videoItem WRITE setVideoItem)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)

public:
    explicit Player(QObject *parent = nullptr);
    ~Player() override;

    void setVideoItem(QQuickItem *item);
    QQuickItem *videoItem() const { return m_item; }

    bool playing() const { return m_playing; }
    bool loading() const { return m_loading; }
    QString title() const { return m_title; }
    void setTitle(const QString &title);

    qint64 duration() const { return m_duration; }
    qint64 position() const { return m_position; }

    Q_INVOKABLE void play(const QString &videoId);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 positionMs);

signals:
    void playingChanged();
    void loadingChanged();
    void titleChanged();
    void durationChanged();
    void positionChanged();
    void errorOccurred(const QString &message);

private:
    void fetchUrls();
    void buildPipeline();
    void disposePipeline();
    void updateProgress();
    static void onBusMessage(GstBus *bus, GstMessage *msg, gpointer userData);

    GstElement *m_pipeline = nullptr;
    QQuickItem *m_item = nullptr;
    QProcess m_urlFetch;
    QTimer *m_tick = nullptr;
    bool m_playing = false;
    bool m_loading = false;
    QString m_title;
    QString m_videoUrl;
    QString m_audioUrl;
    qint64 m_duration = 0;
    qint64 m_position = 0;
};
