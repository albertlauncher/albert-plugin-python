// Copyright (c) 2017-2023 Manuel Schneider

#pragma once

#include "cast_specialization.hpp"  // Has to be imported first

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QCoroGenerator>
#include <QString>
#include <QWidget>
#include <albert/fallbackhandler.h>
#include <albert/icon.h>
#include <albert/indexqueryhandler.h>
#include <albert/generatorqueryhandler.h>
#include <albert/logging.h>
#include <albert/plugininstance.h>
#include <albert/pluginloader.h>
#include <albert/pluginmetadata.h>
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;

// Workaround dysfunctional mixin behavior.
// See https://github.com/pybind/pybind11/issues/5405
#define WORKAROUND_PYBIND_5405(name) \
QString name() const override { \
    py::gil_scoped_acquire gil; \
    if (auto py_instance = py::cast(this); py::isinstance<PluginInstance>(py_instance)) \
        return py::cast<PluginInstance*>(py_instance)->loader().metadata().name; \
    PYBIND11_OVERRIDE_PURE(QString, Base, name, ); \
}

class PyPI : public PluginInstance
{
public:

    vector<Extension *> extensions() override
    {
        py::gil_scoped_acquire gil;
        py::function override = py::get_override(this, "extensions");
        if (override)
            return override().cast<std::vector<Extension *>>();  // may throw, is okay
        else if (auto py_instance = py::cast(this); py::isinstance<Extension>(py_instance))
            return {py_instance.cast<Extension *>()};
        else
            return {};
    }

    void writeConfig(QString key, const py::object &value) const
    {
        py::gil_scoped_acquire a;
        auto s = this->settings();

        if (py::isinstance<py::str>(value))
            s->setValue(key, value.cast<QString>());

        else if (py::isinstance<py::bool_>(value))
            s->setValue(key, value.cast<bool>());

        else if (py::isinstance<py::int_>(value))
            s->setValue(key, value.cast<int>());

        else if (py::isinstance<py::float_>(value))
            s->setValue(key, value.cast<double>());

        else
            WARN << "Invalid data type to write to settings. Has to be one of bool|int|float|str.";
    }

    py::object readConfig(QString key, const py::object &type) const
    {
        py::gil_scoped_acquire a;
        QVariant var = this->settings()->value(key);

        if (var.isNull())
            return py::none();

        if (type.attr("__name__").cast<QString>() == u"str"_s)
            return py::cast(var.toString());

        else if (type.attr("__name__").cast<QString>() == u"bool"_s)
            return py::cast(var.toBool());

        else if (type.attr("__name__").cast<QString>() == u"int"_s)
            return py::cast(var.toInt());

        else if (type.attr("__name__").cast<QString>() == u"float"_s)
            return py::cast(var.toDouble());

        else
            WARN << "Invalid data type to read from settings. Has to be one of bool|int|float|str.";

        return py::none();
    }

    // Dynamically create widget
    QWidget *buildConfigWidget() override
    {
        auto *w = new QWidget;
        auto *l = new QFormLayout(w);
        l->setContentsMargins(0,0,0,0);
        w->setLayout(l);

        static const char *key_label = "label";
        static const char *key_property = "property";
        static const char *key_text = "text";
        static const char *key_type = "type";
        static const char *key_items = "items";

        try
        {
            py::gil_scoped_acquire a;
            if (auto override = pybind11::get_override(static_cast<const PluginInstance*>(this), "configWidget"))
            {
                for (auto item : py::list(override()))
                {
                    auto row_spec = py::cast<py::dict>(item);

                    if (auto type = row_spec[key_type].cast<QString>();
                        type == u"lineedit"_s)
                    {
                        auto *fw = new QLineEdit(w);
                        auto property_name = row_spec[key_property].cast<QString>();

                        fw->setText(getattr<QString>(property_name));

                        QObject::connect(fw, &QLineEdit::editingFinished, fw, [this, fw, property_name](){
                            py::gil_scoped_acquire aq;
                            try { setattr(property_name, fw->text()); }
                            catch (const std::exception &e) { CRIT << e.what(); }
                        });

                        applyWidgetPropertiesIfAny(fw, row_spec);

                        l->addRow(row_spec[key_label].cast<QString>(), fw);
                    }
                    else if (type == u"checkbox"_s)
                    {
                        auto *fw = new QCheckBox(w);
                        auto property_name = row_spec[key_property].cast<QString>();

                        fw->setChecked(getattr<bool>(property_name));

                        QObject::connect(fw, &QCheckBox::toggled, fw, [this, property_name](bool checked){
                            py::gil_scoped_acquire aq;
                            try { setattr(property_name, checked); }
                            catch (const std::exception &e) { CRIT << e.what(); }
                        });

                        applyWidgetPropertiesIfAny(fw, row_spec);

                        l->addRow(row_spec[key_label].cast<QString>(), fw);
                    }
                    else if (type == u"combobox"_s)
                    {
                        auto *fw = new QComboBox(w);
                        auto property_name = row_spec[key_property].cast<QString>();

                        for (auto &value : row_spec[key_items].cast<py::list>())
                            fw->addItem(value.cast<QString>());

                        fw->setCurrentText(getattr<QString>(property_name));

                        QObject::connect(fw, &QComboBox::currentIndexChanged, fw, [this, cb=fw, property_name](){
                            py::gil_scoped_acquire aq;
                            try { setattr(property_name, cb->currentText()); }
                            catch (const std::exception &e) { CRIT << e.what(); }
                        });

                        applyWidgetPropertiesIfAny(fw, row_spec);

                        l->addRow(row_spec[key_label].cast<QString>(), fw);
                    }
                    else if (type == u"spinbox"_s)
                    {
                        auto *fw = new QSpinBox(w);
                        auto property_name = row_spec[key_property].cast<QString>();

                        fw->setValue(getattr<int>(property_name));

                        QObject::connect(fw, &QSpinBox::valueChanged, fw, [this, property_name](int value){
                            py::gil_scoped_acquire aq;
                            try { setattr(property_name, value); }
                            catch (const std::exception &e) { CRIT << e.what(); }
                        });

                        applyWidgetPropertiesIfAny(fw, row_spec);

                        l->addRow(row_spec[key_label].cast<QString>(), fw);
                    }
                    else if (type == u"doublespinbox"_s)
                    {
                        auto *fw = new QDoubleSpinBox(w);
                        auto property_name = row_spec[key_property].cast<QString>();

                        fw->setValue(getattr<double>(property_name));

                        QObject::connect(fw, &QDoubleSpinBox::valueChanged, fw, [this, property_name](double value){
                            py::gil_scoped_acquire aq;
                            try { setattr(property_name, value); }
                            catch (const std::exception &e) { CRIT << e.what(); }
                        });

                        applyWidgetPropertiesIfAny(fw, row_spec);

                        l->addRow(row_spec[key_label].cast<QString>(), fw);
                    }
                    else if (type == u"label"_s)
                    {
                        auto *lbl = new QLabel(w);
                        lbl->setText(row_spec[key_text].cast<QString>());
                        lbl->setWordWrap(true);
                        lbl->setOpenExternalLinks(true);
                        applyWidgetPropertiesIfAny(lbl, row_spec);
                        l->addRow(lbl);
                    }
                    else
                        throw runtime_error(format("Invalid config widget type: {}", type.toStdString()));
                }
                return w;
            }
        }
        catch (const std::exception &e)
        {
            CRIT << e.what();
            delete w;
        }
        return nullptr;
    }

private:

    /// Get a property of this Python instance
    /// DOES NOT LOCK THE GIL!
    template <class T>
    inline T getattr(QString property_name)
    { return py::getattr(py::cast(this), py::cast(property_name)).template cast<T>(); }

    /// Set a property of this Python instance
    /// DOES NOT LOCK THE GIL!
    template <class T>
    inline void setattr(QString property_name, T value)
    { return py::setattr(py::cast(this), py::cast(property_name), py::cast(value)); }

    static void applyWidgetPropertiesIfAny(QWidget *widget, py::dict spec)
    {
        static const char *key_widget_properties = "widget_properties";
        py::gil_scoped_acquire a;
        if (spec.contains(key_widget_properties))
        {
            for (auto &[k, v] : spec[key_widget_properties].cast<py::dict>())
            {
                std::string property_name = py::cast<string>(k);

                if (py::isinstance<py::bool_>(v))
                    widget->setProperty(property_name.c_str(), py::cast<bool>(v));

                else if (py::isinstance<py::int_>(v))
                    widget->setProperty(property_name.c_str(), py::cast<int>(v));

                else if (py::isinstance<py::float_>(v))
                    widget->setProperty(property_name.c_str(), py::cast<double>(v));

                else if (py::isinstance<py::str>(v))
                    widget->setProperty(property_name.c_str(), py::cast<QString>(v));

                else
                    WARN << "Invalid data type set as widget property. Has to be one of bool|int|float|str.";
            }
        }
    }
};


class PyItemTrampoline : public Item, public py::trampoline_self_life_support
{
public:
    QString id() const override
    { PYBIND11_OVERRIDE_PURE(QString, Item, id); }

    QString text() const override
    { PYBIND11_OVERRIDE_PURE(QString, Item, text); }

    QString subtext() const override
    { PYBIND11_OVERRIDE_PURE(QString, Item, subtext); }

    QString inputActionText() const override
    { PYBIND11_OVERRIDE_PURE(QString, Item, inputActionText); }

    std::unique_ptr<Icon> icon() const override
    { PYBIND11_OVERRIDE_PURE(unique_ptr<Icon>, Item, icon); }

    vector<Action> actions() const override
    { PYBIND11_OVERRIDE_PURE(vector<Action>, Item, actions); }
};


template <class Base = Extension>
class PyExtension : public Base
{
public:
    WORKAROUND_PYBIND_5405(id)
    WORKAROUND_PYBIND_5405(name)
    WORKAROUND_PYBIND_5405(description)
};


// class PyQueryExecution : public QueryExecution, public py::trampoline_self_life_support
// {
// public:
//     using QueryExecution::QueryExecution;

//     void cancel() override
//     { PYBIND11_OVERRIDE_PURE(void, QueryExecution, cancel, ); }

//     void fetchMore() override
//     { PYBIND11_OVERRIDE_PURE(void, QueryExecution, fetchMore, ); }

//     bool canFetchMore() const override
//     { PYBIND11_OVERRIDE_PURE(bool, QueryExecution, canFetchMore, ); }

//     bool isActive() const override
//     { PYBIND11_OVERRIDE_PURE(bool, QueryExecution, isActive, ); }
// };


template <class Base = QueryHandler>
class PyQueryHandler : public PyExtension<Base>
{
public:
    QString synopsis(const QString &query) const override
    { PYBIND11_OVERRIDE(QString, Base, synopsis, query); }

    bool allowTriggerRemap() const override
    { PYBIND11_OVERRIDE(bool, Base, allowTriggerRemap, ); }

    QString defaultTrigger() const override
    { PYBIND11_OVERRIDE(QString, Base, defaultTrigger, ); }

    void setTrigger(const QString &trigger) override
    { PYBIND11_OVERRIDE(void, Base, setTrigger, trigger); }

    bool supportsFuzzyMatching() const override
    { PYBIND11_OVERRIDE(bool, Base, supportsFuzzyMatching, ); }

    void setFuzzyMatching(bool enabled) override
    { PYBIND11_OVERRIDE(void, Base, setFuzzyMatching, enabled); }

    // unique_ptr<QueryExecution> execution(QueryContext &context) override
    // { PYBIND11_OVERRIDE_PURE(unique_ptr<QueryExecution>, Base, execution, &context); }
};


// This class makes sure that the GIL is locked when the coroutine frame is unwound
class ItemGeneratorWrapper
{
    py::function fn_next;

public:
    ItemGeneratorWrapper(py::function override, QueryContext &ctx)
    {
        py::gil_scoped_acquire acquire;

        // Make sure to release the function object, before releasing the GIL
        py::function fn_items = ::move(override);

        auto gen = fn_items(&ctx); // may throw

        if (!gen)
            throw runtime_error("Failed creating generator from \"items\" override.");

        if (!py::hasattr(gen, "__next__"))
            throw runtime_error("Generator object has no attr \"__next__\".");

        fn_next = gen.attr("__next__");
    }

    ~ItemGeneratorWrapper()
    {
        py::gil_scoped_acquire acquire;
        fn_next = {};
    }

    optional<vector<shared_ptr<Item>>> next()
    {
        py::gil_scoped_acquire acquire;
        try {
            return fn_next().cast<vector<shared_ptr<Item>>>();
        } catch (const py::error_already_set &e) {
            if (e.matches(PyExc_StopIteration))
                return nullopt;  // Expected end
            else
                throw;
        } catch (const exception &e) {
            CRIT << e.what();
            throw;
        } catch (...) {
            throw;
        }
        return nullopt;
    }

    static ItemGenerator generator(py::function fn_items, QueryContext &ctx)
    {
        ItemGeneratorWrapper generator(::move(fn_items), ctx);
        while (auto next = generator.next())
            co_yield ::move(*next);
    }
};

template<typename T>
py::function getOverrideLocked(const T *this_ptr, const char *name)
{
    py::gil_scoped_acquire acquire;
    return py::get_override(this_ptr, name);
}

template <class Base = GeneratorQueryHandler>
class PyGeneratorQueryHandler : public PyQueryHandler<Base>
{
protected:

public:
    // No type mismatch workaround required since base class is not called.
    ItemGenerator items(QueryContext &context) override
    {
        auto fn_items_override = getOverrideLocked(this, "items");
        if (fn_items_override)
            // ! This move releases the py object, such that GIL is not required on destruction
            return ItemGeneratorWrapper::generator(::move(fn_items_override), context);
        else
            throw runtime_error("Pure virtual function \"items\"");
    }

    // //
    // // This is required due to the "final" quirks of the pybind trampoline chain
    // //
    // // QueryHandler            | declares pure
    // // GeneratorQueryHandler   | overrides "final"
    // // PyQueryHandler          | overrides "pure" on python side
    // // PyGeneratorQueryHandler | calls will throw "call to pure" error
    // //
    // unique_ptr<QueryExecution> execution(QueryContext &context) override
    // {
    //     // PyBind does not suport passing reference, but instead tries to copy.
    //     // Workaround by converting to pointer.
    //     // This is needed because PYBIND11_OVERRIDE_PURE would introduce type mismatch.
    //     PYBIND11_OVERRIDE_IMPL(unique_ptr<QueryExecution>, Base, "execution", &context);  // returns on success
    //     return Base::execution(context);  // otherwise call base class
    // }
};

template <class Base = RankedQueryHandler>
class PyRankedQueryHandler : public PyGeneratorQueryHandler<Base>
{
public:
    // No type mismatch workaround required since base class is not called.
    vector<RankItem> rankItems(QueryContext &context) override
    { PYBIND11_OVERRIDE_PURE(vector<RankItem>, Base, rankItems, &context); }

    //
    // This is required due to the "final" quirks of the pybind trampoline chain
    //
    // GeneratorQueryHandler   | declares pure
    // RankedQueryHandler      | overrides "final"
    // PyGeneratorQueryHandler | overrides "pure" on python side
    // PyRankedQueryHandler    | calls will throw "call to pure" error
    //
    ItemGenerator items(QueryContext &context) override
    {
        auto fn_items_override = getOverrideLocked(this, "items");
        if (fn_items_override)
            // ! This move releases the py object, such that GIL is not required on destruction
            return ItemGeneratorWrapper::generator(::move(fn_items_override), context);
        else
            return Base::items(context);
    }
};


template <class Base = GlobalQueryHandler>
class PyGlobalQueryHandler : public PyRankedQueryHandler<Base>
{
    //
    // This is required due to the "final" quirks of the pybind trampoline chain
    //
    // GeneratorQueryHandler   | declares pure
    // RankedQueryHandler      | overrides "final"
    // PyGeneratorQueryHandler | overrides "pure" on python side
    // PyRankedQueryHandler    | calls will throw "call to pure" error
    //
    ItemGenerator items(QueryContext &context) override
    {
        auto fn_items_override = getOverrideLocked(this, "items");
        if (fn_items_override)
            // ! This move releases the py object, such that GIL is not required on destruction
            return ItemGeneratorWrapper::generator(::move(fn_items_override), context);
        else
            return Base::items(context);
    }

    // No type mismatch workaround required since base class is not called.
    vector<RankItem> rankItems(QueryContext &context) override
    { PYBIND11_OVERRIDE_PURE(vector<RankItem>, Base, rankItems, &context); }

};


template <class Base = IndexQueryHandler>
class PyIndexQueryHandler : public PyGlobalQueryHandler<Base>
{
public:
    void updateIndexItems() override
    { PYBIND11_OVERRIDE_PURE(void, Base, updateIndexItems); }

    //
    // This is required due to the "final" quirks of the pybind trampoline chain
    //
    // GeneratorQueryHandler   | declares pure
    // RankedQueryHandler      | overrides "final"
    // PyGeneratorQueryHandler | overrides "pure" on python side
    // PyRankedQueryHandler    | calls will throw "call to pure" error
    //
    ItemGenerator items(QueryContext &context) override
    {
        auto fn_items_override = getOverrideLocked(this, "items");
        if (fn_items_override)
            // ! This move releases the py object, such that GIL is not required on destruction
            return ItemGeneratorWrapper::generator(::move(fn_items_override), context);
        else
            return Base::items(context);
    }

    //
    // This is required due to the "final" quirks of the pybind trampoline chain
    //
    // RankedQueryHandler   | declares pure
    // IndexQueryHandler    | overrides "final"
    // PyRankedQueryHandler | overrides "pure" on python side
    // PyIndexQueryHandler  | calls will throw "call to pure" error
    //
    vector<RankItem> rankItems(QueryContext &context) override
    {
        // PyBind does not suport passing reference, but instead tries to copy.
        // Workaround by converting to pointer.
        // This is needed because PYBIND11_OVERRIDE_PURE would introduce type mismatch.
        PYBIND11_OVERRIDE_IMPL(vector<RankItem>, Base, "rankItems", &context);  // returns on success
        return Base::rankItems(context);  // otherwise call base class
    }
};


template <class Base = FallbackHandler>
class PyFallbackHandler : public PyExtension<Base>
{
public:
    vector<shared_ptr<Item>> fallbacks(const QString &query) const override
    { PYBIND11_OVERRIDE_PURE(vector<shared_ptr<Item>>, FallbackHandler, fallbacks, query); }
};
