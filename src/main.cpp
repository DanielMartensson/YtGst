#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QProcess>
#include <QQuickWindow>
#include <QSGRendererInterface>

#include <gst/gst.h>

// Startar spelaren som en egen process med ett eget QQuickWindow/GL-kontext.
// Huvudfönstret ("Ytgst") förblir öppet medan "Ytgst Player" spelar.
class Launcher : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    Q_INVOKABLE void play(const QString &videoId)
    {
        if (videoId.isEmpty())
            return;
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                {QStringLiteral("--play"), videoId});
    }
};

int main(int argc, char *argv[])
{
    // VA-API: iHD-drivern initieras inte på denna maskin (Haswell);
    // välj i965 om den finns, så vah264dec (hårdvaruavkodning) fungerar.
    if (QFile::exists(QStringLiteral("/usr/lib/x86_64-linux-gnu/dri/i965_drv_video.so"))) {
        qputenv("LIBVA_DRIVER_NAME", "i965");
    }

    gst_init(&argc, &argv);

    // qml6glsink-pluginet MÅSTE laddas innan QML-motorn skapas,
    // för att registrera GstGLQt6VideoItem-typen i QML.
    GstElement *sink = gst_element_factory_make("qml6glsink", nullptr);
    if (!sink) {
        qWarning("qml6glsink saknas – installation av gstreamer1.0-qt6 krävs");
    } else {
        gst_object_unref(sink);
    }

    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QGuiApplication app(argc, argv);

    QString playId;
    const QStringList args = QCoreApplication::arguments();
    const int playIndex = args.indexOf(QStringLiteral("--play"));
    if (playIndex >= 0 && playIndex + 1 < args.size())
        playId = args.at(playIndex + 1);

    Launcher launcher;

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.rootContext()->setContextProperty(QStringLiteral("launcher"), &launcher);

    if (playId.isEmpty()) {
        engine.load(QUrl(QStringLiteral("qrc:/Ytgst/qml/Main.qml")));
    } else {
        engine.load(QUrl(QStringLiteral("qrc:/Ytgst/qml/PlayerWindow.qml")));
        if (!engine.rootObjects().isEmpty()) {
            QObject *root = engine.rootObjects().first();
            root->setProperty("standalone", true);
            root->setProperty("visible", true);
            root->setProperty("videoId", playId);
        }
    }

    const int result = app.exec();

    gst_deinit();

    return result;
}

#include "main.moc"
