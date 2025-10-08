// Copyright (c) 2022-2025 Manuel Schneider

#include <pybind11/functional.h>
#include <pybind11/native_enum.h>
#include <pybind11/stl.h>
#include "cast_specialization.hpp"  // Has to be imported first
#include "trampolineclasses.hpp"

#include "albert/fallbackhandler.h"
#include "albert/icon.h"
#include "albert/iconutil.h"
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
#include "test.h"
#include <QStandardPaths>
#include <QTest>
#include <albert/indexqueryhandler.h>
using namespace albert::util;
using namespace albert;
using namespace std;
using namespace py::literals;
QTEST_MAIN(PythonTests)

struct MockQuery : public Query
{
    MockQuery(Extension &handler) : handler_(handler) {}

    QString query_;
    QString trigger_;
    Extension &handler_;
    vector<ResultItem> matches_;
    vector<ResultItem> fallbacks_;

    QString synopsis() const override { return "t"; }
    QString trigger() const override { return trigger_; }
    QString string() const override { return query_; }
    bool isActive() const override { return false; }
    const bool &isValid() const override { static bool valid = true; return valid; }
    bool isTriggered() const override { return true; }
    const vector<ResultItem> &matches() override { return matches_; }
    const vector<ResultItem> &fallbacks() override { return fallbacks_; }
    bool activateMatch(uint, uint) override { return false; }
    bool activateFallback(uint, uint) override { return false; }

    void add(const shared_ptr<Item> &item) override
    {
        matches_.emplace_back(handler_, item);
    }

    void add(shared_ptr<Item> &&item) override
    {
        matches_.emplace_back(handler_, ::move(item));
    }

    void add(const vector<shared_ptr<Item>> &items) override
    {
        for (const auto &item : items)
            matches_.emplace_back(handler_, item);
    }

    void add(vector<shared_ptr<Item>> &&items) override
    {
        for (auto &item : items)
            matches_.emplace_back(handler_, ::move(item));
    }
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
    return makeGraphemeIcon("A")


def make_test_standard_item(number:int):
    return StandardItem(
        id="id_" + str(number),
        text="text_" + str(number),
        subtext="subtext_" + str(number),
        iconFactory=make_test_icon,
        actions=[make_test_action()] * number,
        inputActionText="input_action_text_" + str(number)
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
class Plugin(PluginInstance, TriggerQueryHandler):
    def __init__(self):
        PluginInstance.__init__(self)
        TriggerQueryHandler.__init__(self)
)", py::globals(), locals);

    MockLoader mock_loader;
    mock_loader.class_to_load = locals["Plugin"];
    mock_loader.load();
    auto inst = mock_loader.cpp_instance;

    // Test default extensions factory
    auto extensions = inst->extensions();
    QCOMPARE(extensions.size(), 1);
    auto h = dynamic_cast<TriggerQueryHandler*>(extensions[0]);
    QVERIFY(h != nullptr);

    // Test mixin-emulation
    QCOMPARE(h->id(), "id");
    QCOMPARE(h->name(), "name");
    QCOMPARE(h->description(), "description");
}

void PythonTests::testAction()
{
    // Test does not throw

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

    def makeIcon(self):
        return makeGraphemeIcon(str(self._number))

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

    QCOMPARE(py_test_standard_item.attr("inputActionText").cast<QString>(), "input_action_text_1");

    auto icon_factory = py_test_standard_item.attr("iconFactory").cast<function<unique_ptr<Icon>()>>();
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

    py_test_standard_item.attr("inputActionText") = "x_item_input_action_text";
    QCOMPARE(test_standard_item->inputActionText(), "x_item_input_action_text");

    py_test_standard_item.attr("iconFactory") = py::none();
    icon_factory = py_test_standard_item.attr("iconFactory").cast<function<unique_ptr<Icon>()>>();
    QVERIFY(!icon_factory);
    py_test_standard_item.attr("iconFactory") = py_make_test_icon;
    icon_factory = py_test_standard_item.attr("iconFactory").cast<function<unique_ptr<Icon>()>>();
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

    test_test_item(py_test_rank_item.attr("item").cast<shared_ptr<Item>>().get(), 1);
    QCOMPARE(py_test_rank_item.attr("score").cast<double>(), 0.5);

    auto rank_item = py_test_rank_item.cast<unique_ptr<RankItem>>();  // disowns

    test_test_item(rank_item->item.get(), 1);
    QCOMPARE(rank_item->score, 0.5);
}

void PythonTests::testIndexItem()
{
    auto py_test_standard_item = py_make_test_standard_item(1);
    auto py_test_index_item = PyIndexItem("item"_a=py_test_standard_item,
                                          "string"_a="index_item_text");

    test_test_item(py_test_index_item.attr("item").cast<shared_ptr<Item>>().get(), 1);
    QCOMPARE(py_test_index_item.attr("string").cast<QString>(), "index_item_text");

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
    QCOMPARE(mc.attr("separator_regex").cast<QString>(), default_separator_regex.pattern());

    mc = PyMatchConfig("fuzzy"_a=true);
    QCOMPARE(mc.attr("fuzzy").cast<bool>(), true);
    QCOMPARE(mc.attr("ignore_case").cast<bool>(), true);
    QCOMPARE(mc.attr("ignore_diacritics").cast<bool>(), true);
    QCOMPARE(mc.attr("ignore_word_order").cast<bool>(), true);
    QCOMPARE(mc.attr("separator_regex").cast<QString>(), default_separator_regex.pattern());

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

    // seps
    QCOMPARE(PyMatcher("a_c", PyMatchConfig("separator_regex"_a="[\\s_]+")).attr("match")("a c").cast<bool>(), true);
    QCOMPARE(PyMatcher("a_c", PyMatchConfig("separator_regex"_a="[_]+")).attr("match")("a c").cast<bool>(), false);

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

    auto py_icon = albert_module.attr("makeImageIcon")("path"_a="path");
    QVERIFY(py_icon.cast<unique_ptr<Icon>>() != nullptr);

    py_icon = albert_module.attr("makeFileTypeIcon")("path"_a="path");
    QVERIFY(py_icon.cast<unique_ptr<Icon>>() != nullptr);

    py_icon = albert_module.attr("makeStandardIcon")(
        "type"_a=albert_module.attr("StandardIconType").attr("TitleBarMenuButton"));
    QVERIFY(py_icon.cast<unique_ptr<Icon>>() != nullptr);

    py_icon = albert_module.attr("makeThemeIcon")(
        "name"_a="some_name");
    QVERIFY(py_icon.cast<unique_ptr<Icon>>() != nullptr);

    py_icon = albert_module.attr("makeGraphemeIcon")(
        "grapheme"_a="A",
        "scalar"_a=.5,
        "color"_a=py_test_brush);
    QVERIFY(py_icon.cast<unique_ptr<Icon>>() != nullptr);

    py_icon = albert_module.attr("makeIconifiedIcon")(
        "src"_a=albert_module.attr("makeGraphemeIcon")("A"),
        "color"_a=py_test_brush,
        "border_radius"_a=.5,
        "border_width"_a=2,
        "border_color"_a=py_test_brush);
    QVERIFY(py_icon.cast<unique_ptr<Icon>>() != nullptr);

    py_icon = albert_module.attr("makeComposedIcon")(
        "src1"_a=albert_module.attr("makeGraphemeIcon")("A"),
        "src2"_a=albert_module.attr("makeGraphemeIcon")("B"),
        "size1"_a=0.5,
        "size2"_a=0.5,
        "x1"_a=0.5,
        "y1"_a=0.5,
        "x2"_a=0.5,
        "y2"_a=0.5);
    QVERIFY(py_icon.cast<unique_ptr<Icon>>() != nullptr);
}

void PythonTests::testQuery()
{
    py::dict locals;

    py::exec(R"(
class TestTriggerQueryHandler(TriggerQueryHandler):

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

    def handleTriggerQuery(self, query):
        query.add(make_test_standard_item(1))
)", py::globals(), locals);


    auto py_inst = locals["TestTriggerQueryHandler"]();
    auto *cpp_inst = py_inst.cast<TriggerQueryHandler*>();

    auto query = MockQuery(*cpp_inst);
    query.query_ = "test_query";
    query.trigger_ = "test_trigger";

    py::object py_query = py::cast(static_cast<Query*>(&query));

    QCOMPARE(py_query.attr("string").cast<QString>(), "test_query");
    QCOMPARE(py_query.attr("trigger").cast<QString>(), "test_trigger");
    QCOMPARE(py_query.attr("isValid").cast<bool>(), true);
    QCOMPARE(query.matches_.size(), 0);

    py_query.attr("add")(py_make_test_standard_item(1));
    QCOMPARE(query.matches_.size(), 1);

    py::list l;
    l.append(py_make_test_standard_item(2));
    py_query.attr("add")(l);
    QCOMPARE(query.matches_.size(), 2);
}

void PythonTests::testTriggerQueryHandler()
{
    py::dict locals;

    py::exec(R"(
class TestTriggerQueryHandler(TriggerQueryHandler):

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

    def handleTriggerQuery(self, query):
        query.add(make_test_standard_item(1))
)", py::globals(), locals);

    auto py_inst = locals["TestTriggerQueryHandler"]();
    auto *cpp_inst = py_inst.cast<TriggerQueryHandler*>();
    QVERIFY(cpp_inst != nullptr);

    // Test extension properties
    QCOMPARE(cpp_inst->id(), "test_id");
    QCOMPARE(cpp_inst->name(), "test_name");
    QCOMPARE(cpp_inst->description(), "test_description");

    // Test trigger query handler properties
    QCOMPARE(cpp_inst->defaultTrigger(), "test_trigger");
    QCOMPARE(cpp_inst->synopsis("_test"), "test_synopsis_test");
    QCOMPARE(cpp_inst->allowTriggerRemap(), false);
    QCOMPARE(cpp_inst->supportsFuzzyMatching(), true);

    // Test the exposed Python API
    auto py_abstract = py::cast(cpp_inst, py::return_value_policy::reference);
    QCOMPARE(py_inst.attr("id")().cast<QString>(), "test_id");
    QCOMPARE(py_inst.attr("name")().cast<QString>(), "test_name");
    QCOMPARE(py_inst.attr("description")().cast<QString>(), "test_description");
    QCOMPARE(py_inst.attr("synopsis")("_test").cast<QString>(), "test_synopsis_test");
    QCOMPARE(py_inst.attr("allowTriggerRemap")().cast<bool>(), false);
    QCOMPARE(py_inst.attr("defaultTrigger")().cast<QString>(), "test_trigger");


    // Test trigger query handling
    auto query = MockQuery(*cpp_inst);
    cpp_inst->handleTriggerQuery(query);
    QCOMPARE(query.matches().size(), 1);
    test_test_item(query.matches_[0].item.get(), 1);
}

void PythonTests::testGlobalQueryHandler()
{
    py::dict locals;

    py::exec(R"(
class TestGlobalQueryHandler(GlobalQueryHandler):

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

    def handleTriggerQuery(self, query):
        # super().handleTriggerQuery(query=query)  # Test default implementation call (not possible atm)
        query.add(make_test_standard_item(1))

    def handleGlobalQuery(self, query):
        return [RankItem(item=make_test_standard_item(2), score=.5)]

)", py::globals(), locals);

    // Create basic test class
    auto py_inst = locals["TestGlobalQueryHandler"]();
    auto *cpp_inst = py_inst.cast<GlobalQueryHandler*>();
    QVERIFY(cpp_inst != nullptr);

    // Test extension properties
    QCOMPARE(cpp_inst->id(), "test_id");
    QCOMPARE(cpp_inst->name(), "test_name");
    QCOMPARE(cpp_inst->description(), "test_description");

    // Test trigger query handler properties
    QCOMPARE(cpp_inst->defaultTrigger(), "test_trigger");
    QCOMPARE(cpp_inst->synopsis("_test"), "test_synopsis_test");
    QCOMPARE(cpp_inst->allowTriggerRemap(), false);
    QCOMPARE(cpp_inst->supportsFuzzyMatching(), true);

    auto query = MockQuery(*cpp_inst);

    // TODO: Test default handleTriggerQuery implementation
    // NOTE
    // This is not easily testable with the current design since no app is running and as such we dont have
    // a query engine which provides the usage score called into in the defaul implementation in
    // GlobalQueryHandler::handleTriggerQuery.
    // h->handleTriggerQuery(query);
    // QCOMPARE(query.matches().size(), 1);
    // test_test_item(query.matches_[0].item.get(), 1);

    // Test custom handleTriggerQuery
    cpp_inst->handleTriggerQuery(query);
    QCOMPARE(query.matches().size(), 1);
    test_test_item(query.matches_[0].item.get(), 1);

    // Test handleGlobalQuery
    auto rank_items = cpp_inst->handleGlobalQuery(query);
    QCOMPARE(rank_items.size(), 1);
    QCOMPARE(rank_items[0].score, .5);
    test_test_item(rank_items[0].item.get(), 2);

    // Test calling self.handleGlobalQuery (often used in custom trigger query handling)
    rank_items = py_inst.attr("handleGlobalQuery")(static_cast<Query*>(&query)).cast<vector<RankItem>>();
    QCOMPARE(rank_items.size(), 1);
}

void PythonTests::testIndexQueryHandler()
{
    py::dict locals;

    py::exec(R"(
class TestIndexQueryHandler(IndexQueryHandler):

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

    def handleTriggerQuery(self, query):
        query.add(make_test_standard_item(1))

    def handleGlobalQuery(self, query):
        # Test default implementation call
        return super().handleGlobalQuery(query=query) + [RankItem(item=make_test_standard_item(3), score=.5)]

    def updateIndexItems(self):
        item = make_test_standard_item(2)
        self.setIndexItems(index_items=[IndexItem(item=item, string=item.text)])

)", py::globals(), locals);

    // Create basic test class
    auto py_inst = locals["TestIndexQueryHandler"]();
    auto *cpp_inst = py_inst.cast<IndexQueryHandler*>();
    QVERIFY(cpp_inst != nullptr);
    cpp_inst->setFuzzyMatching(false); // lazily creates the index and calls updateIndexItems

    // Test extension properties
    QCOMPARE(cpp_inst->id(), "test_id");
    QCOMPARE(cpp_inst->name(), "test_name");
    QCOMPARE(cpp_inst->description(), "test_description");

    // Test trigger query handler properties
    QCOMPARE(cpp_inst->defaultTrigger(), "test_trigger");
    QCOMPARE(cpp_inst->synopsis("_test"), "test_synopsis_test");
    QCOMPARE(cpp_inst->allowTriggerRemap(), false);
    QCOMPARE(cpp_inst->supportsFuzzyMatching(), true);  // returns true and should actually be final in cpp

    auto query = MockQuery(*cpp_inst);

    // TODO: Test default handleTriggerQuery implementation
    // SEE NOTE in testGlobalQueryHandler

    // Test global query handling
    auto rank_items = cpp_inst->handleGlobalQuery(query);
    QCOMPARE(rank_items.size(), 2);
    QCOMPARE(rank_items[0].score, 0.0); // default implementation one
    test_test_item(rank_items[0].item.get(), 2);
    QCOMPARE(rank_items[1].score, 0.5); // custom implementation one
    test_test_item(rank_items[1].item.get(), 3);

    // Test custom handleTriggerQuery
    cpp_inst->handleTriggerQuery(query);
    QCOMPARE(query.matches().size(), 1);
    test_test_item(query.matches_[0].item.get(), 1);

    // Test calling self.handleGlobalQuery (often used in custom trigger query handling)
    rank_items = py_inst.attr("handleGlobalQuery")(static_cast<Query*>(&query)).cast<vector<RankItem>>();
    QCOMPARE(rank_items.size(), 2);

}

void PythonTests::testFallbackQueryHandler()
{
    py::dict locals;

    py::exec(R"(
class TestFallbackQueryHandler(FallbackHandler):

    def id(self):
        return "test_id"

    def name(self):
        return "test_name"

    def description(self):
        return "test_description"

    def fallbacks(self, s):
        return [make_test_standard_item(1)]
)", py::globals(), locals);

    // Create basic test class
    auto py_inst = locals["TestFallbackQueryHandler"]();
    auto *cpp_inst = py_inst.cast<FallbackHandler*>();

    // Test extension properties
    QCOMPARE(cpp_inst->id(), "test_id");
    QCOMPARE(cpp_inst->name(), "test_name");
    QCOMPARE(cpp_inst->description(), "test_description");

    // Test fallback query handling
    auto fallbacks = cpp_inst->fallbacks("test");
    QCOMPARE(fallbacks.size(), 1);
    test_test_item(fallbacks[0].get(), 1);
}
