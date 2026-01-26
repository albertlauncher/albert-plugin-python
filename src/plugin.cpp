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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QTextEdit>
#include <QUrl>
#include <QtConcurrentRun>
#include <albert/logging.h>
#include <albert/messagebox.h>
#include <albert/systemutil.h>
#include <chrono>
ALBERT_LOGGING_CATEGORY("python")
using namespace Qt::StringLiterals;
using namespace albert;
using namespace chrono;
using namespace std;
using std::filesystem::path;
namespace py = pybind11;
#define XSTR(s) STR(s)
#define STR(s) #s

applications::Plugin *apps;  // used externally

namespace {

const auto& BIN = "bin";
const auto& STUB_VERSION = "stub_version";
const auto& LIB = "lib";
const auto& PIP = "pip" XSTR(PY_MAJOR_VERSION) "." XSTR(PY_MINOR_VERSION);
const auto& PLUGINS = "plugins";
const auto& PYTHON = "python" XSTR(PY_MAJOR_VERSION) "." XSTR(PY_MINOR_VERSION);
const auto& SITE_PACKAGES = "site-packages";
const auto& STUB_FILE = "albert.pyi";
const auto& VENV = "venv";
const auto& sk_venv_python_version = "venv_python_version";
const auto& red = "\x1b[31m";
const auto& reset = "\x1b[0m";
const auto& cyan = "\x1b[36m";

}

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

static QString run(const QString program, const QStringList args, int timeout_ms = 300000)
{
    const auto cmdline = (QStringList(program) + args).join(QChar::Space);

    QProcess p;
    DEBG << u"Running '%1'"_s.arg(cmdline);
    p.start(program, args);

    if (!p.waitForFinished(timeout_ms))
    {
        const auto msg = QT_TRANSLATE_NOOP("Plugin", "'%1' timed out (%2s).");
        WARN << QString::fromUtf8(msg).arg(cmdline).arg(timeout_ms / 1000);
        throw runtime_error(Plugin::tr(msg).arg(cmdline).arg(timeout_ms / 1000).toStdString());
    }
    else if (p.exitStatus() != QProcess::ExitStatus::NormalExit)
    {
        const auto msg = QT_TRANSLATE_NOOP("Plugin", "'%1' crashed.");
        WARN << QString::fromUtf8(msg).arg(cmdline);
        throw runtime_error(Plugin::tr(msg).arg(cmdline).toStdString());
    }
    else if (p.exitCode() != EXIT_SUCCESS)
    {
        const auto msg = QT_TRANSLATE_NOOP("Plugin", "'%1' finished with exit code: %2.");
        WARN << QString::fromUtf8(msg).arg(cmdline).arg(p.exitCode());
        if (const auto stdout = p.readAllStandardOutput(); !stdout.isEmpty())
            WARN << cyan << stdout << reset;
        if (const auto stderr = p.readAllStandardError(); !stderr.isEmpty())
            WARN << red << stderr << reset;
        throw runtime_error(Plugin::tr(msg).arg(cmdline).arg(p.exitCode()).toStdString());
    }
    else
        return QString::fromUtf8(p.readAllStandardOutput());
}


Plugin::Plugin()
{
    ::apps = apps.get();

    DEBG << "Python version:" << PY_VERSION;
    DEBG << "Pybind11 version:" << u"%1.%2.%3"_s
                                       .arg(PYBIND11_VERSION_MAJOR)
                                       .arg(PYBIND11_VERSION_MINOR)
                                       .arg(PYBIND11_VERSION_PATCH);

    filesystem::create_directories(dataLocation() / PLUGINS);

    initPythonInterpreter();
}

Plugin::~Plugin()
{
    release_.reset();
    loaders_.clear();

    // Causes hard to debug crashes, mem leaked, but nobody will toggle it a lot
    // py::finalize_interpreter();
}

void Plugin::initialize()
{
    QtConcurrent::run([this] -> shared_ptr<vector<unique_ptr<PyPluginLoader>>> {
        initVirtualEnvironment();
        updateStubFile();

        // Set loader thread affinity to main thread
        auto loaders = scanPlugins();
        for (auto &loader : loaders)
            loader->moveToThread(this->thread());

        // Make copyable for missing qtconcurrent move semantics
        return make_shared<vector<unique_ptr<PyPluginLoader>>>(::move(loaders));
    })
    .then(this, [this](shared_ptr<vector<unique_ptr<PyPluginLoader>>> loaders) {
        loaders_ = ::move(*loaders);
        PluginInstance::initialize();
    })
    .onCanceled(this, [] {
        WARN << "Cancelled plugin initialization.";
    })
    .onFailed(this, [](const QUnhandledException &que) {
        if (que.exception())
            rethrow_exception(que.exception());
        else
            throw runtime_error("QUnhandledException::exception() returned nullptr.");
    })
    .onFailed(this, [](const exception &e) {
        CRIT << "Exception while initializing plugin:" << e.what();
    })
    .onFailed(this, [] {
        CRIT << "Unknown exception while initializing plugin.";
    });
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

void Plugin::initPythonInterpreter()
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

    // Gil is initially held. We want it to be released by default.
    release_.reset(new py::gil_scoped_release);
}

void Plugin::initVirtualEnvironment() const
{
    py::gil_scoped_acquire acquire;

    // Reset venv if python version changed
    if (is_directory(venvPath())
        && (state()->value(sk_venv_python_version).toString() != QString::fromLatin1(PY_VERSION)))
    {
        INFO << "Python version changed. Resetting virtual environment.";
        QFile::moveToTrash(venvPath());
    }

    if (!is_directory(venvPath()))
    {
        auto system_python = py::module::import("sys").attr("prefix").cast<path>() / BIN / PYTHON;

        DEBG << "Initializing venv using system interpreter" << system_python;

        DEBG << run(QString::fromLocal8Bit(system_python.native()),
                    {u"-m"_s,
                     u"venv"_s,
                     //"--upgrade",
                     //"--upgrade-deps",
                     toQString(venvPath())});

        state()->setValue(sk_venv_python_version, QString::fromLatin1(PY_VERSION));
    }

    // Add venv site packages to path
    py::module::import("site").attr("addsitedir")(siteDirPath().c_str());
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
    for (auto &plugin : loaders_)
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

    ui.label_python_version->setText(QString::fromUtf8(PY_VERSION));

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
            App::restart();
        }
    });

    connect(ui.pushButton_userPluginDir, &QPushButton::clicked,
            this, [this]{ open(userPluginDirectoryPath()); });

    return w;
}

bool Plugin::checkPackages(const QStringList &packages) const
{
    scoped_lock lock(pip_mutex_);

    const auto stdout = run(toQString(venvPath() / BIN / PIP), {u"freeze"_s});

    set<QString> pkgs;
    static const auto re_line = QRegularExpression("[\r\n]"_L1);
    static const auto re_sep = QRegularExpression(u"==|\\s|@"_s);
    for (const auto &line : stdout.split(re_line, Qt::SkipEmptyParts))
        pkgs.insert(line.section(re_sep, 0, 0, QString::SectionSkipEmpty).toLower());

    return ranges::all_of(packages,
                          [&pkgs](const QString &pkg) { return pkgs.contains(pkg.toLower()); });
}

void Plugin::installPackages(const QStringList &packages) const
{
    scoped_lock lock(pip_mutex_);

    DEBG << run(toQString(venvPath() / BIN / PIP),
                QStringList{u"install"_s, u"--disable-pip-version-check"_s} << packages);
}
