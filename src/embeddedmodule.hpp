// Copyright (c) 2017-2025 Manuel Schneider

#pragma once

// Has to be imported first
#include <pybind11/functional.h>
#include <pybind11/native_enum.h>
#include <pybind11/stl/filesystem.h>
#include "cast_specialization.hpp"
#include "trampolineclasses.hpp"

#include <QDir>
#include <albert/app.h>
#include <albert/icon.h>
#include <albert/indexqueryhandler.h>
#include <albert/logging.h>
#include <albert/matcher.h>
#include <albert/notification.h>
#include <albert/plugin/applications.h>
#include <albert/plugininstance.h>
#include <albert/standarditem.h>
#include <albert/systemutil.h>
#include <albert/usagescoring.h>
extern applications::Plugin *apps;
using namespace albert;
using namespace std;


/*
 * In this case a piece of python code is injected into C++ code.
 * The GIL has to be locked whenever the code is touched, i.e. on
 * execution and deletion. Further exceptions thrown from python
 * have to be catched.
 */
struct GilAwareFunctor {
    py::object callable;
    GilAwareFunctor(const py::object &c) : callable(c){}
    GilAwareFunctor(GilAwareFunctor&&) = default;
    GilAwareFunctor & operator=(GilAwareFunctor&&) = default;
    GilAwareFunctor(const GilAwareFunctor &other){
        py::gil_scoped_acquire acquire;
        callable = other.callable;
    }
    GilAwareFunctor & operator=(const GilAwareFunctor &other){
        py::gil_scoped_acquire acquire;
        callable = other.callable;
        return *this;
    }
    ~GilAwareFunctor(){
        py::gil_scoped_acquire acquire;
        callable = py::object();
    }
    void operator()() {
        py::gil_scoped_acquire acquire;
        try {
            callable();
        } catch (exception &e) {
            WARN << e.what();
        }
    }
};


template<class T, class PyT>
struct TrampolineDeleter
{
    void operator()(T* t) const {
        auto *pt = dynamic_cast<PyT*>(t);
        if(pt)
            delete pt;
        else
            CRIT << "Dynamic cast in trampoline deleter failed. Memory leaked.";
    }
};

// This type is required to make C++ generators usable in Python
class PyItemGeneratorWrapper
{
    ItemGenerator gen;
    optional<ItemGenerator::iterator> it;

public:
    explicit PyItemGeneratorWrapper(ItemGenerator g)
        : gen(std::move(g)) {}

    PyItemGeneratorWrapper &iter() { return *this; }

    std::vector<std::shared_ptr<albert::Item>> next()
    {
        if (it)
            if (it == gen.end())
                throw pybind11::stop_iteration();
            else
                ++(*it);
        else
            it = gen.begin();

        if (it == gen.end())
            throw pybind11::stop_iteration();

        return ::move(**it);
    }
};

PYBIND11_EMBEDDED_MODULE(albert, m)
{

    // ------------------------------------------------------------------------

    py::class_<PluginInstance, PyPI,
               unique_ptr<PluginInstance, TrampolineDeleter<PluginInstance, PyPI>>
               >(m, "PluginInstance")

        .def(py::init<>())

        .def("id",
             [](PyPI *self){ return self->loader().metadata().id; })

        .def("name",
             [](PyPI *self){ return self->loader().metadata().name; })

        .def("description",
             [](PyPI *self){ return self->loader().metadata().description; })

        .def("cacheLocation",
             &PluginInstance::cacheLocation)

        .def("configLocation",
             &PluginInstance::configLocation)

        .def("dataLocation",
             &PluginInstance::dataLocation)

        .def("readConfig",
             [](PyPI *self, QString key, py::object type){ return self->readConfig(key, type); },
             py::arg("key"),
             py::arg("type"))

        .def("writeConfig",
             [](PyPI *self, QString key, py::object value){ self->writeConfig(key, value); },
             py::arg("key"),
             py::arg("value"))
        ;

    // ------------------------------------------------------------------------

    py::class_<Action>(m, "Action")

        .def(py::init([](QString id, QString text, const py::object &callable) {
                 py::gil_scoped_acquire acquire;
                 return Action(::move(id), ::move(text), GilAwareFunctor(callable));
             }),
             py::arg("id"),
             py::arg("text"),
             py::arg("callable"))
        ;

    // ------------------------------------------------------------------------

    py::classh<Item, PyItemTrampoline>(m, "Item")

        .def(py::init<>())

        .def("id",
             &Item::id)

        .def("text",
             &Item::text)

        .def("subtext",
             &Item::subtext)

        .def("inputActionText",
             &Item::inputActionText)

        .def("icon",
             &Item::icon)

        .def("actions",
             &Item::actions)
        ;

    // ------------------------------------------------------------------------

    py::classh<StandardItem, Item>(m, "StandardItem")
        .def(py::init<QString, QString, QString, function<unique_ptr<Icon>()>, vector<Action>, QString>(),
             py::arg("id") = QString(),
             py::arg("text") = QString(),
             py::arg("subtext") = QString(),
             py::arg("icon_factory") = nullptr,
             py::arg("actions") = vector<Action>(),
             py::arg("input_action_text") = QString())

        .def_property("id",
                      &StandardItem::id,
                      &StandardItem::setId)

        .def_property("text",
                      &StandardItem::text,
                      &StandardItem::setText)

        .def_property("subtext",
                      &StandardItem::subtext,
                      &StandardItem::setSubtext)

        .def_property("icon_factory",
                      &StandardItem::iconFactory,
                      &StandardItem::setIconFactory)

        .def_property("actions",
                      &StandardItem::actions,
                      &StandardItem::setActions)

        .def_property("input_action_text",
                      &StandardItem::inputActionText,
                      &StandardItem::setInputActionText)
        ;

    // ------------------------------------------------------------------------

    py::class_<MatchConfig>(m, "MatchConfig")

        .def(py::init<>())

        .def(py::init([](bool f, bool c, bool o, bool d) {
                 return MatchConfig{
                     .fuzzy=f,
                     .ignore_case=c,
                     .ignore_word_order=o,
                     .ignore_diacritics=d
                 };
             }),
            py::arg("fuzzy") = false,
            py::arg("ignore_case") = true,
            py::arg("ignore_word_order") = true,
            py::arg("ignore_diacritics") = true)

        .def_readwrite("fuzzy",
                       &MatchConfig::fuzzy)

        .def_readwrite("ignore_case",
                       &MatchConfig::ignore_case)

        .def_readwrite("ignore_word_order",
                       &MatchConfig::ignore_word_order)

        .def_readwrite("ignore_diacritics",
                       &MatchConfig::ignore_diacritics)

        ;

    py::class_<Matcher>(m, "Matcher")

        .def(py::init<QString, MatchConfig>(),
             py::arg("string"),
             py::arg("config") = MatchConfig())

        .def("match",
             static_cast<Match(Matcher::*)(const QString&) const>(&Matcher::match))

        .def("match",
             static_cast<Match(Matcher::*)(const QStringList&) const>(&Matcher::match))

        .def("match",
             [](Matcher *self, py::args args){ return self->match(py::cast<QStringList>(args)); })
        ;

    py::class_<Match>(m, "Match")

        .def("__bool__",
             &Match::operator bool)

        .def("__float__",
             &Match::operator Match::Score)

        .def("isMatch",
             &Match::isMatch)

        .def("isEmptyMatch",
             &Match::isEmptyMatch)

        .def("isExactMatch",
             &Match::isExactMatch)

        .def_property_readonly("score",
                               &Match::score)
        ;

    // ------------------------------------------------------------------------

    py::class_<Extension,
               PyExtension<>,
               unique_ptr<Extension, py::nodelete>
               >(m, "Extension")

        .def("id",
             &Extension::id)

        .def("name",
             &Extension::name)

        .def("description",
             &Extension::description)
        ;

    // ------------------------------------------------------------------------

    py::classh<UsageScoring>(m, "UsageScoring")
        .def("modifyMatchScores",
             &UsageScoring::modifyMatchScores,
             py::arg("extension_id"),
             py::arg("rank_items"))
        ;

    py::class_<QueryContext,
               unique_ptr<QueryContext, py::nodelete>
               >(m, "QueryContext")

        .def_property_readonly("trigger",
                               &QueryContext::trigger)

        .def_property_readonly("query",
                               &QueryContext::query)

        .def_property_readonly("isValid",
                               &QueryContext::isValid)

        .def_property_readonly("usageScoring",
                               &QueryContext::usageScoring)
        ;

    // py::class_<QueryResults>(m, "QueryResults")

    //     .def("add",
    //          [](QueryResults &self, const shared_ptr<Item> &item){ self.add(item); })

    //     .def("add",
    //          [](QueryResults &self, const vector<shared_ptr<Item>> &items){ self.add(items); })
    //     ;

    // py::classh<QueryExecution,
    //            PyQueryExecution
    //            >(m, "QueryExecution")

    //     .def(py::init<albert::QueryContext &>(),
    //          py::arg("context"))

    //     .def_readonly("id",
    //                   &QueryExecution::id)

    //     .def_property_readonly("context",
    //                            [](const QueryExecution &self){ return &self.context; },
    //                            py::return_value_policy::reference)

    //     .def_property_readonly("results",
    //                            [](const QueryExecution &self){ return &self.results; },
    //                            py::return_value_policy::reference)

    //     // .def("cancel",
    //     //      &QueryExecution::cancel)

    //     // .def("fetchMore",
    //     //      &QueryExecution::fetchMore)

    //     // .def("canFetchMore",
    //     //      &QueryExecution::canFetchMore)

    //     // .def("isActive",
    //     //      &QueryExecution::isActive)

    //     ;

    py::class_<QueryHandler,
               Extension,
               PyQueryHandler<>,
               unique_ptr<QueryHandler,
                          TrampolineDeleter<QueryHandler,
                                            PyQueryHandler<>>>
               >(m, "QueryHandler")

        // .def(py::init<>())

        .def("synopsis",
             &QueryHandler::synopsis)

        .def("allowTriggerRemap",
             &QueryHandler::allowTriggerRemap)

        .def("defaultTrigger",
             &QueryHandler::defaultTrigger)

        // .def("setTrigger",
        //      &QueryHandler::setTrigger)

        .def("supportsFuzzyMatching",
             &QueryHandler::supportsFuzzyMatching)

        // .def("setFuzzyMatching",
        //      &QueryHandler::setFuzzyMatching)

        // // PURE VIRTUAL
        // .def("execution",
        //      &QueryHandler::execution,
        //      py::arg("context"))
        ;

    // ------------------------------------------------------------------------


    py::classh<PyItemGeneratorWrapper>(m, "ItemGenerator")

        .def("__iter__", &PyItemGeneratorWrapper::iter,
             py::return_value_policy::reference_internal)

        .def("__next__", &PyItemGeneratorWrapper::next);

        ;

    py::class_<GeneratorQueryHandler,
               QueryHandler,
               PyGeneratorQueryHandler<>,
               unique_ptr<GeneratorQueryHandler,
                          TrampolineDeleter<GeneratorQueryHandler,
                                            PyGeneratorQueryHandler<>>>
               >(m, "GeneratorQueryHandler")

        .def(py::init<>())

        // PURE VIRTUAL
        .def("items",
             &GeneratorQueryHandler::items,
             py::arg("context"))
        ;

    // ------------------------------------------------------------------------

    // Do not expose members to avoid unnecessary casts
    py::classh<RankItem>(m, "RankItem")
        .def(py::init<shared_ptr<Item>,float>(),
             py::arg("item"),
             py::arg("score"))
        ;

    py::class_<RankedQueryHandler,
               GeneratorQueryHandler,
               PyRankedQueryHandler<>,
               unique_ptr<RankedQueryHandler,
                          TrampolineDeleter<RankedQueryHandler,
                                            PyRankedQueryHandler<>>>
               >(m, "RankedQueryHandler")

        .def(py::init<>())

        // BASE IMPLEMENTATION
        .def("items",
             [](RankedQueryHandler &self, QueryContext *ctx) {
                 return PyItemGeneratorWrapper(self.items(*ctx));
             },
             py::arg("context"))

        // PURE VIRTUAL
        .def("rankItems",
             &RankedQueryHandler::rankItems,
             py::arg("context"))

        .def_static("lazySort",
                    [](vector<RankItem> rank_items) {
                        return PyItemGeneratorWrapper(RankedQueryHandler::lazySort(::move(rank_items)));
                    },
                    py::arg("rank_items"))

        ;

    // ------------------------------------------------------------------------

    py::class_<GlobalQueryHandler,
               RankedQueryHandler,
               PyGlobalQueryHandler<>,
               unique_ptr<GlobalQueryHandler,
                          TrampolineDeleter<GlobalQueryHandler,
                                            PyGlobalQueryHandler<>>>
               >(m, "GlobalQueryHandler")

        .def(py::init<>())

        ;

    // ------------------------------------------------------------------------

    // Do not expose members to avoid unnecessary casts
    py::classh<IndexItem>(m, "IndexItem")
        .def(py::init<shared_ptr<Item>,QString>(),
             py::arg("item"),
             py::arg("string"))
        ;

    py::class_<IndexQueryHandler,
               GlobalQueryHandler,
               PyIndexQueryHandler<>,
               unique_ptr<IndexQueryHandler,
                          TrampolineDeleter<IndexQueryHandler,
                                            PyIndexQueryHandler<>>>
               >(m, "IndexQueryHandler")

        .def(py::init<>())

        .def("supportsFuzzyMatching",
             &IndexQueryHandler::supportsFuzzyMatching)

        .def("setFuzzyMatching",
             &IndexQueryHandler::setFuzzyMatching)

         // DEFAULT IMPLEMENTATION
        .def("rankItems",
             &IndexQueryHandler::rankItems,
             py::arg("context"))

        // PURE VIRTUAL
        .def("updateIndexItems",
             &IndexQueryHandler::updateIndexItems)

        .def("setIndexItems",
             &IndexQueryHandler::setIndexItems,
             py::arg("index_items"))
        ;

    //------------------------------------------------------------------------

    py::class_<FallbackHandler,
               Extension,
               PyFallbackHandler<>,
               unique_ptr<FallbackHandler,
                          TrampolineDeleter<FallbackHandler,
                                            PyFallbackHandler<>>>
               >(m, "FallbackHandler")

        .def(py::init<>())

        .def("fallbacks",
             &FallbackHandler::fallbacks,
             py::arg("query_string"))
        ;

    // ------------------------------------------------------------------------

    py::class_<Notification>(m, "Notification")

        .def(py::init<const QString&, const QString&>(),
             py::arg("title") = QString(),
             py::arg("text") = QString())

        .def_property("title",
                      &Notification::title,
                      &Notification::setTitle)

        .def_property("text",
                      &Notification::text,
                      &Notification::setText)

        .def("send",
             &Notification::send)

        .def("dismiss",
             &Notification::dismiss)
        ;

    // ------------------------------------------------------------------------

    py::class_<QBrush>(m, "Brush")
        .def(py::init<QColor>(),
             py::arg("color"))
        ;

    py::class_<QColor>(m, "Color")

        .def(py::init<int, int, int, int>(),
             py::arg("r"),
             py::arg("g"),
             py::arg("b"),
             py::arg("a") = 255)

        .def_property("r",
                      &QColor::setRed,
                      &QColor::red)

        .def_property("g",
                      &QColor::setGreen,
                      &QColor::green)

        .def_property("b",
                      &QColor::setBlue,
                      &QColor::blue)

        .def_property("a",
                      &QColor::setAlpha,
                      &QColor::alpha)

        ;

    py::classh<Icon> Icon(m, "Icon");

    Icon.def("__str__", &Icon::toUrl)
        ;

    // Exposes (path: os.PathLike | str | bytes) -> albert.Icon
    Icon.def_static("image",
                    py::overload_cast<const filesystem::path &>(&Icon::image),
                    py::arg("path"));

    // Exposes (path: os.PathLike | str | bytes) -> albert.Icon
    Icon.def_static("fileType",
                    py::overload_cast<const filesystem::path &>(&Icon::fileType),
                    py::arg("path"));

    Icon.def_static("theme",
                    &Icon::theme,
                    py::arg("name"));

    using enum Icon::StandardIconType;
    py::native_enum<Icon::StandardIconType>(Icon, "StandardIconType", "enum.IntEnum")
        .value("TitleBarMenuButton", TitleBarMenuButton)
        .value("TitleBarMinButton", TitleBarMinButton)
        .value("TitleBarMaxButton", TitleBarMaxButton)
        .value("TitleBarCloseButton", TitleBarCloseButton)
        .value("TitleBarNormalButton", TitleBarNormalButton)
        .value("TitleBarShadeButton", TitleBarShadeButton)
        .value("TitleBarUnshadeButton", TitleBarUnshadeButton)
        .value("TitleBarContextHelpButton", TitleBarContextHelpButton)
        .value("DockWidgetCloseButton", DockWidgetCloseButton)
        .value("MessageBoxInformation", MessageBoxInformation)
        .value("MessageBoxWarning", MessageBoxWarning)
        .value("MessageBoxCritical", MessageBoxCritical)
        .value("MessageBoxQuestion", MessageBoxQuestion)
        .value("DesktopIcon", DesktopIcon)
        .value("TrashIcon", TrashIcon)
        .value("ComputerIcon", ComputerIcon)
        .value("DriveFDIcon", DriveFDIcon)
        .value("DriveHDIcon", DriveHDIcon)
        .value("DriveCDIcon", DriveCDIcon)
        .value("DriveDVDIcon", DriveDVDIcon)
        .value("DriveNetIcon", DriveNetIcon)
        .value("DirOpenIcon", DirOpenIcon)
        .value("DirClosedIcon", DirClosedIcon)
        .value("DirLinkIcon", DirLinkIcon)
        .value("DirLinkOpenIcon", DirLinkOpenIcon)
        .value("FileIcon", FileIcon)
        .value("FileLinkIcon", FileLinkIcon)
        .value("ToolBarHorizontalExtensionButton", ToolBarHorizontalExtensionButton)
        .value("ToolBarVerticalExtensionButton", ToolBarVerticalExtensionButton)
        .value("FileDialogStart", FileDialogStart)
        .value("FileDialogEnd", FileDialogEnd)
        .value("FileDialogToParent", FileDialogToParent)
        .value("FileDialogNewFolder", FileDialogNewFolder)
        .value("FileDialogDetailedView", FileDialogDetailedView)
        .value("FileDialogInfoView", FileDialogInfoView)
        .value("FileDialogContentsView", FileDialogContentsView)
        .value("FileDialogListView", FileDialogListView)
        .value("FileDialogBack", FileDialogBack)
        .value("DirIcon", DirIcon)
        .value("DialogOkButton", DialogOkButton)
        .value("DialogCancelButton", DialogCancelButton)
        .value("DialogHelpButton", DialogHelpButton)
        .value("DialogOpenButton", DialogOpenButton)
        .value("DialogSaveButton", DialogSaveButton)
        .value("DialogCloseButton", DialogCloseButton)
        .value("DialogApplyButton", DialogApplyButton)
        .value("DialogResetButton", DialogResetButton)
        .value("DialogDiscardButton", DialogDiscardButton)
        .value("DialogYesButton", DialogYesButton)
        .value("DialogNoButton", DialogNoButton)
        .value("ArrowUp", ArrowUp)
        .value("ArrowDown", ArrowDown)
        .value("ArrowLeft", ArrowLeft)
        .value("ArrowRight", ArrowRight)
        .value("ArrowBack", ArrowBack)
        .value("ArrowForward", ArrowForward)
        .value("DirHomeIcon", DirHomeIcon)
        .value("CommandLink", CommandLink)
        .value("VistaShield", VistaShield)
        .value("BrowserReload", BrowserReload)
        .value("BrowserStop", BrowserStop)
        .value("MediaPlay", MediaPlay)
        .value("MediaStop", MediaStop)
        .value("MediaPause", MediaPause)
        .value("MediaSkipForward", MediaSkipForward)
        .value("MediaSkipBackward", MediaSkipBackward)
        .value("MediaSeekForward", MediaSeekForward)
        .value("MediaSeekBackward", MediaSeekBackward)
        .value("MediaVolume", MediaVolume)
        .value("MediaVolumeMuted", MediaVolumeMuted)
        .value("LineEditClearButton", LineEditClearButton)
        .value("DialogYesToAllButton", DialogYesToAllButton)
        .value("DialogNoToAllButton", DialogNoToAllButton)
        .value("DialogSaveAllButton", DialogSaveAllButton)
        .value("DialogAbortButton", DialogAbortButton)
        .value("DialogRetryButton", DialogRetryButton)
        .value("DialogIgnoreButton", DialogIgnoreButton)
        .value("RestoreDefaultsButton", RestoreDefaultsButton)
        .value("TabCloseButton", TabCloseButton)
        .export_values()
        .finalize()
        ;

    Icon.def_static("standard",
                    &Icon::standard,
                    py::arg("type"));

    Icon.def_static("grapheme",
                    py::overload_cast<const QString &, double, const QBrush &>(&Icon::grapheme),
                    py::arg("grapheme"),
                    py::arg("scalar") = 1.0,
                    py::arg("brush") = Icon::graphemeDefaultBrush());

    Icon.def_static("iconified",
                    &Icon::iconified,
                    py::arg("icon"),
                    py::arg("background_brush") = Icon::iconifiedDefaultBackgroundBrush(),
                    py::arg("border_radius") = 1.0,
                    py::arg("border_width") = 1,
                    py::arg("border_brush") = Icon::iconifiedDefaultBorderBrush());

    Icon.def_static("composed",
                    &Icon::composed,
                    py::arg("icon1"),
                    py::arg("icon2"),
                    py::arg("size1") = .7,
                    py::arg("size2") = .7,
                    py::arg("x1") = .0,
                    py::arg("y1") = .0,
                    py::arg("x2") = 1.,
                    py::arg("y2") = 1.);

    // ------------------------------------------------------------------------

    m.def("setClipboardText",
          &setClipboardText,
          py::arg("text"));

    m.def("setClipboardTextAndPaste",
          &setClipboardTextAndPaste,
          py::arg("text"));

    m.def("havePasteSupport",
          &havePasteSupport);

    // open conflicsts the built-in open. Use openFile.
    m.def("openFile",
          static_cast<void(*)(const QString &)>(&open),
          py::arg("path"));

    m.def("openUrl",
          &openUrl,
          py::arg("url"));

    m.def("runDetachedProcess",
          static_cast<long long(*)(const QStringList &commandline, const QString &working_dir)>(&runDetachedProcess),
          py::arg("cmdln"),
          py::arg("workdir") = QString());

    m.def("runTerminal",
          [](const QString &s){ apps->runTerminal(s); },
          py::arg("script"));
}
