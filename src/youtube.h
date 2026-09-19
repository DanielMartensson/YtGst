#pragma once

#include "videomodel.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QProcess>
#include <QtQml/qqml.h>

class Youtube : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(VideoListModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit Youtube(QObject *parent = nullptr);

    VideoListModel *model() const;
    void setModel(VideoListModel *model);
    bool busy() const;

    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void loadNextPage();

signals:
    void modelChanged();
    void busyChanged();
    void searchFailed(const QString &message);

private:
    QJsonObject requestBody(const QString &query, const QString &token) const;
    static QString textOf(const QJsonObject &object, const QByteArray &key);
    static QJsonArray itemsFromRoot(const QJsonObject &root, QString &nextToken);
    static QVariantMap videoFromRenderer(const QJsonObject &videoRenderer);
    void sendPage(bool continuation);
    void onReplyFinished();
    void enrichPending();
    void onEnrichLine();

    QNetworkAccessManager m_net;
    VideoListModel *m_model = nullptr;
    QNetworkReply *m_reply = nullptr;
    QProcess *m_enrich = nullptr;

    QString m_query;
    QString m_nextToken;
    bool m_continuation = false;
    int m_enrichCursor = 0;
    static constexpr int kMaxEnrich = 40;
    static constexpr int kPageSize = 40;
};