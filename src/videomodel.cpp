#include "videomodel.h"

#include <QJsonObject>

VideoListModel::VideoListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

QVariant VideoListModel::appendVideo(const QVariant &properties)
{
    const QJsonObject object = properties.toJsonObject();

    const QString id = object.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return false;

    Video video;
    video.id = id;
    video.title = object.value(QStringLiteral("title")).toString();
    video.uploader = object.value(QStringLiteral("channel")).toString();
    const QJsonValue viewsValue = object.value(QStringLiteral("views"));
    video.views = viewsValue.isDouble() ? qRound64(viewsValue.toDouble()) : -1;
    video.age = object.value(QStringLiteral("age")).toString();
    video.duration = object.value(QStringLiteral("duration")).toString();
    video.live = object.value(QStringLiteral("live")).toBool();
    video.upcoming = object.value(QStringLiteral("upcoming")).toBool();
    video.thumbnail = object.value(QStringLiteral("thumbnail")).toString();
    video.likes = -1;

    beginInsertRows({}, m_videos.size(), m_videos.size());
    m_videos.append(video);
    endInsertRows();
    return true;
}

void VideoListModel::clear()
{
    if (m_videos.isEmpty())
        return;
    beginResetModel();
    m_videos.clear();
    endResetModel();
}

int VideoListModel::count() const
{
    return m_videos.size();
}

int VideoListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_videos.size();
}

QVariant VideoListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_videos.size())
        return {};

    const Video &video = m_videos.at(index.row());
    switch (role) {
    case IdRole:
        return video.id;
    case TitleRole:
        return video.title;
    case UploaderRole:
        return video.uploader;
    case ViewsTextRole:
        if (video.views < 0)
            return {};
        return formatCount(video.views) + QLatin1String(" views");
    case AgeRole:
        return video.age;
    case DurationRole:
        return video.duration;
    case IsLiveRole:
        return video.live;
    case IsUpcomingRole:
        return video.upcoming;
    case ThumbnailRole:
        return video.thumbnail;
    case LikesTextRole:
        if (video.likes < 0)
            return {};
        return formatCount(video.likes) + QLatin1Char(' ');
    case HasLikesRole:
        return video.likes >= 0;
    default:
        return {};
    }
}

const Video &VideoListModel::videoAt(int row) const
{
    return m_videos.at(row);
}

void VideoListModel::setLikes(const QString &id, qlonglong likes)
{
    for (int i = 0; i < m_videos.size(); ++i) {
        if (m_videos.at(i).id != id)
            continue;
        m_videos[i].likes = likes;
        const QModelIndex index = this->index(i);
        emit dataChanged(index, index, { LikesTextRole, HasLikesRole });
        return;
    }
}

QHash<int, QByteArray> VideoListModel::roleNames() const
{
    return {
        { IdRole, "id" },
        { TitleRole, "title" },
        { UploaderRole, "uploader" },
        { ViewsTextRole, "viewsText" },
        { AgeRole, "age" },
        { DurationRole, "duration" },
        { IsLiveRole, "isLive" },
        { IsUpcomingRole, "isUpcoming" },
        { ThumbnailRole, "thumbnail" },
        { LikesTextRole, "likesText" },
        { HasLikesRole, "hasLikes" },
    };
}

QString VideoListModel::formatCount(qlonglong value)
{
    if (value < 0)
        return {};

    const int mag = value >= 1000000000 ? 9 : (value >= 1000000 ? 6 : (value >= 1000 ? 3 : 0));
    if (mag == 0)
        return QString::number(value);

    double v = static_cast<double>(value);
    for (int i = 0; i < mag; ++i)
        v /= 10.0;

    QString result = QString::number(v, 'f', 1);
    if (result.endsWith(QLatin1String(".0")))
        result = result.left(result.indexOf(QLatin1Char('.')));

    switch (mag) {
    case 9:
        result += QLatin1String("B");
        break;
    case 6:
        result += QLatin1String("M");
        break;
    case 3:
        result += QLatin1String("K");
        break;
    default:
        break;
    }
    return result;
}
