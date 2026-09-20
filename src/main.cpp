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

// Picks the scene graph's RHI graphics API before the application is
// constructed. OpenGLRhi is the only backend YtGst uses – it is adapted for
// OpenGL and OpenGL ES (as used on the STM32MP257F), which is what the
// qml6glsink video sink requires to draw GStreamer frames in the scene graph.
static void pickGraphicsApi()
{
    qInfo().noquote() << "YtGst: using OpenGL RHI renderer";
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);
}

// Starts the player as a separate process with its own QQuickWindow/GL context.
// The main window ("YtGst") stays open while "YtGst Player" plays.
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

// Reads the GL renderer (for example "llvmpipe" when rendering in software)
// through a separate hidden GL context, so we do not touch the scene graph's
// context.
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

// Collects startup problems that prevent GPU acceleration:
// missing hardware decoder, missing qml6glsink or software rendering.
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
                "The hardware decoder \"%1\" was not found – install "
                "gstreamer1.0-vaapi and a working VA-API driver (for example "
                "i965-va-driver or intel-media-va-driver).")
                             .arg(QString::fromLatin1(decoderName));
        }
    }

    GstPluginFeature *sink = gst_registry_lookup_feature(registry, "qml6glsink");
    if (sink) {
        gst_object_unref(sink);
    } else {
        problems << QStringLiteral(
            "qml6glsink is missing – install gstreamer1.0-qt6.");
    }

    const QString renderer = currentRenderer();
    const QString low = renderer.toLower();
    if (low.contains("llvmpipe") || low.contains("softpipe") || low.contains("swiftshader")) {
        problems << QStringLiteral(
            "Software rendering detected (%1). YtGst refuses to run without a "
            "working GPU driver.")
                         .arg(renderer);
    }

    return problems;
}

int main(int argc, char *argv[])
{
    // qml6glsink must share an OpenGL context with the Qt scene graph, so the
    // scene graph can draw the GStreamer GLMemory textures in the PlayerWindow.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // The RHI backend must be picked before the application is constructed.
    pickGraphicsApi();

    // VA-API driver: use the YTGST_VAAPI_DRIVER CMake flag if set, otherwise
    // i965 is auto-selected if present (Haswell), so vah264dec works.
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

    // Prioritise the configured video decoder (YTGST_VIDEO_DECODER, for example
    // vah264dec) over the others, so playbin chooses hardware decoding first.
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
                qWarning("Video decoder \"%s\" not found – a warning is shown at startup",
                         decoder.constData());
            }
        }
    }
#endif

    // The qml6glsink plugin MUST be loaded before the QML engine is created,
    // to register the GstGLQt6VideoItem type in QML.
    GstElement *sink = gst_element_factory_make("qml6glsink", nullptr);
    if (!sink) {
        qWarning("qml6glsink is missing – install gstreamer1.0-qt6");
    } else {
        gst_object_unref(sink);
    }

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
        engine.loadFromModule(QStringLiteral("Ytgst"), QStringLiteral("Main"));
    } else {
        engine.loadFromModule(QStringLiteral("Ytgst"), QStringLiteral("PlayerWindow"));
        if (!engine.rootObjects().isEmpty()) {
            QObject *root = engine.rootObjects().first();
            root->setProperty("standalone", true);
            root->setProperty("visible", true);
            root->setProperty("videoId", playId);
            root->setProperty("videoTitle", playTitle);
        }
    }

    // Show a warning dialog at startup if GPU acceleration is unavailable.
    if (!problems.isEmpty()) {
        QTimer::singleShot(600, &app, [problems]() {
            QMessageBox *box = new QMessageBox(
                QMessageBox::Warning,
                QStringLiteral("YtGst – warning"),
                QStringLiteral("YtGst detected problems that prevent "
                               "GPU-accelerated playback:"),
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
