/*
 * This file is part of the Cargo plugin for KDevelop.
 *
 * Copyright 2017 Miha Čančula <miha@noughmad.eu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "test_cargo.h"
#include "cargo-test-paths.h"
#include "../cargobuildjob.h"
#include "../cargofindtestsjob.h"
#include "../cargoplugin.h"

#include <QTest>
#include <QSignalSpy>
#include <KJob>

#include <tests/testcore.h>
#include <tests/autotestshell.h>

#include <interfaces/icore.h>
#include <interfaces/iproject.h>
#include <interfaces/iprojectcontroller.h>
#include <interfaces/iplugin.h>
#include <interfaces/iplugincontroller.h>
#include <interfaces/itestcontroller.h>
#include <interfaces/itestsuite.h>

#include <project/interfaces/ibuildsystemmanager.h>
#include <project/interfaces/iprojectbuilder.h>
#include <project/projectmodel.h>

using namespace KDevelop;

Q_DECLARE_METATYPE(KDevelop::TestResult);
Q_DECLARE_METATYPE(KDevelop::ITestSuite*);

IProject* loadProject(const QString& name)
{
    Path path(QStringLiteral(CARGO_TESTS_PROJECTS_DIR));
    path.addPath(name);
    path.addPath(name + QStringLiteral(".kdev4"));

    QSignalSpy spy(Core::self()->projectController(), &IProjectController::projectOpened);
    Q_ASSERT(spy.isValid());

    Core::self()->projectController()->openProject(path.toUrl());

    if ( spy.isEmpty() && !spy.wait(30000) ) {
        qFatal( "Timeout while waiting for opened signal" );
    }

    IProject* project = Core::self()->projectController()->findProjectByName(name);
    Q_ASSERT(project);
    Q_ASSERT(project->buildSystemManager());
    Q_ASSERT(project->projectFile() == path);

    return project;
}

void CargoPluginTest::initTestCase()
{
    AutoTestShell::init({
        QStringLiteral("KDevCargo"),
        QStringLiteral("KDevStandardOutputView"),
    });
    TestCore::initialize();

    qRegisterMetaType<KDevelop::IProject*>();
    qRegisterMetaType<KDevelop::ITestSuite*>();
    qRegisterMetaType<KDevelop::TestResult>();

    qDebug() << Core::self()->pluginController()->loadedPlugins();

    cleanup();
}

void CargoPluginTest::cleanupTestCase()
{
    TestCore::shutdown();
}

void CargoPluginTest::cleanup()
{
    Core::self()->projectController()->closeAllProjects();
}

void CargoPluginTest::testOpenProject()
{
    IProject* project = loadProject(QStringLiteral("kdev-cargo-test"));
    QVERIFY(project);
}

void CargoPluginTest::testBuildProject()
{
    IProject* project = loadProject(QStringLiteral("kdev-cargo-test"));
    QVERIFY(project);

    IBuildSystemManager* build = project->buildSystemManager();
    IProjectBuilder* builder = build->builder();
    bool built = builder->build(project->projectItem())->exec();
    QVERIFY(built);
}

void CargoPluginTest::testFindTests()
{
    IProject* project = loadProject(QStringLiteral("kdev-cargo-test"));
    QVERIFY(project);

    CargoPlugin* plugin = new CargoPlugin(Core::self());

    CargoBuildJob* job = new CargoBuildJob(plugin, project->projectItem(), QStringLiteral("test"));
    job->setRunArguments({ QStringLiteral("--all"), QStringLiteral("--no-run") });
    job->setStandardViewType(KDevelop::IOutputView::BuildView);
    QVERIFY(static_cast<KJob*>(job)->exec());

    CargoFindTestsJob* findTestsJob = new CargoFindTestsJob(plugin, project->projectItem());
    QVERIFY(findTestsJob->exec());

    QList<ITestSuite*> suites = Core::self()->testController()->testSuitesForProject(project);
    QCOMPARE(suites.size(), 1);

    if (suites.size() == 1)
    {
        ITestSuite* suite = suites.first();
        QCOMPARE(suite->name(), QStringLiteral("kdev_cargo_test"));

        QStringList cases = suite->cases();
        QSet<QString> expectedCases = {
            QStringLiteral("tests::passes"),
            QStringLiteral("tests::fails"),
            QStringLiteral("tests::should_fail_and_fails"),
            QStringLiteral("tests::should_fail_and_passes"),
            QStringLiteral("tests::is_ignored_and_passes"),
            QStringLiteral("tests::is_ignored_and_fails"),
        };
        QCOMPARE(suite->cases().toSet(), expectedCases);
    }
}

void CargoPluginTest::testRunTests()
{
    IProject* project = loadProject(QStringLiteral("kdev-cargo-test"));
    QVERIFY(project);

    CargoPlugin* plugin = new CargoPlugin(Core::self());

    CargoBuildJob* job = new CargoBuildJob(plugin, project->projectItem(), QStringLiteral("test"));
    job->setRunArguments({ QStringLiteral("--all"), QStringLiteral("--no-run") });
    job->setStandardViewType(KDevelop::IOutputView::BuildView);
    QVERIFY(static_cast<KJob*>(job)->exec());

    CargoFindTestsJob* findTestsJob = new CargoFindTestsJob(plugin, project->projectItem());
    QVERIFY(findTestsJob->exec());

    QList<ITestSuite*> suites = Core::self()->testController()->testSuitesForProject(project);
    QCOMPARE(suites.size(), 1);

    if (suites.size() == 1)
    {
        ITestSuite* suite = suites.first();

        QSignalSpy spy(Core::self()->testController(), &ITestController::testRunFinished);
        QVERIFY(spy.isValid());

        suite->launchAllCases(ITestSuite::Silent)->exec();

        QCOMPARE(spy.count(), 1);

        TestResult result = qvariant_cast<TestResult>(spy.at(0).at(1));
        QCOMPARE(result.suiteResult, TestResult::Failed);

        QCOMPARE(result.testCaseResults.value(QStringLiteral("tests::passes")), TestResult::Passed);
        QCOMPARE(result.testCaseResults.value(QStringLiteral("tests::fails")), TestResult::Failed);

        /*
         * Rust tests do have a #[should_panic] attribute, but that does not change the test output.
         * We thus cannot infer ExpectedFail or UnexpectedPass states, so such tests are marked with Passed or Failed.
         */
        QCOMPARE(result.testCaseResults.value(QStringLiteral("tests::should_fail_and_fails")), TestResult::Passed);
        QCOMPARE(result.testCaseResults.value(QStringLiteral("tests::should_fail_and_passes")), TestResult::Failed);

        /*
         * Ignored test cases are not run when running the entire suite.
         * They are only run when selected individually
         */
        QCOMPARE(result.testCaseResults.value(QStringLiteral("tests::is_ignored_and_passes")), TestResult::NotRun);
        QCOMPARE(result.testCaseResults.value(QStringLiteral("tests::is_ignored_and_fails")), TestResult::NotRun);
    }
}

void CargoPluginTest::testRunSingleCases()
{
    IProject* project = loadProject(QStringLiteral("kdev-cargo-test"));
    QVERIFY(project);

    CargoPlugin* plugin = new CargoPlugin(Core::self());

    CargoBuildJob* job = new CargoBuildJob(plugin, project->projectItem(), QStringLiteral("test"));
    job->setRunArguments({ QStringLiteral("--all"), QStringLiteral("--no-run") });
    job->setStandardViewType(KDevelop::IOutputView::BuildView);
    QVERIFY(static_cast<KJob*>(job)->exec());

    CargoFindTestsJob* findTestsJob = new CargoFindTestsJob(plugin, project->projectItem());
    QVERIFY(findTestsJob->exec());

    QList<ITestSuite*> suites = Core::self()->testController()->testSuitesForProject(project);
    QCOMPARE(suites.size(), 1);

    if (suites.size() == 1)
    {
        ITestSuite* suite = suites.first();

        {
            QSignalSpy spy(Core::self()->testController(), &ITestController::testRunFinished);
            QVERIFY(spy.isValid());

            suite->launchCase(QStringLiteral("tests::passes"), ITestSuite::Silent)->exec();

            QCOMPARE(spy.count(), 1);

            TestResult result = qvariant_cast<TestResult>(spy.at(0).at(1));
            QCOMPARE(result.suiteResult, TestResult::Passed);
            QCOMPARE(result.testCaseResults.value(QStringLiteral("tests::passes")), TestResult::Passed);
        }

        {
            QSignalSpy spy(Core::self()->testController(), &ITestController::testRunFinished);
            QVERIFY(spy.isValid());

            suite->launchCase(QStringLiteral("tests::fails"), ITestSuite::Silent)->exec();

            QCOMPARE(spy.count(), 1);

            TestResult result = qvariant_cast<TestResult>(spy.at(0).at(1));
            QCOMPARE(result.suiteResult, TestResult::Failed);
            QCOMPARE(result.testCaseResults.value(QStringLiteral("tests::fails")), TestResult::Failed);
        }
    }
}

void CargoPluginTest::testRunIgnoredCases()
{
    IProject* project = loadProject(QStringLiteral("kdev-cargo-test"));
    QVERIFY(project);

    CargoPlugin* plugin = new CargoPlugin(Core::self());

    CargoBuildJob* job = new CargoBuildJob(plugin, project->projectItem(), QStringLiteral("test"));
    job->setRunArguments({ QStringLiteral("--all"), QStringLiteral("--no-run") });
    job->setStandardViewType(KDevelop::IOutputView::BuildView);
    QVERIFY(static_cast<KJob*>(job)->exec());

    CargoFindTestsJob* findTestsJob = new CargoFindTestsJob(plugin, project->projectItem());
    QVERIFY(findTestsJob->exec());

    QList<ITestSuite*> suites = Core::self()->testController()->testSuitesForProject(project);
    QCOMPARE(suites.size(), 1);

    if (suites.size() == 1)
    {
        ITestSuite* suite = suites.first();

        {
            QSignalSpy spy(Core::self()->testController(), &ITestController::testRunFinished);
            QVERIFY(spy.isValid());

            suite->launchCase(QStringLiteral("tests::is_ignored_and_passes"), ITestSuite::Silent)->exec();

            QCOMPARE(spy.count(), 1);

            TestResult result = qvariant_cast<TestResult>(spy.at(0).at(1));
            QCOMPARE(result.suiteResult, TestResult::Passed);
            QCOMPARE(result.testCaseResults.value(QStringLiteral("tests::is_ignored_and_passes")), TestResult::Passed);
        }

        {
            QSignalSpy spy(Core::self()->testController(), &ITestController::testRunFinished);
            QVERIFY(spy.isValid());

            suite->launchCase(QStringLiteral("tests::is_ignored_and_fails"), ITestSuite::Silent)->exec();

            QCOMPARE(spy.count(), 1);

            TestResult result = qvariant_cast<TestResult>(spy.at(0).at(1));
            QCOMPARE(result.suiteResult, TestResult::Failed);
            QCOMPARE(result.testCaseResults.value(QStringLiteral("tests::is_ignored_and_fails")), TestResult::Failed);
        }

    }
}



QTEST_MAIN(CargoPluginTest);
