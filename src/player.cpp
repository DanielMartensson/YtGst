#include "player.h"

#include <QRunnable>

#include <QGuiApplication>
#include <QStandardPaths>
#include <QTimer>

#include <gst/gst.h>

// Sätter User-Agent på playbins interna källa (playbin har ingen skrivbar
// "source"-property, så detta görs via "source-setup"-signalen).
static void onSourceSetup(GstElement *, GstElement *source, gpointer)
{
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "user-agent"))
        g_object_set(source, "user-agent",
                     "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0 Safari/537.36",
                     nullptr);
}

class SetPlaying : public QRunnable
{
public:
    explicit SetPlaying(GstElement *pipeline)
        : m_pipeline(pipeline ? (GstElement *)gst_object_ref(pipeline) : nullptr)
    {
    }

    ~SetPlaying() override
    {
        if (m_pipeline)
            gst_object_unref(m_pipeline);
    }

    void run() override
    {
        if (m_pipeline)
            gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    }

private:
    GstElement *m_pipeline = nullptr;
};

Player::Player(QObject *parent)
    : QObject(parent)
{
    m_urlFetch.setProcessChannelMode(QProcess::SeparateChannels);

    m_tick = new QTimer(this);
    m_tick->setInterval(250);
    connect(m_tick, &QTimer::timeout, this, &Player::updateProgress);

    connect(&m_urlFetch, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            m_loading = false;
            emit loadingChanged();
            const QByteArray err = m_urlFetch.readAllStandardError().trimmed();
            const QString detail = QString::fromUtf8(err).left(300);
            emit errorOccurred(detail.isEmpty()
                                   ? QStringLiteral("yt-dlp kunde inte hämta strömmen")
                                   : QStringLiteral("yt-dlp: %1").arg(detail));
            return;
        }
        fetchUrls();
    });
}

Player::~Player()
{
    disposePipeline();
}

void Player::setTitle(const QString &title)
{
    if (m_title == title)
        return;
    m_title = title;
    emit titleChanged();
}

void Player::setVideoItem(QQuickItem *item)
{
    m_item = item;
}

void Player::play(const QString &videoId)
{
    if (videoId.isEmpty())
        return;

    disposePipeline();

    const QString executable = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    if (executable.isEmpty()) {
        emit errorOccurred(QStringLiteral("yt-dlp hittades inte"));
        return;
    }

    const QString url = QStringLiteral("https://www.youtube.com/watch?v=%1").arg(videoId);
    m_loading = true;
    emit loadingChanged();

    QStringList arguments{
        QStringLiteral("-f"),
        QStringLiteral("bestvideo[height<=480][protocol^=m3u8]+bestaudio[protocol^=m3u8]/bestvideo[height<=480]+bestaudio/best"),
        QStringLiteral("-g"), QStringLiteral("--no-warnings"),
        url,
    };
    m_urlFetch.start(executable, arguments);
}

void Player::stop()
{
    disposePipeline();
}

void Player::seek(qint64 positionMs)
{
    if (!m_pipeline || m_duration <= 0)
        return;
    positionMs = qBound<qint64>(0, positionMs, m_duration);
    m_position = positionMs;
    emit positionChanged();

    const GstSeekFlags flags = static_cast<GstSeekFlags>(
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_ACCURATE);
    // playbin har inga sink-pads, så en seek på topp-pipelinen når inte fram.
    // Skicka seek till varje playbin (video- och ljudströmmen) i stället.
    const char *names[] = {"vplay", "aplay"};
    for (const char *name : names) {
        GstElement *play = gst_bin_get_by_name(GST_BIN(m_pipeline), name);
        if (!play)
            continue;
        gst_element_seek_simple(play, GST_FORMAT_TIME, flags, positionMs * GST_MSECOND);
        gst_object_unref(play);
    }
}

void Player::updateProgress()
{
    if (!m_pipeline)
        return;

    gint64 position = 0;
    if (gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &position) && position >= 0) {
        const qint64 ms = position / GST_MSECOND;
        if (ms != m_position) {
            m_position = ms;
            emit positionChanged();
        }
    }

    gint64 duration = 0;
    if (gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &duration) && duration > 0) {
        const qint64 ms = duration / GST_MSECOND;
        if (ms != m_duration) {
            m_duration = ms;
            emit durationChanged();
        }
    }
}

void Player::fetchUrls()
{
    const QByteArray output = m_urlFetch.readAllStandardOutput();
    QStringList urls;
    for (const QByteArray &line : output.split('\n')) {
        const QString candidate = QString::fromUtf8(line).trimmed();
        if (candidate.startsWith(QLatin1String("https://")))
            urls.append(candidate);
    }
    if (urls.size() >= 2) {
        m_videoUrl = urls.at(0);
        m_audioUrl = urls.at(1);
    } else if (urls.size() == 1) {
        // progressiv ström (video+ljud i samma fil) – ena playbin räcker
        m_videoUrl = urls.at(0);
        m_audioUrl.clear();
    }
    m_loading = false;
    emit loadingChanged();

    if (m_videoUrl.isEmpty()) {
        emit errorOccurred(QStringLiteral("Inga strömmar hittades för videon"));
        return;
    }
    buildPipeline();
}

void Player::buildPipeline()
{
    if (m_item == nullptr) {
        emit errorOccurred(QStringLiteral("Video-ytan är inte kopplad"));
        return;
    }
    if (m_videoUrl.isEmpty()) {
        emit errorOccurred(QStringLiteral("Inga strömmar hittades för videon"));
        return;
    }

    m_pipeline = gst_pipeline_new("pipe");

    // === VIDEO: playbin med qml6glsink bakom glupload/glcolorconvert ===
    // Obs: playbin saknar skrivbar "source" – sätt bara giltiga properties,
    // annars avbryter g_object_set och "video-sink" appliceras aldrig
    // (då tar playbins standardsink över och öppnar ett eget fönster).
    // qml6glsink tar endast GLMemory (RGBA/BGRA/RGB/YV12).
    GstElement *vplay = gst_element_factory_make("playbin", "vplay");
    GstElement *vbin = gst_parse_bin_from_description(
        "glupload ! glcolorconvert ! capsfilter caps=\"video/x-raw(memory:GLMemory),format=(string)RGBA\" ! qml6glsink name=gsink",
        TRUE, nullptr);
    g_object_set(vplay, "uri", m_videoUrl.toUtf8().constData(),
                 "video-sink", vbin, nullptr);
    g_signal_connect(vplay, "source-setup", G_CALLBACK(onSourceSetup), nullptr);
    gst_bin_add(GST_BIN(m_pipeline), vplay);

    if (!m_audioUrl.isEmpty()) {
        // === AUDIO: egen playbin i samma pipeline (delad klocka => synk) ===
        GstElement *aplay = gst_element_factory_make("playbin", "aplay");
        g_object_set(aplay, "uri", m_audioUrl.toUtf8().constData(),
                     "audio-sink", gst_parse_bin_from_description("autoaudiosink", TRUE, nullptr),
                     nullptr);
        g_signal_connect(aplay, "source-setup", G_CALLBACK(onSourceSetup), nullptr);
        gst_bin_add(GST_BIN(m_pipeline), aplay);
        gst_element_sync_state_with_parent(aplay);
    }

    GstElement *gsink = gst_bin_get_by_name(GST_BIN(vbin), "gsink");
    if (gsink) {
        g_object_set(gsink, "widget", m_item, nullptr);
        gst_object_unref(gsink);
    }

    gst_bus_add_signal_watch(gst_element_get_bus(m_pipeline));
    g_signal_connect(gst_element_get_bus(m_pipeline), "message",
                     G_CALLBACK(onBusMessage), this);

    // Starta via en scenegraph-uppdatering så att GL-kontexten som är aktiv när
    // pipelinen startar är PlayerWindow:s (annars ritar qml6glsink i den
    // kontext som senast renderade).
    QQuickWindow *win = m_item->window();
    gst_element_set_state(m_pipeline, GST_STATE_READY);

    if (win) {
        win->update();
        win->scheduleRenderJob(new SetPlaying(m_pipeline), QQuickWindow::BeforeSynchronizingStage);
    } else {
        gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    }

    m_duration = 0;
    m_position = 0;
    emit durationChanged();
    emit positionChanged();
    m_tick->start();
}

void Player::disposePipeline()
{
    if (m_tick)
        m_tick->stop();
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_bus_remove_signal_watch(gst_element_get_bus(m_pipeline));
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_playing) {
        m_playing = false;
        emit playingChanged();
    }
    if (m_duration != 0) {
        m_duration = 0;
        emit durationChanged();
    }
    if (m_position != 0) {
        m_position = 0;
        emit positionChanged();
    }
}

void Player::onBusMessage(GstBus *bus, GstMessage *msg, gpointer userData)
{
    Player *self = static_cast<Player *>(userData);
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *error = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(msg, &error, &debug);
            const QString message = QString::fromUtf8(error->message);
            g_error_free(error);
            g_free(debug);
            self->stop();
            self->m_playing = false;
            emit self->playingChanged();
            emit self->errorOccurred(message);
            break;
        }
        case GST_MESSAGE_EOS:
            self->stop();
            break;
        case GST_MESSAGE_STATE_CHANGED: {
            GstState oldState;
            GstState newState;
            GstState pending;
            gst_message_parse_state_changed(msg, &oldState, &newState, &pending);
            if (GST_ELEMENT(msg->src) == self->m_pipeline) {
                const bool nowPlaying = (newState == GST_STATE_PLAYING);
                if (self->m_playing != nowPlaying) {
                    self->m_playing = nowPlaying;
                    emit self->playingChanged();
                }
            }
            break;
        }
        default:
            break;
    }
    (void)bus;
}
