#include <glib.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>
#include <QList>

#include "seafile-applet.h"
#include "ccnet-init.h"
#include "ui/init-seafile-dialog.h"

#if defined(Q_WS_WIN)
#include "utils/registry.h"
#include <shlobj.h>
#include <shlwapi.h>
#endif

#include "configurator.h"


namespace {

#if defined(Q_WS_WIN)

const char *kCcnetConfDir = "ccnet";
const char *kVirtualDriveGUID = "F817C393-A76E-435E-B6B1-485844BC9C2E";
const char *kMyComputerNamespacePath =
    "Software\\Microsoft\\Windows\\CurrentVersion"
    "\\Explorer\\MyComputer\\Namespace";

#else

const char *kCcnetConfDir = ".ccnet";

#endif

QString defaultCcnetDir() {
    const char *env = g_getenv("CCNET_CONF_DIR");
    if (env) {
        return QString::fromUtf8(env);
    } else {
        return QDir::home().filePath(kCcnetConfDir);
    }
}

} // namespace


Configurator::Configurator()
    : ccnet_dir_(defaultCcnetDir()),
      first_use_(false)
{
}

void Configurator::checkInit()
{
    if (needInitConfig()) {
        // first time use
        initConfig();
    } else {
        validateExistingConfig();
    }
}

bool Configurator::needInitConfig()
{
    if (QDir(ccnet_dir_).exists()) {
        return false;
    }

    return true;
}

void Configurator::initConfig()
{
    initCcnet();
    initSeafile();
}

void Configurator::initCcnet()
{
    QString path = QDir::toNativeSeparators(ccnet_dir_);
    if (create_ccnet_config(path.toUtf8().data()) < 0) {
        seafApplet->errorAndExit(tr("Error when creating ccnet configuration"));
    }

    first_use_ = true;
}

void Configurator::initSeafile()
{
    InitSeafileDialog dialog;
    connect(&dialog, SIGNAL(seafileDirSet(const QString&)),
            this, SLOT(onSeafileDirSet(const QString&)));

    if (dialog.exec() != QDialog::Accepted) {
        seafApplet->exit(1);
    }

    first_use_ = true;
}

void Configurator::onSeafileDirSet(const QString& path)
{
    // Write seafile dir to <ccnet dir>/seafile.ini
    QFile seafile_ini(QDir(ccnet_dir_).filePath("seafile.ini"));

    if (!seafile_ini.open(QIODevice::WriteOnly)) {
        return;
    }

    seafile_ini.write(path.toUtf8().data());

    seafile_dir_ = path;

    QDir d(path);

    d.cdUp();
    worktree_ = d.absolutePath();

    setSeafileDirAttributes();
}

void Configurator::setSeafileDirAttributes()
{
#if defined(Q_WS_WIN)
    std::wstring seafdir = seafile_dir_.toStdWString();

    // Make seafile-data folder hidden
    SetFileAttributesW (seafdir.c_str(),
                        FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

    // Set seafdir folder icon.
    SetFileAttributesW (worktree_.toStdWString().c_str(), FILE_ATTRIBUTE_SYSTEM);
    QString desktop_ini_path = QDir(worktree_).filePath("Desktop.ini");
    QFile desktop_ini(desktop_ini_path);

    if (!desktop_ini.open(QIODevice::WriteOnly |  QIODevice::Text)) {
        return;
    }

    QString icon_path = QDir(QCoreApplication::applicationDirPath()).filePath("seafdir.ico");

    QTextStream out(&desktop_ini);
    out << "[.ShellClassInfo]\n";
    out << QString("IconFile=%1\n").arg(icon_path);
    out << "IconIndex=0\n";

    // Make the "Desktop.ini" file hidden.
    SetFileAttributesW (desktop_ini_path.toStdWString().c_str(),
                        FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
#endif
}

void Configurator::validateExistingConfig()
{
    QFile ccnet_conf(QDir(ccnet_dir_).filePath("ccnet.conf"));
    if (!ccnet_conf.exists()) {
        initConfig();
        return;
    }

    if (readSeafileIni(&seafile_dir_) < 0 || !QDir(seafile_dir_).exists()) {
        initSeafile();
        return;
    }

    QDir d(seafile_dir_);
#ifndef Q_WS_WIN
    QString old_client_wt = d.filePath("../seafile/");
    if (QFile(old_client_wt).exists()) {
        // old client
        worktree_ = QFileInfo(old_client_wt).absoluteFilePath();
        return;
    }
#endif

    d.cdUp();
    worktree_ = d.absolutePath();
}

int Configurator::readSeafileIni(QString *content)
{
    QFile seafile_ini(QDir(ccnet_dir_).filePath("seafile.ini"));
    if (!seafile_ini.exists()) {
        return -1;
    }

    if (!seafile_ini.open(QIODevice::ReadOnly | QIODevice::Text)) {
        seafApplet->errorAndExit(tr("failed to read %1").arg(seafile_ini.fileName()));
    }

    QTextStream input(&seafile_ini);
    input.setCodec("UTF-8");

    if (input.atEnd()) {
        return -1;
    }

    *content = input.readLine();

    return 0;
}

int Configurator::setVirtualDrive(const QString& path)
{
    printf ("Configurator::setVirtualDrive is called\n");
#if defined(Q_WS_WIN)
    QString clsid_path = QString("Software\\Classes\\CLSID\\{%1}").arg(kVirtualDriveGUID);

    QList<RegElement> list;

    /*
      See http://msdn.microsoft.com/en-us/library/ms997573.aspx

        HKEY_CLASSES_ROOT\CLSID\Classes\CLSID\{GUID}
        - DefaultIcon = "c:/Program Files/Seafile/bin/seafile-appelet.exe", 0
        - InProcServer32 = "%SystemRoot%\system32\shdocvw.dll"
          - ThreadingModel = "Apartment"
        - Instance
          - CLSID = {0AFACED1-E828-11D1-9187-B532F1E9575D}
          - InitPropertyBag
            -Target(REG_SZ) = D:\Seafile
        - ShellFolder
          - Attributes=0xXXXXX
          - PinToNameSpaceTree=""
          - wantsFORPARSING=""
    */

    HKEY root = HKEY_CURRENT_USER;

    list.append(RegElement(root, clsid_path,
                           "", tr("Seafile")));

    list.append(RegElement(root, clsid_path,
                           "InfoTip", tr("Seafile default library")));

    list.append(RegElement(root, clsid_path + "\\DefaultIcon",
                           "", QCoreApplication::applicationFilePath(), true));

    list.append(RegElement(root, clsid_path + "\\InProcServer32",
                           "", "shdocvw.dll", true));

    list.append(RegElement(root, clsid_path + "\\InProcServer32",
                           "ThreadingModel", "Apartment"));

    list.append(RegElement(root, clsid_path + "\\Instance",
                           "CLSID", "{0AFACED1-E828-11D1-9187-B532F1E9575D}"));

    list.append(RegElement(root, clsid_path + "\\Instance\\InitPropertyBag",
                           "Target", QDir::toNativeSeparators(path)));

    list.append(RegElement(root, clsid_path + "\\ShellFolder",
                           "Attributes",
                           SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_HASSUBFOLDER));

    list.append(RegElement(root, clsid_path + "\\ShellFolder",
                           "PinToNameSpaceTree", ""));

    list.append(RegElement(root, clsid_path + "\\ShellFolder",
                           "wantsFORPARSING", ""));

    // HKEY_CURRENT_USESR\Software\Microsoft\Windows\CurrentVersion\Explorer\MyComputer\Namespace
    // - {GUID}

    list.append(RegElement(root, kMyComputerNamespacePath + QString("\\{%1}").arg(kVirtualDriveGUID),
                           "", ""));

    for (int i = 0; i < list.size(); i++) {
        RegElement& reg = list[i];
        if (reg.add() < 0) {
            return -1;
        }
    }

#endif
}
