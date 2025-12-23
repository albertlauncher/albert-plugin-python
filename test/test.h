// Copyright (c) 2022-2024 Manuel Schneider
#include <QCoreApplication>

class PythonTests : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase();

    void testBasicPluginInstance();
    void testExtensionPluginInstance();

    void testAction();
    void testItem();
    void testStandardItem();
    void testRankItem();
    void testIndexItem();
    void testMatcher();
    void testIconFactories();

    void testQueryContext();
    // void testQueryResults();
    // void testQueryExecution();

    // void testQueryHandler();
    void testGeneratorQueryHandler();
    void testRankedQueryHandler();
    void testGlobalQueryHandler();
    void testIndexQueryHandler();
    void testFallbackQueryHandler();

};
