// Copyright (c) 2022-2025 Manuel Schneider

#include "cast_specialization.hpp"
#include "embeddedmodule.hpp"
// import pybind first

#include "plugin.h"
#include "pypluginloader.h"
#include "ui_configwidget.h"
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QTextEdit>
#include <QUrl>
#include <albert/extensionregistry.h>
#include <albert/logging.h>
#include <albert/messagebox.h>
#include <albert/systemutil.h>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <chrono>
ALBERT_LOGGING_CATEGORY("python")
using namespace Qt::StringLiterals;
using namespace albert::util;
using namespace albert;
using namespace chrono;
using namespace std;
using std::filesystem::path;
namespace py = pybind11;
#define XSTR(s) STR(s)
#define STR(s) #s

applications::Plugin *apps;  // used externally
static const char *BIN = "bin";
static const char *STUB_VERSION = "stub_version";
static const char *LIB = "lib";
static const char *PIP = "pip" XSTR(PY_MAJOR_VERSION) "." XSTR(PY_MINOR_VERSION);
static const char *PLUGINS = "plugins";
static const char *PYTHON = "python" XSTR(PY_MAJOR_VERSION) "." XSTR(PY_MINOR_VERSION);
static const char *SITE_PACKAGES = "site-packages";
static const char *STUB_FILE = "albert.pyi";
static const char *VENV = "venv";

static void dumpPyConfig(PyConfig &config)
{

    DEBG << "config.home" << QString::fromWCharArray(config.home);
    DEBG << "config.base_executable" << QString::fromWCharArray(config.base_executable);
    DEBG << "config.executable" << QString::fromWCharArray(config.executable);
    DEBG << "config.base_exec_prefix" << QString::fromWCharArray(config.base_exec_prefix);
    DEBG << "config.exec_prefix" << QString::fromWCharArray(config.exec_prefix);
    DEBG << "config.base_prefix" << QString::fromWCharArray(config.base_prefix);
    DEBG << "config.prefix" << QString::fromWCharArray(config.prefix);
    DEBG << "config.program_name" << QString::fromWCharArray(config.program_name);
    DEBG << "config.pythonpath_env" << QString::fromWCharArray(config.pythonpath_env);
    DEBG << "config.platlibdir" << QString::fromWCharArray(config.platlibdir);
    // DEBG << "config.stdlib_dir" << QString::fromWCharArray(config.stdlib_dir);  // Added in version 3.11
    // ^DEBG << "config.safe_path" << config.safe_path;  // Added in version 3.11
    DEBG << "config.install_signal_handlers" << config.install_signal_handlers;
    DEBG << "config.site_import" << config.site_import;
    DEBG << "config.user_site_directory" << config.user_site_directory;
    DEBG << "config.verbose" << config.verbose;
    DEBG << "config.module_search_paths_set" << config.module_search_paths_set;
    DEBG << "config.module_search_paths:";
    for (Py_ssize_t i = 0; i < config.module_search_paths.length; ++i)
        DEBG << " -" << QString::fromWCharArray(config.module_search_paths.items[i]);
}

// static void dumpSysAttributes(const py::module &sys)
// {
//     DEBG << "version          :" << sys.attr("version").cast<QString>();
//     DEBG << "executable       :" << sys.attr("executable").cast<QString>();
//     DEBG << "base_exec_prefix :" << sys.attr("base_exec_prefix").cast<QString>();
//     DEBG << "exec_prefix      :" << sys.attr("exec_prefix").cast<QString>();
//     DEBG << "base_prefix      :" << sys.attr("base_prefix").cast<QString>();
//     DEBG << "prefix           :" << sys.attr("prefix").cast<QString>();
//     DEBG << "path:";
//     for (const auto &path : sys.attr("path").cast<QStringList>())
//         DEBG << " -" << path;
// }

Plugin::Plugin()
{
    ::apps = apps.get();

    DEBG << "Python version:" << u"%1.%2.%3"_s
                                     .arg(PY_MAJOR_VERSION)
                                     .arg(PY_MINOR_VERSION)
                                     .arg(PY_MICRO_VERSION);

    DEBG << "Pybind11 version:" << u"%1.%2.%3"_s
                                       .arg(PYBIND11_VERSION_MAJOR)
                                       .arg(PYBIND11_VERSION_MINOR)
                                       .arg(PYBIND11_VERSION_PATCH);

    tryCreateDirectory(dataLocation() / PLUGINS);

    updateStubFile();

    initPythonInterpreter();

    // Add venv site packages to path
    py::module::import("site").attr("addsitedir")(siteDirPath().c_str());

    release_.reset(new py::gil_scoped_release);  // Gil is initially held.

    initVirtualEnvironment();

    plugins_ = scanPlugins();
}

Plugin::~Plugin()
{
    release_.reset();
    plugins_.clear();

    // Causes hard to debug crashes, mem leaked, but nobody will toggle it a lot
    // py::finalize_interpreter();
}

void Plugin::updateStubFile() const
{
    QFile stub_rc(u":"_s + QString::fromLatin1(STUB_FILE));
    QFile stub_fs(stubFilePath());

    auto interface_version = u"%1.%2"_s
                                 .arg(PyPluginLoader::MAJOR_INTERFACE_VERSION)
                                 .arg(PyPluginLoader::MINOR_INTERFACE_VERSION);

    if (interface_version != state()->value(STUB_VERSION).toString()
        && stub_fs.exists() && !stub_fs.remove())
        WARN << "Failed removing former stub file" << stub_fs.error();

    if (!stub_fs.exists())
    {
        INFO << "Writing stub file to" << stub_fs.fileName();
        if (stub_rc.copy(stub_fs.fileName()))
            state()->setValue(STUB_VERSION, interface_version);
        else
            WARN << "Failed copying stub file to" << stub_fs.fileName() << stub_rc.error();
    }
}

void Plugin::initPythonInterpreter() const
{
    DEBG << "Initializing Python interpreter";
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    config.site_import = 0;
    dumpPyConfig(config);
    if (auto status = Py_InitializeFromConfig(&config); PyStatus_Exception(status))
        throw runtime_error(format("Failed initializing the interpreter: {} {}",
                                   status.func, status.err_msg));
    PyConfig_Clear(&config);
    dumpPyConfig(config);
}

void Plugin::initVirtualEnvironment() const
{
    if (is_directory(venvPath()))
        return;

    py::gil_scoped_acquire acquire;

    auto system_python =
        path(py::module::import("sys").attr("prefix").cast<string>()) / BIN / PYTHON;

    // Create the venv
    QProcess p;
    p.start(QString::fromLocal8Bit(system_python.native()),
            {
             u"-m"_s,
             u"venv"_s,
             //"--upgrade",
             //"--upgrade-deps",
             toQString(venvPath())
            });
    DEBG << "Initializing venv using system interpreter:"
         << (QStringList() << p.program() << p.arguments()).join(QChar::Space);
    p.waitForFinished(-1);
    if (auto out = p.readAllStandardOutput(); !out.isEmpty())
        DEBG << out;
    if (auto err = p.readAllStandardError(); !err.isEmpty())
        WARN << err;
    if (p.exitCode() != 0)
        throw runtime_error(tr("Failed initializing virtual environment. Exit code: %1.")
                                .arg(p.exitCode()).toStdString());

    DEBG << "Upgrade pip";
    p.setProgram(toQString(venvPath() / BIN / PIP));
    p.setArguments({u"install"_s, u"--upgrade"_s, u"pip"_s, });
    p.start();
    p.waitForFinished();
}

path Plugin::venvPath() const { return dataLocation() / VENV; }

path Plugin::siteDirPath() const { return venvPath() / LIB / PYTHON / SITE_PACKAGES; }

path Plugin::userPluginDirectoryPath() const { return dataLocation() / PLUGINS; }

path Plugin::stubFilePath() const { return userPluginDirectoryPath() / STUB_FILE; }

vector<unique_ptr<PyPluginLoader>> Plugin::scanPlugins() const
{
    auto start = system_clock::now();

    vector<unique_ptr<PyPluginLoader>> plugins;
    for (const auto &data_location : dataLocations())
    {
        if (QDir dir{data_location/PLUGINS}; dir.exists())
        {
            DEBG << "Searching Python plugins in" << dir.absolutePath();
            for (const auto r = dir.entryInfoList(QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot);
                 const QFileInfo &file_info : r)
            {
                try {
                    auto loader = make_unique<PyPluginLoader>(*this, file_info.absoluteFilePath());
                    DEBG << "Found valid Python plugin" << loader->path();
                    plugins.emplace_back(::move(loader));
                }
                catch (const NoPluginException &e) {
                    DEBG << "Invalid plugin" << file_info.filePath() << e.what();
                }
                catch (const exception &e) {
                    WARN << e.what() << file_info.filePath();
                }
            }
        }
    }

    INFO << u"[%1 ms] Python plugin scan"_s
                .arg(duration_cast<milliseconds>(system_clock::now()-start).count());

    return plugins;
}

vector<PluginLoader*> Plugin::plugins()
{
    vector<PluginLoader*> plugins;
    for (auto &plugin : plugins_)
        plugins.emplace_back(plugin.get());
    return plugins;
}

QWidget *Plugin::buildConfigWidget()
{
    auto *w = new QWidget;
    Ui::ConfigWidget ui;
    ui.setupUi(w);

    ui.label_api_version->setText(u"<a href=\"file://%1\">v%2.%3</a>"_s
                                      .arg(QString::fromStdString(stubFilePath().native()))
                                      .arg(PyPluginLoader::MAJOR_INTERFACE_VERSION)
                                      .arg(PyPluginLoader::MINOR_INTERFACE_VERSION));

    ui.label_python_version->setText(u"%1.%2.%3"_s
                                         .arg(PY_MAJOR_VERSION)
                                         .arg(PY_MINOR_VERSION)
                                         .arg(PY_MICRO_VERSION));

    ui.label_pybind_version->setText(u"%1.%2.%3"_s
                                         .arg(PYBIND11_VERSION_MAJOR)
                                         .arg(PYBIND11_VERSION_MINOR)
                                         .arg(PYBIND11_VERSION_PATCH));

    connect(ui.pushButton_venv_open, &QPushButton::clicked,
            this, [this]{ open(venvPath()); });

    connect(ui.pushButton_venv_term, &QPushButton::clicked, this, [this]{
        apps->runTerminal(u"cd '%1' && . bin/activate; exec $SHELL"_s
                              .arg(toQString(venvPath())));
    });

    connect(ui.pushButton_venv_reset, &QPushButton::clicked, this, [this]
    {
        if (question(tr("Resetting the virtual environment requires a restart. Restart now?")))
        {
            QFile::moveToTrash(venvPath());
            restart();
        }
    });

    connect(ui.pushButton_userPluginDir, &QPushButton::clicked,
            this, [this]{ open(userPluginDirectoryPath()); });

    return w;
}

bool Plugin::checkPackages(const QStringList &packages) const
{
    scoped_lock lock(pip_mutex_);

    QProcess p;
    p.setProgram(toQString(venvPath() / BIN / PIP));
    p.setArguments({u"inspect"_s});
    p.start();
    p.waitForFinished();

    if (p.exitStatus() == QProcess::ExitStatus::NormalExit && p.exitCode() == EXIT_SUCCESS)
    {
        const auto output = p.readAllStandardOutput();
        const auto installed = QJsonDocument::fromJson(output).object()["installed"_L1].toArray();
        auto view = installed
                    | views::transform([](const auto &v){ return v["metadata"_L1]["name"_L1].toString().toLower(); });
        set<QString> pkgs(view.begin(), view.end());
        return ranges::all_of(packages,
                              [&pkgs](const QString &pkg) { return pkgs.contains(pkg.toLower()); });
    }
    else
    {
        WARN << "Failed inspecting packages with exit code" << p.exitCode();
        return false;
    }
}

QString Plugin::installPackages(const QStringList &packages) const
{
    scoped_lock lock(pip_mutex_);

    QProcess p;
    p.setProgram(toQString(venvPath() / BIN / PIP));
    p.setArguments(QStringList{u"install"_s, u"--disable-pip-version-check"_s} << packages);

    DEBG << QString(u"Installing %1. [%2]"_s)
                .arg(packages.join(u", "_s), (QStringList(p.program()) << p.arguments()).join(u" "_s));

    p.start();
    p.waitForFinished();

    auto success = p.exitStatus() == QProcess::ExitStatus::NormalExit && p.exitCode() == EXIT_SUCCESS;

    if (!success)
        return QString::fromUtf8(p.readAllStandardError());

    return {};
}
