#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QProcess>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTimer>
#include <QSGRendererInterface>

#include <cstring>

#include <gst/gst.h>

#ifndef GL_RENDERER
#define GL_RENDERER 0x1F01
#endif

// Startar spelaren som en egen process med ett eget QQuickWindow/GL-kontext.
// Huvudfönstret ("YtGst") förblir öppet medan "YtGst Player" spelar.
class Launcher : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    Q_INVOKABLE void play(const QString &videoId, const QString &title)
    {
        if (videoId.isEmpty())
            return;
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                {QStringLiteral("--play"), videoId,
                                 QStringLiteral("--title"), title});
    }
};

// Hämtar GL-renderaren (t.ex. "llvmpipe" vid mjukvarurendering) via en
// separat, dold GL-kontext så att vi slipper röra scenografens kontext.
static QString currentRenderer()
{
    QOffscreenSurface surface;
    surface.setFormat(QSurfaceFormat::defaultFormat());
    surface.create();
    if (!surface.isValid())
        return QString();

    QOpenGLContext ctx;
    ctx.setFormat(surface.format());
    if (!ctx.create())
        return QString();
    if (!ctx.makeCurrent(&surface))
        return QString();

    QString renderer;
    const GLubyte *s = ctx.functions()->glGetString(GL_RENDERER);
    if (s)
        renderer = QString::fromUtf8(reinterpret_cast<const char *>(s));
    ctx.doneCurrent();
    return renderer;
}

// Sammanställer startproblem som gör att GPU-acceleration inte fungerar:
// saknad hårdvaruavkodare, saknad qml6glsink eller mjukvarurendering.
static QStringList startupProblems()
{
    QStringList problems;
    GstRegistry *registry = gst_registry_get();

#if defined(YTGST_VIDEO_DECODER)
    const char *decoderName = YTGST_VIDEO_DECODER;
#else
    const char *decoderName = "vah264dec";
#endif
    if (std::strlen(decoderName) > 0) {
        GstPluginFeature *feature = gst_registry_lookup_feature(registry, decoderName);
        if (feature) {
            gst_object_unref(feature);
        } else {
            problems << QStringLiteral(
                "Hårdvaruavkodaren \"%1\" hittades inte – installera "
                "gstreamer1.0-vaapi och en fungerande VA-API-drivrutin (t.ex. "
                "i965-va-driver eller intel-media-va-driver).")
                             .arg(QString::fromLatin1(decoderName));
        }
    }

    GstPluginFeature *sink = gst_registry_lookup_feature(registry, "qml6glsink");
    if (sink) {
        gst_object_unref(sink);
    } else {
        problems << QStringLiteral(
            "qml6glsink saknas – installera gstreamer1.0-qt6.");
    }

    const QString renderer = currentRenderer();
    const QString low = renderer.toLower();
    if (low.contains("llvmpipe") || low.contains("softpipe") || low.contains("swiftshader")) {
        problems << QStringLiteral(
            "Mjukvarurendering upptäckt (%1). YtGst vägrar köra utan "
            "fungerande GPU-drivrutin.")
                         .arg(renderer);
    }

    return problems;
}

int main(int argc, char *argv[])
{
    // VA-API-drivrutin: använd CMake-flaggan YTGST_VAAPI_DRIVER om den är satt,
    // annars auto-väljs i965 om den finns (Haswell), så vah264dec fungerar.
#ifdef YTGST_VAAPI_DRIVER
    const QString configuredVa = QStringLiteral(YTGST_VAAPI_DRIVER);
#else
    const QString configuredVa;
#endif
    if (!configuredVa.isEmpty()) {
        qputenv("LIBVA_DRIVER_NAME", configuredVa.toUtf8());
    } else if (QFile::exists(QStringLiteral("/usr/lib/x86_64-linux-gnu/dri/i965_drv_video.so"))) {
        qputenv("LIBVA_DRIVER_NAME", "i965");
    }

    gst_init(&argc, &argv);

    // Prioritera vald videodekoder (YTGST_VIDEO_DECODER, t.ex. vah264dec)
    // framför övriga, så playbin väljer hårdvaruavkodning i första hand.
#ifdef YTGST_VIDEO_DECODER
    {
        const QByteArray decoder = QByteArrayLiteral(YTGST_VIDEO_DECODER);
        if (!decoder.isEmpty()) {
            GstRegistry *registry = gst_registry_get();
            GstPluginFeature *feature = gst_registry_lookup_feature(registry, decoder.constData());
            if (feature) {
                gst_plugin_feature_set_rank(feature, GST_RANK_PRIMARY + 1);
                gst_object_unref(feature);
            } else {
                qWarning("Videodekodern \"%s\" hittades inte – varning visas vid start",
                         decoder.constData());
            }
        }
    }
#endif

    // qml6glsink-pluginet MÅSTE laddas innan QML-motorn skapas,
    // för att registrera GstGLQt6VideoItem-typen i QML.
    GstElement *sink = gst_element_factory_make("qml6glsink", nullptr);
    if (!sink) {
        qWarning("qml6glsink saknas – installation av gstreamer1.0-qt6 krävs");
    } else {
        gst_object_unref(sink);
    }

    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QApplication app(argc, argv);

    const QStringList problems = startupProblems();

    QString playId;
    QString playTitle;
    const QStringList args = QCoreApplication::arguments();
    const int playIndex = args.indexOf(QStringLiteral("--play"));
    if (playIndex >= 0 && playIndex + 1 < args.size())
        playId = args.at(playIndex + 1);
    const int titleIndex = args.indexOf(QStringLiteral("--title"));
    if (titleIndex >= 0 && titleIndex + 1 < args.size())
        playTitle = args.at(titleIndex + 1);

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
            root->setProperty("videoTitle", playTitle);
        }
    }

    // Visa en varningsruta vid start om GPU-acceleration inte fungerar.
    if (!problems.isEmpty()) {
        QTimer::singleShot(600, &app, [problems]() {
            QMessageBox *box = new QMessageBox(
                QMessageBox::Warning,
                QStringLiteral("YtGst – varning"),
                QStringLiteral("YtGst har upptäckt problem som hindrar "
                               "GPU-accelererad uppspelning:"),
                QMessageBox::Ok);
            box->setInformativeText(problems.join(QLatin1Char('\n')));
            box->setWindowModality(Qt::ApplicationModal);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->show();
        });
    }

    const int result = app.exec();

    gst_deinit();

    return result;
}

#include "main.moc"
