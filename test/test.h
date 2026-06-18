// Copyright (c) 2022-2024 Manuel Schneider
#include <QCoreApplication>

class PythonTests : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase();


    void testPluginInstance();

    void testExtensionPluginInstance();


    void testAction();

    void testItem();

    void testStandardItem();

    void testRankItem();

    void testIndexItem();

    void testIconFactories();


    void testUsageScoring();

    void testMatcher();

    void testQueryContext();


    void testExtension();

    void testGeneratorQueryHandler();

    void testGlobalQueryHandler();

    void testIndexQueryHandler();

    void testFallbackQueryHandler();

};
