#include <glib-object.h>
#include <cstdio>
#include <cstdlib>
#include <QTimer>
#include <QStringList>
#include <QString>
#include <QDebug>
#include <QDir>
#include <QCoreApplication>

extern "C" {
#include <ccnet/ccnet-client.h>
}


#include "utils/utils.h"
#include "utils/process.h"
#include "configurator.h"
#include "seafile-applet.h"
#include "daemon-mgr.h"

namespace {

const int kConnDaemonIntervalMilli = 1000;

#if defined(Q_WS_WIN)
const char *kCcnetDaemonExecutable = "ccnet.exe";
const char *kSeafileDaemonExecutable = "seaf-daemon.exe";
#else
const char *kCcnetDaemonExecutable = "ccnet";
const char *kSeafileDaemonExecutable = "seaf-daemon";
#endif

} // namespace



DaemonManager::DaemonManager()
    : ccnet_daemon_(0),
    seaf_daemon_(0),
    sync_client_(0)
{
    conn_daemon_timer_ = new QTimer(this);
    connect(conn_daemon_timer_, SIGNAL(timeout()), this, SLOT(tryConnCcnet()));
    shutdown_process (kCcnetDaemonExecutable);
}

void DaemonManager::startCcnetDaemon()
{
    sync_client_ = ccnet_client_new();

    const QString config_dir = seafApplet->configurator()->ccnetDir();
    const QByteArray path = config_dir.toUtf8();
    if (ccnet_client_load_confdir(sync_client_, path.data()) <  0) {
        seafApplet->errorAndExit(tr("failed to load ccnet config dir %1").arg(config_dir));
    }

    ccnet_daemon_ = new QProcess(this);
    connect(ccnet_daemon_, SIGNAL(started()), this, SLOT(onCcnetDaemonStarted()));
    connect(ccnet_daemon_, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(onCcnetDaemonExited()));

    QStringList args;
    args << "-c" << config_dir;
#if defined(XCODE_APP)
    ccnet_daemon_->start(RESOURCE_PATH(kCcnetDaemonExecutable), args);
#else
    ccnet_daemon_->start(kCcnetDaemonExecutable, args);
#endif
    qDebug() << "starting ccnet: " << args;
}

void DaemonManager::startSeafileDaemon()
{
    const QString config_dir = seafApplet->configurator()->ccnetDir();
    const QString seafile_dir = seafApplet->configurator()->seafileDir();
    const QString worktree_dir = seafApplet->configurator()->worktreeDir();

    seaf_daemon_ = new QProcess(this);
    connect(seaf_daemon_, SIGNAL(started()), this, SLOT(onSeafDaemonStarted()));
    connect(seaf_daemon_, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(onSeafDaemonExited()));

    QStringList args;
    args << "-c" << config_dir << "-d" << seafile_dir << "-w" << worktree_dir;
#if defined(XCODE_APP)
    seaf_daemon_->start(RESOURCE_PATH(kSeafileDaemonExecutable), args);
#else
    seaf_daemon_->start(kSeafileDaemonExecutable, args);
#endif
    qDebug() << "starting seaf-daemon: " << args;
}

void DaemonManager::onCcnetDaemonStarted()
{
    conn_daemon_timer_->start(kConnDaemonIntervalMilli);
}

void DaemonManager::onSeafDaemonStarted()
{
    qDebug("seafile daemon is now running");
    emit daemonStarted();
}

void DaemonManager::onCcnetDaemonExited()
{
    seafApplet->errorAndExit(tr("ccnet daemon has exited abnormally"));
}

void DaemonManager::onSeafDaemonExited()
{
    seafApplet->errorAndExit(tr("seafile daemon has exited abnormally"));
}

void DaemonManager::stopAll()
{
    qDebug("[Daemon Mgr] stopping ccnet/seafile daemon");
    if (seaf_daemon_)
        seaf_daemon_->kill();
    if (ccnet_daemon_)
        ccnet_daemon_->kill();
}

void DaemonManager::tryConnCcnet()
{
    qDebug("trying to connect to ccnet daemon...\n");

    if (ccnet_client_connect_daemon(sync_client_, CCNET_CLIENT_SYNC) < 0) {
        return;
    } else {
        conn_daemon_timer_->stop();

        qDebug("connected to ccnet daemon\n");

        startSeafileDaemon();
    }
}
