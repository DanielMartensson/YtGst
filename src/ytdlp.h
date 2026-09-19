#pragma once

#include <QFile>
#include <QStandardPaths>
#include <QString>

// Sökvägen till yt-dlp. Sätts vid kompilering via CMake-flaggan
// YTGST_YTDLP_PATH. Är den tom eller pekar på en fil som inte finns
// faller vi tillbaka på att söka i PATH.
inline QString ytDlpExecutable()
{
#ifdef YTGST_YTDLP_PATH
    const QString configured = QStringLiteral(YTGST_YTDLP_PATH);
    if (!configured.isEmpty() && QFile::exists(configured))
        return configured;
#endif
    return QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
}
