// Copyright (c) 2022-2025 Manuel Schneider

#include <pybind11/functional.h>
#include <pybind11/native_enum.h>
#include <pybind11/stl.h>
#include "cast_specialization.hpp"  // Has to be imported first
#include "queryexecution.h"
#include "queryresults.h"
#include "trampolineclasses.hpp"

#include "albert/fallbackhandler.h"
#include "albert/icon.h"
#include "albert/indexqueryhandler.h"
#include "albert/item.h"
#include "albert/matcher.h"
#include "albert/plugininstance.h"
#include "albert/pluginloader.h"
#include "albert/pluginmetadata.h"
#include "albert/query.h"
#include "albert/standarditem.h"
#include "albert/systemutil.h"
#include "albert/usagescoring.h"
#include "test.h"
#include <QStandardPaths>
#include <QTest>
#include <QTimer>
#include <albert/indexqueryhandler.h>
using namespace albert;
using namespace py::literals;
using namespace std;
QTEST_MAIN(PythonTests)

struct MockQueryContext : public QueryContext
{
    MockQueryContext(QueryHandler *handler,
                     QString trigger = "",
                     QString string = "")
        : handler_(handler)
        , trigger_(trigger)
        , query_(string)
        , is_valid_(true)
    {
    }

    QueryHandler *handler_;
    QString trigger_;
    QString query_;
    bool is_valid_;
    UsageScoring usage_scoring_;

    const QueryHandler &handler() const override { return *handler_; }
    QString trigger() const override { return trigger_; }
    QString query() const override { return query_; }
    bool isValid() const override { return is_valid_; }
    const UsageScoring &usageScoring() const override { return usage_scoring_; }
};

struct MockHandler : public QueryHandler
{
    QString id() const override { return "test_id"; }
    QString name() const override { return "test_name"; }
    QString description() const override { return "test_desctription"; }
    std::unique_ptr<QueryExecution> execution(QueryContext) override { return nullptr; }
};

struct MockLoader : public PluginLoader
{
    py::object class_to_load;
    py::object py_instance;  // owner
    PluginInstance* cpp_instance;  // borrowed

    PluginMetadata metadata_{
        .iid="iid",
        .id="id",
        .version="version",
        .name="name",
        .description="description",
        .license="license",
        .url="url",
        .readme_url="readme_url",
        .translations={"translations"},
        .authors={"authors"},
        .maintainers={"maintainers"},
        .runtime_dependencies={"runtime_dependencies"},
        .binary_dependencies={"binary_dependencies"},
        .plugin_dependencies={"plugin_dependencies"},
        .third_party_credits={"third_party_credits"},
        .platforms={"platforms"},
        .load_type=PluginMetadata::LoadType::User
    };
    QString path() const noexcept override { return "path"; }
    const PluginMetadata &metadata() const noexcept override { return metadata_; }
    void load() noexcept override
    {
        current_loader = this;
        py_instance = class_to_load();
        cpp_instance = py_instance.cast<PluginInstance*>();
    }
    void unload() noexcept override
    {
        cpp_instance = nullptr;
        py_instance = py::object();
        py::module::import("gc").attr("collect")();
    }
    PluginInstance *instance() noexcept override { return cpp_instance; }
};

py::module albert_module;

py::object PyAction;
py::object PyItem;
py::object PyStandardItem;
py::object PyRankItem;
py::object PyIndexItem;
py::object PyMatcher;
py::object PyMatchConfig;
py::object PyMatch;

py::object py_get_test_action_variable;
py::object py_increment_test_action_variable;
py::object py_make_test_action;
py::object py_make_test_icon;
py::object py_make_test_standard_item;

static auto test_initialization = R"(
from albert import *


test_action_variable = 0


def get_test_action_variable():
    return test_action_variable


def increment_test_action_variable():
    global test_action_variable
    test_action_variable += 1


def make_test_action():
    return Action(
        id="test_action_id",
        text="test_action_text",
        callable=increment_test_action_variable
    )


def make_test_icon():
    return Icon.grapheme("A")


def make_test_standard_item(number:int):
    return StandardItem(
        id="id_" + str(number),
        text="text_" + str(number),
        subtext="subtext_" + str(number),
        icon_factory=make_test_icon,
        actions=[make_test_action()] * number,
        input_action_text="input_action_text_" + str(number)
    )
)";

static shared_ptr<Item> makeTestItem(int number)
{
    return StandardItem::make(
        "id_" + QString::number(number),
        "text_" + QString::number(number),
        "subtext_" + QString::number(number),
        []{ return Icon::grapheme("!"); },
        {{
            "test_action_id",
            "test_action_text",
            []{}
        }},
        "input_action_text_" + QString::number(number)
    );
}

static void testTestItem(Item *item, int number)
{
    QCOMPARE(item->id(), "id_" + QString::number(number));
    QCOMPARE(item->text(), "text_" + QString::number(number));
    QCOMPARE(item->subtext(), "subtext_" + QString::number(number));
    QCOMPARE(item->inputActionText(), "input_action_text_" + QString::number(number));
    const auto icon = item->icon();
    QVERIFY(icon != nullptr);
    QVERIFY(dynamic_cast<Icon*>(icon.get()) != nullptr);
    QCOMPARE(item->actions().size(), number);
}

template <typename... Args>
py::object instantiateClass(const char *py_src, Args&&... args)
{
    py::dict locals;
    py::exec(py_src, py::globals(), locals);
    return locals["Class"](std::forward<Args>(args)...);
}

//==================================================================================================
//==================================================================================================

void PythonTests::initTestCase()
{
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    if (auto status = Py_InitializeFromConfig(&config); PyStatus_Exception(status))
        throw runtime_error(QString("Failed initializing the interpreter: %1 %2")
                                .arg(status.func, status.err_msg).toStdString());

    PyConfig_Clear(&config);

    py::exec(test_initialization);

    albert_module = py::module::import("albert");

    // Classes
    PyAction = albert_module.attr("Action");
    PyItem = albert_module.attr("Item");
    PyStandardItem = albert_module.attr("StandardItem");
    PyRankItem = albert_module.attr("RankItem");
    PyIndexItem = albert_module.attr("IndexItem");
    PyMatcher = albert_module.attr("Matcher");
    PyMatchConfig = albert_module.attr("MatchConfig");
    PyMatch = albert_module.attr("Match");

    // Functions
    py_get_test_action_variable = py::globals()["get_test_action_variable"];
    py_increment_test_action_variable = py::globals()["increment_test_action_variable"];
    py_make_test_action = py::globals()["make_test_action"];
    py_make_test_icon = py::globals()["make_test_icon"];
    py_make_test_standard_item = py::globals()["make_test_standard_item"];
}

void PythonTests::testPluginInstance()
{
    py::dict locals;

    py::exec(R"(
class Plugin(PluginInstance):

    def __init__(self):
        PluginInstance.__init__(self)
        self.property_lineedit = "lineedit"
        self.property_checkbox = True
        self.property_combobox = "id_2"
        self.property_spinbox = 5
        self.property_doublespinbox = 5.5

    def configWidget(self):
        return [
            {
                'type': 'label',
                'text': "test_label",
                'widget_properties': {
                    'textFormat': 'Qt::MarkdownText'
                }
            },
            {
                'type': 'lineedit',
                'label': "test_lineedit",
                'property': "property_lineedit",
                'widget_properties': {
                    'placeholderText': 'test_placeholder'
                }
            },
            {
                'type': 'checkbox',
                'label': "test_checkbox",
                'property': "property_checkbox",
            },
            {
                'type': 'combobox',
                'label': "test_combobox",
                'property': "property_combobox",
                'items': ["id_1", "id_2", "id_3"],
            },
            {
                'type': 'spinbox',
                'label': "test_spinbox",
                'property': "property_spinbox",
            },
            {
                'type': 'doublespinbox',
                'label': "test_doublespinbox",
                'property': "property_doublespinbox",
            }
        ]

    def extensions(self):
        return []
)", py::globals(), locals);

    MockLoader mock_loader;
    mock_loader.class_to_load = locals["Plugin"];
    mock_loader.load();

    // Python interface

    QCOMPARE(mock_loader.py_instance.attr("id")().cast<QString>(), "id");
    QCOMPARE(mock_loader.py_instance.attr("name")().cast<QString>(), "name");
    QCOMPARE(mock_loader.py_instance.attr("description")().cast<QString>(), "description");

    // partially since no app available
    QVERIFY(py::str(mock_loader.py_instance.attr("cacheLocation")()).cast<QString>()
                .startsWith(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)));
    QVERIFY(py::str(mock_loader.py_instance.attr("configLocation")()).cast<QString>()
                .startsWith(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)));
    QVERIFY(py::str(mock_loader.py_instance.attr("dataLocation")()).cast<QString>()
                .startsWith(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)));

    // readConfig?
    // writeConfig?

    // Trampoline

    auto extensions = mock_loader.py_instance.attr("extensions")().cast<vector<Extension *>>();
    QCOMPARE(extensions.size(), 0);

    auto widget = unique_ptr<QWidget>(mock_loader.cpp_instance->buildConfigWidget());
    QVERIFY(widget != nullptr);

    auto *label = widget->findChild<QLabel*>();
    QVERIFY(label != nullptr);
    QCOMPARE(label->text(), "test_label");
    QCOMPARE(label->textFormat(), Qt::MarkdownText);

    auto lineedit = widget->findChild<QLineEdit*>();
    QVERIFY(lineedit != nullptr);
    QCOMPARE(lineedit->text(), "lineedit");
    QCOMPARE(lineedit->placeholderText(), "test_placeholder");
    lineedit->setText("new_lineedit");
    emit lineedit->editingFinished();
    QCOMPARE(mock_loader.py_instance.attr("property_lineedit").cast<QString>(), "new_lineedit");

    auto checkbox = widget->findChild<QCheckBox*>();
    QVERIFY(checkbox != nullptr);
    QCOMPARE(checkbox->isChecked(), true);
    checkbox->toggle();
    QCOMPARE(mock_loader.py_instance.attr("property_checkbox").cast<bool>(), false);

    auto combobox = widget->findChild<QComboBox*>();
    QVERIFY(combobox != nullptr);
    QCOMPARE(combobox->currentText(), "id_2");
    combobox->setCurrentIndex(0);
    QCOMPARE(mock_loader.py_instance.attr("property_combobox").cast<QString>(), "id_1");

    auto spinbox = widget->findChild<QSpinBox*>();
    QVERIFY(spinbox != nullptr);
    QCOMPARE(spinbox->value(), 5);
    spinbox->setValue(10);
    QCOMPARE(mock_loader.py_instance.attr("property_spinbox").cast<int>(), 10);

    auto doublespinbox = widget->findChild<QDoubleSpinBox*>();
    QVERIFY(doublespinbox != nullptr);
    QCOMPARE(doublespinbox->value(), 5.5);
    doublespinbox->setValue(10.5);
    QCOMPARE(mock_loader.py_instance.attr("property_doublespinbox").cast<double>(), 10.5);
}

void PythonTests::testExtensionPluginInstance()
{
    py::dict locals;

    py::exec(R"(
class Plugin(PluginInstance, GeneratorQueryHandler):
    def __init__(self):
        PluginInstance.__init__(self)
        GeneratorQueryHandler.__init__(self)
)", py::globals(), locals);

    MockLoader mock_loader;
    mock_loader.class_to_load = locals["Plugin"];
    mock_loader.load();
    auto inst = mock_loader.cpp_instance;

    // Test default extensions factory
    auto extensions = inst->extensions();
    QCOMPARE(extensions.size(), 1);
    auto h = dynamic_cast<GeneratorQueryHandler*>(extensions[0]);
    QVERIFY(h != nullptr);

    // Test mixin-emulation
    QCOMPARE(h->id(), "id");
    QCOMPARE(h->name(), "name");
    QCOMPARE(h->description(), "description");
}

void PythonTests::testAction()
{
    auto python = PyAction(
        "id"_a="test_action_id",
        "text"_a="test_action_text",
        "callable"_a=py::globals()["increment_test_action_variable"]
        );
    auto native = python.cast<Action&>();

    QCOMPARE(native.id, "test_action_id");
    QCOMPARE(native.text, "test_action_text");
    QCOMPARE(py_get_test_action_variable().cast<int>(), 0);
    native.function();
    QCOMPARE(py_get_test_action_variable().cast<int>(), 1);
}

void PythonTests::testItem()
{
    // BINDINGS

    class Native : public Item {
        QString id() const override { return "id"; }
        QString text() const override { return "text"; }
        QString subtext() const override { return "subtext"; }
        QString inputActionText() const override { return "input_action_text"; }
        unique_ptr<Icon> icon() const override { return Icon::grapheme("!"); }
        vector<Action> actions() const override { return {{"id", "text", []{} }}; }
    };

    auto python = py::cast(shared_ptr<Item>(new Native));

    QCOMPARE(python.attr("id")().cast<QString>(), "id");
    QCOMPARE(python.attr("text")().cast<QString>(), "text");
    QCOMPARE(python.attr("subtext")().cast<QString>(), "subtext");
    QCOMPARE(python.attr("inputActionText")().cast<QString>(), "input_action_text");
    QVERIFY(python.attr("icon")().cast<unique_ptr<Icon>>());
    QCOMPARE(py::len(python.attr("actions")()), 1);

    // TRAMPOLINE (pure)

    python = instantiateClass(R"(
class Class(Item):
    pass
)");

    auto native = python.cast<shared_ptr<Item>>();

    QVERIFY_THROWS_EXCEPTION(runtime_error, native->id());
    QVERIFY_THROWS_EXCEPTION(runtime_error, native->text());
    QVERIFY_THROWS_EXCEPTION(runtime_error, native->subtext());
    QVERIFY_THROWS_EXCEPTION(runtime_error, native->inputActionText());
    QVERIFY_THROWS_EXCEPTION(runtime_error, native->icon());
    QVERIFY_THROWS_EXCEPTION(runtime_error, native->actions());

    // TRAMPOLINE

    python = instantiateClass(R"(
class Class(Item):

    def id(self):
        return "id"

    def text(self):
        return "text"

    def subtext(self):
        return "subtext"

    def inputActionText(self):
        return "inputActionText"

    def icon(self):
        return Icon.grapheme("!")

    def actions(self):
        return [Action("id", "text", lambda: None)]
)");

    native = python.cast<shared_ptr<Item>>();

    QCOMPARE(native->id(), "id");
    QCOMPARE(native->text(), "text");
    QCOMPARE(native->subtext(), "subtext");
    QCOMPARE(native->inputActionText(), "inputActionText");
    QVERIFY(native->icon());
    auto actions = native->actions();
    QCOMPARE(actions.size(), 1);
    QCOMPARE(actions[0].id, "id");
    QCOMPARE(actions[0].text, "text");
    QVERIFY(actions[0].function);
}

void PythonTests::testStandardItem()
{
    // BINDINGS (Getters)

    auto native = makeTestItem(1);
    auto python = py::cast(native);

    QCOMPARE(python.attr("id").cast<QString>(), "id_1");
    QCOMPARE(python.attr("text").cast<QString>(), "text_1");
    QCOMPARE(python.attr("subtext").cast<QString>(), "subtext_1");
    QCOMPARE(python.attr("input_action_text").cast<QString>(), "input_action_text_1");
    QVERIFY(python.attr("icon_factory").cast<function<unique_ptr<Icon>()>>());
    QCOMPARE(py::len(python.attr("actions")), 1);

    // BINDINGS (Default constructor)

    python = PyStandardItem();

    QCOMPARE(python.attr("id").cast<QString>(), "");
    QCOMPARE(python.attr("text").cast<QString>(), "");
    QCOMPARE(python.attr("subtext").cast<QString>(), "");
    QCOMPARE(python.attr("input_action_text").cast<QString>(), "");
    QVERIFY(!python.attr("icon_factory").cast<function<unique_ptr<Icon>()>>());
    QCOMPARE(py::len(python.attr("actions")), 0);

    // BINDINGS (Constructor arguments)

    auto py_actions_list = py::list();
    py_actions_list.append(py_make_test_action());

    python = PyStandardItem(
        "id"_a="id",
        "text"_a="text",
        "subtext"_a="subtext",
        "icon_factory"_a=py_make_test_icon,
        "actions"_a=py_actions_list,
        "input_action_text"_a="input_action_text"_s
        );

    QCOMPARE(python.attr("id").cast<QString>(), "id");
    QCOMPARE(python.attr("text").cast<QString>(), "text");
    QCOMPARE(python.attr("subtext").cast<QString>(), "subtext");
    QCOMPARE(python.attr("input_action_text").cast<QString>(), "input_action_text");
    QVERIFY(python.attr("icon_factory").cast<function<unique_ptr<Icon>()>>());
    QCOMPARE(py::len(python.attr("actions")), 1);

    // BINDINGS (Setters)

    python = py_make_test_standard_item;

    python.attr("id") = "1";
    python.attr("text") = "2";
    python.attr("subtext") = "3";
    python.attr("input_action_text") = "4";
    python.attr("icon_factory") = py::none();
    python.attr("actions") = py::list();

    QCOMPARE(python.attr("id").cast<QString>(), "1");
    QCOMPARE(python.attr("text").cast<QString>(),  "2");
    QCOMPARE(python.attr("subtext").cast<QString>(),  "3");
    QCOMPARE(python.attr("input_action_text").cast<QString>(),  "4");
    QVERIFY(!python.attr("icon_factory").cast<function<unique_ptr<Icon>()>>());
    QCOMPARE(py::len(python.attr("actions")), 0);

    // TRAMPOLINE

    python = py_make_test_standard_item(1);
    native = python.cast<shared_ptr<Item>>();
    testTestItem(native.get(), 1);
}

void PythonTests::testRankItem()
{
    auto python = PyRankItem("item"_a=py_make_test_standard_item(0), "score"_a=0.5);
    auto &native = python.cast<RankItem&>();

    QCOMPARE(python.attr("item").cast<shared_ptr<Item>>()->id(), "id_0");
    QCOMPARE(python.attr("score").cast<double>(), 0.5);

    python.attr("item") = py_make_test_standard_item(1);
    python.attr("score") = 1.0;

    QCOMPARE(python.attr("item").cast<shared_ptr<Item>>()->id(), "id_1");
    QCOMPARE(python.attr("score").cast<double>(), 1.0);

    QCOMPARE(native.item->id(), "id_1");
    QCOMPARE(native.score, 1.0);
}

void PythonTests::testIndexItem()
{
    auto python = PyIndexItem("item"_a=py_make_test_standard_item(0), "string"_a="x");
    auto &native = python.cast<IndexItem&>();

    QCOMPARE(python.attr("item").cast<shared_ptr<Item>>()->id(), "id_0");
    QCOMPARE(python.attr("string").cast<QString>(), "x");

    python.attr("item") = py_make_test_standard_item(1);
    python.attr("string") = "y";

    QCOMPARE(python.attr("item").cast<shared_ptr<Item>>()->id(), "id_1");
    QCOMPARE(python.attr("string").cast<QString>(), "y");

    QCOMPARE(native.item->id(), "id_1");
    QCOMPARE(native.string, "y");
}

void PythonTests::testMatcher()
{
    using Score = Match::Score;

    // This merely tests the Matcher API.
    // Thourough tests are done in the core tests.

    auto matcher = PyMatcher("string"_a="x");

    auto m = matcher.attr("match")("x");
    QCOMPARE(m.cast<bool>(), true);
    QCOMPARE(m.attr("isMatch")().cast<bool>(), true);
    QCOMPARE(m.attr("isEmptyMatch")().cast<bool>(), false);
    QCOMPARE(m.attr("isExactMatch")().cast<bool>(), true);
    QCOMPARE(m.attr("score").cast<Score>(), 1.0);
    QCOMPARE(m.cast<Score>(), 1.0);

    m = matcher.attr("match")(QStringList({"x y", "y z"}));
    QCOMPARE(m.cast<bool>(), true);
    QCOMPARE(m.attr("isMatch")().cast<bool>(), true);
    QCOMPARE(m.attr("isEmptyMatch")().cast<bool>(), false);
    QCOMPARE(m.attr("isExactMatch")().cast<bool>(), false);
    QCOMPARE(m.attr("score").cast<Score>(), .5);
    QCOMPARE(m.cast<Score>(), .5);

    m = matcher.attr("match")("x y", "y z");
    QCOMPARE(m.cast<bool>(), true);
    QCOMPARE(m.attr("isMatch")().cast<bool>(), true);
    QCOMPARE(m.attr("isEmptyMatch")().cast<bool>(), false);
    QCOMPARE(m.attr("isExactMatch")().cast<bool>(), false);
    QCOMPARE(m.attr("score").cast<Score>(), .5);
    QCOMPARE(m.cast<Score>(), .5);

    auto mc = PyMatchConfig();
    QCOMPARE(mc.attr("fuzzy").cast<bool>(), false);
    QCOMPARE(mc.attr("ignore_case").cast<bool>(), true);
    QCOMPARE(mc.attr("ignore_diacritics").cast<bool>(), true);
    QCOMPARE(mc.attr("ignore_word_order").cast<bool>(), true);

    mc = PyMatchConfig("fuzzy"_a=true);
    QCOMPARE(mc.attr("fuzzy").cast<bool>(), true);
    QCOMPARE(mc.attr("ignore_case").cast<bool>(), true);
    QCOMPARE(mc.attr("ignore_diacritics").cast<bool>(), true);
    QCOMPARE(mc.attr("ignore_word_order").cast<bool>(), true);

    // fuzzy
    QCOMPARE(PyMatcher("tost", PyMatchConfig("fuzzy"_a=false)).attr("match")("test").cast<bool>(), false);
    QCOMPARE(PyMatcher("tost", PyMatchConfig("fuzzy"_a=true)).attr("match")("test").cast<Score>(), 0.75);

    // case
    QCOMPARE(PyMatcher("Test", PyMatchConfig("ignore_case"_a=true)).attr("match")("test").cast<bool>(), true);
    QCOMPARE(PyMatcher("Test", PyMatchConfig("ignore_case"_a=false)).attr("match")("test").cast<bool>(), false);

    // diacritics
    QCOMPARE(PyMatcher("tést", PyMatchConfig("ignore_diacritics"_a=true)).attr("match")("test").cast<bool>(), true);
    QCOMPARE(PyMatcher("tést", PyMatchConfig("ignore_diacritics"_a=false)).attr("match")("test").cast<bool>(), false);

    // order
    QCOMPARE(PyMatcher("b a", PyMatchConfig("ignore_word_order"_a=true)).attr("match")("a b").cast<bool>(), true);
    QCOMPARE(PyMatcher("b a", PyMatchConfig("ignore_word_order"_a=false)).attr("match")("a b").cast<bool>(), false);

    // contextual conversion in rank item
    m = PyMatcher("x").attr("match")("x y");
    auto pyri = PyRankItem(PyStandardItem("x"), m);
    auto ri = pyri.cast<shared_ptr<RankItem>>();  // disowns
    QCOMPARE(ri->score, .5);
}

void PythonTests::testIconFactories()
{
    auto PyColor = albert_module.attr("Color");
    auto PyBrush = albert_module.attr("Brush");

    auto py_test_color = PyColor("r"_a=255, "g"_a=0, "b"_a=0, "a"_a=255);
    auto py_test_brush = PyBrush("color"_a=py_test_color);

    auto py_icon = albert_module.attr("Icon").attr("image")("path"_a="path");
    QVERIFY(py_icon.cast<unique_ptr<Icon>>() != nullptr);

    py_icon = albert_module.attr("Icon").attr("fileType")("path"_a="path");
    QVERIFY(py_icon.cast<unique_ptr<Icon>>() != nullptr);

    py_icon = albert_module.attr("Icon").attr("standard")(
        "type"_a=albert_module.attr("Icon").attr("StandardIconType").attr("TitleBarMenuButton"));
    QVERIFY(py_icon.cast<unique_ptr<Icon>>() != nullptr);

    py_icon = albert_module.attr("Icon").attr("theme")(
        "name"_a="some_name");
    QVERIFY(py_icon.cast<unique_ptr<Icon>>() != nullptr);

    py_icon = albert_module.attr("Icon").attr("grapheme")(
        "grapheme"_a="A",
        "scalar"_a=.5,
        "brush"_a=py_test_brush);
    QVERIFY(py_icon.cast<unique_ptr<Icon>>() != nullptr);

    py_icon = albert_module.attr("Icon").attr("iconified")(
        "icon"_a=albert_module.attr("Icon").attr("grapheme")("A"),
        "background_brush"_a=py_test_brush,
        "border_radius"_a=.5,
        "border_width"_a=2,
        "border_brush"_a=py_test_brush);
    QVERIFY(py_icon.cast<unique_ptr<Icon>>() != nullptr);

    py_icon = albert_module.attr("Icon").attr("composed")(
        "icon1"_a=albert_module.attr("Icon").attr("grapheme")("A"),
        "icon2"_a=albert_module.attr("Icon").attr("grapheme")("B"),
        "size1"_a=0.5,
        "size2"_a=0.5,
        "x1"_a=0.5,
        "y1"_a=0.5,
        "x2"_a=0.5,
        "y2"_a=0.5);
    QVERIFY(py_icon.cast<unique_ptr<Icon>>() != nullptr);
}

void PythonTests::testUsageScoring()
{
    UsageScoring usage_scoring;
    auto py_usage_scoring = py::cast(&usage_scoring);

    auto py_rank_items0 = py::module_::import("albert").attr("RankItemList")();
    py_rank_items0.attr("append")(RankItem{makeTestItem(0), 1.0});
    py_rank_items0.attr("append")(RankItem{makeTestItem(1), 0.0});

    QCOMPARE(py::len(py_rank_items0), 2);
    QCOMPARE(py_rank_items0.attr("__getitem__")(0).attr("score").cast<double>(), 1.0);
    QCOMPARE(py_rank_items0.attr("__getitem__")(1).attr("score").cast<double>(), 0.0);

    auto py_rank_items1 = py_usage_scoring.attr("applied")("test_extension", py_rank_items0);

    QCOMPARE(py::len(py_rank_items0), 0);  // consumed
    QCOMPARE(py::len(py_rank_items1), 2);
    QCOMPARE(py_rank_items1.attr("__getitem__")(0).attr("score").cast<double>(), 1.0);
    QCOMPARE(py_rank_items1.attr("__getitem__")(1).attr("score").cast<double>(), 0.0);
}

void PythonTests::testQueryContext()
{
    auto handler = MockHandler();
    auto native = MockQueryContext(&handler, "trigger", "query");
    py::object python = py::cast(static_cast<QueryContext*>(&native));

    QCOMPARE(python.attr("trigger").cast<QString>(), "trigger");
    QCOMPARE(python.attr("query").cast<QString>(), "query");
    QCOMPARE(python.attr("is_valid").cast<bool>(), true);
    QVERIFY(python.attr("usage_scoring").cast<UsageScoring*>());
}

//==================================================================================================

static void standardExtensionBindingsTest(py::object object)
{
    QCOMPARE(object.attr("id")().cast<QString>(), "id");
    QCOMPARE(object.attr("name")().cast<QString>(), "name");
    QCOMPARE(object.attr("description")().cast<QString>(), "description");
}

static void standardExtensionTrampolineTest(Extension *extension)
{
    QCOMPARE(extension->id(), "id");
    QCOMPARE(extension->name(), "name");
    QCOMPARE(extension->description(), "description");
}

void PythonTests::testExtension()
{
    // BINDINGS

    class : public Extension {
        QString id() const override { return "id"; }
        QString name() const override { return "name"; }
        QString description() const override { return "description"; }
    } impl;

    auto python = py::cast((Extension*)&impl);  // IMPORTANT! type must be base type
    standardExtensionBindingsTest(python);

    // TRAMPOLINE

    python = instantiateClass(R"(
class Class(Extension):

    def id(self):
        return "id"

    def name(self):
        return "name"

    def description(self):
        return "description"
)");

    auto native = python.cast<Extension*>();
    standardExtensionTrampolineTest(native);
}

//==================================================================================================

static void standardGeneratorQueryHandlerBindingsTest(py::object object)
{
    QCOMPARE(object.attr("synopsis")("test").cast<QString>(), "synopsis_test");

    QCOMPARE(object.attr("allowTriggerRemap")().cast<bool>(), false);

    QCOMPARE(object.attr("defaultTrigger")().cast<QString>(), "trigger");

    // QVERIFY_THROWS_EXCEPTION(exception, object.attr("setTrigger")(""));

    QCOMPARE(object.attr("supportsFuzzyMatching")().cast<bool>(), true);

    // QVERIFY_THROWS_EXCEPTION(exception, object.attr("setFuzzyMatching")(true));

    auto ctx = make_unique<MockQueryContext>(object.cast<QueryHandler*>(), "", "x");
    auto gen = object.attr("items")((QueryContext*)ctx.get());
    auto list = gen.attr("__next__")();
    QCOMPARE(py::len(list), 1);
    QCOMPARE(list.cast<vector<shared_ptr<Item>>>()[0]->id(), "id_0");
    QVERIFY_THROWS_EXCEPTION(py::error_already_set, gen.attr("__next__")());

    auto rank_items = py::list();
    rank_items.append(PyRankItem("item"_a=makeTestItem(0), "score"_a=.0));
    rank_items.append(PyRankItem("item"_a=makeTestItem(1), "score"_a=1.));

    auto generator = object.attr("lazySort")(rank_items, ctx->usageScoring());
    auto items = generator.attr("__next__")().cast<vector<shared_ptr<Item>>>();
    QCOMPARE(items[0]->id(), "id_1");
    QCOMPARE(items[1]->id(), "id_0");
    QCOMPARE(items.size(), 2);

    generator = object.attr("lazySort")(rank_items);
    items = generator.attr("__next__")().cast<vector<shared_ptr<Item>>>();
    QCOMPARE(items[0]->id(), "id_1");
    QCOMPARE(items[1]->id(), "id_0");
    QCOMPARE(items.size(), 2);
}

static void standardGeneratorQueryHandlerTrampolineTest(GeneratorQueryHandler *handler)
{
    QCOMPARE(handler->defaultTrigger(), "trigger");

    QCOMPARE(handler->synopsis("test"), "synopsis_test");

    QCOMPARE(handler->allowTriggerRemap(), false);

    QCOMPARE(handler->supportsFuzzyMatching(), true);

    vector<vector<shared_ptr<Item>>> items;
    auto ctx = MockQueryContext(handler, "", "x");
    for (auto batch : handler->items(ctx))
        items.emplace_back(batch);

    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0].size(), 1);
    QCOMPARE(items[0][0]->id(), "id_0");
}

void PythonTests::testGeneratorQueryHandler()
{
    // BINDINGS

    class : public GeneratorQueryHandler {
        QString id() const override { return "id"; }
        QString name() const override { return "name"; }
        QString description() const override { return "description"; }
        QString synopsis(const QString& query) const override { return "synopsis_" + query; }
        QString defaultTrigger() const override { return "trigger"; }
        bool allowTriggerRemap() const override { return false; }
        bool supportsFuzzyMatching() const override { return true; }
        ItemGenerator items(QueryContext&) override { co_yield {makeTestItem(0)}; }
    } impl;

    auto python = py::cast((GeneratorQueryHandler*)&impl);  // IMPORTANT! type must be base type

    standardExtensionBindingsTest(python);
    standardGeneratorQueryHandlerBindingsTest(python);

    // TRAMPOLINE

    python = instantiateClass(R"(
class Class(GeneratorQueryHandler):

    def id(self):
        return "id"

    def name(self):
        return "name"

    def description(self):
        return "description"

    def synopsis(self, query):
        return "synopsis_" + query

    def defaultTrigger(self):
        return "trigger"

    def allowTriggerRemap(self):
        return False

    def supportsFuzzyMatching(self):
        return True

    def setFuzzyMatching(self, fuzzy):
        pass

    def items(self, context):
        yield [make_test_standard_item(0)]
)");
    auto native = python.cast<GeneratorQueryHandler*>();

    standardExtensionTrampolineTest(native);
    standardGeneratorQueryHandlerTrampolineTest(native);
}

//==================================================================================================

static void standardGlobalQueryHandlerBindingsTest(py::object object)
{
    // QVERIFY_THROWS_EXCEPTION(exception, object.attr("rankItems")(true));

    auto ctx = make_unique<MockQueryContext>(object.cast<QueryHandler*>(), "", "x");
    py::list list = object.attr("rankItems")((QueryContext*)ctx.get());
    QCOMPARE(py::len(list), 1);
    auto rank_item = list[0].cast<RankItem>();
    QCOMPARE(rank_item.item->id(), "id_0");
    QCOMPARE(rank_item.score, 1.);
}

static void standardGlobalQueryHandlerTrampolineTest(GlobalQueryHandler *handler)
{
    auto ctx = MockQueryContext(handler, "", "x");
    auto rank_items = handler->rankItems(ctx);
    QCOMPARE(rank_items.size(), 1);
    QCOMPARE(rank_items[0].item->id(), "id_0");
}

void PythonTests::testGlobalQueryHandler()
{
    // BINDINGS

    class : public GlobalQueryHandler {
        QString id() const override { return "id"; }
        QString name() const override { return "name"; }
        QString description() const override { return "description"; }
        QString synopsis(const QString& query) const override { return "synopsis_" + query; }
        QString defaultTrigger() const override { return "trigger"; }
        bool allowTriggerRemap() const override { return false; }
        bool supportsFuzzyMatching() const override { return true; }
        vector<RankItem> rankItems(QueryContext &) override
        { return {RankItem(makeTestItem(0), 1.)}; }
    } impl;

    auto python = py::cast((GlobalQueryHandler*)&impl);  // IMPORTANT! type must be base type

    standardExtensionBindingsTest(python);
    standardGeneratorQueryHandlerBindingsTest(python);
    standardGlobalQueryHandlerBindingsTest(python);

    // TRAMPOLINE

    python = instantiateClass(R"(
class Class(GlobalQueryHandler):

    def id(self):
        return "id"

    def name(self):
        return "name"

    def description(self):
        return "description"

    def synopsis(self, query):
        return "synopsis_" + query

    def defaultTrigger(self):
        return "trigger"

    def allowTriggerRemap(self):
        return False

    def supportsFuzzyMatching(self):
        return True

    def setFuzzyMatching(self, fuzzy):
        pass

    def rankItems(self, context):
        return [RankItem(item=make_test_standard_item(0), score=1.)]
)");

    auto native = python.cast<GlobalQueryHandler*>();

    standardExtensionTrampolineTest(native);
    standardGeneratorQueryHandlerTrampolineTest(native);
    standardGlobalQueryHandlerTrampolineTest(native);

    // CALLING BASE

    auto ctx = make_unique<MockQueryContext>(python.cast<QueryHandler*>(), "", "x");
    auto gen = python.attr("items")((QueryContext*)ctx.get());
    auto list = gen.attr("__next__")();
    QCOMPARE(py::len(list), 1);
    QCOMPARE(list.cast<vector<shared_ptr<Item>>>()[0]->id(), "id_0");
    QVERIFY_THROWS_EXCEPTION(py::error_already_set, gen.attr("__next__")());

}

//==================================================================================================

static void standardIndexQueryHandlerBindingsTest(py::object object)
{
    QCOMPARE(object.attr("supportsFuzzyMatching")().cast<bool>(), true);

    // QVERIFY_THROWS_NO_EXCEPTION(object.attr("setFuzzyMatching")(true));

    QVERIFY_THROWS_EXCEPTION(exception, object.attr("updateIndexItems")(true));

    vector<IndexItem> index_items{{makeTestItem(0), "x"}};
    QVERIFY_THROWS_NO_EXCEPTION(object.attr("setIndexItems")(py::cast(index_items)));
}

static void standardIndexQueryHandlerTrampolineTest(IndexQueryHandler *handler)
{
    QVERIFY_THROWS_NO_EXCEPTION(handler->updateIndexItems());
}

void PythonTests::testIndexQueryHandler()
{
    // BINDINGS

    class : public IndexQueryHandler {
        QString id() const override { return "id"; }
        QString name() const override { return "name"; }
        QString description() const override { return "description"; }
        QString synopsis(const QString& query) const override { return "synopsis_" + query; }
        QString defaultTrigger() const override { return "trigger"; }
        bool allowTriggerRemap() const override { return false; }
        void updateIndexItems() override { setIndexItems({IndexItem(makeTestItem(0), u"x"_s)}); }
    } impl;
    impl.setFuzzyMatching(false);  // required to populate the index

    auto python = py::cast((IndexQueryHandler*)&impl);  // IMPORTANT! type must be base type

    standardExtensionBindingsTest(python);
    standardGeneratorQueryHandlerBindingsTest(python);
    standardGlobalQueryHandlerBindingsTest(python);
    standardIndexQueryHandlerBindingsTest(python);

    // TRAMPOLINE

    python = instantiateClass(R"(
class Class(IndexQueryHandler):

    def id(self):
        return "id"

    def name(self):
        return "name"

    def description(self):
        return "description"

    def synopsis(self, query):
        return "synopsis_" + query

    def defaultTrigger(self):
        return "trigger"

    def allowTriggerRemap(self):
        return False

    def updateIndexItems(self):
        self.setIndexItems(index_items=[
            IndexItem(item=make_test_standard_item(0), string="x")
        ])
)");

    auto native = python.cast<IndexQueryHandler*>();
    native->setFuzzyMatching(false);  // required to populate the index

    standardExtensionTrampolineTest(native);
    standardGeneratorQueryHandlerTrampolineTest(native);
    standardGlobalQueryHandlerTrampolineTest(native);
    standardIndexQueryHandlerTrampolineTest(native);

    // CALLING BASE

    auto ctx = make_unique<MockQueryContext>(python.cast<QueryHandler*>(), "", "x");
    auto gen = python.attr("items")((QueryContext*)ctx.get());
    auto chunk = gen.attr("__next__")();
    QCOMPARE(py::len(chunk), 1);
    QCOMPARE(chunk.cast<vector<shared_ptr<Item>>>()[0]->id(), "id_0");
    QVERIFY_THROWS_EXCEPTION(py::error_already_set, gen.attr("__next__")());

    py::list rank_items = python.attr("rankItems")((QueryContext*)ctx.get());
    QCOMPARE(py::len(rank_items), 1);
    auto rank_item = rank_items[0].cast<RankItem>();
    QCOMPARE(rank_item.item->id(), "id_0");
    QCOMPARE(rank_item.score, 1.);
}

//==================================================================================================

void PythonTests::testFallbackQueryHandler()
{
    // BINDINGS

    class : public FallbackHandler {
        QString id() const override { return "id"; }
        QString name() const override { return "name"; }
        QString description() const override { return "description"; }
        vector<shared_ptr<Item>>
        fallbacks(const QString &) const override { return {makeTestItem(0)}; }
    } impl;

    auto python = py::cast((FallbackHandler*)&impl);  // IMPORTANT! type must be base type

    standardExtensionBindingsTest(python);

    py::list py_list = python.attr("fallbacks")(""_s);
    QCOMPARE(py::len(py_list), 1);
    QCOMPARE(py_list[0].cast<shared_ptr<Item>>()->id(), "id_0");

    // TRAMPOLINE

    python = instantiateClass(R"(
class Class(FallbackHandler):

    def id(self):
        return "test_id"

    def name(self):
        return "test_name"

    def description(self):
        return "test_description"

    def fallbacks(self, s):
        return [make_test_standard_item(0)]
)");

    auto native = python.cast<FallbackHandler*>();

    auto fallbacks = native->fallbacks({});
    QCOMPARE(fallbacks.size(), 1);
    QCOMPARE(fallbacks[0]->id(), "id_0");
}

