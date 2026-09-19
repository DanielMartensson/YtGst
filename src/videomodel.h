#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QtQml/qqml.h>

class Video
{
public:
    QString id;
    QString title;
    QString uploader;
    qlonglong views = -1;
    QString age;
    QString duration;
    bool live = false;
    bool upcoming = false;
    QString thumbnail;
    qlonglong likes = -1;
};

class VideoListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        UploaderRole,
        ViewsTextRole,
        AgeRole,
        DurationRole,
        IsLiveRole,
        IsUpcomingRole,
        ThumbnailRole,
        LikesTextRole,
        HasLikesRole
    };
    Q_ENUM(Role)

    explicit VideoListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariant appendVideo(const QVariant &properties);
    Q_INVOKABLE void clear();
    Q_INVOKABLE int count() const;

    const Video &videoAt(int row) const;
    void setLikes(const QString &id, qlonglong likes);

private:
    static QString formatCount(qlonglong value);

    QList<Video> m_videos;
};