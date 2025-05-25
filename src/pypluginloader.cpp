// Copyright (c) 2023-2025 Manuel Schneider

#include "trampolineclasses.hpp"

#include "plugin.h"
#include "pypluginloader.h"
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtConcurrentRun>
#include <albert/messagebox.h>
namespace py = pybind11;
using namespace Qt::StringLiterals;
using namespace albert::util;
using namespace albert;
using namespace std;

static const auto ATTR_PLUGIN_CLASS   = "Plugin";
static const auto ATTR_MD_IID         = "md_iid";
static const auto ATTR_MD_ID          = "md_id";
static const auto ATTR_MD_LICENSE     = "md_license";
static const auto ATTR_MD_VERSION     = "md_version";
static const auto ATTR_MD_NAME        = "md_name";
static const auto ATTR_MD_DESCRIPTION = "md_description";
static const auto ATTR_MD_AUTHORS     = "md_authors";
static const auto ATTR_MD_URL         = "md_url";
static const auto ATTR_MD_BIN_DEPS    = "md_bin_dependencies";
static const auto ATTR_MD_LIB_DEPS    = "md_lib_dependencies";
static const auto ATTR_MD_CREDITS     = "md_credits";
static const auto ATTR_MD_PLATFORMS   = "md_platforms";
//static const char *ATTR_MD_MINPY     = "md_min_python";

PyPluginLoader::PyPluginLoader(const Plugin &plugin, const QString &module_path) :
    plugin_(plugin),
    module_path_(module_path)
{
    const QFileInfo file_info(module_path);
    if(!file_info.exists())
        throw runtime_error("File path does not exist");
    else if (file_info.isFile()){
        if (module_path.endsWith(u".py"_s))
            source_path_ = module_path;
        else
            throw NoPluginException("Path is not a python file");
    }
    else if (QFileInfo fi(QDir(module_path).filePath(u"__init__.py"_s)); fi.exists() && fi.isFile())
        source_path_ = fi.absoluteFilePath();
    else
        throw NoPluginException("Python package init file does not exist");

    //
    // Extract metadata
    //

    metadata_.id = file_info.completeBaseName();

    QString source;

    if(QFile file(source_path_); file.open(QIODevice::ReadOnly))
        source = QTextStream(&file).readAll();
    else
        throw runtime_error(format("Can't open source file: {}", file.fileName().toStdString()));

    //Parse the source code using ast and get all FunctionDef and Assign ast nodes
    py::gil_scoped_acquire acquire;
    py::module ast = py::module::import("ast");
    py::object ast_root = ast.attr("parse")(source.toStdString());

    map<QString, py::object> ast_assignments;

    for (auto node : ast_root.attr("body")){
        if (py::isinstance(node, ast.attr("Assign"))){
            auto py_value = node.attr("value");
            for (py::handle target : node.attr("targets")){
                if (py::isinstance(target, ast.attr("Name"))){
                    auto target_name = target.attr("id").cast<string>();

                    if (py::isinstance(py_value, ast.attr("Str"))){
                        const auto value = py_value.attr("value").cast<QString>();

                        if (target_name == ATTR_MD_IID)
                            metadata_.iid = value;

                        else if (target_name == ATTR_MD_ID)
                        {
                            WARN << metadata_.id
                                 << ": Using 'md_id' to overwrite the plugin id is deprecated and "
                                    "will be dropped without replacement in interface v3.0. Plugin "
                                    "ids will be 'python.<modulename>' to avoid conflicts with "
                                    "native plugins.";
                            metadata_.id = value;
                        }

                        else if (target_name == ATTR_MD_NAME)
                            metadata_.name = value;

                        else if (target_name == ATTR_MD_VERSION)
                            metadata_.version = value;

                        else if (target_name == ATTR_MD_DESCRIPTION)
                            metadata_.description = value;

                        else if (target_name == ATTR_MD_LICENSE)
                            metadata_.license = value;

                        else if (target_name == ATTR_MD_URL)
                            metadata_.url = value;

                        else if (target_name == ATTR_MD_AUTHORS)
                            metadata_.authors = {value};

                        else if (target_name == ATTR_MD_LIB_DEPS)
                            metadata_.runtime_dependencies = {value};

                        else if (target_name == ATTR_MD_BIN_DEPS)
                            metadata_.binary_dependencies = {value};

                        else if (target_name == ATTR_MD_CREDITS)
                            metadata_.third_party_credits = {value};
                    }

                    if (py::isinstance(py_value, ast.attr("List"))){
                        QStringList list;
                        for (const py::handle item : py_value.attr("elts").cast<py::list>())
                            if (py::isinstance(item, ast.attr("Str")))
                                list << item.attr("s").cast<py::str>().cast<QString>();

                        if (target_name == ATTR_MD_AUTHORS)
                            metadata_.authors = list;

                        else if (target_name == ATTR_MD_LIB_DEPS)
                            metadata_.runtime_dependencies = list;

                        else if (target_name == ATTR_MD_BIN_DEPS)
                            metadata_.binary_dependencies = list;

                        else if (target_name == ATTR_MD_CREDITS)
                            metadata_.third_party_credits = list;

                        else if (target_name == ATTR_MD_PLATFORMS)
                            metadata_.platforms = list;
                    }
                }
            }
        }
    }

    //
    // Check interface
    //

    if (metadata_.iid.isEmpty())
        throw NoPluginException("No interface id found");

    // Namespace id
    metadata_.id = u"python."_s + metadata_.id;

    QStringList errors;
    static const QRegularExpression regex_version(uR"R(^(\d+)\.(\d+)$)R"_s);

    if (auto match = regex_version.match(metadata_.iid); !match.hasMatch())
        errors << u"Invalid version format: '%1'. Expected <major>.<minor>."_s
                      .arg(match.captured(0));
    else if (uint maj = match.captured(1).toUInt(); maj != MAJOR_INTERFACE_VERSION)
        errors << u"Incompatible major interface version. Expected %1, got %2"_s
                      .arg(MAJOR_INTERFACE_VERSION).arg(maj);
    else if (uint min = match.captured(2).toUInt(); min > MINOR_INTERFACE_VERSION)
        errors << u"Incompatible minor interface version. Up to %1 supported, got %2."_s
                      .arg(MINOR_INTERFACE_VERSION).arg(min);

    if (!metadata_.platforms.isEmpty())
#if defined(Q_OS_MACOS)
        if (!metadata_.platforms.contains(u"Darwin"_s))
#elif defined(Q_OS_UNIX)
        if (!metadata_.platforms.contains(u"Linux"_s))
#elif defined(Q_OS_WIN)
        if (!metadata_.platforms.contains(u"Windows"_s))
#endif
        errors << u"Platform not supported. Supported: "_s + metadata_.platforms.join(u", "_s);

    //
    // Logging category
    //

    // QLoggingCategory does not take ownership of the cstr. Keep the std::string alive.
    logging_category_name = "albert." + metadata_.id.toUtf8().toStdString();
    logging_category = make_unique<QLoggingCategory>(logging_category_name.c_str());

    // Finally set state based on errors
    if (!errors.isEmpty())
        throw runtime_error(errors.join(u", "_s).toUtf8().constData());
}

PyPluginLoader::~PyPluginLoader() = default;


QString PyPluginLoader::path() const { return module_path_; }

const albert::PluginMetaData &PyPluginLoader::metaData() const { return metadata_; }

void PyPluginLoader::load()
{
    if (instance_)
    {
        WARN << metadata_.id << "Plugin already loaded.";
        return;
    }

    // Check binary dependencies
    for (const auto& exec : metadata_.binary_dependencies)
        if (QStandardPaths::findExecutable(exec).isNull())
            throw runtime_error(Plugin::tr("No '%1' in $PATH.").arg(exec).toStdString());

    try {
        QFutureWatcher<void> watcher;
        watcher.setFuture(QtConcurrent::run([this]() {
            load_();
        }));

        QEventLoop loop;
        QObject::connect(&watcher, &decltype(watcher)::finished, &loop, &QEventLoop::quit);
        loop.exec();

        try{
            watcher.waitForFinished();
        } catch (const QUnhandledException &e) {
            if (e.exception())
                std::rethrow_exception(e.exception());
            else
                throw;
        }

    }
    catch (py::error_already_set &e)
    {
        // Catch only import errors, rethrow anything else
        if (!e.matches(PyExc_ModuleNotFoundError))
            throw;

        // ask user if dependencies should be installed
        auto button = question(Plugin::tr("Some modules in the plugin '%1' were not found.\n\n"
                                          "Install dependencies into the virtual environment?")
                                   .arg(metadata_.name));
        if (button == QMessageBox::Yes)
        {
            if (plugin_.installPackages(metadata_.runtime_dependencies))
                return load_();  // On success try to load again
            else
                throw;
        }
        else
            throw;
    }
}

void PyPluginLoader::load_()
{
    py::gil_scoped_acquire acquire;

    try {
        // Import as __name__ = albert.package_name
        const auto importlib_util = py::module::import("importlib.util");
        const auto spec_from_file_location = importlib_util.attr("spec_from_file_location");
        const auto pyspec = spec_from_file_location(u"albert.%1"_s.arg(metadata_.id), source_path_); // Prefix to avoid conflicts
        module_ = importlib_util.attr("module_from_spec")(pyspec);

        // Attach logcat functions
        // https://bugreports.qt.io/browse/QTBUG-117153
        // https://code.qt.io/cgit/pyside/pyside-setup.git/commit/?h=6.5&id=2823763072ce3a2da0210dbc014c6ad3195fbeff
        py::setattr(module_, "debug",
                    py::cpp_function([this](const QString &s){
                        qCDebug((*logging_category),).noquote() << s;
                    }));

        py::setattr(module_, "info",
                    py::cpp_function([this](const QString &s){
                        qCInfo((*logging_category),).noquote() << s;
                    }));

        py::setattr(module_, "warning",
                    py::cpp_function([this](const QString &s){
                        qCWarning((*logging_category),).noquote() << s;
                    }));

        py::setattr(module_, "critical",
                    py::cpp_function([this](const QString &s){
                        qCCritical((*logging_category),).noquote() << s;
                    }));

        // Execute module
        pyspec.attr("loader").attr("exec_module")(module_);
    }
    catch (...) {
        module_ = py::object();
        throw;
    }
}

void PyPluginLoader::unload()
{
    py::gil_scoped_acquire acquire;

    instance_ = py::object();
    module_ = py::object();

    // Run garbage collection to make sure that __del__ will be called.
    py::module::import("gc").attr("collect")();
}

PluginInstance *PyPluginLoader::createInstance()
{
    if (!instance_)
    {
        py::gil_scoped_acquire acquire;
        try {
            instance_ = module_.attr(ATTR_PLUGIN_CLASS)();  // may throw

            if (!py::isinstance<PyPI>(instance_))
                throw runtime_error("Python Plugin class is not of type PluginInstance.");

        } catch (const std::exception &e) {
            instance_ = py::object();
            module_ = py::object();
            throw;
        }
    }
    return instance_.cast<PluginInstance*>();
}
