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
#include "albert/logging.h"
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
using namespace std;
using namespace py::literals;
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
        usage_scoring_.usage_scores = make_shared<const std::unordered_map<ItemKey, double>>();
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
    std::unique_ptr<QueryExecution> execution(QueryContext &) override { return nullptr; }
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

// Item has to be tested a lot while being passed around. Make it a oneliner.
static void test_test_item(Item *item, int number)
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

void PythonTests::testBasicPluginInstance()
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
    auto py_action = PyAction(
        "id"_a="test_action_id",
        "text"_a="test_action_text",
        "callable"_a=py::globals()["increment_test_action_variable"]
        );

    auto &action = py_action.cast<Action&>();

    QCOMPARE(action.id, "test_action_id");
    QCOMPARE(action.text, "test_action_text");
    QCOMPARE(py_get_test_action_variable().cast<int>(), 0);
    action.function();
    QCOMPARE(py_get_test_action_variable().cast<int>(), 1);
}

void PythonTests::testItem()
{
    py::dict locals;

    py::exec(R"(
class TestItem(Item):
    def __init__(self, number:int):
        Item.__init__(self)
        self._number = number

    def id(self):
        return "id_" + str(self._number)

    def text(self):
        return "text_" + str(self._number)

    def subtext(self):
        return "subtext_" + str(self._number)

    def inputActionText(self):
        return "input_action_text_" + str(self._number)

    def icon(self):
        return Icon.grapheme(str(self._number))

    def actions(self):
        return [make_test_action()] * self._number

class InvalidTestItem(Item):
    pass
)", py::globals(), locals);

    auto py_item = locals["TestItem"](1);
    auto item = py_item.cast<shared_ptr<Item>>();

    test_test_item(item.get(), 1);
    py_item = py::object();  // release python ownership
    test_test_item(item.get(), 1);

    py_item = locals["InvalidTestItem"]();
    item = py_item.cast<shared_ptr<Item>>();

    QVERIFY_THROWS_EXCEPTION(runtime_error, item->id());
    QVERIFY_THROWS_EXCEPTION(runtime_error, item->text());
    QVERIFY_THROWS_EXCEPTION(runtime_error, item->subtext());
    QVERIFY_THROWS_EXCEPTION(runtime_error, item->inputActionText());
    QVERIFY_THROWS_EXCEPTION(runtime_error, item->icon());
    QVERIFY_THROWS_EXCEPTION(runtime_error, item->actions());
}

void PythonTests::testStandardItem()
{
    auto py_test_standard_item = py_make_test_standard_item(1);
    auto test_standard_item = py_test_standard_item.cast<shared_ptr<StandardItem>>();

    // Test C++ interface

    test_test_item(test_standard_item.get(), 1);


    // Test Python property getters

    QCOMPARE(py_test_standard_item.attr("id").cast<QString>(), "id_1");

    QCOMPARE(py_test_standard_item.attr("text").cast<QString>(), "text_1");

    QCOMPARE(py_test_standard_item.attr("subtext").cast<QString>(), "subtext_1");

    QCOMPARE(py_test_standard_item.attr("input_action_text").cast<QString>(), "input_action_text_1");

    auto icon_factory = py_test_standard_item.attr("icon_factory").cast<function<unique_ptr<Icon>()>>();
    QVERIFY(icon_factory);
    QVERIFY(icon_factory() != nullptr);
    QVERIFY(dynamic_cast<Icon*>(icon_factory().get()) != nullptr);

    auto actions = py_test_standard_item.attr("actions").cast<vector<Action>>();
    QCOMPARE(actions.size(), 1);
    QCOMPARE(actions[0].id, "test_action_id");
    QCOMPARE(actions[0].text, "test_action_text");


    // Test Python property setters

    py_test_standard_item.attr("id") = "x_item_id";
    QCOMPARE(test_standard_item->id(), "x_item_id");

    py_test_standard_item.attr("text") = "x_item_text";
    QCOMPARE(test_standard_item->text(), "x_item_text");

    py_test_standard_item.attr("subtext") = "x_item_subtext";
    QCOMPARE(test_standard_item->subtext(), "x_item_subtext");

    py_test_standard_item.attr("input_action_text") = "x_item_input_action_text";
    QCOMPARE(test_standard_item->inputActionText(), "x_item_input_action_text");

    py_test_standard_item.attr("icon_factory") = py::none();
    icon_factory = py_test_standard_item.attr("icon_factory").cast<function<unique_ptr<Icon>()>>();
    QVERIFY(!icon_factory);
    py_test_standard_item.attr("icon_factory") = py_make_test_icon;
    icon_factory = py_test_standard_item.attr("icon_factory").cast<function<unique_ptr<Icon>()>>();
    QVERIFY(icon_factory);
    QVERIFY(icon_factory() != nullptr);
    QVERIFY(dynamic_cast<Icon*>(icon_factory().get()) != nullptr);

    py_test_standard_item.attr("actions") = py::list();
    actions = py_test_standard_item.attr("actions").cast<vector<Action>>();
    QVERIFY(actions.empty());
    auto py_actions_list = py::list();
    py_actions_list.append(py_make_test_action());
    py_actions_list.append(py_make_test_action());
    py_test_standard_item.attr("actions") = py_actions_list;
    actions = py_test_standard_item.attr("actions").cast<vector<Action>>();
    QCOMPARE(actions.size(), 2);
    QCOMPARE(actions[0].id, "test_action_id");
    QCOMPARE(actions[1].id, "test_action_id");
}

void PythonTests::testRankItem()
{
    auto py_test_standard_item = py_make_test_standard_item(1);
    auto py_test_rank_item = PyRankItem("item"_a=py_test_standard_item,
                                        "score"_a=0.5);

    auto rank_item = py_test_rank_item.cast<unique_ptr<RankItem>>();  // disowns

    test_test_item(rank_item->item.get(), 1);
    QCOMPARE(rank_item->score, 0.5);
}

void PythonTests::testIndexItem()
{
    auto py_test_standard_item = py_make_test_standard_item(1);
    auto py_test_index_item = PyIndexItem("item"_a=py_test_standard_item,
                                          "string"_a="index_item_text");

    auto index_item = py_test_index_item.cast<unique_ptr<IndexItem>>();  // disowns

    test_test_item(index_item->item.get(), 1);
    QCOMPARE(index_item->string, "index_item_text");
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

void PythonTests::testQueryContext()
{
    auto handler = MockHandler();
    auto ctx = MockQueryContext(&handler, "test_trigger", "test_query");

    py::object py_ctx = py::cast(static_cast<QueryContext*>(&ctx));
    // QCOMPARE(py_query.attr("handler")().cast<py::object>().ptr(), &handler);
    QCOMPARE(py_ctx.attr("trigger").cast<QString>(), "test_trigger");
    QCOMPARE(py_ctx.attr("query").cast<QString>(), "test_query");
    QCOMPARE(py_ctx.attr("isValid").cast<bool>(), true);
}

// void PythonTests::testQueryResults()
// {
//     auto handler = MockHandler();
//     auto query = MockQueryContext(&handler);
//     auto cpp = QueryResults(query);
//     auto py = py::cast(&cpp);

//     QCOMPARE(cpp.count(), 0);

//     py.attr("add")(py_make_test_standard_item(1));

//     QCOMPARE(cpp.count(), 1);

//     py::list l;
//     l.append(py_make_test_standard_item(2));
//     l.append(py_make_test_standard_item(3));
//     py.attr("add")(l);

//     QCOMPARE(cpp.count(), 3);

//     test_test_item(cpp.operator[](0).item.get(), 1);
//     test_test_item(cpp.operator[](1).item.get(), 2);
//     test_test_item(cpp.operator[](2).item.get(), 3);
// }

// void PythonTests::testQueryExecution()
// {
//     py::dict locals;

//     py::exec(R"(
// class TestQueryExecution(QueryExecution):

//     def __init__(self, query):
//         QueryExecution.__init__(self, query)
//         self.active = True
//         self.can_fetch_more = True

//     def cancel(self):
//         self.active = False

//     def fetchMore(self):
//         self.can_fetch_more = False

//     def canFetchMore(self):
//         return self.can_fetch_more

//     def isActive(self):
//         return self.active
// )", py::globals(), locals);

//     auto handler = MockHandler();
//     auto ctx = MockQueryContext(&handler);
//     // auto py_query = py::cast(&query, py::return_value_policy::reference);

//     auto py_inst = locals["TestQueryExecution"](static_cast<QueryContext*>(&ctx));
//     auto *cpp_inst = py_inst.cast<QueryExecution*>();

//     QCOMPARE(py_inst.attr("id").cast<uint>(), 0);
//     QCOMPARE(cpp_inst->id, 0);

//     // Just test access. Types have dedicated test cases.
//     QVERIFY(py::isinstance(py_inst.attr("query"), albert_module.attr("Query")));
//     QVERIFY(py::isinstance(py_inst.attr("results"), albert_module.attr("QueryResults")));

//     QCOMPARE(py_inst.attr("active").cast<bool>(), true);
//     QCOMPARE(py_inst.attr("isActive")().cast<bool>(), true);
//     QCOMPARE(cpp_inst->isActive(), true);
//     cpp_inst->cancel();
//     QCOMPARE(py_inst.attr("active").cast<bool>(), false);
//     QCOMPARE(py_inst.attr("isActive")().cast<bool>(), false);
//     QCOMPARE(cpp_inst->isActive(), false);

//     QCOMPARE(py_inst.attr("can_fetch_more").cast<bool>(), true);
//     QCOMPARE(py_inst.attr("canFetchMore")().cast<bool>(), true);
//     QCOMPARE(cpp_inst->canFetchMore(), true);
//     cpp_inst->fetchMore();
//     QCOMPARE(py_inst.attr("can_fetch_more").cast<bool>(), false);
//     QCOMPARE(py_inst.attr("canFetchMore")().cast<bool>(), false);
//     QCOMPARE(cpp_inst->canFetchMore(), false);

//     // emit activeChanged?
// }


template<typename T>
tuple<py::object, T*> makeTestClass(const char *py_src, const char *class_name = "Handler")
{
    py::dict locals;
    py::exec(py_src, py::globals(), locals);
    auto py_inst = locals[class_name]();
    auto *cpp_inst = py_inst.cast<T*>();
    return {py_inst, cpp_inst};
}

static void testPythonExtensionApi(py::object object)
{
    QCOMPARE(object.attr("id")().cast<QString>(), "test_id");
    QCOMPARE(object.attr("name")().cast<QString>(), "test_name");
    QCOMPARE(object.attr("description")().cast<QString>(), "test_description");
}

static void testPythonQueryHandlerApi(py::object object)
{
    QCOMPARE(object.attr("synopsis")("_test").cast<QString>(), "test_synopsis_test");
    QCOMPARE(object.attr("allowTriggerRemap")().cast<bool>(), false);
    QCOMPARE(object.attr("defaultTrigger")().cast<QString>(), "test_trigger");
    QCOMPARE(object.attr("supportsFuzzyMatching")().cast<bool>(), true);
}

static void testCppExtensionApi(Extension *extension)
{
    QCOMPARE(extension->id(), "test_id");
    QCOMPARE(extension->name(), "test_name");
    QCOMPARE(extension->description(), "test_description");
}

static void testCppQueryHandlerApi(QueryHandler *handler)
{
    QCOMPARE(handler->defaultTrigger(), "test_trigger");
    QCOMPARE(handler->synopsis("_test"), "test_synopsis_test");
    QCOMPARE(handler->allowTriggerRemap(), false);
    QCOMPARE(handler->supportsFuzzyMatching(), true);
}

// void PythonTests::testQueryHandler()
// {
//     const auto *py_init = R"(
// class TestQueryHandler(QueryHandler):

//     def id(self):
//         return "test_id"

//     def name(self):
//         return "test_name"

//     def description(self):
//         return "test_description"

//     def synopsis(self, query):
//         return "test_synopsis" + query

//     def defaultTrigger(self):
//         return "test_trigger"

//     def allowTriggerRemap(self):
//         return False

//     def supportsFuzzyMatching(self):
//         return True

//     #def execution(self, query):

//     #    class TestQueryExecution(QueryExecution):

//     #        def __init__(self, query):
//     #            QueryExecution.__init__(self, query)
//     #            self.step = 0

//     #        def cancel(self):
//     #            self.step = -1

//     #        def fetchMore(self):
//     #            if self.step < 0:
//     #                return
//     #            self.results.add(make_test_standard_item(self.step))
//     #            self.step += 1
//     #            if self.step == 2:
//     #                self.step = -1

//     #        def canFetchMore(self):
//     #            return not self.step < 0

//     #        def isActive(self):
//     #            return False  # well no asynchronicity in python yet

//     #    return TestQueryExecution(query)
// )";

//     py::dict locals;
//     py::exec(py_init, py::globals(), locals);
//     auto py_inst = locals["TestQueryHandler"]();
//     auto *cpp_inst = py_inst.cast<QueryHandler*>();
//     QVERIFY(cpp_inst != nullptr);

//     testPythonExtensionApi(py_inst);
//     testPythonQueryHandlerApi(py_inst);

//     py::gil_scoped_release release;
//     testCppExtensionApi(cpp_inst);
//     testCppQueryHandlerApi(cpp_inst);

//     // auto query = MockQueryContext(cpp_inst);
//     // auto exec = cpp_inst->execution(query);
//     // QVERIFY(exec.get() != nullptr);

//     // // empty in the beginning
//     // QCOMPARE(exec->results.count(), 0);
//     // QCOMPARE(exec->isActive(), false);
//     // QCOMPARE(exec->canFetchMore(), true);

//     // // first
//     // exec->fetchMore();
//     // QCOMPARE(exec->results.count(), 1);
//     // QCOMPARE(exec->isActive(), false);
//     // QCOMPARE(exec->canFetchMore(), true);

//     // // sencond (end)
//     // exec->fetchMore();
//     // QCOMPARE(exec->results.count(), 2);
//     // QCOMPARE(exec->isActive(), false);
//     // QCOMPARE(exec->canFetchMore(), false);

//     // // test noop
//     // exec->fetchMore();
//     // QCOMPARE(exec->isActive(), false);
//     // QCOMPARE(exec->canFetchMore(), false);
//     // QCOMPARE(exec->results.count(), 2);

//     // exec->cancel();

//     // QCOMPARE(exec->isActive(), false);
//     // QCOMPARE(exec->canFetchMore(), false);
//     // QCOMPARE(exec->results.count(), 2);

//     // test_test_item(exec->results[0].item.get(), 0);
//     // test_test_item(exec->results[1].item.get(), 1);
// }

static void testCppQueryExecution(QueryHandler* handler,
                                  vector<vector<int>> expected,
                                  const char* query = "")
{
    auto ctx = MockQueryContext(handler, "", query);

    auto exec = handler->execution(ctx);

    QEventLoop loop;
    QObject::connect(exec.get(), &QueryExecution::activeChanged, exec.get(), [&] {
        if (!exec->isActive())
            loop.quit();
    });

    uint item_count = 0;

    exec->fetchMore();
    if(exec->isActive())
    {
        QCOMPARE(exec->results.count(), item_count);
        loop.exec();
    }

    for (const auto &batch : expected)
    {
        for (size_t i = 0; i < batch.size(); ++i)
            test_test_item(exec->results[item_count + i].item.get(), batch[i]);
        item_count += batch.size();
        QCOMPARE(exec->results.count(), item_count);
        exec->fetchMore();
        loop.exec();
    }

    QCOMPARE(exec->results.count(), item_count);
    QCOMPARE(exec->canFetchMore(), false);
}

static void testCppItemGenerator(GeneratorQueryHandler* handler,
                                 vector<vector<int>> expected,
                                 const char* query = "")
{
    auto ctx = MockQueryContext(handler, "", query);

    vector<vector<shared_ptr<Item>>> items;
    for (auto batch : handler->items(ctx))
        items.emplace_back(batch);

    QCOMPARE(items.size(), expected.size());  // compare number of batches
    for (size_t b = 0; b < items.size(); ++b)
    {
        QCOMPARE(items[b].size(), expected[b].size());  // compare size of batches
        for (size_t i = 0; i < items[b].size(); ++i)
            test_test_item(items[b][i].get(), expected[b][i]);  // compare items
    }
}

static void testCppRankItems(RankedQueryHandler* handler,
                             vector<pair<int, double>> expected,
                             const char* query = "")
{
    auto ctx = MockQueryContext(handler, "", query);
    auto rank_items = handler->rankItems(ctx);
    ranges::sort(rank_items, greater());
    // ranges::sort(rank_items, greater(), &pair<int, double>::second);

    QCOMPARE(rank_items.size(), expected.size());

    for (size_t i = 0; i < rank_items.size(); ++i)
    {
        test_test_item(rank_items[i].item.get(), expected[i].first);
        QCOMPARE(rank_items[i].score, expected[i].second);
    }
}

void PythonTests::testGeneratorQueryHandler()
{
    auto [py_inst, cpp_inst] = makeTestClass<GeneratorQueryHandler>(R"(
class Handler(GeneratorQueryHandler):

    def id(self):
        return "test_id"

    def name(self):
        return "test_name"

    def description(self):
        return "test_description"

    def synopsis(self, query):
        return "test_synopsis" + query

    def defaultTrigger(self):
        return "test_trigger"

    def allowTriggerRemap(self):
        return False

    def supportsFuzzyMatching(self):
        return True

    def items(self, context):
        yield [make_test_standard_item(1)]
        yield [make_test_standard_item(1), make_test_standard_item(2)]
        yield [make_test_standard_item(1), make_test_standard_item(2), make_test_standard_item(3)]
)");

    testPythonExtensionApi(py_inst);
    testPythonQueryHandlerApi(py_inst);

    py::gil_scoped_release release;

    testCppExtensionApi(cpp_inst);
    testCppQueryHandlerApi(cpp_inst);
    testCppQueryExecution(cpp_inst, {{1}, {1, 2}, {1, 2, 3}});
    testCppItemGenerator(cpp_inst,  {{1}, {1, 2}, {1, 2, 3}});
}

void PythonTests::testRankedQueryHandler()
{
    auto [py_inst, cpp_inst] = makeTestClass<RankedQueryHandler>(R"(
class Handler(RankedQueryHandler):

    def id(self):
        return "test_id"

    def name(self):
        return "test_name"

    def description(self):
        return "test_description"

    def synopsis(self, query):
        return "test_synopsis" + query

    def defaultTrigger(self):
        return "test_trigger"

    def allowTriggerRemap(self):
        return False

    def supportsFuzzyMatching(self):
        return True

    def items(self, ctx):
        yield from super().items(context=ctx)  # Default implementation call
        yield from self.lazySort([
            RankItem(item=make_test_standard_item(3), score=.125),
            RankItem(item=make_test_standard_item(2), score=.25)
        ])

    def rankItems(self, context):
        return [
            RankItem(item=make_test_standard_item(1), score=.5),
            RankItem(item=make_test_standard_item(0), score=1.)
        ]
)");

    testPythonExtensionApi(py_inst);
    testPythonQueryHandlerApi(py_inst);

    py::gil_scoped_release release;

    testCppExtensionApi(cpp_inst);
    testCppQueryHandlerApi(cpp_inst);
    testCppQueryExecution(cpp_inst, {{0, 1}, {2, 3}});
    testCppItemGenerator(cpp_inst, {{0, 1}, {2, 3}});  // Assumes batch size 10
    testCppRankItems(cpp_inst, {{0, 1.}, {1, .5}});
}

void PythonTests::testGlobalQueryHandler()
{
    auto [py_inst, cpp_inst] = makeTestClass<GlobalQueryHandler>(R"(
class Handler(GlobalQueryHandler):

    def id(self):
        return "test_id"

    def name(self):
        return "test_name"

    def description(self):
        return "test_description"

    def synopsis(self, query):
        return "test_synopsis" + query

    def defaultTrigger(self):
        return "test_trigger"

    def allowTriggerRemap(self):
        return False

    def supportsFuzzyMatching(self):
        return True

    def items(self, ctx):
        yield from super().items(context=ctx)  # Default implementation call
        yield from self.lazySort([
            RankItem(item=make_test_standard_item(3), score=.125),
            RankItem(item=make_test_standard_item(2), score=.25)
        ])

    def rankItems(self, context):
        return [
            RankItem(item=make_test_standard_item(1), score=.5),
            RankItem(item=make_test_standard_item(0), score=1.)
        ]
)");

    testPythonExtensionApi(py_inst);
    testPythonQueryHandlerApi(py_inst);

    py::gil_scoped_release release;

    testCppExtensionApi(cpp_inst);
    testCppQueryHandlerApi(cpp_inst);
    testCppQueryExecution(cpp_inst, {{0, 1}, {2, 3}});
    testCppItemGenerator(cpp_inst, {{0, 1}, {2, 3}});  // Assumes batch size 10
    testCppRankItems(cpp_inst, {{0, 1.}, {1, .5}});
}

void PythonTests::testIndexQueryHandler()
{
    auto [py_inst, cpp_inst] = makeTestClass<IndexQueryHandler>(R"(
class Handler(IndexQueryHandler):

    def id(self):
        return "test_id"

    def name(self):
        return "test_name"

    def description(self):
        return "test_description"

    def synopsis(self, query):
        return "test_synopsis" + query

    def defaultTrigger(self):
        return "test_trigger"

    def allowTriggerRemap(self):
        return False

    def items(self, ctx):
        yield from super().items(context=ctx)  # Default implementation call
        yield from self.lazySort([
            RankItem(item=make_test_standard_item(4), score=.125),
            RankItem(item=make_test_standard_item(3), score=.25)
        ])

    def rankItems(self, ctx):
        rank_items = super().rankItems(context=ctx)  # Default implementation call
        rank_items.append(RankItem(item=make_test_standard_item(2), score=.25))
        return rank_items

    def updateIndexItems(self):
        self.setIndexItems(index_items=[
            IndexItem(item=make_test_standard_item(0), string="0"),
            IndexItem(item=make_test_standard_item(1), string="00")
        ])
)");
    cpp_inst->setFuzzyMatching(false);  // required to populate the index

    testPythonExtensionApi(py_inst);
    testPythonQueryHandlerApi(py_inst);

    py::gil_scoped_release release;

    testCppExtensionApi(cpp_inst);
    testCppQueryHandlerApi(cpp_inst);
    testCppQueryExecution(cpp_inst, {{0, 1, 2}, {3, 4}}, "0");
    testCppItemGenerator(cpp_inst, {{0, 1, 2}, {3, 4}}, "0");
    testCppRankItems(cpp_inst, {{0, 1.}, {1, .5}, {2, .25}}, "0");
}

void PythonTests::testFallbackQueryHandler()
{
    auto [py_inst, cpp_inst] = makeTestClass<FallbackHandler>(R"(
class Handler(FallbackHandler):

    def id(self):
        return "test_id"

    def name(self):
        return "test_name"

    def description(self):
        return "test_description"

    def fallbacks(self, s):
        return [make_test_standard_item(1)]
)");

    testPythonExtensionApi(py_inst);

    auto fallbacks = cpp_inst->fallbacks("test");
    QCOMPARE(fallbacks.size(), 1);
    test_test_item(fallbacks[0].get(), 1);
}
