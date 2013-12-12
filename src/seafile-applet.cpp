#include <QtGui>
#include <QApplication>
#include <QDesktopServices>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QCoreApplication>
#include <QMessageBox>

#include <glib.h>

#include "utils/utils.h"
#include "utils/log.h"
#include "account-mgr.h"
#include "configurator.h"
#include "daemon-mgr.h"
#include "message-listener.h"
#include "settings-mgr.h"
#include "rpc/rpc-client.h"
#include "ui/main-window.h"
#include "ui/tray-icon.h"
#include "ui/settings-dialog.h"
#include "ui/welcome-dialog.h"
#include "ui/init-vdrive-dialog.h"
#include "ui/login-dialog.h"

#include "seafile-applet.h"

namespace {

void myLogHandler(QtMsgType type, const char *msg)
{
    switch (type) {
    case QtDebugMsg:
        g_debug("%s\n", msg);
        break;
    case QtWarningMsg:
        g_warning("%s\n", msg);
        break;
    case QtCriticalMsg:
        g_critical("%s\n", msg);
        break;
    case QtFatalMsg:
        g_critical("%s\n", msg);
        abort();
    }
}


} // namespace


SeafileApplet *seafApplet;

SeafileApplet::SeafileApplet()
    : configurator_(new Configurator),
      account_mgr_(new AccountManager),
      daemon_mgr_(new DaemonManager),
      main_win_(NULL),
      rpc_client_(new SeafileRpcClient),
      message_listener_(new MessageListener),
      settings_dialog_(new SettingsDialog),
      settings_mgr_(new SettingsManager),
      first_login_ok_(false),
      started_(false),
      in_exit_(false)
{
    tray_icon_ = new SeafileTrayIcon(this);
}

void SeafileApplet::start()
{
    refreshQss();

    configurator_->checkInit();

    initLog();

    account_mgr_->start();

#if defined(Q_WS_WIN)
    QString crash_rpt_path = QDir(configurator_->ccnetDir()).filePath("logs/seafile-crash-report.txt");
    if (!g_setenv ("CRASH_RPT_PATH", toCStr(crash_rpt_path), FALSE))
        qDebug("Failed to set CRASH_RPT_PATH env variable.\n");
#endif

    if (configurator_->firstUse()) {
        // WelcomeDialog welcome_dialog;
        // welcome_dialog.exec();
        LoginDialog login_dialog;
        if (login_dialog.exec() == QDialog::Accepted) {
            first_login_ok_ = true;
        }
    }

    daemon_mgr_->startCcnetDaemon();

    connect(daemon_mgr_, SIGNAL(daemonStarted()),
            this, SLOT(onDaemonStarted()));
}

void SeafileApplet::onDaemonStarted()
{
    // tray_icon_->notify(SEAFILE_CLIENT_BRAND, "daemon is started");
    main_win_ = new MainWindow;

    rpc_client_->connectDaemon();
    message_listener_->connectDaemon();
    seafApplet->settingsManager()->loadSettings();

    started_ = true;

    // if (account_mgr_->accounts().size() > 0) {
    // if (first_login_ok_ && !settings_mgr_->defaultLibraryAlreadySetup()) {
    if (!settings_mgr_->defaultLibraryAlreadySetup()) {

        const Account& account = account_mgr_->accounts()[0];
        InitVirtualDriveDialog dialog(account);
        dialog.exec();
    }

    if (configurator_->firstUse() || !settings_mgr_->hideMainWindowWhenStarted()) {
        main_win_->showWindow();
    }

    tray_icon_->start();
    tray_icon_->setState(SeafileTrayIcon::STATE_DAEMON_UP);
}

void SeafileApplet::exit(int code)
{
    // Must use the global namespace, or the "exit" would call itself util
    // stack overflow
    daemon_mgr_->stopAll();
    // Remove tray icon from system tray
    delete tray_icon_;
    if (main_win_) {
        main_win_->writeSettings();
    }
    ::exit(code);
}

void SeafileApplet::errorAndExit(const QString& error)
{
    if (in_exit_) {
        return;
    }

    in_exit_ = true;
    QMessageBox::warning(main_win_, tr(SEAFILE_CLIENT_BRAND), error, QMessageBox::Ok);
    this->exit(1);
}

void SeafileApplet::initLog()
{
    if (applet_log_init(toCStr(configurator_->ccnetDir())) < 0) {
        errorAndExit(tr("Failed to initialize log"));
    } else {
        qInstallMsgHandler(myLogHandler);
    }
}

void SeafileApplet::loadQss(const QString& path)
{
    QFile file(path);
    if (!QFileInfo(file).exists()) {
        return;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream input(&file);
    style_ += "\n";
    style_ += input.readAll();
    qApp->setStyleSheet(style_);
}

void SeafileApplet::refreshQss()
{
    style_.clear();
    loadQss(":/qt.css");
    loadQss("qt.css");

#if defined(Q_WS_WIN)
    loadQss(":/qt-win.css");
    loadQss("qt-win.css");
#elif defined(Q_WS_X11)
    loadQss(":/qt-linux.css");
    loadQss("qt-linux.css");
#else
    loadQss(":/qt-mac.css");
    loadQss("qt-mac.css");
#endif
}

void SeafileApplet::warningBox(const QString& msg)
{
    QMessageBox::warning(main_win_, tr(SEAFILE_CLIENT_BRAND), msg, QMessageBox::Ok);
}

void SeafileApplet::messageBox(const QString& msg)
{
    QMessageBox::information(main_win_, tr(SEAFILE_CLIENT_BRAND), msg, QMessageBox::Ok);
}
