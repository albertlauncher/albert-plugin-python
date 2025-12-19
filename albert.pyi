"""
.. https://docutils.sourceforge.io/docs/ref/rst/restructuredtext.html
.. https://www.sphinx-doc.org/en/master/usage/restructuredtext/basics.html

====================================================================================================
Albert Python interface v5.0
====================================================================================================

To be a valid Python plugin a Python module has to contain at least the mandatory metadata fields
and a class named ``Plugin`` that inherits the :class:`PluginInstance` class.

See Also:
    `Albert C++ Reference <https://albertlauncher.github.io/reference/namespacealbert.html>`_

----------------------------------------------------------------------------------------------------
Mandatory metadata variables
----------------------------------------------------------------------------------------------------

``md_iid`` : *str*
    Python interface version (<major>.<minor>)

``md_version`` : *str*
    Plugin version (<major>.<minor>)

``md_name`` : *str*
    Human readable name

``md_description`` : *str*
    A brief, imperative description. (Like "Launch apps" or "Open files")


----------------------------------------------------------------------------------------------------
Optional metadata variables
----------------------------------------------------------------------------------------------------

``md_license`` : *str*
    Short form e.g. MIT or BSD-2

``md_url`` : *str*
    Browsable source, issues etc

``md_readme_url`` : *str*
    Human readable online README

``md_authors`` : *List(str)*
    The authors. Preferably using mentionable Github usernames.

``md_maintainers`` : *List(str)*
    The current maintainers. Preferably using mentionable Github usernames.

``md_bin_dependencies`` : *List(str)*
    Required executable(s). Have to match the name of the executable in $PATH.

``md_lib_dependencies`` : *List(str)*
    Required Python package(s). Have to match the PyPI package name.

``md_credits`` : *List(str)*
    Third party credit(s) and license notes

``md_platforms`` : *List(str)*
    List of supported platforms. If empty all platforms are supported.


====================================================================================================
Changelog
====================================================================================================

- ``5.0``

  This change adopts the coroutine based query handler API and internationalized tokenization.

  - Class ``MatchConfig``
    - Drop property ``separator_regex``.
    - Drop constructor parameter ``separator_regex``.
  - Class ``Query``
    - Rename to ``QueryContext``.
    - Remove ``add`` overloads.
    - Add property ``usageScoring``.
    - Rename property ``string`` to ``query``.
  - Rename ``TriggerQueryHandler`` to ``QueryHandler`` (New async trigger query handler base class).
    - Remove abstract method ``handleTriggerQuery`` (Use ``GeneratorQueryHandler`` instead).
  - Add class ``GeneratorQueryHandler`` (Lazy, coroutine based query handler).
    - Add abstract method ``items`` yielding batches of items.
  - Add class ``RankedQueryHandler`` (Eager, usage scored query handler).
    - Add abstract method ``rankItems`` returning scored items.
    - Add static method ``lazySort`` yielding lazily sorted items.
  - ``GlobalQueryHandler``
    - Remove abstract method ``handleGlobalQuery`` (Use ``rankItems`` instead).
  - Class ``Icon``
    - Add static method ``Icon.image(str|Path)``
    - Add static method ``Icon.fileType(str|Path)``
    - Add static method ``Icon.theme(str)``
    - Add enum ``Icon.StandardIconType``
    - Add static method ``Icon.standard(StandardIconType)``
    - Add static method ``Icon.grapheme(str, float, Color)``
    - Add static method ``Icon.iconified(Icon, Color, float, int, Color)``
    - Add static method ``Icon.composed(Icon, Icon, float, float, float, float, float, float)``
  - Remove enum ``StandardIconType``
  - Remove function ``makeImageIcon(str|Path)``
  - Remove function ``makeFileTypeIcon(str|Path)``
  - Remove function ``makeStandardIcon(StandardIconType)``
  - Remove function ``makeThemeIcon(str)``
  - Remove function ``makeGraphemeIcon(str, float, Color)``
  - Remove function ``makeIconifiedIcon(Icon, Color, float, int, Color)``
  - Remove function ``makeComposedIcon(Icon, Icon, float, float, float, float, float, float)``

- ``v4.0``

  - Add built-in icon factories and related types:
    - Add class ``Brush``
    - Add class ``Color``
    - Add class ``Icon``
    - Add enum ``StandardIconType``
    - Add function ``makeImageIcon(str|Path)``
    - Add function ``makeFileTypeIcon(str|Path)``
    - Add function ``makeStandardIcon(StandardIconType)``
    - Add function ``makeThemeIcon(str)``
    - Add function ``makeGraphemeIcon(str, float, Color)``
    - Add function ``makeIconifiedIcon(Icon, Color, float, int, Color)``
    - Add function ``makeComposedIcon(Icon, Icon, float, float, float, float, float, float)``
  - ``Item``
    - Remove abstract method ``Item.iconUrls(self) -> List[str]``
    - Add abstract method ``Item.icon(self) -> Icon``
  - ``StandardItem``
    - Remove property ``iconUrls``
    - Add property ``icon_factory``
    - Rename property  ``inputActionText`` to ``input_action_text``
  - ``RankItem``
    - Remove property access
  - ``IndexItem``
    - Remove property access

- ``v3.1``

  - Add metadata field ``md_readme_url``.
  - Add metadata field ``md_maintainers``.

- ``v3.0``

  - Drop metadata field ``md_id``.
  - ``PluginInstance``

    - Add ``extensions(…)``.
    - Drop ``__init__(…)`` parameter ``extensions``. Use ``extensions(…)``.
    - Drop ``de``-/``registerExtension(…)``. Use ``extensions(…)``.
    - Drop ``initialize(…)``/``finalize(…)``. Use ``__init__(…)``/``__del__(…)``.
    - Make property a method:

      - ``id``
      - ``name``
      - ``description``
      - ``cacheLocation``
      - ``configLocation``
      - ``dataLocation``

    - Do not implicitly create the directory in:

      - ``cacheLocation``
      - ``configLocation``
      - ``dataLocation``

  - Revert the property based approach of the extenision hierarchy. I.e. drop all properties and
    constructors in relevant classes:

    - ``Extension``
    - ``TriggerQueryHandler``
    - ``GlobalQueryHandler``
    - ``IndexQueryHandler``
    - ``FallbackHandler``

  - ``Item``: Make all readonly properties methods.
  - ``RankItem.__init__(…)`` add overload that takes a ``Match``.
  - ``MatchConfig``: Add new class.
  - ``Matcher.__init__(…)``: Add optional parameter ``config`` of type ``MatchConfig``.
  - ``runTerminal(…)``:

      - Drop parameter ``workdir``. Prepend ``cd <workdir>;`` to the script.
      - Drop parameter ``close_on_exit``. Append ``exec $SHELL;`` to the script.

  - Add ``openFile(…)``.

- ``v2.5``

  - Matcher now not considered experimental anymore.
  - Add ``Matcher.match(strings: List[str])``.
  - Add ``Matcher.match(*args: str)``.

- ``v2.4``

  - Deprecate parameter ``workdir`` of runTerminal. Prepend ``cd <workdir>;`` to your script.
  - Deprecate parameter ``close_on_exit`` of runTerminal. Append ``exec $SHELL;`` to your script.

- ``v2.3``

  - Module

    - Deprecate ``md_id``. Use ``PluginInstance.id``.

  - PluginInstance:

    - Add read only property ``id``.
    - Add read only property ``name``.
    - Add read only property ``description``.
    - Add instance method ``registerExtension(…)``.
    - Add instance method ``deregisterExtension(…)``.
    - Deprecate ``initialize(…)``. Use ``__init__(…)``.
    - Deprecate ``finalize(…)``. Use ``__del__(…)``.
    - Deprecate ``__init__`` extensions parameter. Use ``(de)``-/``registerExtension(…)``.
    - Auto(de)register plugin extension if ``Plugin`` is instance of ``Extension``.

  - Use ``Query`` instead of ``TriggerQuery`` and ``GlobalQuery``.

    - The interface is backward compatible, however type hints may break.

  - Add ``Matcher`` and ``Match`` convenience classes.
  - Notification:

    - Add property ``title``.
    - Add property ``text``.
    - Add instance method ``send()``.
    - Add instance method ``dismiss()``.
    - Note: Notification does not display unless ``send(…)`` has been called.

- ``v2.2``

  - ``PluginInstance.configWidget`` supports ``'label'``
  - ``__doc__`` is not used anymore, since 0.23 drops ``long_description`` metadata
  - Deprecate ``md_maintainers`` not used anymore
  - Add optional variable ``md_authors``

- ``v2.1``

  - Add ``PluginInstance.readConfig``
  - Add ``PluginInstance.writeConfig``
  - Add ``PluginInstance.configWidget``

"""

from abc import abstractmethod, ABC
from enum import IntEnum
from pathlib import Path
from typing import Any, Callable, List, overload, final
from collections.abc import Generator

class Action:
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1Action.html>`_
    """

    def __init__(self,
                 id: str,
                 text: str,
                 callable: Callable):
        ...


class Color:
    ...

    def __init__(self,
                 r: int,
                 g: int,
                 b: int,
                 a: int = 255):
        ...

    r: int
    g: int
    b: int
    a: int


class Icon(ABC):

    @staticmethod
    def image(path: str | Path) -> Icon:
        """
        Returns an icon from an image file at **path**.
        """

    @staticmethod
    def fileType(path: str | Path) -> Icon:
        """
        Returns an icon representing the file type of the file at **path**.
        """

    @staticmethod
    def theme(name: str) -> Icon:
        """
        Returns an icon from the current icon theme with the given **name**.
        """

    class StandardIconType(IntEnum):
        TitleBarMenuButton = ...,
        TitleBarMinButton = ...,
        TitleBarMaxButton = ...,
        TitleBarCloseButton = ...,
        TitleBarNormalButton = ...,
        TitleBarShadeButton = ...,
        TitleBarUnshadeButton = ...,
        TitleBarContextHelpButton = ...,
        DockWidgetCloseButton = ...,
        MessageBoxInformation = ...,
        MessageBoxWarning = ...,
        MessageBoxCritical = ...,
        MessageBoxQuestion = ...,
        DesktopIcon = ...,
        TrashIcon = ...,
        ComputerIcon = ...,
        DriveFDIcon = ...,
        DriveHDIcon = ...,
        DriveCDIcon = ...,
        DriveDVDIcon = ...,
        DriveNetIcon = ...,
        DirOpenIcon = ...,
        DirClosedIcon = ...,
        DirLinkIcon = ...,
        DirLinkOpenIcon = ...,
        FileIcon = ...,
        FileLinkIcon = ...,
        ToolBarHorizontalExtensionButton = ...,
        ToolBarVerticalExtensionButton = ...,
        FileDialogStart = ...,
        FileDialogEnd = ...,
        FileDialogToParent = ...,
        FileDialogNewFolder = ...,
        FileDialogDetailedView = ...,
        FileDialogInfoView = ...,
        FileDialogContentsView = ...,
        FileDialogListView = ...,
        FileDialogBack = ...,
        DirIcon = ...,
        DialogOkButton = ...,
        DialogCancelButton = ...,
        DialogHelpButton = ...,
        DialogOpenButton = ...,
        DialogSaveButton = ...,
        DialogCloseButton = ...,
        DialogApplyButton = ...,
        DialogResetButton = ...,
        DialogDiscardButton = ...,
        DialogYesButton = ...,
        DialogNoButton = ...,
        ArrowUp = ...,
        ArrowDown = ...,
        ArrowLeft = ...,
        ArrowRight = ...,
        ArrowBack = ...,
        ArrowForward = ...,
        DirHomeIcon = ...,
        CommandLink = ...,
        VistaShield = ...,
        BrowserReload = ...,
        BrowserStop = ...,
        MediaPlay = ...,
        MediaStop = ...,
        MediaPause = ...,
        MediaSkipForward = ...,
        MediaSkipBackward = ...,
        MediaSeekForward = ...,
        MediaSeekBackward = ...,
        MediaVolume = ...,
        MediaVolumeMuted = ...,
        LineEditClearButton = ...,
        DialogYesToAllButton = ...,
        DialogNoToAllButton = ...,
        DialogSaveAllButton = ...,
        DialogAbortButton = ...,
        DialogRetryButton = ...,
        DialogIgnoreButton = ...,
        RestoreDefaultsButton = ...,
        TabCloseButtom = ...

    @staticmethod
    def standard(type: StandardIconType) -> Icon:
        """
        Returns a standard icon for the given **type**.
        """

    @staticmethod
    def grapheme(grapheme: str,
                 scalar: float | None = None,
                 brush: Color | None = None) -> Icon:
        """
        Returns an icon rendering the given **grapheme**, scaled by **scalar** and colored with
        **brush**.
        """

    @staticmethod
    def iconified(icon: Icon,
                  background_brush: Color | None = None,
                  border_radius: float | None = None,
                  border_width: int | None = None,
                  border_color: Color | None = None) -> Icon:
        """
        Returns an iconified **icon**. i.e. drawn in a colored rounded rectangle with a border.
        **background_brush** specifies the background color, **border_width** the border width in
        device independent pixels, **border_radius** the relative border radius (0.0 - 1.0),
        and **border_color** the border color.
        """


    @staticmethod
    def composed(icon1: Icon,
                 icon2: Icon,
                 size1: float | None  = None,
                 size2: float | None  = None,
                 x1: float | None  = None,
                 y1: float | None  = None,
                 x2: float | None  = None,
                 y2: float | None  = None) -> Icon:
        """
        Returns a composed icon of **icon1** and **icon2**.
        **size1** and **size2** specify the relative sizes (0.0 - 1.0) of the icons.
        **x1**, **y1**, **x2**, and **y2** specify the relative positions (0.0 - 1.0, 0.5 is
        centered) of the icons.
        """


class MatchConfig:
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1util_1_1MatchConfig.html>`_
    """

    def __init__(self,
                 fuzzy: bool = False,
                 ignore_case: bool = True,
                 ignore_word_order: bool = True,
                 ignore_diacritics: bool = True):
        """
        Constructs a ``MatchConfig`` initialized with the values of **fuzzy**, **ignore_case**,
        **ignore_diacritics* and **ignore_word_order**. All parameters are optional.
        """

    fuzzy: bool
    """
    Match strings error tolerant.
    """

    ignore_case: bool
    """
    Match strings case insensitive.
    """

    ignore_word_order: bool
    """
    Match strings independent of their order.
    """

    ignore_diacritics: bool
    """
    Match strings normalized.
    """


class Match:
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1util_1_1Match.html>`_
    """

    @property
    def score(self) -> float:
        """
        The score of this match.
        """

    def isMatch(self) -> bool:
        """
        Returns ``True`` if this is a match, otherwise returns ``False``.
        """

    def isEmptyMatch(self) -> bool:
        """
        Returns ``True`` if this is a zero score match, otherwise returns ``False``.
        """

    def isExactMatch(self) -> bool:
        """
        Returns ``True`` if this is a perfect match, otherwise returns ``False``.
        """

    def __bool__(self) -> bool:
        """
        Converts the match to ``bool`` using ``isMatch()``.
        """

    def __float__(self) -> float:
        """
        Converts the match to ``float`` using ``score()``.
        """


class Matcher:
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1util_1_1Matcher.html>`_
    """

    def __init__(self,
                 string: str,
                 config: MatchConfig = MatchConfig()):
        """
        Constructs a ``Matcher`` for the given **string** and **config**.
        """

    @overload
    def match(self, string: str) -> Match:
        """
        Returns a ``Match`` for the **string**.
        """

    @overload
    def match(self, strings: List[str]) -> Match:
        """
        Returns the best ``Match`` for the **strings**.
        """

    @overload
    def match(self, *args: str) -> Match:
        """
        Returns the best ``Match`` for the **args**.
        """


class Item(ABC):
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1Item.html>`_
    """

    @abstractmethod
    def id(self) -> str:
        """
        Returns the item identifier.
        """

    @abstractmethod
    def text(self) -> str:
        """
        Returns the item text.
        """

    @abstractmethod
    def subtext(self) -> str:
        """
        Returns the item subtext.
        """

    @abstractmethod
    def inputActionText(self) -> str:
        """
        Returns the item input action text.
        """

    @abstractmethod
    def icon(self) -> Icon:
        """
        Creates and returns an item icon on demand.
        """

    @abstractmethod
    def actions(self) -> List[Action]:
        """
        Returns the item actions.
        """


class RankItem:
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1RankItem.html>`_
    """

    def __init__(self,
                 item: Item,
                 score: float|Match):
        ...


class IndexItem:
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1util_1_1IndexItem.html>`_
    """

    def __init__(self,
                 item: Item,
                 string: str):
        ...


class UsageScoring:
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1UsageScoring.html>`_
    """

    def modifyMatchScores(self,
                          extension_id: str,
                          rank_items: list[RankItem]):
        ...


class StandardItem(Item):
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1util_1_1StandardItem.html>`_
    """

    def __init__(self,
                 id: str | None = None,
                 text: str | None = None,
                 subtext: str | None = None,
                 icon_factory: Callable[[], Icon] | None = None,
                 actions: List[Action] | None = None,
                 input_action_text: str | None = None
                 ):
        ...

    def id(self) -> str:
        ...

    def text(self) -> str:
        ...

    def subtext(self) -> str:
        ...

    def inputActionText(self) -> str:
        ...

    def icon(self) -> Icon:
        ...

    def actions(self) -> List[Action]:
        ...

    id: str
    """
    The item identifier.
    """

    text: str
    """
    The item text.
    """

    subtext: str
    """
    The item subtext.
    """

    icon_factory: Callable[[], Icon]
    """
    The item icon.
    """

    actions: List[Action]
    """
    The item actions.
    """

    input_action_text: str
    """
    The item input action text.
    """


class QueryContext:
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1QueryContext.html>`_
    """

    @property
    def trigger(self) -> str:
        """
        Returns the trigger string of the query.
        """

    @property
    def query(self) -> str:
        """
        Returns the query string of the query.
        """

    @property
    def isValid(self) -> bool:
        """
        Returns ``True`` if the query is valid; ``False`` if it has been cancelled.
        """

    @property
    def usageScoring(self) -> UsageScoring:
        """
        Returns the usage scoring.
        """


class Extension(ABC):
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1Extension.html>`_
    """

    @abstractmethod
    def id(self) -> str:
        """
        Returns the extension identifier.
        """

    @abstractmethod
    def name(self) -> str:
        """
        Returns the pretty, human readable extension name.
        """

    @abstractmethod
    def description(self) -> str:
        """
        Returns the brief extension description.
        """


class QueryHandler(Extension):
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1QueryHandler.html>`_
    """

    def synopsis(self, query: str) -> str:
        """
        Returns the input hint for the given **query**.
        The returned string will be displayed in the input line if space permits.
        The base class implementation returns an empty string.
        """

    def allowTriggerRemap(self) -> bool:
        """
        Returns ``True`` if the user is allowed to set a custom trigger, otherwise returns ``False``.
        The base class implementation returns ``True``.
        """

    def defaultTrigger(self) -> str:
        """
        Returns the default trigger.
        The base class implementation returns ``Extension.id`` with a space appended.
        """

    def setTrigger(self, trigger: str):
        """
        Notifies that the user-defined trigger has changed to **trigger**.
        The base class implementation does nothing.
        """

    def supportsFuzzyMatching(self) -> bool:
        """
        Returns ``True`` if the handler supports fuzzy matching, otherwise returns ``False``.
        If ``True``, the user can enable fuzzy matching for this handler and
        ``setFuzzyMatching(bool)`` should be implemented accordingly.
        The base class implementation returns ``False``.
        """

    def setFuzzyMatching(self, enabled: bool):
        """
        Sets the fuzzy matching mode to **enabled**.
        This function is called when the user toggles fuzzy matching for this handler.
        The base class implementation does nothing.
        """


class GeneratorQueryHandler(QueryHandler):
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1GeneratorQueryHandler.html>`_
    """

    @abstractmethod
    def items(self, context: QueryContext) -> Generator[List[Item]]:
        """
        Yields batches of items for **context** lazily.

        The batch size is defined by the implementation.

        Note: Executed in a background thread.
        """


class RankedQueryHandler(GeneratorQueryHandler):
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1RankedQueryHandler.html>`_
    """

    def items(self, context: QueryContext) -> Generator[List[Item]]:
        """
        Implements ``GeneratorQueryHandler.items()``.
        Yields result of ``rankItems`` for **context** usage scored and lazily sorted.
        """

    @staticmethod
    def lazySort(self, rank_items: List[RankItem]) -> Generator[List[Item]]:
        """
        Yields **rank_items** lazily sorted.
        """

    @abstractmethod
    def rankItems(self, context: QueryContext) -> List[RankItem]:
        """
        Returns a list of scored matches for **context**.

        The match score should make sense and often is the fraction of matched characters (legth of
        query string / length of matched string). The empty pattern matches everything and returns
        all items with a score of 0.

        Note: Executed in a background thread.
        """


class GlobalQueryHandler(RankedQueryHandler):
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1GlobalQueryHandler.html>`_
    """


class IndexQueryHandler(GlobalQueryHandler):
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1util_1_1IndexQueryHandler.html>`_
    """

    @final
    def setFuzzyMatching(self, enabled: bool):
        """
        Sets the fuzzy matching mode to **enabled** and triggers ``updateIndexItems()``.
        """

    @final
    def supportsFuzzyMatching(self) -> bool:
        """
        Returns ``True``.
        """

    def rankItems(self, context: QueryContext) -> List[RankItem]:
        """
        Implements ``RankedQueryHandler.rankItems()``.

        Returns a list of scored matches for **context** using the index.
        """

    def setIndexItems(self, index_items: List[IndexItem]):
        """
        Sets the items of the index to **index_items**.

        Meant to be called in ``updateIndexItems()``.
        """

    @abstractmethod
    def updateIndexItems(self):
        """
        Updates the index.

        Called when the index needs to be updated, i.e. for initialization, on user changes to the
        index config (fuzzy, etc…) and probably by the client itself if the items changed. This
        function should call ``setIndexItems`` to update the index.

        Do not call this method on plugin initialization. It will be called once loaded.
        """


class FallbackHandler(Extension):
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1FallbackHandler.html>`_
    """

    @abstractmethod
    def fallbacks(self, query: str) -> List[Item]:
        """
        Returns fallback items for **query**.
        """


class Notification:
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1util_1_1Notification.html>`_
    """

    def __init__(self,
                 title: str = '',
                 text: str = ''):
        ...

    title: str

    text: str

    def send(self):
        ...

    def dismiss(self):
        ...


def debug(arg: Any):
    """
    Logs ``str(arg)`` as debug message in the logging category of this plugin.

    Note:
        This function is not part of the albert module and here for reference only.
        The attribute is attached to the module at load time.
    """


def info(arg: Any):
    """
    Logs ``str(arg)`` as info message in the logging category of this plugin.

    Note:
        This function is not part of the albert module and here for reference only.
        The attribute is attached to the module at load time.
    """


def warning(arg: Any):
    """
    Logs ``str(arg)`` as warning message in the logging category of this plugin.

    Note:
        This function is not part of the albert module and here for reference only.
        The attribute is attached to the module at load time.
    """


def critical(arg: Any):
    """
    Logs ``str(arg)`` as critical message in the logging category of this plugin.

    Note:
        This function is not part of the albert module and here for reference only.
        The attribute is attached to the module at load time.
    """


def setClipboardText(text: str):
    """
    Sets the system clipboard to **text**.
    """


def setClipboardTextAndPaste(text: str):
    """
    Sets the system clipboard to **text** and paste the content to the front-most window.

    Note:
        Requires paste support. Check ``havePasteSupport()`` before using this function.
    """


def havePasteSupport() -> bool:
    """
    Returns ``True`` if the platform supports pasting, otherwise returns ``False``.

    Note:
        This is a requirement for ``setClipboardTextAndPaste(…)`` to work.
    """


def openUrl(url: str):
    """
    Opens the URL **url** with the default URL handler.
    """


def openFile(path: str):
    """
    Opens the file at **path** with the default application.
    """


def runDetachedProcess(cmdln: List[str], workdir: str = '') -> int:
    """
    Starts the **cmdln** in a new process, and detaches from it. Returns the PID on success;
    otherwise returns 0. The process will be started in the directory **workdir**. If
    **workdir** is empty, the working directory is the users home directory.
    """


def runTerminal(script: str):
    """
    Runs a **script** in the users shell and terminal.
    """


class PluginInstance(ABC):
    """
    `C++ Reference <https://albertlauncher.github.io/reference/classalbert_1_1PluginInstance.html>`_
    """

    def id(self) -> str:
        """
        Returns the id from the plugin metadata.
        """

    def name(self) -> str:
        """
        Returns the name from the plugin metadata.
        """

    def description(self) -> str:
        """
        Returns the description from the plugin metadata.
        """

    def cacheLocation(self) -> Path:
        """
        Returns the recommended cache location for this plugin.
        """

    def configLocation(self) -> Path:
        """
        Returns the recommended config location for this plugin.
        """

    def dataLocation(self) -> Path:
        """
        Returns the recommended data location for this plugin.
        """

    def extensions(self) -> List[Extension]:
        """
        Returns the extensions of this plugin. You are responsible to keep the extensions alive for
        the lifetime of this plugin. The base class implementation returns **self** if the plugin
        is an instance of ``Extension``, otherwise an empty list.
        """

    def readConfig(self, key: str, type: type[str|int|float|bool]) -> str|int|float|bool|None:
        """
        Returns the config value for **key** from the Albert settings or ``None`` if the value does
        notexist or errors occurred. Due to limitations of QSettings on some platforms the type may
        be lost, therefore the **type** has to be passed.
        """

    def writeConfig(self, key: str, value: str|int|float|bool):
        """
        Writes **value** to **key** in the Albert settings.
        """

    def configWidget(self) -> List[dict]:
        """
        Returns a list of dicts, describing a form layout as described below.

        **Descriptive config widget factory.**

        Define a static config widget using a list of dicts, each defining a row in the resulting
        form layout. Each dict must contain key ``type`` having one of the supported types specified
        below. Each type may define further keys.

        **A note on widget properties**

        This is a dict setting the properties of a widget. Note that due to the restricted type
        conversion only properties of type ``str``, ``int``, ``float, ``bool`` are settable.

        **Supported row types**

        * ``label``

          Display text spanning both columns. Additional keys:

          - ``text``: The text to display
          - ``widget_properties``: `QLabel properties <https://doc.qt.io/qt-6/qlabel.html#properties>`_

        * ``checkbox``

          A form layout item to edit boolean properties. Additional keys:

          - ``label``: The text displayed in front of the the editor widget.
          - ``property``: The name of the property that will be set on changes.
          - ``widget_properties``: `QCheckBox properties <https://doc.qt.io/qt-6/qcheckbox.html#properties>`_

        * ``lineedit``

          A form layout item to edit string properties. Additional keys:

          - ``label``: The text displayed in front of the the editor widget.
          - ``property``: The name of the property that will be set on changes.
          - ``widget_properties``: `QLineEdit properties <https://doc.qt.io/qt-6/qlineedit.html#properties>`_

        * ``combobox``

          A form layout item to set string properties using a list of options. Additional keys:

          - ``label``: The text displayed in front of the the editor widget.
          - ``property``: The name of the property that will be set on changes.
          - ``items``: The list of strings used to populate the combobox.
          - ``widget_properties``: `QComboBox properties <https://doc.qt.io/qt-6/qcombobox.html#properties>`_

        * ``spinbox``

          A form layout item to edit integer properties. Additional keys:

          - ``label``: The text displayed in front of the the editor widget.
          - ``property``: The name of the property that will be set on changes.
          - ``widget_properties``: `QSpinBox properties <https://doc.qt.io/qt-6/qspinbox.html#properties>`_

        * ``doublespinbox``

          A form layout item to edit float properties. Additional keys:

          - ``label``: The text displayed in front of the the editor widget.
          - ``property``: The name of the property that will be set on changes.
          - ``widget_properties``: `QDoubleSpinBox properties <https://doc.qt.io/qt-6/qdoublespinbox.html#properties>`_
        """


