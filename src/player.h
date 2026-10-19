#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QVariantList>
#include <QtQml/qqml.h>

#include <atomic>

#include <gst/gst.h>

class QTimer;
class QNetworkAccessManager;
class QNetworkReply;

struct SubtitleCue
{
    qint64 start = 0;
    qint64 end = 0;
    QString text;
};

class Player : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(double rate READ rate NOTIFY rateChanged)
    Q_PROPERTY(QVariantList resolutions READ resolutions NOTIFY resolutionsChanged)
    Q_PROPERTY(int resolution READ resolution NOTIFY resolutionChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY subtitleTracksChanged)
    Q_PROPERTY(QString subtitleLanguage READ subtitleLanguage NOTIFY subtitleLanguageChanged)
    Q_PROPERTY(QString subtitleText READ subtitleText NOTIFY subtitleTextChanged)
    Q_PROPERTY(double volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY fullscreenChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadingChanged)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)
    Q_PROPERTY(QString downloadStatus READ downloadStatus NOTIFY downloadStatusChanged)

public:
    explicit Player(QObject *parent = nullptr);
    ~Player() override;

    bool playing() const { return m_playing; }
    bool loading() const { return m_loading; }
    QString title() const { return m_title; }
    void setTitle(const QString &title);

    qint64 duration() const { return m_duration; }
    qint64 position() const { return m_position; }
    double rate() const { return m_rate; }
    QVariantList resolutions() const { return m_resolutions; }
    int resolution() const { return m_resolution; }
    QVariantList subtitleTracks() const { return m_subtitleTracks; }
    QString subtitleLanguage() const { return m_subtitleLanguage; }
    QString subtitleText() const { return m_subtitleText; }
    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    bool fullscreen() const { return m_fullscreen; }
    bool downloading() const { return m_downloading; }
    double downloadProgress() const { return m_downloadProgress; }
    QString downloadStatus() const { return m_downloadStatus; }

    Q_INVOKABLE void play(const QString &videoId);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void togglePlayPause();
    Q_INVOKABLE void setRate(double rate);
    Q_INVOKABLE void setResolution(int height);
    Q_INVOKABLE void setSubtitleLanguage(const QString &language);
    Q_INVOKABLE void setVolume(double volume);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void toggleFullscreen();
    Q_INVOKABLE void download();

signals:
    void playingChanged();
    void loadingChanged();
    void titleChanged();
    void durationChanged();
    void positionChanged();
    void rateChanged();
    void resolutionsChanged();
    void resolutionChanged();
    void subtitleTracksChanged();
    void subtitleLanguageChanged();
    void subtitleTextChanged();
    void volumeChanged();
    void mutedChanged();
    void fullscreenChanged();
    void downloadingChanged();
    void downloadProgressChanged();
    void downloadStatusChanged();
    void errorOccurred(const QString &message);

private:
    void requestUrls();
    void handleFetchOutput();
    void handleSubtitleData(const QByteArray &data, bool autoCaptions);
    void parseVttSubtitles(const QString &vtt);
    void parseJsonSubtitles(const QByteArray &data, bool autoCaptions);
    void updateSubtitleCue();
    void setSubtitleText(const QString &text);
    void fetchSubtitle();
    void readCookiesFromFile();
    void setDownloadStatus(const QString &status);
    void readDownloadOutput();
    void buildPipeline();
    void disposePipeline();
    void attachSinkProbe(GstElement *videoBin);
    void applySeek(qint64 positionMs, double rate);
    void applyVolume();
    void updateProgress();
    static void onBusMessage(GstBus *bus, GstMessage *msg, gpointer userData);

    GstElement *m_pipeline = nullptr;
    // The waylandsink of the built pipeline (owned by m_pipeline). Kept so the
    // fullscreen state can be driven at runtime through its "fullscreen"
    // property; cleared when the pipeline is disposed.
    GstElement *m_videoSink = nullptr;
    // The video the current/last pipeline was built for, so the play button can
    // start playback again after it has been stopped (EOS, error, or the video
    // window being closed).
    QString m_videoId;
    // The audio playbin inside m_pipeline (owned by it). Kept so a failure that
    // comes from the audio side can be told apart from one that should stop
    // playback.
    GstElement *m_audioPlay = nullptr;
    // Frame counter for YTGST_DEBUG_FPS=1: how many buffers the video sink
    // actually consumed, so a sink that cannot keep up can be told apart from
    // a source that cannot deliver.
    std::atomic<int> m_sinkBuffers{0};
    QElapsedTimer m_sinkBufferClock;
    int m_sinkBuffersLast = 0;
    QProcess m_urlFetch;
    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_subtitleReply = nullptr;
    QTimer *m_tick = nullptr;
    bool m_playing = false;
    bool m_loading = false;
    QString m_title;
    QString m_watchUrl;
    QString m_videoUrl;
    QString m_videoProtocol;
    QString m_audioUrl;
    qint64 m_duration = 0;
    qint64 m_position = 0;
    double m_rate = 1.0;
    QVariantList m_resolutions;
    int m_resolution = 0;
    int m_requestedHeight = 480;
    qint64 m_pendingSeek = -1;
    QVariantList m_subtitleTracks;
    QString m_subtitleLanguage;
    QString m_subtitleText;
    QString m_subtitleUrl;
    bool m_subtitleAuto = false;
    int m_subtitleRetries = 0;
    QHash<QString, QList<SubtitleCue>> m_cueCache;
    QList<SubtitleCue> m_cues;
    QByteArray m_cookieHeader;
    int m_activeCue = -1;
    double m_volume = 1.0;
    bool m_muted = false;
    bool m_fullscreen = false;
    QProcess m_download;
    bool m_downloading = false;
    bool m_downloadCancelled = false;
    double m_downloadProgress = 0.0;
    QString m_downloadStatus;
};
