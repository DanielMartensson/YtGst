#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
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
#include <QRegularExpression>
#include <QVulkanInstance>
#include <QVersionNumber>

#include <cstring>
#include <clocale>
#include <langinfo.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include <gst/gst.h>

#ifndef GL_RENDERER
#define GL_RENDERER 0x1F01
#endif

// NOTE: The target's Qt 6.8 (OpenSTLinux) is a trimmed/renamed Qt build.
// This code must therefore avoid the standard Qt 6 APIs that are missing
// there: QString::leftOfLastPathSeparator()/leftPart(),
// QIODevice::AppendOnly, QFile::openStatus()/QFile::Ok, and
// QQmlEngine::addContextProperty(). Context properties are set through
// QQmlEngine::rootContext()->setContextProperty() instead.

static QString g_graphicsApi = QStringLiteral("OpenGL");
static QString g_graphicsReason = {};

// Which backend to use when YTGST_RENDERER says nothing. Set at build time
// (YTGST_DEFAULT_RENDERER), the same way NanoBrowser does it, so a board whose
// driver prefers one backend does not have to export a variable in every
// launcher.
#ifndef YTGST_DEFAULT_RENDERER
#define YTGST_DEFAULT_RENDERER "auto"
#endif

static QByteArray rendererFromEnv()
{
    return qgetenv("YTGST_RENDERER").toLower();
}

// Reports true when an unstable Vulkan ICD (nouveau/hasvk) is installed. Those
// ICDs load but produce a broken rendering path, so we keep them out and fall
// back to OpenGL instead.
static bool unstableVulkanIcdPresent()
{
    const QDir::Filters filters = QDir::Files;
    const QStringList filter({QStringLiteral("*.json")});
    const char *const icdDirs[] = {
        "/usr/local/share/vulkan/icd.d",
        "/usr/share/vulkan/icd.d",
        "/etc/vulkan/icd.d",
    };
    const char *const unstableNames[] = { "nouveau", "hasvk" };

    const QString envIcds = QString::fromUtf8(qgetenv("VK_ICD_FILENAMES"));
    for (const QString &icd : envIcds.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        const QString name = QFileInfo(icd).fileName();
        for (const char *u : unstableNames)
            if (name.contains(QLatin1String(u), Qt::CaseInsensitive))
                return true;
    }

    for (const char *dir : icdDirs) {
        const QDir d{QLatin1String(dir)};
        if (!d.exists())
            continue;
        const QStringList icds = d.entryList(filter, filters);
        for (const QString &name : icds)
            for (const char *u : unstableNames)
                if (name.contains(QLatin1String(u), Qt::CaseInsensitive))
                    return true;
    }
    return false;
}

// Enumerates the physical devices and reports true when a real GPU (not a CPU
// type, not llvmpipe, not NVK/nouveau) is present.
static bool vulkanHardwareAvailable(QVulkanInstance &instance)
{
    PFN_vkEnumeratePhysicalDevices enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        instance.getInstanceProcAddr("vkEnumeratePhysicalDevices"));
    PFN_vkGetPhysicalDeviceProperties getProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        instance.getInstanceProcAddr("vkGetPhysicalDeviceProperties"));
    if (!enumerate || !getProperties)
        return false;

    uint32_t count = 0;
    if (enumerate(instance.vkInstance(), &count, nullptr) != VK_SUCCESS || count == 0)
        return false;

    QVarLengthArray<VkPhysicalDevice, 8> devices(count);
    if (enumerate(instance.vkInstance(), &count, devices.data()) != VK_SUCCESS)
        return false;

    bool sawHardware = false;
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties props{};
        getProperties(device, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
            continue;
        const QByteArray name = QByteArray(props.deviceName);
        if (name.contains("llvmpipe"))
            continue;
        if (name.contains("NVK") || name.contains("nouveau"))
            return false;
        sawHardware = true;
    }
    return sawHardware;
}

// Requires a 1.2+ instance. The legacy GCNANO/VeriSilicon ICD on the Weston
// target reports only loader interface v2 and does not expose
// vkEnumerateInstanceVersion; the loader then falls back to a 1.0-level
// instance. Requiring 1.2+ keeps that broken probe path out and enables real
// Vulkan once a fully conformant ICD is present. The target Vulkan headers
// declare vkEnumerateInstanceVersion with a single uint32_t* argument and
// define VK_API_VERSION_1_2.
static bool vulkanInstanceUsable(QVulkanInstance &instance)
{
    PFN_vkEnumerateInstanceVersion enumerate = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        instance.getInstanceProcAddr("vkEnumerateInstanceVersion"));
    if (!enumerate)
        return false;

    uint32_t supportedVersion = 0;
    if (enumerate(&supportedVersion) != VK_SUCCESS)
        return false;
    return supportedVersion >= VK_API_VERSION_1_2;
}

// All of Qt's platform plugins can create a QPlatformIntegration, but only some
// can also create a platform *Vulkan* instance (createPlatformVulkanInstance()).
// On the STM32MP2 (OpenSTLinux) image the default Wayland plugin,
// libqwayland-generic.so, cannot - so QVulkanInstance::create() fails and Qt
// Quick silently falls back to OpenGL even though the VeriSilicon GPU exposes
// Vulkan. Its sibling libqwayland-egl.so does implement it.
//
// Prefer the EGL variant when we are on Wayland and the user has not chosen a
// platform themselves. QT_QPA_PLATFORM still wins.
static void preferVulkanCapablePlatform()
{
    if (!qgetenv("QT_QPA_PLATFORM").isEmpty())
        return;
    if (qgetenv("WAYLAND_DISPLAY").isEmpty() && qgetenv("WAYLAND_SOCKET").isEmpty())
        return;

    // Deliberately avoid QLibraryInfo here (the target Qt build is trimmed) and
    // this has to run before QGuiApplication exists. Qt's own plugin search
    // paths plus the well-known target locations are enough.
    QStringList pluginDirs = QCoreApplication::libraryPaths();
    pluginDirs << QStringLiteral("/usr/lib/plugins")
               << QStringLiteral("/usr/lib/qt6/plugins")
               << QStringLiteral("/usr/lib/x86_64-linux-gnu/qt6/plugins");
    for (const QString &dir : pluginDirs) {
        if (QFile::exists(dir + QStringLiteral("/platforms/libqwayland-egl.so"))) {
            qputenv("QT_QPA_PLATFORM", "wayland-egl");
            qInfo("YtGst: platform plugin wayland-egl (can create a Vulkan instance)");
            return;
        }
    }
}

// Picks the scene graph's RHI graphics API before the application is
// constructed. The picture is drawn by waylandsink into a Wayland sub-surface,
// so the RHI only affects the window content around it - it does not touch the
// decoding or the picture itself.
//
// The default is chosen at build time (YTGST_DEFAULT_RENDERER, "auto" unless a
// recipe says otherwise). "auto" takes Vulkan when the driver looks usable,
// because the VeriSilicon driver implements Vulkan 1.3 and Qt Quick's Vulkan
// scene graph renders and submits frames on it.
//
// An earlier version of this player had to stay on OpenGL: with the picture
// inside the scene graph (qml6glsink/GstGL) a Vulkan window froze the UI as soon
// as it was exposed or restacked - the Qt render thread blocked in the driver
// while dispatching Wayland events and the GUI thread waited for it. That was a
// GstGL/interop problem, and it went away with the move to waylandsink, which
// never enters the scene graph. Measured on the STM32MP257F, OpenGL is the
// faster backend for the panel (first frame 0.79 s against 4.06 s, see the
// README), which is why the board build picks it; YTGST_RENDERER=vulkan|opengl
// overrides the choice for a run.
static void useOpenGL(const QString &reason, bool fallback)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);
    g_graphicsApi = QStringLiteral("OpenGL");
    g_graphicsReason = reason;
    qInfo("YtGst: using OpenGL renderer (%s)%s", reason.toUtf8().constData(),
          fallback ? " - Vulkan was not usable" : "");
}

static void useVulkan()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    g_graphicsApi = QStringLiteral("Vulkan");
    g_graphicsReason.clear();
    qInfo("YtGst: using Vulkan renderer");
}

static void pickGraphicsApi()
{
    const QByteArray forced = rendererFromEnv();
    const QString wanted = forced.isEmpty()
        ? QStringLiteral(YTGST_DEFAULT_RENDERER)
        : QString::fromLatin1(forced);

    if (wanted == QLatin1String("opengl")) {
        useOpenGL(forced.isEmpty() ? QStringLiteral("the build's choice")
                                   : QStringLiteral("forced via YTGST_RENDERER"),
                  false);
        return;
    }

    // "vulkan" asks for Vulkan even on a driver that the auto path would
    // reject; "auto" only takes Vulkan when it looks usable.
    const bool explicitVulkan = (wanted == QLatin1String("vulkan"));
    if (!explicitVulkan && unstableVulkanIcdPresent()) {
        useOpenGL(QStringLiteral("unstable Vulkan drivers detected (nouveau/hasvk)"), true);
        return;
    }

    QVulkanInstance probe;
    // The VeriSilicon ICD on the target refuses a VkApplicationInfo whose
    // apiVersion is 0 ("value of 0 which is not permitted") and prints an ERROR
    // line about it before the window exists. Qt leaves the field at 0 unless a
    // version is set, so ask for the lowest version Vulkan defines - this probe
    // only enumerates devices, it never creates one.
    probe.setApiVersion(QVersionNumber(1, 0));
    const bool instanceCreated = probe.create();
    const bool hardwareSuitable = instanceCreated && vulkanHardwareAvailable(probe);
    const bool instanceUsable = hardwareSuitable && vulkanInstanceUsable(probe);

    // Even an explicit YTGST_RENDERER=vulkan falls back: an instance Qt cannot
    // use would leave the window without a renderer, and the picture would
    // still play but the panel would not be drawn.
    if (!instanceUsable) {
        const QString reason =
            !instanceCreated
                ? QStringLiteral("could not create a Vulkan instance")
                : !hardwareSuitable
                    ? QStringLiteral("no supported Vulkan GPU")
                    : QStringLiteral("Vulkan instance below 1.2 (ICD does not expose vkEnumerateInstanceVersion)");
        useOpenGL(reason, !explicitVulkan);
        return;
    }

    useVulkan();
}

// Starts the player as a separate process with its own QQuickWindow/GL context.
// The main window ("YtGst") stays open while "YtGst Player" plays.
//
// The players are kept as children of this process: a video window must not
// outlive the window that started it, and a second click on a video replaces
// the first player instead of stacking another one on top of it.

class Launcher : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    void stopPlayers()
    {
        const QList<QProcess *> players = m_players;
        m_players.clear();
        for (QProcess *player : players) {
            if (player->state() != QProcess::NotRunning) {
                player->terminate();
                if (!player->waitForFinished(3000))
                    player->kill();
            }
            player->deleteLater();
        }
    }

    // Closing the search window quits the application. It is done here rather
    // than by letting the window close on its own: the Quick window's teardown
    // races the Vulkan driver's WSI thread, which makes the process die with a
    // SIGSEGV instead of a clean exit (and then the players it started are
    // orphaned).
    Q_INVOKABLE void quit()
    {
        qInfo("YtGst: closing the search window");
        stopPlayers();
        QCoreApplication::quit();
    }

    Q_INVOKABLE void play(const QString &videoId, const QString &title)
    {
        if (videoId.isEmpty())
            return;
        stopPlayers();
        auto *player = new QProcess(this);
        player->setProgram(QCoreApplication::applicationFilePath());
        player->setArguments({QStringLiteral("--play"), videoId,
                              QStringLiteral("--title"), title,
                              QStringLiteral("--parent-pid"),
                              QString::number(QCoreApplication::applicationPid())});
        player->start();
        m_players.append(player);
    }

private:
    QList<QProcess *> m_players;
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
// missing hardware decoder, missing waylandsink or software rendering.
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

    GstPluginFeature *sink = gst_registry_lookup_feature(registry, "waylandsink");
    if (sink) {
        gst_object_unref(sink);
    } else {
        problems << QStringLiteral(
            "waylandsink is missing – install gstreamer1.0-plugins-good.");
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

// Raises the rank of a configured decoder (YTGST_VIDEO_DECODER or
// YTGST_AUDIO_DECODER, see CMakeLists.txt), so the elements that pick a decoder
// themselves - the audio playbin, or uridecodebin in the video path - prefer it.
static void raiseDecoderRank(const QByteArray &name)
{
    if (name.isEmpty())
        return;
    GstRegistry *registry = gst_registry_get();
    GstPluginFeature *feature = gst_registry_lookup_feature(registry, name.constData());
    if (!feature) {
        qWarning("Decoder \"%s\" was not found", name.constData());
        return;
    }
    gst_plugin_feature_set_rank(feature, GST_RANK_PRIMARY + 1);
    gst_object_unref(feature);
}

// Extracts a YouTube video ID from a full URL (or a bare 11-char ID).
// Returns an empty string when nothing could be extracted.
static QString videoIdFrom(const QString &text)
{
    if (text.isEmpty())
        return text;
    if (text.size() == 11 && QRegularExpression("^[A-Za-z0-9_-]{11}$").match(text).hasMatch())
        return text;
    const QRegularExpression re(
        QStringLiteral("(?i)\\b(?:youtube\\.com/(?:v/|watch\\?v=|embed/|shorts/)"
                       "|youtu\\.be/)([A-Za-z0-9_-]{11})"));
    const QRegularExpressionMatch match = re.match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}

// ---------------------------------------------------------------------------
// A UTF-8 character type
// ---------------------------------------------------------------------------
//
// The image ships en_US.UTF-8 in /usr/lib/locale/locale-archive, but sessions
// start with LANG=C, and Qt then warns that it depends on a UTF-8 locale. That
// warning is printed once per Qt process, and Qt's text handling really does
// want UTF-8: with an ASCII character type, QString::fromLocal8Bit() mangles
// non-ASCII paths, titles and environment values.
//
// Only LC_CTYPE is changed. Number, date and collation formats keep following
// whatever the session asks for, and LANG is left alone, so the language used
// for search results and subtitles does not change.
static void preferUtf8CharacterType()
{
    if (setlocale(LC_CTYPE, "") == nullptr)
        return;
    const QByteArray codeset = nl_langinfo(CODESET);
    if (codeset == "UTF-8")
        return;
    if (setlocale(LC_CTYPE, "en_US.UTF-8") == nullptr)
        return;   // no UTF-8 locale in the image; nothing to switch to
    qputenv("LC_CTYPE", "en_US.UTF-8");
    qInfo("YtGst: character type was %s, using en_US.UTF-8", codeset.constData());
}

int main(int argc, char *argv[])
{
    // The platform plugin must be chosen before the application is constructed
    // (it decides how the Vulkan instance is created).
    preferVulkanCapablePlatform();
    preferUtf8CharacterType();

    // The scene graph (the control panel) renders with the chosen RHI. The
    // video is drawn by waylandsink in its own Weston window, so no OpenGL
    // context sharing is required for the picture.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

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

    // Prioritise the configured decoders (YTGST_VIDEO_DECODER and
    // YTGST_AUDIO_DECODER, for example vah264dec and avdec_aac) over the others.
    // The audio stream is decoded by a playbin, which picks a decoder itself, so
    // the rank is what steers it; the video decoder is named explicitly in the
    // video chain as well (see buildPipeline()).
#ifdef YTGST_VIDEO_DECODER
    raiseDecoderRank(QByteArrayLiteral(YTGST_VIDEO_DECODER));
#endif
#ifdef YTGST_AUDIO_DECODER
    raiseDecoderRank(QByteArrayLiteral(YTGST_AUDIO_DECODER));
#endif

    QApplication app(argc, argv);

    // The RHI backend must be picked before the first QQuickWindow is created,
    // but NOT before the application object exists: pickGraphicsApi() probes
    // with QVulkanInstance::create(), which asks the platform plugin for
    // createPlatformVulkanInstance() and therefore needs a QGuiApplication.
    // Calling it earlier on this Qt build crashes inside
    // QVulkanInstancePrivate::ensureVulkan(). NanoBrowser orders the two the
    // same way (create the application, then pick the backend).
    pickGraphicsApi();

    const QStringList problems = startupProblems();

    QString playId;
    QString playTitle;
    const QStringList args = QCoreApplication::arguments();
    const int watchIndex = args.indexOf(QStringLiteral("--watch"));
    const int playIndex = args.indexOf(QStringLiteral("--play"));
    const int titleIndex = args.indexOf(QStringLiteral("--title"));
    // --watch <URL> plays directly in one window (standalone PlayerWindow),
    // e.g.: ytgst --watch https://www.youtube.com/watch?v=abc123xyz
    if (watchIndex >= 0 && watchIndex + 1 < args.size()) {
        const QString url = args.at(watchIndex + 1);
        const QString id = videoIdFrom(url);
        if (id.isEmpty()) {
            qWarning("YtGst: no video ID found in --watch URL \"%1\"", url);
        } else {
            playId = id;
            playTitle = url;
        }
    } else if (playIndex >= 0 && playIndex + 1 < args.size())
        playId = videoIdFrom(args.at(playIndex + 1));
    if (titleIndex >= 0 && titleIndex + 1 < args.size())
        playTitle = args.at(titleIndex + 1);

    qint64 parentPid = 0;
    const int parentIndex = args.indexOf(QStringLiteral("--parent-pid"));
    if (parentIndex >= 0 && parentIndex + 1 < args.size())
        parentPid = args.at(parentIndex + 1).toLongLong();

    // A player belongs to the search window that started it. If that window
    // disappears without stopping its players - closed, or killed before it
    // could - the player must not stay behind with its windows on screen.
    if (parentPid > 0) {
        auto *watchdog = new QTimer(&app);
        watchdog->setInterval(2000);
        QObject::connect(watchdog, &QTimer::timeout, &app, [parentPid]() {
            if (::getppid() != parentPid)
                QCoreApplication::quit();
        });
        watchdog->start();
    }

    Launcher launcher;
    // The search window owns the players: closing it closes the video window
    // too, instead of leaving it (and its invisible panel) on screen.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &launcher,
                     [&launcher]() { launcher.stopPlayers(); });

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
            // The panel shows itself (and the picture's window first) once
            // standalone is set - see PlayerWindow.qml.
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

    // Leave without tearing the application down: destroying the Quick windows
    // makes the Vulkan driver's WSI thread and GStreamer's waylandsink thread
    // run their teardown at the same time as the rest of the process, which
    // aborts in glibc's robust-mutex check. The compositor removes the windows
    // as soon as the Wayland connection closes, so there is nothing to gain
    // from that teardown - and gst_deinit() is equally unnecessary here.
    fflush(nullptr);
    ::_exit(result);
}

#include "main.moc"
