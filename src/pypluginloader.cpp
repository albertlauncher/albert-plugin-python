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
#include <albert/plugininstance.h>
#include <chrono>
namespace py = pybind11;
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std::chrono;
using namespace std;

static const auto ATTR_PLUGIN_CLASS   = "Plugin";
static const auto ATTR_MD_IID         = "md_iid";
static const auto ATTR_MD_LICENSE     = "md_license";
static const auto ATTR_MD_VERSION     = "md_version";
static const auto ATTR_MD_NAME        = "md_name";
static const auto ATTR_MD_DESCRIPTION = "md_description";
static const auto ATTR_MD_AUTHORS     = "md_authors";
static const auto ATTR_MD_MAINTAINERS = "md_maintainers";
static const auto ATTR_MD_URL         = "md_url";
static const auto ATTR_MD_README_URL  = "md_readme_url";
static const auto ATTR_MD_BIN_DEPS    = "md_bin_dependencies";
static const auto ATTR_MD_LIB_DEPS    = "md_lib_dependencies";
static const auto ATTR_MD_CREDITS     = "md_credits";
static const auto ATTR_MD_PLATFORMS   = "md_platforms";
//static const char *ATTR_MD_MINPY     = "md_min_python";

static QString extractAstString(const py::handle &ast_assign_node)
{
    // if (py::isinstance(ast_value, ast.attr("Constant"))
    return ast_assign_node.attr("value").attr("value").cast<QString>();
}

static QStringList extractAstStringList(const py::handle &ast_assign_node)
{
    QStringList list;
    for (const py::handle item : ast_assign_node.attr("value").attr("elts").cast<py::list>())
        // if (py::isinstance(item, ast.attr("Constant")))
        list << item.attr("value").cast<py::str>().cast<QString>();
    return list;
}

static PluginMetadata extractMetadata(const QString &path)
{
    PluginMetadata metadata;

    QString source_code;

    if(QFile file(path); file.open(QIODevice::ReadOnly))
        source_code = QTextStream(&file).readAll();
    else
        throw runtime_error(format("Can't open source file: {}", file.fileName().toStdString()));

            //Parse the source code using ast and get all FunctionDef and Assign ast nodes
    py::gil_scoped_acquire acquire;
    py::module ast = py::module::import("ast");
    py::object ast_root = ast.attr("parse")(source_code.toStdString());

    map<QString, py::object> ast_assignments;

    for (auto node : ast_root.attr("body"))
    {
        if (py::isinstance(node, ast.attr("Assign")))
        {
            for (py::handle target : node.attr("targets"))
            {
                if (py::isinstance(target, ast.attr("Name")))
                {
                    if (auto target_name = target.attr("id").cast<string>();
                        target_name == ATTR_MD_IID)
                        metadata.iid = extractAstString(node);

                    else if (target_name == ATTR_MD_NAME)
                        metadata.name = extractAstString(node);

                    else if (target_name == ATTR_MD_VERSION)
                        metadata.version = extractAstString(node);

                    else if (target_name == ATTR_MD_DESCRIPTION)
                        metadata.description = extractAstString(node);

                    else if (target_name == ATTR_MD_LICENSE)
                        metadata.license = extractAstString(node);

                    else if (target_name == ATTR_MD_URL)
                        metadata.url = extractAstString(node);

                    else if (target_name == ATTR_MD_README_URL)
                        metadata.readme_url = extractAstString(node);

                    else if (target_name == ATTR_MD_AUTHORS)
                        metadata.authors = extractAstStringList(node);

                    else if (target_name == ATTR_MD_MAINTAINERS)
                        metadata.maintainers = extractAstStringList(node);

                    else if (target_name == ATTR_MD_LIB_DEPS)
                        metadata.runtime_dependencies = extractAstStringList(node);

                    else if (target_name == ATTR_MD_BIN_DEPS)
                        metadata.binary_dependencies = extractAstStringList(node);

                    else if (target_name == ATTR_MD_CREDITS)
                        metadata.third_party_credits = extractAstStringList(node);

                    else if (target_name == ATTR_MD_PLATFORMS)
                        metadata.platforms = extractAstStringList(node);

                }
            }
        }
    }

    return metadata;
}

PyPluginLoader::PyPluginLoader(const Plugin &plugin, const QString &module_path) :
    plugin_(plugin),
    module_path_(module_path),
    instance_(nullptr)
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

    metadata_ = extractMetadata(source_path_);
    metadata_.id = u"python."_s + file_info.completeBaseName();  // Namespace id
    metadata_.load_type = PluginMetadata::LoadType::User;


    //
    // Check interface
    //

    if (metadata_.iid.isEmpty())
        throw NoPluginException("No interface id found");

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

QString PyPluginLoader::path() const noexcept { return module_path_; }

const albert::PluginMetadata &PyPluginLoader::metadata() const noexcept{ return metadata_; }

void PyPluginLoader::load() noexcept
{
    auto future = QtConcurrent::run([this]
    {
        // Check binary dependencies
        for (const auto& exec : as_const(metadata_.binary_dependencies))
            if (QStandardPaths::findExecutable(exec).isNull())
                throw runtime_error(Plugin::tr("No '%1' in $PATH.").arg(exec).toStdString());

        // Check runtime dependencies
        if (!metadata_.runtime_dependencies.isEmpty()
            && !plugin_.checkPackages(metadata_.runtime_dependencies))
            plugin_.installPackages(metadata_.runtime_dependencies);

        py::gil_scoped_acquire acquire;

        auto tp = system_clock::now();

        // Import as __name__ = albert.package_name
        const auto importlib_util = py::module::import("importlib.util");
        const auto spec_from_file_location = importlib_util.attr("spec_from_file_location");
        const auto pyspec = spec_from_file_location(u"albert.%1"_s  // Prefix to avoid conflicts
                                                        .arg(metadata_.id), source_path_);
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

        DEBG << u"%1: Module loaded in %2 ms (%3)"_s
                    .arg(metadata().id)
                    .arg(duration_cast<milliseconds>(system_clock::now() - tp).count())
                    .arg(source_path_);
    })
    .then(this, [this] {
        auto tp = system_clock::now();
        py::gil_scoped_acquire acquire;
        current_loader = this;

        if (py_instance_ = module_.attr(ATTR_PLUGIN_CLASS)();  // may throw
            !py::isinstance<PyPI>(py_instance_))
            throw runtime_error("Python Plugin class is not of type PluginInstance.");

        instance_ = py_instance_.cast<PluginInstance*>(); // should never fail
        if (!instance_)
            throw runtime_error("Plugin instance is null.");

        DEBG << u"%1: Instantiated in %2 ms"_s
                    .arg(metadata().id)
                    .arg(duration_cast<milliseconds>(system_clock::now() - tp).count());

        emit finished({});
    })
    .onCanceled(this, [] {
    })
    .onFailed(this, [](const QUnhandledException &que) {
        if (que.exception())
            rethrow_exception(que.exception());
        else
            throw runtime_error("QUnhandledException::exception() returned nullptr.");
    })
    .onFailed(this, [this](const exception &e) {
        unload();
        emit finished(QString::fromStdString(e.what()));
    })
    .onFailed(this, [this]{
        unload();
        emit finished(u"Unknown exception while loading plugin."_s);
    });
}

void PyPluginLoader::unload() noexcept
{
    py::gil_scoped_acquire acquire;

    instance_= nullptr;
    py_instance_ = py::object();
    module_ = py::object();

    // Run garbage collection to make sure that __del__ will be called.
    py::module::import("gc").attr("collect")();
}

PluginInstance *PyPluginLoader::instance() noexcept { return instance_; }
