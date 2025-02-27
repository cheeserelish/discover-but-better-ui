/*
 *   SPDX-FileCopyrightText: 2012 Aleix Pol Gonzalez <aleixpol@blue-systems.com>
 *
 *   SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "DiscoverObject.h"
#include "CachedNetworkAccessManager.h"
#include "DiscoverBackendsFactory.h"
#include "DiscoverDeclarativePlugin.h"
#include "FeaturedModel.h"
#include "OdrsAppsModel.h"
#include "PaginateModel.h"
#include "UnityLauncher.h"
#include <Transaction/TransactionModel.h>

// Qt includes
#include "discover_debug.h"
#include <QClipboard>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDesktopServices>
#include <QFile>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSessionManager>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QtQuick/QQuickItem>
#include <qqml.h>

// KDE includes
#include <KAboutData>
#include <KAuthorized>
#include <KConfigGroup>
#include <KCrash>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <KOSRelease>
#include <KSharedConfig>
#include <KStatusNotifierItem>
#include <KUiServerV2JobTracker>
#include <kcoreaddons_version.h>
// #include <KSwitchLanguageDialog>

// DiscoverCommon includes
#include <Category/Category.h>
#include <Category/CategoryModel.h>
#include <resources/AbstractResource.h>
#include <resources/ResourcesModel.h>
#include <resources/ResourcesProxyModel.h>

#include <QMimeDatabase>
#include <cmath>
#include <functional>
#include <resources/StoredResultsStream.h>
#include <unistd.h>
#include <utils.h>

#ifdef WITH_FEEDBACK
#include "plasmauserfeedback.h"
#endif
#include "PowerManagementInterface.h"
#include "discoversettings.h"

class CachedNetworkAccessManagerFactory : public QQmlNetworkAccessManagerFactory
{
    virtual QNetworkAccessManager *create(QObject *parent) override
    {
        return new CachedNetworkAccessManager(QStringLiteral("images"), parent);
    }
};

class OurSortFilterProxyModel : public QSortFilterProxyModel, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
public:
    void classBegin() override
    {
    }
    void componentComplete() override
    {
        if (dynamicSortFilter())
            sort(0);
    }
};

DiscoverObject::DiscoverObject(CompactMode mode, const QVariantMap &initialProperties)
    : QObject()
    , m_engine(new QQmlApplicationEngine)
    , m_mode(mode)
    , m_networkAccessManagerFactory(new CachedNetworkAccessManagerFactory)
{
    setObjectName(QStringLiteral("DiscoverMain"));
    m_engine->rootContext()->setContextObject(new KLocalizedContext(m_engine));
    auto factory = m_engine->networkAccessManagerFactory();
    m_engine->setNetworkAccessManagerFactory(nullptr);
    delete factory;
    m_engine->setNetworkAccessManagerFactory(m_networkAccessManagerFactory.data());

    qmlRegisterType<UnityLauncher>("org.kde.discover.app", 1, 0, "UnityLauncher");
    qmlRegisterType<PaginateModel>("org.kde.discover.app", 1, 0, "PaginateModel");
    qmlRegisterType<FeaturedModel>("org.kde.discover.app", 1, 0, "FeaturedModel");
    qmlRegisterType<OdrsAppsModel>("org.kde.discover.app", 1, 0, "OdrsAppsModel");
    qmlRegisterType<PowerManagementInterface>("org.kde.discover.app", 1, 0, "PowerManagementInterface");
    qmlRegisterType<OurSortFilterProxyModel>("org.kde.discover.app", 1, 0, "QSortFilterProxyModel");
#ifdef WITH_FEEDBACK
    qmlRegisterSingletonType<PlasmaUserFeedback>("org.kde.discover.app", 1, 0, "UserFeedbackSettings", [](QQmlEngine *, QJSEngine *) -> QObject * {
        return new PlasmaUserFeedback(KSharedConfig::openConfig(QStringLiteral("PlasmaUserFeedback"), KConfig::NoGlobals));
    });
#endif
    qmlRegisterSingletonType<DiscoverSettings>("org.kde.discover.app", 1, 0, "DiscoverSettings", [](QQmlEngine *engine, QJSEngine *) -> QObject * {
        auto r = new DiscoverSettings;
        r->setParent(engine);
        connect(r, &DiscoverSettings::installedPageSortingChanged, r, &DiscoverSettings::save);
        connect(r, &DiscoverSettings::appsListPageSortingChanged, r, &DiscoverSettings::save);
        return r;
    });
    qmlRegisterAnonymousType<QQuickView>("org.kde.discover.app", 1);

    qmlRegisterAnonymousType<KAboutData>("org.kde.discover.app", 1);
    qmlRegisterAnonymousType<KAboutLicense>("org.kde.discover.app", 1);
    qmlRegisterAnonymousType<KAboutPerson>("org.kde.discover.app", 1);

    qmlRegisterUncreatableType<DiscoverObject>("org.kde.discover.app", 1, 0, "DiscoverMainWindow", QStringLiteral("don't do that"));

    auto uri = "org.kde.discover";
    DiscoverDeclarativePlugin *plugin = new DiscoverDeclarativePlugin;
    plugin->setParent(this);
    plugin->initializeEngine(m_engine, uri);
    plugin->registerTypes(uri);

    m_engine->setInitialProperties(initialProperties);
    m_engine->rootContext()->setContextProperty(QStringLiteral("app"), this);
    m_engine->rootContext()->setContextProperty(QStringLiteral("discoverAboutData"), QVariant::fromValue(KAboutData::applicationData()));

    connect(m_engine, &QQmlApplicationEngine::objectCreated, this, &DiscoverObject::integrateObject);
    m_engine->load(QUrl(QStringLiteral("qrc:/qml/DiscoverWindow.qml")));

    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() {
        const auto objs = m_engine->rootObjects();
        for (auto o : objs)
            delete o;
    });
    auto action = new OneTimeAction(
        [this]() {
            bool found = DiscoverBackendsFactory::hasRequestedBackends();
            const auto backends = ResourcesModel::global()->backends();
            for (auto b : backends)
                found |= b->hasApplications();

            if (!found) {
                QString errorText = i18n(
                    "Discover currently cannot be used to install any apps or "
                    "perform system updates because none of its app backends are "
                    "available.");
                QString errorExplanation = xi18nc("@info",
                    "You can install some on the Settings page, under the "
                    "<interface>Missing Backends</interface> section.<nl/><nl/>"
                    "Also please consider reporting this as a packaging issue to "
                    "your distribution.");
                QString buttonIcon = QStringLiteral("tools-report-bug");
                QString buttonText = i18n("Report This Issue");
                QString buttonUrl = KOSRelease().bugReportUrl();

                if (KOSRelease().name().contains(QStringLiteral("Arch Linux"))) {
                    errorExplanation = xi18nc("@info",
                        "You can use <command>pacman</command> to "
                        "install the optional dependencies that are needed to "
                        "enable the application backends.<nl/><nl/>Please note "
                        "that Arch Linux developers recommend using "
                        "<command>pacman</command> for managing software because "
                        "the PackageKit backend is not well-integrated on Arch "
                        "Linux.");
                    buttonIcon = QStringLiteral("help-about");
                    buttonText = i18n("Learn More");
                    buttonUrl = KOSRelease().supportUrl();
                }

                Q_EMIT openErrorPage(errorText, errorExplanation, buttonText, buttonIcon, buttonUrl);
            }
        },
        this);

    if (ResourcesModel::global()->backends().isEmpty()) {
        connect(ResourcesModel::global(), &ResourcesModel::allInitialized, action, &OneTimeAction::trigger);
    } else {
        action->trigger();
    }
}

DiscoverObject::~DiscoverObject()
{
    m_engine->deleteLater();
}

bool DiscoverObject::isRoot()
{
    return ::getuid() == 0;
}

QStringList DiscoverObject::modes() const
{
    QStringList ret;
    QObject *obj = rootObject();
    for (int i = obj->metaObject()->propertyOffset(); i < obj->metaObject()->propertyCount(); i++) {
        QMetaProperty p = obj->metaObject()->property(i);
        QByteArray compName = p.name();
        if (compName.startsWith("top") && compName.endsWith("Comp")) {
            ret += QString::fromLatin1(compName.mid(3, compName.length() - 7));
        }
    }
    return ret;
}

void DiscoverObject::openMode(const QString &_mode)
{
    QObject *obj = rootObject();
    if (!obj) {
        qCWarning(DISCOVER_LOG) << "could not get the main object";
        return;
    }

    if (!modes().contains(_mode, Qt::CaseInsensitive))
        qCWarning(DISCOVER_LOG) << "unknown mode" << _mode << modes();

    QString mode = _mode;
    mode[0] = mode[0].toUpper();

    const QByteArray propertyName = "top" + mode.toLatin1() + "Comp";
    const QVariant modeComp = obj->property(propertyName.constData());
    if (!modeComp.isValid())
        qCWarning(DISCOVER_LOG) << "couldn't open mode" << _mode;
    else
        obj->setProperty("currentTopLevel", modeComp);
}

void DiscoverObject::openMimeType(const QString &mime)
{
    Q_EMIT listMimeInternal(mime);
}

void DiscoverObject::showLoadingPage()
{
    QObject *obj = rootObject();
    if (!obj) {
        qCWarning(DISCOVER_LOG) << "could not get the main object";
        return;
    }

    obj->setProperty("currentTopLevel", QStringLiteral("qrc:/qml/LoadingPage.qml"));
}

void DiscoverObject::openCategory(const QString &category)
{
    showLoadingPage();
    auto action = new OneTimeAction(
        [this, category]() {
            Category *cat = CategoryModel::global()->findCategoryByName(category);
            if (cat) {
                Q_EMIT listCategoryInternal(cat);
            } else {
                openMode(QStringLiteral("Browsing"));
                showPassiveNotification(i18n("Could not find category '%1'", category));
            }
        },
        this);

    if (CategoryModel::global()->rootCategories().isEmpty()) {
        connect(CategoryModel::global(), &CategoryModel::rootCategoriesChanged, action, &OneTimeAction::trigger);
    } else {
        action->trigger();
    }
}

void DiscoverObject::openLocalPackage(const QUrl &localfile)
{
    if (!QFile::exists(localfile.toLocalFile())) {
        showPassiveNotification(i18n("Trying to open inexisting file '%1'", localfile.toString()));
        openMode(QStringLiteral("Browsing"));
        return;
    }
    showLoadingPage();
    auto action = new OneTimeAction(
        [this, localfile]() {
            AbstractResourcesBackend::Filters f;
            f.resourceUrl = localfile;
            auto stream = new StoredResultsStream({ResourcesModel::global()->search(f)});
            connect(stream, &StoredResultsStream::finishedResources, this, [this, localfile](const QVector<AbstractResource *> &res) {
                if (res.count() == 1) {
                    Q_EMIT openApplicationInternal(res.first());
                } else {
                    QMimeDatabase db;
                    auto mime = db.mimeTypeForUrl(localfile);
                    auto fIsFlatpakBackend = [](AbstractResourcesBackend *backend) {
                        return backend->metaObject()->className() == QByteArray("FlatpakBackend");
                    };
                    if (mime.name().startsWith(QLatin1String("application/vnd.flatpak"))
                        && !kContains(ResourcesModel::global()->backends(), fIsFlatpakBackend)) {
                        openApplication(QUrl(QStringLiteral("appstream://org.kde.discover.flatpak")));
                        showPassiveNotification(i18n("Cannot interact with flatpak resources without the flatpak backend %1. Please install it first.",
                                                     localfile.toDisplayString()));
                    } else {
                        openMode(QStringLiteral("Browsing"));
                        showPassiveNotification(i18n("Could not open %1", localfile.toDisplayString()));
                    }
                }
            });
        },
        this);

    if (ResourcesModel::global()->backends().isEmpty()) {
        connect(ResourcesModel::global(), &ResourcesModel::backendsChanged, action, &OneTimeAction::trigger);
    } else {
        action->trigger();
    }
}

void DiscoverObject::openApplication(const QUrl &url)
{
    Q_ASSERT(!url.isEmpty());
    showLoadingPage();
    auto action = new OneTimeAction(
        [this, url]() {
            AbstractResourcesBackend::Filters f;
            f.resourceUrl = url;
            auto stream = new StoredResultsStream({ResourcesModel::global()->search(f)});
            connect(stream, &StoredResultsStream::finishedResources, this, [this, url](const QVector<AbstractResource *> &res) {
                if (res.count() >= 1) {
                    QPointer<QTimer> timeout = new QTimer(this);
                    timeout->setSingleShot(true);
                    timeout->setInterval(20000);
                    connect(timeout, &QTimer::timeout, timeout, &QTimer::deleteLater);

                    auto openResourceOrWait = [this, res, timeout] {
                        auto idx = kIndexOf(res, [](auto res) {
                            return res->isInstalled();
                        });
                        if (idx < 0) {
                            bool oneBroken = kContains(res, [](auto res) {
                                return res->state() == AbstractResource::Broken;
                            });
                            if (oneBroken && timeout) {
                                return false;
                            }

                            idx = 0;
                        }
                        Q_EMIT openApplicationInternal(res[idx]);
                        return true;
                    };

                    if (!openResourceOrWait()) {
                        auto f = new OneTimeAction(0, openResourceOrWait, this);
                        for (auto r : res) {
                            if (r->state() == AbstractResource::Broken) {
                                connect(r, &AbstractResource::stateChanged, f, &OneTimeAction::trigger);
                            }
                        }
                        timeout->start();
                        connect(timeout, &QTimer::destroyed, f, &OneTimeAction::trigger);
                    } else {
                        delete timeout;
                    }
                } else if (url.scheme() == QLatin1String("snap")) {
                    openApplication(QUrl(QStringLiteral("appstream://org.kde.discover.snap")));
                    showPassiveNotification(i18n("Please make sure Snap support is installed"));
                } else {
                    const QString errorText = i18n("Could not open %1 because it "
                    "was not found in any available software repositories.",
                    url.toDisplayString());
                    const QString errorExplanation = i18n("Please report this "
                    "issue to the packagers of your distribution.");
                    QString buttonIcon = QStringLiteral("tools-report-bug");
                    QString buttonText = i18n("Report This Issue");
                    QString buttonUrl = KOSRelease().bugReportUrl();
                    Q_EMIT openErrorPage(errorText, errorExplanation, buttonText, buttonIcon, buttonUrl);
                }
            });
        },
        this);

    if (ResourcesModel::global()->backends().isEmpty()) {
        connect(ResourcesModel::global(), &ResourcesModel::backendsChanged, action, &OneTimeAction::trigger);
    } else {
        action->trigger();
    }
}

class TransactionsJob : public KJob
{
public:
    void start() override
    {
        // no-op, this is just observing

        setTotalAmount(Items, TransactionModel::global()->rowCount());
        setPercent(TransactionModel::global()->progress());
        connect(TransactionModel::global(), &TransactionModel::lastTransactionFinished, this, &TransactionsJob::emitResult);
        connect(TransactionModel::global(), &TransactionModel::transactionRemoved, this, &TransactionsJob::refreshInfo);
        connect(TransactionModel::global(), &TransactionModel::progressChanged, this, [this] {
            setPercent(TransactionModel::global()->progress());
        });
        refreshInfo();
    }

    void refreshInfo()
    {
        if (TransactionModel::global()->rowCount() == 0) {
            return;
        }

        setProcessedAmount(Items, totalAmount(Items) - TransactionModel::global()->rowCount() + 1);
        auto firstTransaction = TransactionModel::global()->transactions().constFirst();
        Q_EMIT description(this, firstTransaction->name());
    }

    void cancel()
    {
        setError(KJob::KilledJobError /*KIO::ERR_USER_CANCELED*/);
        deleteLater();
    }
};

bool DiscoverObject::quitWhenIdle()
{
    if (!ResourcesModel::global()->isBusy()) {
        return true;
    }

    if (!m_sni) {
        auto tracker = new KUiServerV2JobTracker(m_sni);

        m_sni = new KStatusNotifierItem(this);
        m_sni->setStatus(KStatusNotifierItem::Active);
        m_sni->setIconByName("plasmadiscover");
        m_sni->setTitle(i18n("Discover"));
        m_sni->setToolTip("process-working-symbolic", i18n("Discover"), i18n("Discover was closed before certain tasks were done, waiting for it to finish."));
        m_sni->setStandardActionsEnabled(false);

        connect(TransactionModel::global(), &TransactionModel::countChanged, this, &DiscoverObject::reconsiderQuit);
        connect(m_sni, &KStatusNotifierItem::activateRequested, this, &DiscoverObject::restore);

        auto job = new TransactionsJob;
        job->setParent(this);
        tracker->registerJob(job);
        job->start();
        connect(m_sni, &KStatusNotifierItem::activateRequested, job, &TransactionsJob::cancel);

        rootObject()->hide();
    }
    return false;
}

void DiscoverObject::restore()
{
    if (!m_sni) {
        return;
    }

    disconnect(TransactionModel::global(), &TransactionModel::countChanged, this, &DiscoverObject::reconsiderQuit);
    disconnect(m_sni, &KStatusNotifierItem::activateRequested, this, &DiscoverObject::restore);

    rootObject()->show();
    m_sni->deleteLater();
    m_sni = nullptr;
}

void DiscoverObject::reconsiderQuit()
{
    if (ResourcesModel::global()->isBusy()) {
        return;
    }

    m_sni->deleteLater();
    // Let the job UI to finalise properly
    QTimer::singleShot(20, qGuiApp, &QCoreApplication::quit);
}

void DiscoverObject::integrateObject(QObject *object)
{
    if (!object) {
        qCWarning(DISCOVER_LOG) << "Errors when loading the GUI";
        QTimer::singleShot(0, QCoreApplication::instance(), []() {
            QCoreApplication::instance()->exit(1);
        });
        return;
    }

    Q_ASSERT(object == rootObject());

    KConfigGroup window(KSharedConfig::openConfig(), "Window");
    if (window.hasKey("geometry"))
        rootObject()->setGeometry(window.readEntry("geometry", QRect()));
    if (window.hasKey("visibility")) {
        QWindow::Visibility visibility(QWindow::Visibility(window.readEntry<int>("visibility", QWindow::Windowed)));
        rootObject()->setVisibility(qMax(visibility, QQuickView::AutomaticVisibility));
    }
    connect(rootObject(), &QQuickView::sceneGraphError, this, [](QQuickWindow::SceneGraphError /*error*/, const QString &message) {
        KCrash::setErrorMessage(message);
        qFatal("%s", qPrintable(message));
    });

    object->installEventFilter(this);
    connect(object, &QObject::destroyed, qGuiApp, &QCoreApplication::quit);

    object->setParent(m_engine);
    connect(qGuiApp, &QGuiApplication::commitDataRequest, this, [this](QSessionManager &sessionManager) {
        if (!quitWhenIdle()) {
            sessionManager.cancel();
        }
    });
}

bool DiscoverObject::eventFilter(QObject *object, QEvent *event)
{
    if (object != rootObject())
        return false;

    if (event->type() == QEvent::Close) {
        if (!quitWhenIdle()) {
            return true;
        }

        KConfigGroup window(KSharedConfig::openConfig(), "Window");
        window.writeEntry("geometry", rootObject()->geometry());
        window.writeEntry<int>("visibility", rootObject()->visibility());
        // } else if (event->type() == QEvent::ShortcutOverride) {
        //     qCWarning(DISCOVER_LOG) << "Action conflict" << event;
    }
    return false;
}

QString DiscoverObject::iconName(const QIcon &icon)
{
    return icon.name();
}

void DiscoverObject::switchApplicationLanguage()
{
    // auto langDialog = new KSwitchLanguageDialog(nullptr);
    // connect(langDialog, SIGNAL(finished(int)), this, SLOT(dialogFinished()));
    // langDialog->show();
}

void DiscoverObject::setCompactMode(DiscoverObject::CompactMode mode)
{
    if (m_mode != mode) {
        m_mode = mode;
        Q_EMIT compactModeChanged(m_mode);
    }
}

class DiscoverTestExecutor : public QObject
{
public:
    DiscoverTestExecutor(QObject *rootObject, QQmlEngine *engine, const QUrl &url)
        : QObject(engine)
    {
        connect(engine, &QQmlEngine::quit, this, &DiscoverTestExecutor::finish, Qt::QueuedConnection);

        QQmlComponent *component = new QQmlComponent(engine, url, engine);
        m_testObject = component->create(engine->rootContext());

        if (!m_testObject) {
            qCWarning(DISCOVER_LOG) << "error loading test" << url << m_testObject << component->errors();
            Q_ASSERT(false);
        }

        m_testObject->setProperty("appRoot", QVariant::fromValue<QObject *>(rootObject));
        connect(engine, &QQmlEngine::warnings, this, &DiscoverTestExecutor::processWarnings);
    }

    void processWarnings(const QList<QQmlError> &warnings)
    {
        for (const QQmlError &warning : warnings) {
            if (warning.url().path().endsWith(QLatin1String("DiscoverTest.qml"))) {
                qCWarning(DISCOVER_LOG) << "Test failed!" << warnings;
                qGuiApp->exit(1);
            }
        }
        m_warnings << warnings;
    }

    void finish()
    {
        if (m_warnings.isEmpty())
            qCDebug(DISCOVER_LOG) << "cool no warnings!";
        else
            qCDebug(DISCOVER_LOG) << "test finished successfully despite" << m_warnings;
        qGuiApp->exit(m_warnings.count());
    }

private:
    QObject *m_testObject;
    QList<QQmlError> m_warnings;
};

void DiscoverObject::loadTest(const QUrl &url)
{
    new DiscoverTestExecutor(rootObject(), engine(), url);
}

QQuickWindow *DiscoverObject::rootObject() const
{
    return qobject_cast<QQuickWindow *>(m_engine->rootObjects().at(0));
}

void DiscoverObject::showPassiveNotification(const QString &msg)
{
    QTimer::singleShot(100, this, [this, msg]() {
        QMetaObject::invokeMethod(rootObject(),
                                  "showPassiveNotification",
                                  Qt::QueuedConnection,
                                  Q_ARG(QVariant, msg),
                                  Q_ARG(QVariant, {}),
                                  Q_ARG(QVariant, {}),
                                  Q_ARG(QVariant, {}));
    });
}

void DiscoverObject::copyTextToClipboard(const QString text)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(text);
}

void DiscoverObject::reboot()
{
    auto method = QDBusMessage::createMethodCall(QStringLiteral("org.kde.LogoutPrompt"),
                                                 QStringLiteral("/LogoutPrompt"),
                                                 QStringLiteral("org.kde.LogoutPrompt"),
                                                 QStringLiteral("promptReboot"));
    QDBusConnection::sessionBus().asyncCall(method);
}

void DiscoverObject::rebootNow()
{
    auto method = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
                                                 QStringLiteral("/org/freedesktop/login1"),
                                                 QStringLiteral("org.freedesktop.login1.Manager"),
                                                 QStringLiteral("Reboot"));
    method.setArguments({true /*interactive*/});
    QDBusConnection::systemBus().asyncCall(method);
}

QRect DiscoverObject::initialGeometry() const
{
    KConfigGroup window(KSharedConfig::openConfig(), "Window");
    return window.readEntry("geometry", QRect());
}

QString DiscoverObject::describeSources() const
{
    return rootObject()->property("describeSources").toString();
}

#include "DiscoverObject.moc"
