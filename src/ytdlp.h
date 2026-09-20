#pragma once

#include <QFile>
#include <QStandardPaths>
#include <QString>

// Path to yt-dlp. Set at compile time through the CMake flag
// YTGST_YTDLP_PATH. If it is empty or points to a missing file
// we fall back to searching the PATH.
inline QString ytDlpExecutable()
{
#ifdef YTGST_YTDLP_PATH
    const QString configured = QStringLiteral(YTGST_YTDLP_PATH);
    if (!configured.isEmpty() && QFile::exists(configured))
        return configured;
#endif
    return QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
}
