// Copyright (c) 2017-2024 Manuel Schneider

#pragma once
#include "pybind11/pybind11.h"

#include <QLoggingCategory>
#include <albert/pluginloader.h>
#include <albert/pluginmetadata.h>
#include <memory>
class Plugin;
class QFileInfo;
namespace albert { class PluginProvider; }


class NoPluginException: public std::exception
{
public:
    NoPluginException(const std::string &what): what_(what) {}
    const char *what() const noexcept override { return what_.c_str(); }
private:
    std::string what_;
};


class PyPluginLoader : public albert::PluginLoader
{
public:

    static const int MAJOR_INTERFACE_VERSION = 3;
    static const int MINOR_INTERFACE_VERSION = 1;

    PyPluginLoader(const Plugin &plugin, const QString &module_path);
    ~PyPluginLoader();

    QString path() const noexcept override;
    const albert::PluginMetadata &metadata() const noexcept override;
    void load() noexcept override;
    void unload() noexcept override;
    albert::PluginInstance *instance() noexcept override;

private:

    void load_();

    const Plugin &plugin_;

    const QString module_path_;
    QString source_path_;

    albert::PluginMetadata metadata_;
    std::string logging_category_name;
    std::unique_ptr<QLoggingCategory> logging_category;

    pybind11::module module_;
    pybind11::object py_instance_;
    albert::PluginInstance *instance_;

};
