#include "player.h"
#include "ytdlp.h"

#include <QRunnable>

#include <QFile>
#include <QGuiApplication>
#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>

#include <algorithm>
#include <cstdio>

#include <sys/stat.h>
#include <unistd.h>

#include <gst/gst.h>

// Short code -> display name for the most common subtitle languages.
static QString languageName(const QString &code)
{
    static const QHash<QString, QString> names{
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("sv"), QStringLiteral("Svenska")},
        {QStringLiteral("de"), QStringLiteral("Deutsch")},
        {QStringLiteral("fr"), QStringLiteral("Français")},
        {QStringLiteral("es"), QStringLiteral("Español")},
        {QStringLiteral("it"), QStringLiteral("Italiano")},
        {QStringLiteral("pt"), QStringLiteral("Português")},
        {QStringLiteral("nl"), QStringLiteral("Nederlands")},
        {QStringLiteral("pl"), QStringLiteral("Polski")},
        {QStringLiteral("ru"), QStringLiteral("Русский")},
        {QStringLiteral("ja"), QStringLiteral("日本語")},
        {QStringLiteral("ko"), QStringLiteral("한국어")},
        {QStringLiteral("zh"), QStringLiteral("中文")},
        {QStringLiteral("ar"), QStringLiteral("العربية")},
        {QStringLiteral("tr"), QStringLiteral("Türkçe")},
        {QStringLiteral("fi"), QStringLiteral("Suomi")},
        {QStringLiteral("no"), QStringLiteral("Norsk")},
        {QStringLiteral("da"), QStringLiteral("Dansk")},
    };
    // "en-US" / "en-orig" -> "en"
    const QString base = code.section(QLatin1Char('-'), 0, 0).toLower();
    const auto it = names.constFind(base);
    if (it != names.constEnd())
        return it.value();
    return code;
}

static QString stripVttTags(QString text)
{
    static const QRegularExpression tag(QStringLiteral("<[^>]*>"));
    text.remove(tag);
    text.replace(QLatin1String("&amp;"), QLatin1String("&"));
    text.replace(QLatin1String("&lt;"), QLatin1String("<"));
    text.replace(QLatin1String("&gt;"), QLatin1String(">"));
    text.replace(QLatin1String("&quot;"), QLatin1String("\""));
    text.replace(QLatin1String("&#39;"), QLatin1String("'"));
    text.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    return text.trimmed();
}

static bool parseVttTimestamp(const QString &value, qint64 *ms)
{
    // Supports "HH:MM:SS.mmm" and "MM:SS.mmm".
    const QStringList parts = value.trimmed().split(QLatin1Char(':'));
    if (parts.size() < 2 || parts.size() > 3)
        return false;
    bool ok = false;
    double seconds = parts.last().toDouble(&ok);
    if (!ok)
        return false;
    const int minutes = parts.at(parts.size() - 2).toInt(&ok);
    if (!ok)
        return false;
    const int hours = parts.size() == 3 ? parts.at(0).toInt(&ok) : 0;
    if (!ok)
        return false;
    *ms = static_cast<qint64>(((hours * 60 + minutes) * 60 + seconds) * 1000.0);
    return true;
}

// The STM32MP25 VPU only has stateless H.264 and VP8 decoders (v4l2slh264dec,
// v4l2slvp8dec), while YouTube serves VP9/AV1 by default - and libav's software
// VP9 decoder is far too slow for the two Cortex-A35 cores. On that target the
// video therefore has to be pinned to H.264, or yt-dlp hands over a stream that
// is either unplayable or runs at a few frames per second. Everywhere else (no
// v4l2slh264dec in the registry at all) keep yt-dlp's own choice, so VP9/AV1
// still play - decoded in software or on the GPU.
static bool preferH264()
{
    static const bool onlyH264 = []() -> bool {
        gst_init(nullptr, nullptr); // idempotent; the registry needs it
        if (!gst_element_factory_find("v4l2slh264dec"))
            return false;
        return !gst_element_factory_find("v4l2slvp9dec")
            && !gst_element_factory_find("v4l2slav1dec");
    }();
    return onlyH264;
}

// Builds the yt-dlp format string. height <= 0 means the best available stream.
//
// HLS/m3u8 comes first and DASH/mp4 (protocol "https") second. Measured on the
// target: qtdemux refuses to seek in YouTube's fragmented DASH mp4 (the seek
// returns false and the position just keeps running), while the HLS video
// stream seeks exactly - and "gst-discoverer" reports "Seekable: yes" for both,
// so only an actual seek tells them apart. yt-dlp names the protocols
// "m3u8_native" and "https" (older releases: "m3u8"/"mp4"), so match the
// prefixes - matching the wrong name means the first alternative silently never
// applies. Where the hardware only has H.264/VP8 decoders the codec is pinned as
// well, see preferH264() above.
static QString formatForHeight(int height)
{
    const QString codec = preferH264() ? QStringLiteral("[vcodec^=avc1]") : QString();
    const QString hls = QStringLiteral("[protocol^=m3u8]");
    const QString dash = QStringLiteral("[protocol^=https]");
    if (height > 0) {
        return QStringLiteral("bestvideo[height<=%1]%2%3+bestaudio%3"
                              "/bestvideo[height<=%1]%2%4+bestaudio%4"
                              "/bestvideo[height<=%1]%2+bestaudio"
                              "/best[height<=%1]%2")
            .arg(height)
            .arg(codec)
            .arg(hls)
            .arg(dash);
    }
    return QStringLiteral("bestvideo%1%2+bestaudio%2"
                          "/bestvideo%1%3+bestaudio%3"
                          "/bestvideo%1+bestaudio/best%1")
        .arg(codec)
        .arg(hls)
        .arg(dash);
}

// Picks an installed browser to fetch YouTube cookies from.
static QString browserForCookies()
{
    const QString home = QDir::homePath();
    static const struct { const char *name; const char *dir; } browsers[] = {
        {"firefox", "/.mozilla/firefox"},
        {"chromium", "/.config/chromium"},
        {"chrome", "/.config/google-chrome"},
        {"brave", "/.config/BraveSoftware/Brave-Browser"},
        {"edge", "/.config/microsoft-edge"},
        {"vivaldi", "/.config/vivaldi"},
        {"opera", "/.config/opera"},
    };
    for (const auto &browser : browsers) {
        if (QDir(home + QLatin1String(browser.dir)).exists())
            return QString::fromLatin1(browser.name);
    }
    return QString();
}

// Temporary cookie jar that yt-dlp writes to; read immediately afterwards and removed.
static QString cookieFilePath()
{
    return QDir::tempPath() + QStringLiteral("/ytgst-cookies.txt");
}

// Sets the User-Agent on playbin's internal source (playbin has no writable
// "source" property, so this is done through the "source-setup" signal).
static void onSourceSetup(GstElement *, GstElement *source, gpointer)
{
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "user-agent"))
        g_object_set(source, "user-agent",
                     "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0 Safari/537.36",
                     nullptr);
}

// Links a pad that a demuxer only exposes once the stream arrives to the next
// element in the chain.
//
// gst_parse_bin_from_description() completes this "delayed link" by itself when
// it parses a whole pipeline, but not when the chain ends up in a bin:
// hlsdemux, tsdemux and qtdemux create their pads at runtime, the parser gives
// up with "Delayed linking failed" and no frame ever reaches the sink. The
// video chain is therefore built from single elements in buildPipeline() and
// its dynamic pads are linked here.
static void linkDemuxPadTo(GstPad *pad, GstElement *element)
{
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
        return;
    GstPad *sink = gst_element_get_static_pad(element, "sink");
    if (!sink)
        return;
    if (!gst_pad_is_linked(sink))
        gst_pad_link(pad, sink);
    gst_object_unref(sink);
}

// hlsdemux announces its stream pad after the playlist has been read.
static void onHlsdemuxPadAdded(GstElement *, GstPad *pad, gpointer userData)
{
    linkDemuxPadTo(pad, GST_ELEMENT(userData));
}

// tsdemux (MPEG-TS) and qtdemux (mp4) expose one pad per elementary stream.
// Only the H.264 video stream continues to h264parse; anything else (an audio
// track of the same container, for example) is left alone.
static void onDemuxPadAdded(GstElement *, GstPad *pad, gpointer userData)
{
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
        return;
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps)
        caps = gst_pad_query_caps(pad, nullptr);
    bool video = false;
    if (caps && gst_caps_get_size(caps) > 0) {
        const gchar *name = gst_structure_get_name(gst_caps_get_structure(caps, 0));
        video = g_str_has_prefix(name, "video/");
    }
    if (caps)
        gst_caps_unref(caps);
    if (video)
        linkDemuxPadTo(pad, GST_ELEMENT(userData));
}

// gst_bin_get_by_name() only looks at direct children; the video sink chain is
// a bin of its own, so its elements have to be searched recursively.
static GstElement *findElementByName(GstElement *root, const char *name)
{
    GstIterator *iterator = gst_bin_iterate_recurse(GST_BIN(root));
    GValue item = G_VALUE_INIT;
    GstElement *found = nullptr;
    while (!found && gst_iterator_next(iterator, &item) == GST_ITERATOR_OK) {
        GstElement *element = GST_ELEMENT(g_value_get_object(&item));
        if (g_strcmp0(GST_ELEMENT_NAME(element), name) == 0)
            found = GST_ELEMENT(gst_object_ref(element));
        g_value_reset(&item);
    }
    g_value_unset(&item);
    gst_iterator_free(iterator);
    return found;
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

    // YTGST_HEIGHT=<n> starts playback at that height instead of the 480p
    // default. The resolution menu in the window overrides it; this is meant
    // for measuring and for scripted playback, where no menu can be clicked.
    const QByteArray startHeight = qgetenv("YTGST_HEIGHT");
    if (!startHeight.isEmpty()) {
        bool ok = false;
        const int height = startHeight.toInt(&ok);
        if (ok && height >= 0)
            m_requestedHeight = height;
    }

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
                                   ? QStringLiteral("yt-dlp could not fetch the stream")
                                   : QStringLiteral("yt-dlp: %1").arg(detail));
            return;
        }
        handleFetchOutput();
    });

    m_net = new QNetworkAccessManager(this);

    m_download.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_download, &QProcess::readyReadStandardOutput, this, &Player::readDownloadOutput);
    connect(&m_download, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (!m_downloading)
            return;
        m_downloading = false;
        emit downloadingChanged();

        if (m_downloadCancelled) {
            m_downloadCancelled = false;
            m_downloadProgress = 0.0;
            emit downloadProgressChanged();
            setDownloadStatus(QString());
            return;
        }

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            m_downloadProgress = 1.0;
            emit downloadProgressChanged();
            setDownloadStatus(tr("Download complete"));
        } else {
            const QByteArray err = m_download.readAllStandardError().trimmed();
            setDownloadStatus(err.isEmpty()
                                  ? tr("Download failed")
                                  : tr("Error: %1").arg(QString::fromUtf8(err).right(160)));
        }
        QTimer::singleShot(5000, this, [this]() {
            if (!m_downloading)
                setDownloadStatus(QString());
        });
    });
}

Player::~Player()
{
    disposePipeline();
    if (m_download.state() != QProcess::NotRunning) {
        m_download.kill();
        m_download.waitForFinished(2000);
    }
}

void Player::setDownloadStatus(const QString &status)
{
    if (m_downloadStatus == status)
        return;
    m_downloadStatus = status;
    emit downloadStatusChanged();
}

void Player::download()
{
    if (m_watchUrl.isEmpty())
        return;

    if (m_downloading) {
        m_downloadCancelled = true;
        m_download.kill();
        return;
    }

    const QString executable = ytDlpExecutable();
    if (executable.isEmpty()) {
        setDownloadStatus(tr("yt-dlp not found"));
        return;
    }

    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    QDir().mkpath(dir);

    m_downloadCancelled = false;
    m_downloading = true;
    emit downloadingChanged();
    m_downloadProgress = 0.0;
    emit downloadProgressChanged();
    setDownloadStatus(tr("Downloading..."));

    const QStringList arguments{
        QStringLiteral("--newline"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--no-playlist"),
        QStringLiteral("-P"), dir,
        QStringLiteral("-o"), QStringLiteral("%(title)s [%(id)s].%(ext)s"),
        QStringLiteral("-f"), QStringLiteral("bestvideo+bestaudio/best"),
        QStringLiteral("--merge-output-format"), QStringLiteral("mkv"),
        m_watchUrl,
    };
    m_download.start(executable, arguments);
}

void Player::readDownloadOutput()
{
    const QString text = QString::fromUtf8(m_download.readAllStandardOutput());

    static const QRegularExpression percent(QStringLiteral("(\\d+(?:\\.\\d+)?)%"));
    auto matches = percent.globalMatch(text);
    double latest = m_downloadProgress;
    bool found = false;
    while (matches.hasNext()) {
        latest = matches.next().captured(1).toDouble() / 100.0;
        found = true;
    }
    if (!found)
        return;

    latest = qBound(0.0, latest, 1.0);
    if (!qFuzzyCompare(latest, m_downloadProgress)) {
        m_downloadProgress = latest;
        emit downloadProgressChanged();
    }
    setDownloadStatus(tr("Downloading... %1%").arg(qRound(latest * 100.0)));
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

void Player::setSinkDisplay(const QString &display)
{
    m_sinkDisplay = display;
}

void Player::play(const QString &videoId)
{
    if (videoId.isEmpty())
        return;

    disposePipeline();

    if (!qFuzzyCompare(m_rate, 1.0)) {
        m_rate = 1.0;
        emit rateChanged();
    }

    m_watchUrl = QStringLiteral("https://www.youtube.com/watch?v=%1").arg(videoId);
    m_pendingSeek = -1;

    if (m_subtitleReply) {
        m_subtitleReply->abort();
        m_subtitleReply->deleteLater();
        m_subtitleReply = nullptr;
    }
    m_cues.clear();
    m_activeCue = -1;
    m_cueCache.clear();
    setSubtitleText(QString());
    if (!m_subtitleTracks.isEmpty()) {
        m_subtitleTracks.clear();
        emit subtitleTracksChanged();
    }
    if (!m_subtitleLanguage.isEmpty()) {
        m_subtitleLanguage.clear();
        emit subtitleLanguageChanged();
    }

    requestUrls();
}

void Player::requestUrls()
{
    const QString executable = ytDlpExecutable();
    if (executable.isEmpty()) {
        m_loading = false;
        emit loadingChanged();
        emit errorOccurred(QStringLiteral("yt-dlp not found"));
        return;
    }

    m_loading = true;
    emit loadingChanged();

    QStringList arguments{
        QStringLiteral("-f"), formatForHeight(m_requestedHeight),
        QStringLiteral("-j"), QStringLiteral("--no-warnings"),
    };
    // Cookies are required for automatic/translated subtitles (otherwise HTTP 429).
    const QString browser = browserForCookies();
    if (!browser.isEmpty()) {
        arguments << QStringLiteral("--cookies-from-browser") << browser
                  << QStringLiteral("--cookies") << cookieFilePath();
    }
    arguments << m_watchUrl;
    m_urlFetch.start(executable, arguments);
}

void Player::readCookiesFromFile()
{
    m_cookieHeader.clear();
    const QString path = cookieFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;

    QStringList pairs;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const QList<QByteArray> parts = line.split('\t');
        if (parts.size() < 7)
            continue;
        const QString domain = QString::fromUtf8(parts.at(0));
        if (!domain.endsWith(QLatin1String("youtube.com"))
            && !domain.endsWith(QLatin1String("google.com")))
            continue;
        pairs.append(QStringLiteral("%1=%2")
                         .arg(QString::fromUtf8(parts.at(5)), QString::fromUtf8(parts.at(6))));
    }
    file.close();
    QFile::remove(path);
    m_cookieHeader = pairs.join(QStringLiteral("; ")).toUtf8();
}

void Player::setResolution(int height)
{
    if (height == m_requestedHeight && !m_videoUrl.isEmpty())
        return;
    if (m_watchUrl.isEmpty())
        return;

    m_requestedHeight = height;
    // Keep the position across the pipeline rebuild.
    m_pendingSeek = m_position > 0 ? m_position : 0;
    disposePipeline();
    requestUrls();
}

void Player::setSubtitleLanguage(const QString &language)
{
    if (language == m_subtitleLanguage)
        return;

    m_subtitleLanguage = language;
    emit subtitleLanguageChanged();

    if (m_subtitleReply) {
        m_subtitleReply->abort();
        m_subtitleReply->deleteLater();
        m_subtitleReply = nullptr;
    }
    m_subtitleRetries = 0;

    m_cues.clear();
    m_activeCue = -1;
    setSubtitleText(QString());

    if (language.isEmpty() || m_watchUrl.isEmpty())
        return;

    // Already fetched? Use the cache (avoids unnecessary calls and 429 throttling).
    const auto cached = m_cueCache.constFind(language);
    if (cached != m_cueCache.constEnd()) {
        m_cues = cached.value();
        updateSubtitleCue();
        return;
    }

    m_subtitleUrl.clear();
    m_subtitleAuto = false;
    for (const QVariant &entry : m_subtitleTracks) {
        const QVariantMap track = entry.toMap();
        if (track.value(QStringLiteral("code")).toString() != language)
            continue;
        m_subtitleUrl = track.value(QStringLiteral("json3")).toString();
        if (m_subtitleUrl.isEmpty())
            m_subtitleUrl = track.value(QStringLiteral("vtt")).toString();
        m_subtitleAuto = track.value(QStringLiteral("auto")).toBool();
        break;
    }
    if (m_subtitleUrl.isEmpty())
        return;

    fetchSubtitle();
}

void Player::fetchSubtitle()
{
    if (m_subtitleUrl.isEmpty())
        return;

    if (m_subtitleReply) {
        m_subtitleReply->abort();
        m_subtitleReply->deleteLater();
        m_subtitleReply = nullptr;
    }

    QNetworkRequest request{QUrl(m_subtitleUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                                     "(KHTML, like Gecko) Chrome/126.0 Safari/537.36"));
    request.setRawHeader("Referer", "https://www.youtube.com/");
    request.setRawHeader("Origin", "https://www.youtube.com");
    if (!m_cookieHeader.isEmpty()
        && QUrl(m_subtitleUrl).host().endsWith(QLatin1String("youtube.com")))
        request.setRawHeader("Cookie", m_cookieHeader);
    m_subtitleReply = m_net->get(request);
    QNetworkReply *reply = m_subtitleReply;
    const QString language = m_subtitleLanguage;
    const bool autoCaptions = m_subtitleAuto;
    connect(reply, &QNetworkReply::finished, this, [this, reply, language, autoCaptions]() {
        if (m_subtitleReply != reply) {
            reply->deleteLater();
            return;
        }
        m_subtitleReply = nullptr;
        const QByteArray data = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();

        if (language != m_subtitleLanguage)
            return;

        if (ok && !data.isEmpty()) {
            handleSubtitleData(data, autoCaptions);
            if (!m_cues.isEmpty()) {
                m_cueCache.insert(language, m_cues);
                return;
            }
        }

        // Temporary 429/throttling – retry with increasing delay.
        if (m_subtitleRetries < 3) {
            ++m_subtitleRetries;
            const int delay = 1200 * m_subtitleRetries;
            QTimer::singleShot(delay, this, [this, language]() {
                if (language == m_subtitleLanguage && !m_subtitleReply)
                    fetchSubtitle();
            });
            return;
        }

        setSubtitleText(tr("Subtitle could not be fetched"));
    });
}

void Player::handleSubtitleData(const QByteArray &data, bool autoCaptions)
{
    if (data.trimmed().startsWith('{'))
        parseJsonSubtitles(data, autoCaptions);
    else
        parseVttSubtitles(QString::fromUtf8(data));
    updateSubtitleCue();
}

void Player::parseJsonSubtitles(const QByteArray &data, bool autoCaptions)
{
    m_cues.clear();
    m_activeCue = -1;

    const QJsonArray events =
        QJsonDocument::fromJson(data).object().value(QStringLiteral("events")).toArray();

    if (!autoCaptions) {
        for (const QJsonValue &value : events) {
            const QJsonObject event = value.toObject();
            const qint64 start = static_cast<qint64>(event.value(QStringLiteral("tStartMs")).toDouble());
            const qint64 duration = static_cast<qint64>(event.value(QStringLiteral("dDurationMs")).toDouble());
            QStringList parts;
            for (const QJsonValue &seg : event.value(QStringLiteral("segs")).toArray())
                parts.append(seg.toObject().value(QStringLiteral("utf8")).toString());
            QString text = parts.join(QString()).trimmed();
            if (text.isEmpty())
                continue;
            text.replace(QLatin1Char('\n'), QLatin1Char(' '));
            SubtitleCue cue;
            cue.start = start;
            cue.end = start + duration;
            cue.text = text;
            m_cues.append(cue);
        }
        return;
    }

    // Automatic captions: an event containing only "\n" ends the line.
    QString pending;
    qint64 pendingStart = -1;
    const auto flush = [&](qint64 end) {
        if (pendingStart >= 0 && !pending.trimmed().isEmpty()) {
            SubtitleCue cue;
            cue.start = pendingStart;
            cue.end = end > pendingStart ? end : pendingStart + 2000;
            cue.text = pending.trimmed();
            m_cues.append(cue);
        }
        pending.clear();
        pendingStart = -1;
    };
    for (const QJsonValue &value : events) {
        const QJsonObject event = value.toObject();
        const qint64 start = static_cast<qint64>(event.value(QStringLiteral("tStartMs")).toDouble());
        QStringList parts;
        for (const QJsonValue &seg : event.value(QStringLiteral("segs")).toArray())
            parts.append(seg.toObject().value(QStringLiteral("utf8")).toString());
        const QString text = parts.join(QString());
        if (text.trimmed().isEmpty()) {
            flush(start);
            continue;
        }
        if (pendingStart < 0)
            pendingStart = start;
        pending += text;
        if (pending.size() > 120)
            flush(start);
    }
    flush(m_duration > 0 ? m_duration : 0);

    std::sort(m_cues.begin(), m_cues.end(), [](const SubtitleCue &a, const SubtitleCue &b) {
        return a.start < b.start;
    });
}

void Player::parseVttSubtitles(const QString &data)
{
    m_cues.clear();
    m_activeCue = -1;

    QString text = data;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    const QStringList blocks = text.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        const QStringList lines = block.split(QLatin1Char('\n'));
        int timingLine = -1;
        for (int i = 0; i < lines.size(); ++i) {
            if (lines.at(i).contains(QLatin1String("-->"))) {
                timingLine = i;
                break;
            }
        }
        if (timingLine < 0)
            continue;

        const QStringList timing = lines.at(timingLine).split(QLatin1String("-->"));
        if (timing.size() != 2)
            continue;

        SubtitleCue cue;
        if (!parseVttTimestamp(timing.at(0).trimmed(), &cue.start))
            continue;
        if (!parseVttTimestamp(timing.at(1).trimmed().section(QLatin1Char(' '), 0, 0), &cue.end))
            continue;

        QStringList cueLines;
        for (int i = timingLine + 1; i < lines.size(); ++i) {
            const QString cleaned = stripVttTags(lines.at(i));
            if (!cleaned.isEmpty())
                cueLines.append(cleaned);
        }
        if (cueLines.isEmpty())
            continue;
        cue.text = cueLines.join(QLatin1Char('\n'));

        // Skip blocks that end entirely before the previous cue (autocaption rolling).
        if (!m_cues.isEmpty() && cue.end <= m_cues.last().start)
            continue;
        m_cues.append(cue);
    }

    std::sort(m_cues.begin(), m_cues.end(), [](const SubtitleCue &a, const SubtitleCue &b) {
        return a.start < b.start;
    });
}

void Player::updateSubtitleCue()
{
    QString text;
    if (!m_subtitleLanguage.isEmpty() && !m_cues.isEmpty()) {
        if (m_activeCue >= 0 && m_activeCue < m_cues.size()
            && m_position >= m_cues.at(m_activeCue).start
            && m_position < m_cues.at(m_activeCue).end) {
            text = m_cues.at(m_activeCue).text;
        } else {
            for (int i = 0; i < m_cues.size(); ++i) {
                if (m_position >= m_cues.at(i).start && m_position < m_cues.at(i).end) {
                    m_activeCue = i;
                    text = m_cues.at(i).text;
                    break;
                }
            }
            if (text.isEmpty())
                m_activeCue = -1;
        }
    }
    setSubtitleText(text);
}

void Player::setSubtitleText(const QString &text)
{
    if (m_subtitleText == text)
        return;
    m_subtitleText = text;
    emit subtitleTextChanged();
}

void Player::stop()
{
    disposePipeline();
}

void Player::togglePlayPause()
{
    if (!m_pipeline)
        return;
    const GstState state = GST_STATE(m_pipeline);
    gst_element_set_state(m_pipeline,
                          state == GST_STATE_PLAYING ? GST_STATE_PAUSED : GST_STATE_PLAYING);
}

void Player::setRate(double rate)
{
    rate = qBound(0.25, rate, 2.0);
    if (qFuzzyCompare(rate, m_rate))
        return;
    m_rate = rate;
    emit rateChanged();
    if (!m_pipeline)
        return;

    // Change speed by seeking to the same position with a new rate.
    gint64 position = 0;
    if (!gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &position) || position < 0)
        position = 0;

    applySeek(position / GST_MSECOND, rate);
}

void Player::seek(qint64 positionMs)
{
    if (!m_pipeline || m_duration <= 0)
        return;
    positionMs = qBound<qint64>(0, positionMs, m_duration);
    m_position = positionMs;
    emit positionChanged();

    applySeek(positionMs, m_rate);
}

void Player::applySeek(qint64 positionMs, double rate)
{
    if (!m_pipeline)
        return;

    const GstSeekFlags flags =
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE);
    // playbin has no sink pads, so a seek on the top pipeline does not reach it.
    // Send the seek to each playbin (video and audio streams) instead.
    const gint64 start = positionMs * GST_MSECOND;
    const char *names[] = {"vplay", "aplay"};
    for (const char *name : names) {
        GstElement *play = gst_bin_get_by_name(GST_BIN(m_pipeline), name);
        if (!play)
            continue;
        gst_element_seek(play, rate, GST_FORMAT_TIME, flags,
                         GST_SEEK_TYPE_SET, start,
                         GST_SEEK_TYPE_SET, GST_CLOCK_TIME_NONE);
        gst_object_unref(play);
    }
}

void Player::setVolume(double volume)
{
    volume = qBound(0.0, volume, 1.0);
    if (qFuzzyCompare(volume, m_volume))
        return;
    m_volume = volume;
    emit volumeChanged();
    if (m_muted) {
        m_muted = false;
        emit mutedChanged();
    }
    applyVolume();
}

void Player::toggleMute()
{
    m_muted = !m_muted;
    emit mutedChanged();
    applyVolume();
}

void Player::applyVolume()
{
    if (!m_pipeline)
        return;

    const double effective = m_muted ? 0.0 : m_volume;
    const char *names[] = {"vplay", "aplay"};
    for (const char *name : names) {
        GstElement *play = gst_bin_get_by_name(GST_BIN(m_pipeline), name);
        if (!play)
            continue;
        // The video branch is a plain chain now, not a playbin, so it has no
        // "volume" property; only the audio playbin is set.
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(play), "volume"))
            g_object_set(play, "volume", effective, nullptr);
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

    // Restore position/speed after a pipeline rebuild (resolution change).
    if (m_pendingSeek >= 0 && m_duration > 0) {
        const qint64 target = qBound<qint64>(0, m_pendingSeek, m_duration);
        m_pendingSeek = -1;
        m_position = target;
        emit positionChanged();
        applySeek(target, m_rate);
    }

    updateSubtitleCue();

    if (m_sinkBufferClock.isValid() && m_sinkBufferClock.elapsed() >= 2000) {
        const int total = m_sinkBuffers.load();
        const double seconds = m_sinkBufferClock.restart() / 1000.0;
        if (seconds > 0.0)
            fprintf(stderr, "YtGst: video sink: %d frames in %.1f s = %.1f fps\n",
                    total - m_sinkBuffersLast, seconds, (total - m_sinkBuffersLast) / seconds);
        m_sinkBuffersLast = total;
    }
}

void Player::handleFetchOutput()
{
    const QByteArray output = m_urlFetch.readAllStandardOutput();
    readCookiesFromFile();
    const QJsonObject obj = QJsonDocument::fromJson(output).object();

    // Available resolutions, highest first. Only offer what the format string
    // can deliver: the HLS ladder when there is one (it is what gets picked),
    // otherwise the DASH/mp4 ladder. yt-dlp reports the protocols as
    // "m3u8_native" and "https" (older releases: "m3u8"/"mp4"), so match on the
    // prefixes - a strict comparison against the wrong name rejects every format
    // and leaves the menu empty. When the hardware decodes H.264 only, every
    // stream is pinned to H.264, so other codecs must not be listed either.
    QVariantList heights;
    const bool onlyH264 = preferH264();
    const QJsonArray formats = obj.value(QStringLiteral("formats")).toArray();
    for (int pass = 0; pass < 2 && heights.isEmpty(); ++pass) {
        for (const QJsonValue &value : formats) {
            const QJsonObject format = value.toObject();
            const QString vcodec = format.value(QStringLiteral("vcodec")).toString();
            if (vcodec.isEmpty() || vcodec == QLatin1String("none"))
                continue;
            if (onlyH264 && !vcodec.startsWith(QLatin1String("avc1")))
                continue;
            const QString proto = format.value(QStringLiteral("protocol")).toString();
            const bool hls = proto.startsWith(QLatin1String("m3u8"));
            const bool dash = proto.startsWith(QLatin1String("https"))
                || proto.startsWith(QLatin1String("mp4"));
            if (pass == 0 ? !hls : !dash)
                continue;
            const int h = format.value(QStringLiteral("height")).toInt();
            if (h > 0 && !heights.contains(h))
                heights.append(h);
        }
    }
    std::sort(heights.begin(), heights.end(), [](const QVariant &a, const QVariant &b) {
        return a.toInt() > b.toInt();
    });
    if (heights != m_resolutions) {
        m_resolutions = heights;
        emit resolutionsChanged();
    }

    // Available subtitle tracks (manual first, then automatic).
    QVariantList tracks;
    QSet<QString> seen;
    QSet<QString> names;
    const QJsonObject manualSubs = obj.value(QStringLiteral("subtitles")).toObject();
    const QJsonObject autoSubs = obj.value(QStringLiteral("automatic_captions")).toObject();

    // json3 URL per language code; also used as a fallback for regional variants
    // (for example "de-DE") that are otherwise only offered as an m3u8 playlist.
    const auto collectJson3 = [](const QJsonObject &source) {
        QHash<QString, QString> map;
        for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
            for (const QJsonValue &value : it.value().toArray()) {
                const QJsonObject format = value.toObject();
                if (format.value(QStringLiteral("ext")).toString() != QLatin1String("json3"))
                    continue;
                const QString url = format.value(QStringLiteral("url")).toString();
                if (!url.isEmpty())
                    map.insert(it.key(), url);
                break;
            }
        }
        return map;
    };
    const QHash<QString, QString> manualJson3 = collectJson3(manualSubs);
    const QHash<QString, QString> autoJson3 = collectJson3(autoSubs);

    const auto addTracks = [&tracks, &seen, &names](const QJsonObject &source,
                                                    const QHash<QString, QString> &json3Map,
                                                    bool autoCaptions) {
        for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
            const QString code = it.key();
            if (code.isEmpty() || seen.contains(code))
                continue;
            QString json3;
            QString vtt;
            const QJsonArray formats = it.value().toArray();
            for (const QJsonValue &value : formats) {
                const QJsonObject format = value.toObject();
                const QString ext = format.value(QStringLiteral("ext")).toString();
                const QString url = format.value(QStringLiteral("url")).toString();
                if (url.isEmpty())
                    continue;
                if (ext == QLatin1String("json3") && json3.isEmpty())
                    json3 = url;
                else if (ext == QLatin1String("vtt") && vtt.isEmpty())
                    vtt = url;
            }
            if (json3.isEmpty()) {
                const QString base = code.section(QLatin1Char('-'), 0, 0).toLower();
                const auto baseIt = json3Map.constFind(base);
                if (baseIt != json3Map.constEnd())
                    json3 = baseIt.value();
            }
            if (json3.isEmpty() && vtt.isEmpty())
                continue;
            const QString name =
                autoCaptions
                    ? QStringLiteral("%1 (%2)").arg(languageName(code), QObject::tr("auto"))
                    : languageName(code);
            if (names.contains(name))
                continue;
            seen.insert(code);
            names.insert(name);
            QVariantMap track;
            track.insert(QStringLiteral("code"), code);
            track.insert(QStringLiteral("name"), name);
            track.insert(QStringLiteral("auto"), autoCaptions);
            track.insert(QStringLiteral("json3"), json3);
            track.insert(QStringLiteral("vtt"), vtt);
            tracks.append(track);
        }
    };
    addTracks(manualSubs, manualJson3, false);
    addTracks(autoSubs, autoJson3, true);
    if (tracks != m_subtitleTracks) {
        m_subtitleTracks = tracks;
        emit subtitleTracksChanged();
    }

    QString videoUrl;
    QString audioUrl;
    int actualHeight = 0;
    const QJsonArray requested = obj.value(QStringLiteral("requested_formats")).toArray();
    if (requested.size() >= 2) {
        videoUrl = requested.at(0).toObject().value(QStringLiteral("url")).toString();
        audioUrl = requested.at(1).toObject().value(QStringLiteral("url")).toString();
        actualHeight = requested.at(0).toObject().value(QStringLiteral("height")).toInt();
        m_videoProtocol = requested.at(0).toObject().value(QStringLiteral("protocol")).toString();
    } else {
        videoUrl = obj.value(QStringLiteral("url")).toString();
        actualHeight = obj.value(QStringLiteral("height")).toInt();
        m_videoProtocol = obj.value(QStringLiteral("protocol")).toString();
    }

    m_loading = false;
    emit loadingChanged();

    if (videoUrl.isEmpty()) {
        emit errorOccurred(QStringLiteral("No streams found for this video"));
        return;
    }

    if (actualHeight != m_resolution) {
        m_resolution = actualHeight;
        emit resolutionChanged();
    }

    m_videoUrl = videoUrl;
    m_audioUrl = audioUrl;
    buildPipeline();
}

// Makes the audio sink able to reach the session's sound server, and reports
// whether one was found at all.
//
// The plain case needs nothing: $XDG_RUNTIME_DIR is the session's runtime
// directory and pulsesink finds pulse/native on its own. Two launcher setups
// break that, and both are handled here by naming the socket in PULSE_SERVER:
//
//   * the player inherits an XDG_RUNTIME_DIR that is not the session's. On
//     Watermelon Wine weston.service sets /home/weston, so the sound server in
//     /run/user/1000 is never looked at, even though it belongs to the same
//     user (the socket is then in /run/user/<our uid>, and libpulse accepts it
//     once it is pointed at);
//   * the player runs as root inside the user's session. libpulse refuses that
//     runtime directory ("is not owned by us"), but connects when the socket is
//     named - root may open it regardless of the permissions.
static bool prepareSoundServer()
{
    if (!qgetenv("PULSE_SERVER").isEmpty())
        return true;

    const QByteArray runtimeDir = qgetenv("XDG_RUNTIME_DIR");
    const QByteArray ownDir = QByteArrayLiteral("/run/user/") + QByteArray::number(getuid());
    struct stat info {};

    const QByteArray sessionSocket = runtimeDir + QByteArrayLiteral("/pulse/native");
    if (!runtimeDir.isEmpty()
        && ::stat(sessionSocket.constData(), &info) == 0
        && S_ISSOCK(info.st_mode) && info.st_uid == getuid())
        return true;

    const QByteArray ownSocket = ownDir + QByteArrayLiteral("/pulse/native");
    if (::stat(ownSocket.constData(), &info) == 0
        && S_ISSOCK(info.st_mode) && info.st_uid == getuid()) {
        qputenv("PULSE_SERVER", QByteArrayLiteral("unix:") + ownSocket);
        qWarning("YtGst: sound server is not in XDG_RUNTIME_DIR - using %s",
                 ownSocket.constData());
        return true;
    }

    if (getuid() == 0) {
        const QDir runUser(QStringLiteral("/run/user"));
        const QStringList entries =
            runUser.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &entry : entries) {
            const QByteArray path = QByteArrayLiteral("/run/user/") + entry.toLocal8Bit()
                + QByteArrayLiteral("/pulse/native");
            if (::stat(path.constData(), &info) == 0 && S_ISSOCK(info.st_mode)) {
                qputenv("PULSE_SERVER", QByteArrayLiteral("unix:") + path);
                qWarning("YtGst: using the sound server of uid %u (%s)",
                         static_cast<unsigned>(info.st_uid), path.constData());
                return true;
            }
        }
    }
    return false;
}

// Picks an audio sink description that works on this system.
// PipeWire owns the HDMI device at 48 kHz on Watermelon Wine, so route the
// audio through it (pulsesink): it resamples the 44.1 kHz AAC of DASH mp4
// streams and shares the device instead of fighting for it. A direct alsasink
// cannot be used while PipeWire holds the codec ("Device or resource busy"), so
// it is only the last resort on a system without a sound server.
static QString audioSinkDescription()
{
    // The CMake flag YTGST_AUDIO_SINK (see CMakeLists.txt) decides on a build
    // for other hardware; the environment overrides it for a quick comparison
    // without a rebuild.
    const QByteArray configured = qgetenv("YTGST_AUDIO_SINK");
    const QString forced = configured.isEmpty()
        ? QStringLiteral(YTGST_AUDIO_SINK)
        : QString::fromLocal8Bit(configured);
    if (!forced.isEmpty())
        return forced;

    const char *candidates[] = {"pulsesink", "pipewiresink", "autoaudiosink", nullptr};
    for (int i = 0; candidates[i]; ++i) {
        GstElementFactory *factory = gst_element_factory_find(candidates[i]);
        if (factory) {
            gst_object_unref(factory);
            return QString(candidates[i]);
        }
    }
    return QStringLiteral(
        "audioconvert ! audioresample ! "
        "capsfilter caps=audio/x-raw,rate=48000,channels=2,format=S16LE "
        "! alsasink");
}

void Player::buildPipeline()
{
    if (m_item == nullptr && m_sinkDisplay.isEmpty()) {
        emit errorOccurred(QStringLiteral("Video surface is not connected"));
        return;
    }
    if (m_videoUrl.isEmpty()) {
        emit errorOccurred(QStringLiteral("No streams found for this video"));
        return;
    }

    m_pipeline = gst_pipeline_new("pipe");
    m_audioPlay = nullptr;

    // === VIDEO: an explicit chain instead of a playbin ===
    // A playbin/uridecodebin inserts a buffering queue (use-buffering=true,
    // high-percent=99, max-size-time=5 s) that stalls this board's V4L2
    // stateless H.264 decoder: the video renders very slowly while the audio
    // stays fine. An explicit chain feeds the decoder directly and plays
    // 1080p60 at a sustained 60 fps. YouTube video here is always HLS
    // (MPEG-TS segments); qtdemux handles another container (DASH mp4).
    //
    // The elements are created one by one rather than through
    // gst_parse_bin_from_description(): the demuxers only expose their pads
    // once the stream arrives and the parser cannot finish that delayed link
    // inside a bin (it logs "Delayed linking failed" and no frame ever reaches
    // the sink). The dynamic pads are linked in onHlsdemuxPadAdded() and
    // onDemuxPadAdded().
    QString videoSinkDescription = QStringLiteral(YTGST_VIDEO_SINK);
    if (videoSinkDescription.isEmpty()) {
        if (m_sinkDisplay.isEmpty()) {
            // No compositor of our own: upload the decoder's frames into the
            // QML scene graph and let qml6glsink draw them as a texture there.
            videoSinkDescription = QStringLiteral(
                "glupload ! glcolorconvert ! capsfilter "
                "caps=\"video/x-raw(memory:GLMemory),format=(string)RGBA\" "
                "! qml6glsink name=gsink");
        } else {
            // PlayerWindow.qml runs a Wayland compositor of its own (see
            // sinkDisplay). waylandsink hands the decoder's dmabuf straight to
            // it, the way it hands it to Weston, and the compositor draws the
            // surface inside the player window through a ShellSurfaceItem:
            // no copy through system memory and no window of its own.
            videoSinkDescription = QStringLiteral("waylandsink name=gsink");
        }
    }

    // YTGST_VIDEO_SINK=<gst description> replaces the video sink chain, so the
    // sinks can be compared on a board without rebuilding (for example
    // YTGST_VIDEO_SINK=waylandsink).
    const QByteArray sinkOverride = qgetenv("YTGST_VIDEO_SINK");
    if (!sinkOverride.isEmpty())
        videoSinkDescription = QString::fromLocal8Bit(sinkOverride);

    // The decoder is a CMake flag (YTGST_VIDEO_DECODER, see CMakeLists.txt), so
    // a build for other hardware picks its own. The environment overrides it
    // without a rebuild.
    const QByteArray decoderOverride = qgetenv("YTGST_VIDEO_DECODER");
    const QString videoDecoder = decoderOverride.isEmpty()
        ? QStringLiteral(YTGST_VIDEO_DECODER)
        : QString::fromLocal8Bit(decoderOverride);

    const bool hls = m_videoProtocol.startsWith(QLatin1String("m3u8"));
    GstElement *vplay = gst_bin_new("vplay");
    GstElement *vsrc = gst_element_factory_make("souphttpsrc", "vsrc");
    GstElement *vdemux = gst_element_factory_make(hls ? "hlsdemux" : "qtdemux", "vdemux");
    GstElement *vtsdemux = hls ? gst_element_factory_make("tsdemux", "vtsdemux") : nullptr;
    GstElement *vparse = gst_element_factory_make("h264parse", "vparse");
    GstElement *vdecoder = gst_element_factory_make(videoDecoder.toUtf8().constData(),
                                                    "vdecoder");
    GstElement *vsink = gst_parse_bin_from_description(videoSinkDescription.toUtf8().constData(),
                                                       TRUE, nullptr);
    if (!vsrc || !vdemux || (hls && !vtsdemux) || !vparse || !vdecoder || !vsink) {
        emit errorOccurred(QStringLiteral("Could not build the video pipeline"));
        return;
    }

    g_object_set(vsrc, "location", m_videoUrl.toUtf8().constData(),
                 "user-agent",
                 "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                 "(KHTML, like Gecko) Chrome/126.0 Safari/537.36",
                 nullptr);

    gst_bin_add(GST_BIN(vplay), vsrc);
    gst_bin_add(GST_BIN(vplay), vdemux);
    if (vtsdemux)
        gst_bin_add(GST_BIN(vplay), vtsdemux);
    gst_bin_add(GST_BIN(vplay), vparse);
    gst_bin_add(GST_BIN(vplay), vdecoder);
    gst_bin_add(GST_BIN(vplay), vsink);

    // h264parse, the decoder and the sink have static pads and are linked right
    // away; the demuxers are linked when they announce their pads.
    bool linked = gst_element_link(vsrc, vdemux)
        && gst_element_link(vparse, vdecoder)
        && gst_element_link(vdecoder, vsink);
    if (vtsdemux) {
        g_signal_connect(vdemux, "pad-added", G_CALLBACK(onHlsdemuxPadAdded), vtsdemux);
        g_signal_connect(vtsdemux, "pad-added", G_CALLBACK(onDemuxPadAdded), vparse);
    } else {
        g_signal_connect(vdemux, "pad-added", G_CALLBACK(onDemuxPadAdded), vparse);
    }
    if (!linked) {
        emit errorOccurred(QStringLiteral("Could not link the video pipeline"));
        return;
    }

    GstElement *vbin = vplay;
    gst_bin_add(GST_BIN(m_pipeline), vplay);

    if (!m_audioUrl.isEmpty()) {
        if (!prepareSoundServer()) {
            // No sound server could be found (see prepareSoundServer()). Say why
            // there is no sound and play the picture alone, instead of letting
            // the audio sink fail the whole pipeline with "Failed to connect:
            // Connection refused".
            emit errorOccurred(tr("No sound server found - playing without audio."));
        } else {
            // === AUDIO: separate playbin in the same pipeline (shared clock => sync) ===
            GstElement *aplay = gst_element_factory_make("playbin", "aplay");
            g_object_set(aplay, "uri", m_audioUrl.toUtf8().constData(),
                         "audio-sink",
                         gst_parse_bin_from_description(
                             audioSinkDescription().toUtf8().constData(), TRUE, nullptr),
                         nullptr);
            g_signal_connect(aplay, "source-setup", G_CALLBACK(onSourceSetup), nullptr);
            gst_bin_add(GST_BIN(m_pipeline), aplay);
            gst_element_sync_state_with_parent(aplay);
            m_audioPlay = aplay;
        }
    }

    // The named element at the end of the chain is either the QML sink (which
    // draws into the QML item) or waylandsink (which draws into the socket of
    // the compositor that window runs).
    GstElement *gsink = findElementByName(vbin, "gsink");
    bool usesQmlSink = false;
    if (gsink) {
        GstElementFactory *factory = gst_element_get_factory(gsink);
        const gchar *factoryName = factory
            ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) : nullptr;
        usesQmlSink = g_strcmp0(factoryName, "qml6glsink") == 0;
        if (usesQmlSink && m_item)
            g_object_set(gsink, "widget", m_item, nullptr);
        else if (!m_sinkDisplay.isEmpty() && g_strcmp0(factoryName, "waylandsink") == 0)
            g_object_set(gsink, "display", m_sinkDisplay.toUtf8().constData(), nullptr);
        gst_object_unref(gsink);
    }

    attachSinkProbe(vbin);

    applyVolume();

    gst_bus_add_signal_watch(gst_element_get_bus(m_pipeline));
    g_signal_connect(gst_element_get_bus(m_pipeline), "message",
                     G_CALLBACK(onBusMessage), this);

    // qml6glsink has to start from a scene graph update, so that the GL context
    // that is active when the pipeline starts is the PlayerWindow's (otherwise
    // that sink draws in whichever context rendered last).
    QQuickWindow *win = m_item ? m_item->window() : nullptr;

    if (usesQmlSink && win) {
        win->update();
        win->scheduleRenderJob(new SetPlaying(m_pipeline), QQuickWindow::BeforeSynchronizingStage);
    } else {
        // Every other sink starts on a thread of its own. The player window
        // runs a Wayland compositor and this thread is the one that drives it;
        // waylandsink talks to that compositor while it changes state (it waits
        // for the compositor to configure its surface before it prerolls), so
        // setting the state from here would leave this thread waiting for a
        // compositor that only this thread can run - the pipeline would never
        // reach PLAYING and neither the picture nor the sound would come.
        GstElement *pipeline = GST_ELEMENT(gst_object_ref(m_pipeline));
        QThread *starter = QThread::create([pipeline]() {
            gst_element_set_state(pipeline, GST_STATE_PLAYING);
            gst_object_unref(pipeline);
        });
        connect(starter, &QThread::finished, starter, &QObject::deleteLater);
        starter->start();
    }

    m_duration = 0;
    m_position = 0;
    emit durationChanged();
    emit positionChanged();
    if (m_pendingSeek < 0 && !qFuzzyCompare(m_rate, 1.0))
        m_pendingSeek = 0;
    m_tick->start();
}

// With YTGST_DEBUG_FPS=1 the buffers that reach the video sink are counted and
// the rate is printed every couple of seconds. A sink that cannot keep up and a
// source that cannot deliver look the same on screen, and this tells them
// apart.
void Player::attachSinkProbe(GstElement *videoBin)
{
    if (qgetenv("YTGST_DEBUG_FPS") != "1")
        return;

    GstIterator *sinks = gst_bin_iterate_sinks(GST_BIN(videoBin));
    GValue item = G_VALUE_INIT;
    while (gst_iterator_next(sinks, &item) == GST_ITERATOR_OK) {
        GstElement *sink = GST_ELEMENT(g_value_get_object(&item));
        GstPad *pad = gst_element_get_static_pad(sink, "sink");
        if (!pad)
            pad = gst_element_get_static_pad(sink, "video_sink");
        if (pad) {
            gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
                              [](GstPad *, GstPadProbeInfo *, gpointer data) -> GstPadProbeReturn {
                                  static_cast<Player *>(data)->m_sinkBuffers.fetch_add(1);
                                  return GST_PAD_PROBE_OK;
                              },
                              this, nullptr);
            gst_object_unref(pad);
        }
        g_value_reset(&item);
    }
    g_value_unset(&item);
    gst_iterator_free(sinks);
    m_sinkBufferClock.start();
    fprintf(stderr, "YtGst: counting video sink frames (YTGST_DEBUG_FPS=1)\n");
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
    m_audioPlay = nullptr;
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
            // Without this the reason a stream fails never reaches the journal,
            // only the QML error label - and most failures happen before that.
            qWarning("YtGst: GStreamer error: %s", error->message);
            if (debug)
                qWarning("YtGst: GStreamer debug: %s", debug);
            // A failure in the audio playbin (a sound server that went away, an
            // HDMI hiccup) must not take the picture down with it: report it and
            // keep playing.
            const bool audioOnly = self->m_audioPlay
                && GST_OBJECT(msg->src) != GST_OBJECT(self->m_pipeline)
                && gst_object_has_as_ancestor(GST_OBJECT(msg->src), GST_OBJECT(self->m_audioPlay));
            g_error_free(error);
            g_free(debug);
            if (audioOnly) {
                emit self->errorOccurred(message);
                break;
            }
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
