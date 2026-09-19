#include "youtube.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

static const char kInnertubeKey[] = "AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8";
static const char kInnertubeSearchUrl[] = "https://www.youtube.com/youtubei/v1/search";

Youtube::Youtube(QObject *parent)
    : QObject(parent)
{
}

VideoListModel *Youtube::model() const
{
    return m_model;
}

void Youtube::setModel(VideoListModel *model)
{
    if (m_model == model)
        return;
    m_model = model;
    emit modelChanged();
}

bool Youtube::busy() const
{
    return m_reply != nullptr;
}

QJsonObject Youtube::requestBody(const QString &query, const QString &token) const
{
    QJsonObject client{
        { QStringLiteral("clientName"), QStringLiteral("WEB") },
        { QStringLiteral("clientVersion"), QStringLiteral("2.20240101.00.00") },
        { QStringLiteral("hl"), QStringLiteral("en") },
    };
    QJsonObject body{
        { QStringLiteral("context"), QJsonObject{ { QStringLiteral("client"), client } } },
        { QStringLiteral("query"), query },
    };
    if (!token.isEmpty())
        body.insert(QStringLiteral("continuation"), token);
    return body;
}

QString Youtube::textOf(const QJsonObject &object, const QByteArray &key)
{
    const QJsonValue value = object.value(QString::fromLatin1(key));
    if (value.isString())
        return value.toString();
    if (value.isObject()) {
        const QJsonObject nested = value.toObject();
        const QJsonArray runs = nested.value(QStringLiteral("runs")).toArray();
        if (!runs.isEmpty())
            return runs.first().toObject().value(QStringLiteral("text")).toString();
        return nested.value(QStringLiteral("simpleText")).toString();
    }
    return {};
}

QJsonArray Youtube::itemsFromRoot(const QJsonObject &root, QString &nextToken)
{
    QJsonArray sectionContents;

    if (root.contains(QStringLiteral("contents"))) {
        const QJsonObject results = root.value(QStringLiteral("contents")).toObject()
                                        .value(QStringLiteral("twoColumnSearchResultsRenderer")).toObject()
                                        .value(QStringLiteral("primaryContents")).toObject()
                                        .value(QStringLiteral("sectionListRenderer")).toObject();
        sectionContents = results.value(QStringLiteral("contents")).toArray();
    } else if (root.contains(QStringLiteral("continuationContents"))) {
        sectionContents = root.value(QStringLiteral("continuationContents")).toObject()
                              .value(QStringLiteral("sectionListContinuation")).toObject()
                              .value(QStringLiteral("contents")).toArray();
    } else if (root.contains(QStringLiteral("onResponseReceivedCommands"))
               || root.contains(QStringLiteral("onResponseReceivedEndpoints"))) {
        const QJsonArray commands = root.value(QStringLiteral("onResponseReceivedCommands")).isArray()
                                        ? root.value(QStringLiteral("onResponseReceivedCommands")).toArray()
                                        : root.value(QStringLiteral("onResponseReceivedEndpoints")).toArray();
        for (const QJsonValue &command : commands) {
            const QJsonArray cont = command.toObject()
                                       .value(QStringLiteral("appendContinuationItemsAction")).toObject()
                                       .value(QStringLiteral("continuationItems")).toArray();
            for (const QJsonValue &entry : cont)
                sectionContents.append(entry);
        }
    }

    QJsonArray items;
    for (const QJsonValue &entry : sectionContents) {
        const QJsonObject entryObject = entry.toObject();
        if (entryObject.contains(QStringLiteral("continuationItemRenderer"))) {
            nextToken = entryObject.value(QStringLiteral("continuationItemRenderer")).toObject()
                            .value(QStringLiteral("continuationEndpoint")).toObject()
                            .value(QStringLiteral("continuationCommand")).toObject()
                            .value(QStringLiteral("token")).toString();
            continue;
        }
        const QJsonArray inner = entryObject.value(QStringLiteral("itemSectionRenderer")).toObject()
                                    .value(QStringLiteral("contents")).toArray();
        for (const QJsonValue &item : inner)
            items.append(item);
    }
    return items;
}

QVariantMap Youtube::videoFromRenderer(const QJsonObject &videoRenderer)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), videoRenderer.value(QStringLiteral("videoId")).toString());
    map.insert(QStringLiteral("title"), textOf(videoRenderer, "title"));
    map.insert(QStringLiteral("channel"), textOf(videoRenderer, "ownerText"));

    QString viewText = textOf(videoRenderer, "viewCountText").simplified();
    bool parsed = false;
    const qlonglong views = viewText.remove(QLatin1Char(',')).toLongLong(&parsed);
    map.insert(QStringLiteral("views"), parsed ? views : -1);

    map.insert(QStringLiteral("age"), textOf(videoRenderer, "publishedTimeText"));
    map.insert(QStringLiteral("duration"), textOf(videoRenderer, "lengthText"));

    bool live = false;
    bool upcoming = false;
    const QJsonArray badges = videoRenderer.value(QStringLiteral("badges")).toArray();
    for (const QJsonValue &badge : badges) {
        const QString label = badge.toObject()
                                  .value(QStringLiteral("metadataBadgeRenderer")).toObject()
                                  .value(QStringLiteral("label")).toString();
        if (label.startsWith(QStringLiteral("LIVE")))
            live = true;
        else if (label.startsWith(QStringLiteral("UPCOMING")) || label.startsWith(QStringLiteral("PREMIERE")))
            upcoming = true;
    }
    map.insert(QStringLiteral("live"), live);
    map.insert(QStringLiteral("upcoming"), upcoming);

    const QJsonArray thumbnails = videoRenderer.value(QStringLiteral("thumbnail")).toObject()
                                      .value(QStringLiteral("thumbnails")).toArray();
    for (const QJsonValue &t : thumbnails) {
        const int width = t.toObject().value(QStringLiteral("width")).toInt();
        if (width >= 320) {
            QString url = t.toObject().value(QStringLiteral("url")).toString();
            url = url.left(url.indexOf(QLatin1Char('?')));
            map.insert(QStringLiteral("thumbnail"), url);
            break;
        }
    }

    return map;
}

void Youtube::search(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
        return;

    if (m_enrich) {
        m_enrich->kill();
        m_enrich->deleteLater();
        m_enrich = nullptr;
    }
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_model)
        m_model->clear();

    m_query = trimmed;
    m_nextToken.clear();
    m_continuation = false;
    m_enrichCursor = 0;
    emit busyChanged();
    sendPage(false);
}

void Youtube::loadNextPage()
{
    if (m_reply || m_nextToken.isEmpty() || m_query.isEmpty())
        return;
    sendPage(true);
}

void Youtube::sendPage(bool continuation)
{
    const QJsonObject body = requestBody(m_query, continuation ? m_nextToken : QString());
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest request(QUrl(QString::fromLatin1(kInnertubeSearchUrl)
                                     + QStringLiteral("?key=") + QString::fromLatin1(kInnertubeKey)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36");
    request.setRawHeader("Origin", "https://www.youtube.com");

    m_continuation = continuation;
    m_reply = m_net.post(request, payload);
    connect(m_reply, &QNetworkReply::finished, this, &Youtube::onReplyFinished);
    emit busyChanged();
}

void Youtube::onReplyFinished()
{
    if (!m_reply)
        return;

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit busyChanged();
        emit searchFailed(QStringLiteral("Network error: %1").arg(reply->errorString()));
        return;
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        emit busyChanged();
        emit searchFailed(QStringLiteral("Failed to parse YouTube response"));
        return;
    }

    QString nextToken;
    const QJsonArray items = itemsFromRoot(document.object(), nextToken);
    m_nextToken = nextToken;

    if (m_model) {
        for (const QJsonValue &item : items) {
            const QJsonObject renderer = item.toObject().value(QStringLiteral("videoRenderer")).toObject();
            if (renderer.isEmpty())
                continue;
            if (renderer.value(QStringLiteral("videoId")).toString().isEmpty())
                continue;
            m_model->appendVideo(videoFromRenderer(renderer));
        }
    }

    emit busyChanged();
    if (!m_continuation) {
        m_enrichCursor = 0;
        enrichPending();
    }
}

void Youtube::enrichPending()
{
    if (m_enrich || !m_model)
        return;

    const int visibleCap = qMin(m_model->count(), kMaxEnrich);
    QList<int> batch;
    for (int i = m_enrichCursor; i < visibleCap && batch.size() < 6; ++i) {
        if (m_model->videoAt(i).likes < 0)
            batch.append(i);
    }
    if (batch.isEmpty())
        return;
    m_enrichCursor = batch.last() + 1;

    const QString executable = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    if (executable.isEmpty())
        return;

    QStringList arguments{ QStringLiteral("-j"), QStringLiteral("--no-warnings"),
                           QStringLiteral("--extractor-args"), QStringLiteral("youtube:player_client=android") };
    for (const int row : batch)
        arguments << QStringLiteral("https://www.youtube.com/watch?v=%1").arg(m_model->videoAt(row).id);

    m_enrich = new QProcess(this);
    m_enrich->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_enrich, &QProcess::readyReadStandardOutput, this, &Youtube::onEnrichLine);
    connect(m_enrich, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        QProcess *process = m_enrich;
        m_enrich = nullptr;
        process->deleteLater();
        emit busyChanged();
        QTimer::singleShot(1500, this, &Youtube::enrichPending);
    });
    m_enrich->start(executable, arguments);
    emit busyChanged();
}

void Youtube::onEnrichLine()
{
    if (!m_enrich || !m_model)
        return;
    while (m_enrich->canReadLine()) {
        const QByteArray line = m_enrich->readLine();
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject())
            continue;
        const QJsonObject object = document.object();
        const QString id = object.value(QStringLiteral("id")).toString();
        const qlonglong likes = object.value(QStringLiteral("like_count")).toDouble(-1);
        if (id.isEmpty() || likes < 0)
            continue;
        m_model->setLikes(id, likes);
    }
}