#include "coreapplication.h"
#include "coreapplication_p.h"

#include "logs.h"
#include "settings.h"
#include "updatemanager.h"
#include "settingsgroup.h"
#include "proofglobal.h"
#include "expirator.h"
#include "errornotifier.h"
#include "memorystoragenotificationhandler.h"
#include "helpers/versionhelper.h"

#include <QDir>
#include <QLocale>

#if (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)) || defined(Q_OS_MAC)
# include <unistd.h>
# include <cxxabi.h>
# include <execinfo.h>
# include <signal.h>
# include <sys/ucontext.h>
# include <stdio.h>
# include <fcntl.h>
# include <ctime>
#endif

namespace {
Proof::CoreApplication *&instance()
{
    static Proof::CoreApplication *obj = nullptr;
    return obj;
}

QVector<std::function<void()> > &initializers()
{
    static QVector<std::function<void()>> obj;
    return obj;
}
QMap<quint64, QList<Proof::CoreApplication::Migration>> &migrations()
{
    static QMap<quint64, QList<Proof::CoreApplication::Migration>> obj;
    return obj;
}
}

#if (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)) || defined(Q_OS_MAC)
constexpr int BACKTRACE_MAX_SIZE = 50;

static void signalHandler(int sig, siginfo_t *info, void *context)
{
    static bool handlerAlreadyCalled = false;
    if (handlerAlreadyCalled)
        return;
    handlerAlreadyCalled = true;

    alarm(10);
    char *homeDir = getenv("HOME");
    QString crashFileName = QStringLiteral("%1/proof_crash_%2").arg(homeDir ? homeDir : "/tmp").arg(time(0)); // clazy:skip=qstring-allocations
    int crashFileDescriptor = open(crashFileName.toLatin1().constData(), O_WRONLY | O_TRUNC | O_CREAT, 0644);
    ucontext_t *uc = (ucontext_t *)context;
# ifdef Q_OS_LINUX
    void *caller = (void *) uc->uc_mcontext.fpregs->rip;
# else
    void *caller = (void *) uc->uc_mcontext->__ss.__rip;
# endif

    QString toLog = QStringLiteral("#######################################");
    if (crashFileDescriptor != -1) {
        write(crashFileDescriptor, toLog.toLatin1().constData(), toLog.length());
        write(crashFileDescriptor, "\n", 1);
    }
    write(STDOUT_FILENO, toLog.toLatin1().constData(), toLog.length());
    write(STDOUT_FILENO, "\n", 1);
    toLog = QStringLiteral("signal %1 (%2), address is 0x%3 from 0x%4")
            .arg(sig)
            .arg(strsignal(sig))
            .arg((unsigned long long) info->si_addr, 0, 16)
            .arg((unsigned long long) caller, 0, 16);
    if (crashFileDescriptor != -1) {
        write(crashFileDescriptor, toLog.toLatin1().constData(), toLog.length());
        write(crashFileDescriptor, "\n", 1);
    }
    write(STDOUT_FILENO, toLog.toLatin1().constData(), toLog.length());
    write(STDOUT_FILENO, "\n", 1);

    void *backtraceInfo[BACKTRACE_MAX_SIZE];
    int size = backtrace(backtraceInfo, BACKTRACE_MAX_SIZE);

    backtraceInfo[0] = backtraceInfo[1];
    backtraceInfo[1] = caller;

    char **backtraceArray = backtrace_symbols(backtraceInfo, size);

    if (!backtraceArray) {
        if (crashFileDescriptor != -1)
            close(crashFileDescriptor);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < size; ++i) {
# ifdef Q_OS_LINUX
        QRegExp re("^(.+)\\((.*)\\+([x0-9a-fA-F]*)\\)\\s+\\[(.+)\\]\\s*$");
# else
        QRegExp re("^\\d*\\s+(.+)\\s+(.+)\\s+(.+)\\s+\\+\\s+(\\d*)\\s*$");
# endif

        if (re.indexIn(backtraceArray[i]) < 0) {
            toLog = QStringLiteral("[trace] #%1) %2")
                    .arg(i)
                    .arg(backtraceArray[i]);
            if (crashFileDescriptor != -1) {
                write(crashFileDescriptor, toLog.toLatin1().constData(), toLog.length());
                write(crashFileDescriptor, "\n", 1);
            }
            write(STDOUT_FILENO, toLog.toLatin1().constData(), toLog.length());
            write(STDOUT_FILENO, "\n", 1);
        } else {
# ifdef Q_OS_LINUX
            QString mangledName = re.cap(2).trimmed();
# else
            QString mangledName = re.cap(3).trimmed();
# endif
            char *name = abi::__cxa_demangle(mangledName.toLatin1().constData(), 0, 0, 0);
# ifdef Q_OS_LINUX
            toLog = QString("[trace] #%1) %2 : %3+%4 (%5)")
                    .arg(i)
                    .arg(re.cap(1).trimmed()) //scope
                    .arg(name ? name : mangledName) //name
                    .arg(re.cap(3).trimmed()) // clazy:exclude=qstring-arg
                    .arg(re.cap(4).trimmed()); //offset and address
# else
            toLog = QStringLiteral("[trace] #%1) %2 : %3+%4 (%5)")
                    .arg(i)
                    .arg(re.cap(1).trimmed()) //scope
                    .arg(name ? name : mangledName) //name
                    .arg(re.cap(4).trimmed()) // clazy:exclude=qstring-arg
                    .arg(re.cap(2).trimmed()); //offset and address
# endif
            if (crashFileDescriptor != -1) {
                write(crashFileDescriptor, toLog.toLatin1().constData(), toLog.length());
                write(crashFileDescriptor, "\n", 1);
            }
            write(STDOUT_FILENO, toLog.toLatin1().constData(), toLog.length());
            write(STDOUT_FILENO, "\n", 1);
            free(name);
        }
    }

    free(backtraceArray);
    if (crashFileDescriptor != -1)
        close(crashFileDescriptor);
    exit(EXIT_FAILURE);
}
#endif

const QString TRANSLATIONS_PATH = QStringLiteral(":/translations");

using namespace Proof;

CoreApplication::CoreApplication(int &argc, char **argv,
                                 const QString &orgName, const QString &appName, const QString &version,
                                 const QStringList &defaultLoggingRules)
    : CoreApplication(new QCoreApplication(argc, argv), orgName, appName, version, defaultLoggingRules)
{
}

CoreApplication::CoreApplication(QCoreApplication *app,
                                 const QString &orgName, const QString &appName, const QString &version,
                                 const QStringList &defaultLoggingRules)
    : CoreApplication(*new CoreApplicationPrivate, app, orgName, appName, version, defaultLoggingRules)
{
}

CoreApplication::CoreApplication(CoreApplicationPrivate &dd, QCoreApplication *app,
                                 const QString &orgName, const QString &appName, const QString &version,
                                 const QStringList &defaultLoggingRules)
    : ProofObject(dd)
{
    Q_D(CoreApplication);
    d->initCrashHandler();

    Q_ASSERT(::instance() == nullptr);
    ::instance() = this;

    app->setOrganizationName(orgName);
    app->setApplicationName(appName);
    app->setApplicationVersion(version);

    Logs::setup(defaultLoggingRules);

    bool daemonized = d->daemonizeIfNeeded();
    d->updatePrettifiedName();
    Expirator::instance();
    ErrorNotifier::instance();
    d->settings = new Proof::Settings(this);
    d->initLogs(daemonized);
    d->execMigrations();
    d->initQca();
    d->initTranslator();
    d->initUpdateManager();

    qCDebug(proofCoreMiscLog).noquote() << QStringLiteral("%1 started").arg(qApp->applicationName()).toLatin1().constData() << "with config at" << Proof::Settings::filePath();
}

CoreApplication::~CoreApplication()
{
}

QString CoreApplication::prettifiedApplicationName() const
{
    Q_D(const CoreApplication);
    return d->prettifiedApplicationName;
}

QStringList CoreApplication::availableLanguages()
{
    Q_D(const CoreApplication);
    return d->availableLanguages;
}

QVariantMap CoreApplication::fullLanguageNames()
{
    Q_D(const CoreApplication);
    return d->fullLanguageNames;
}

void CoreApplication::setLanguage(const QString &language)
{
    Q_D(CoreApplication);
    d->setLanguage(language);
    emit languageChanged(language);
}

QString CoreApplication::language() const
{
    Q_D(const CoreApplication);
    return d->currentLanguage;
}

int CoreApplication::languageIndex() const
{
    Q_D(const CoreApplication);
    return d->availableLanguages.indexOf(d->currentLanguage);
}

UpdateManager *CoreApplication::updateManager() const
{
    Q_D(const CoreApplication);
    return d->updateManager;
}

QDateTime CoreApplication::startedAt() const
{
    Q_D(const CoreApplication);
    return d->startedAt;
}

Settings *CoreApplication::settings() const
{
    Q_D(const CoreApplication);
    return d->settings;
}

void CoreApplication::postInit()
{
    Q_D(CoreApplication);
    d->postInit();
}

int CoreApplication::exec()
{
    return qApp->exec();
}

CoreApplication *CoreApplication::instance()
{
    return ::instance();
}

void CoreApplication::addInitializer(const std::function<void()> &initializer)
{
    if (instance() && instance()->d_func()->initialized)
        initializer();
    else
        initializers() << initializer;
}

void CoreApplication::addMigration(quint64 maxRelatedVersion, CoreApplication::Migration &&migration)
{
    Q_ASSERT_X(::instance() == nullptr, "addMigration", "Migration can only be added before Application object was created");
    migrations()[maxRelatedVersion] << std::forward<Migration>(migration);
}

void CoreApplication::addMigration(const QString &maxRelatedVersion, CoreApplication::Migration &&migration)
{
    addMigration(packVersion(maxRelatedVersion), std::forward<Migration>(migration));
}

void CoreApplication::addMigrations(const QMap<quint64, QList<CoreApplication::Migration>> &migrations)
{
    for (auto it = migrations.cbegin(); it != migrations.cend(); ++it) {
        quint64 version = it.key();
        for (Migration migration : it.value())
            addMigration(version, std::forward<Migration>(migration));
    }
}

void CoreApplication::addMigrations(const QMap<QString, QList<CoreApplication::Migration>> &migrations)
{
    for (auto it = migrations.cbegin(); it != migrations.cend(); ++it) {
        quint64 packedVersion = packVersion(it.key());
        for (Migration migration : it.value())
            addMigration(packedVersion, std::forward<Migration>(migration));
    }
}

void CoreApplicationPrivate::postInit()
{
    updateManager->start();

    initialized = true;
    for (const auto &initializer : qAsConst(initializers()))
        initializer();
    initializers().clear();
}

void CoreApplicationPrivate::initCrashHandler()
{
#if (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)) || defined(Q_OS_MAC)
    static struct sigaction sigSegvAction;
    sigSegvAction.sa_sigaction = signalHandler;
    sigSegvAction.sa_flags = SA_SIGINFO;
    static struct sigaction sigAbrtAction;
    sigAbrtAction.sa_sigaction = signalHandler;
    sigAbrtAction.sa_flags = SA_SIGINFO;

    if (sigaction(SIGSEGV, &sigSegvAction, (struct sigaction *)NULL) != 0)
        qCWarning(proofCoreLoggerLog) << "No segfault handler is on your back.";
    if (sigaction(SIGABRT, &sigAbrtAction, (struct sigaction *)NULL) != 0)
        qCWarning(proofCoreLoggerLog) << "No abort handler is on your back.";
#endif
}

void CoreApplicationPrivate::updatePrettifiedName()
{
    QString appType = QStringLiteral("Station");
    QString appName = qApp->applicationName();
    if (appName.startsWith(QLatin1String("proofservice"))) {
        appName.remove(QStringLiteral("proofservice-"));
        appType = QStringLiteral("Service");
    } else {
        appName.remove(QStringLiteral("proofstation-"));
    }

    if (!appName.isEmpty())
        appName[0] = appName[0].toUpper();
    for (int i = 0; i + 1 < appName.length(); ++i) {
        if (appName[i] == QChar('-'))
            appName[i + 1] = appName[i + 1].toUpper();
    }
    prettifiedApplicationName = QStringLiteral("%1-%2").arg(appName, appType);
}

bool CoreApplicationPrivate::daemonizeIfNeeded()
{
#if (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID))
    if (qApp->arguments().contains("-d")) {
        daemon(0, 0);
        return true;
    }
#endif
    return false;
}

void CoreApplicationPrivate::initLogs(bool daemonized)
{
    bool consoleOutputEnabled = true;
    QString logsStoragePath = QLatin1String("");
    QString logFileName = qApp->applicationName();
    Settings::NotFoundPolicy policy = Proof::proofUsesSettings() ? Settings::NotFoundPolicy::Add : Settings::NotFoundPolicy::DoNothing;

    SettingsGroup *logGroup = settings->group(QStringLiteral("logs"), policy);
    if (logGroup) {
        consoleOutputEnabled = !daemonized && logGroup->value(QStringLiteral("console"), consoleOutputEnabled, policy).toBool();
        logsStoragePath = logGroup->value(QStringLiteral("custom_storage_path"), logsStoragePath, Settings::NotFoundPolicy::AddGlobal).toString();
        logFileName = logGroup->value(QStringLiteral("filename"), logFileName, policy).toString();
    }

    Logs::setConsoleOutputEnabled(consoleOutputEnabled);
    Logs::setLogsStoragePath(logsStoragePath);
    if (!logFileName.isEmpty())
        Logs::installFileHandler(logFileName);
}

void CoreApplicationPrivate::execMigrations()
{
    quint64 packedAppVersion = packVersion(settings->mainGroup()->value(QStringLiteral("__app_version__"), "").toString().trimmed());
    quint64 packedProofVersion = packVersion(settings->mainGroup()->value(QStringLiteral("__proof_version__"), "").toString().trimmed());

    if (packedAppVersion >= packVersion(qApp->applicationVersion())
            && packedProofVersion >= packVersion(proofVersion())) {
        migrations().clear();
        return;
    }

    bool versionChanged = true;

    while (versionChanged) {
        quint64 minVersion = std::min(packedAppVersion, packedProofVersion);
        qCDebug(proofCoreMiscLog) << "Minimal appliable version for migrations is" << unpackVersionToString(minVersion);
        migrations()[minVersion];

        versionChanged = false;

        for (auto it = ++qAsConst(migrations()).find(minVersion); !versionChanged && it != qAsConst(migrations()).cend(); ++it) {
            qCDebug(proofCoreMiscLog) << "There are" << it.value().count() << "migrations for" << unpackVersionToString(it.key()) << "to run";
            for (const auto &migration : it.value()) {
                qCDebug(proofCoreMiscLog) << "Running migration";
                migration(packedAppVersion, packedProofVersion, settings);
                quint64 newPackedAppVersion = packVersion(settings->mainGroup()->value(QStringLiteral("__app_version__"), "").toString().trimmed());
                quint64 newPackedProofVersion = packVersion(settings->mainGroup()->value(QStringLiteral("__proof_version__"), "").toString().trimmed());
                // We need to start again with selecting start point
                if (newPackedAppVersion != packedAppVersion || newPackedProofVersion != packedProofVersion) {
                    packedAppVersion = newPackedAppVersion;
                    packedProofVersion = newPackedProofVersion;
                    versionChanged = true;
                    qCDebug(proofCoreMiscLog) << "Versions in config changed after migration, we need to restart from new point";
                    break;
                }
            }
        }
    }

    migrations().clear();

    settings->mainGroup()->setValue(QStringLiteral("__app_version__"), qApp->applicationVersion());
    settings->mainGroup()->setValue(QStringLiteral("__proof_version__"), proofVersion());
}

void CoreApplicationPrivate::initQca()
{
#ifndef QCA_DISABLED
    qcaInit.reset(new QCA::Initializer);
    QCA::scanForPlugins();
#endif
}

void CoreApplicationPrivate::initTranslator()
{
    QSet<QString> languagesSet;
    languagesSet.insert(QStringLiteral("en"));
    QDir translationsDir(TRANSLATIONS_PATH);
    const auto allTranslations = translationsDir.entryInfoList({"*.qm"}, QDir::Files);
    for (const QFileInfo &fileInfo : allTranslations) {
        QString fileName = fileInfo.fileName();
        translationPrefixes.insert(fileName.mid(0, fileName.indexOf('.')));
        languagesSet.insert(fileName.mid(fileName.indexOf('.') + 1, -3).mid(0, 2));
    }
    availableLanguages = languagesSet.toList();
    std::sort(availableLanguages.begin(), availableLanguages.end());
    SettingsGroup *localeGroup = settings->group(QStringLiteral("locale"), Settings::NotFoundPolicy::Add);
    currentLanguage = localeGroup->value(QStringLiteral("language"), QStringLiteral("en"), Settings::NotFoundPolicy::Add).toString();
    setLanguage(currentLanguage);

    for (const QString &lang : qAsConst(availableLanguages)) {
        QLocale locale(lang);
        fullLanguageNames[lang] = QLocale::languageToString(locale.language());
    }
}

void CoreApplicationPrivate::initUpdateManager()
{
    Q_Q(CoreApplication);
    updateManager = new UpdateManager(q);
#if (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID))
    SettingsGroup *updatesGroup = settings->group("updates", Settings::NotFoundPolicy::Add);
    updateManager->setAutoUpdateEnabled(updatesGroup->value("auto_update", false, Settings::NotFoundPolicy::Add).toBool());
    updateManager->setAptSourcesListFilePath(updatesGroup->value("sources_list_file", "", Settings::NotFoundPolicy::AddGlobal).toString());

    updateManager->setCurrentVersion(qApp->applicationVersion());
    updateManager->setPackageName(qApp->applicationName());
#endif
}

void CoreApplicationPrivate::setLanguage(const QString &language)
{
    Q_Q(CoreApplication);
    currentLanguage = language;
    SettingsGroup *localeGroup = settings->group(QStringLiteral("locale"), Settings::NotFoundPolicy::Add);
    localeGroup->setValue(QStringLiteral("language"), language);

    for (QTranslator *installedTranslator : qAsConst(installedTranslators)) {
        if (installedTranslator) {
            qApp->removeTranslator(installedTranslator);
            delete installedTranslator;
        }
    }
    installedTranslators.clear();

    for (const QString &prefix : qAsConst(translationPrefixes)) {
        qCDebug(proofCoreMiscLog) << "Language prefix" << prefix;
        QTranslator *translator = new QTranslator(q);
        installedTranslators.append(translator);
        translator->load(QStringLiteral("%1.%2").arg(prefix, language), TRANSLATIONS_PATH);
        qApp->installTranslator(translator);
    }
}
