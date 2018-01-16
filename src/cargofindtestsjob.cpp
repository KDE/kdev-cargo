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

#include "cargofindtestsjob.h"

#include <QDir>
#include <KLocalizedString>
#include <KShell>

#include <interfaces/iproject.h>
#include <interfaces/itestsuite.h>
#include <interfaces/icore.h>
#include <interfaces/itestcontroller.h>
#include <interfaces/iruncontroller.h>
#include <outputview/outputmodel.h>
#include <outputview/outputdelegate.h>
#include <outputview/filtereditem.h>
#include <outputview/outputfilteringstrategies.h>
#include <util/commandexecutor.h>
#include <util/executecompositejob.h>
#include <project/projectmodel.h>
#include <language/duchain/indexeddeclaration.h>

#include "cargoplugin.h"
#include "debug.h"

using namespace KDevelop;

class CargoTestSuite : public KDevelop::ITestSuite
{
public:
    CargoTestSuite(const QString& suiteName, const Path& executable, const QStringList& cases, const QStringList& ignoredCases, IProject* project);
    virtual ~CargoTestSuite();

    QString name() const override { return m_suiteName; }
    Path executable() const { return m_executable; }
    QStringList cases() const override { return m_cases; }
    KDevelop::IProject * project() const override { return m_project; }

    KDevelop::IndexedDeclaration declaration() const override
    {
        return IndexedDeclaration();
    }

    KDevelop::IndexedDeclaration caseDeclaration(const QString & testCase) const override
    {
        Q_UNUSED(testCase);
        return IndexedDeclaration();
    }

    bool isIgnored(const QString& caseName)
    {
        return m_ignoredCases.contains(caseName);
    }

    KJob * launchCase(const QString & testCase, KDevelop::ITestSuite::TestJobVerbosity verbosity) override;
    KJob * launchAllCases(KDevelop::ITestSuite::TestJobVerbosity verbosity) override;
    KJob * launchCases(const QStringList & testCases, KDevelop::ITestSuite::TestJobVerbosity verbosity) override;

private:
    QString m_suiteName;
    Path m_executable;
    QStringList m_cases;
    QStringList m_ignoredCases;
    IProject* m_project;
};

CargoTestSuite::CargoTestSuite(const QString& suiteName, const KDevelop::Path& executable, const QStringList& cases, const QStringList& ignoredCases, KDevelop::IProject* project)
 : m_suiteName(suiteName)
 , m_executable(executable)
 , m_cases(cases)
 , m_ignoredCases(ignoredCases)
 , m_project(project)
{}

CargoTestSuite::~CargoTestSuite()
{}

class CargoRunTestsJob : public KDevelop::OutputJob
{
    Q_OBJECT
public:
    CargoRunTestsJob(CargoTestSuite* suite, const QString& caseName, KDevelop::ITestSuite::TestJobVerbosity verbosity)
    : KDevelop::OutputJob()
    , killed(false)
    , suite(suite)
    , caseName(caseName)
    , verbosity(verbosity)
    {
    }

    TestResult::TestCaseResult parseResult(const QString& res)
    {
        if (res == QStringLiteral("ok"))
        {
            return TestResult::Passed;
        }
        else if (res == QStringLiteral("FAILED"))
        {
            return TestResult::Failed;
        }
        else if (res == QStringLiteral("ignored"))
        {
            return TestResult::NotRun;
        }

        return TestResult::Error;
    }

    void start() override
    {
        setStandardToolView(KDevelop::IOutputView::TestView);
        QStringList arguments;
        arguments << QStringLiteral("--test");

        if (verbosity == ITestSuite::Verbose)
        {
            arguments << QStringLiteral("--nocapture");
        }

        if (!caseName.isEmpty())
        {
            arguments << QStringLiteral("--exact") << caseName;

            if (suite->isIgnored(caseName))
            {
                // When running a single test case, we also allow running ignored cases.
                // They will only be skipped when running the whole suite.
                arguments << QStringLiteral("--ignored");
            }
        }

        setStandardToolView( KDevelop::IOutputView::TestView );
        setVerbosity( verbosity == ITestSuite::Verbose ? KDevelop::OutputJob::Verbose : KDevelop::OutputJob::Silent );
        setBehaviours( KDevelop::IOutputView::AllowUserClose | KDevelop::IOutputView::AutoScroll );

        KDevelop::OutputModel* model = new KDevelop::OutputModel();
        setModel( model );

        startOutput();

        exec = new KDevelop::CommandExecutor( suite->executable().path(), this );

        exec->setArguments( arguments );

        connect( exec, &CommandExecutor::completed, this, &CargoRunTestsJob::procFinished );
        connect( exec, &CommandExecutor::failed, this, &CargoRunTestsJob::procError );

        connect( exec, &CommandExecutor::receivedStandardError, model, &OutputModel::appendLines );
        connect( exec, &CommandExecutor::receivedStandardOutput, [this, model](const QStringList& lines) {
            model->appendLines(lines);

            for (auto& line : lines)
            {
                qCDebug(KDEV_CARGO) << "Received output line" << line;
                QStringList elements = line.split(' ');
                if (elements.size() == 4 && elements[0] == QStringLiteral("test"))
                {
                    QString testCase = elements[1];
                    TestResult::TestCaseResult result = parseResult(elements[3]);

                    qCDebug(KDEV_CARGO) << "Received test case result" << testCase << elements[3] << result;

                    caseResults.insert(testCase, result);
                }
            }
        });

        model->appendLine( QStringLiteral("Test %1 %2").arg( suite->name() ).arg( caseName ) );
        exec->start();

        if (caseName.isEmpty())
        {
            ICore::self()->testController()->notifyTestRunStarted(suite, {caseName});
        }
        else
        {
            ICore::self()->testController()->notifyTestRunStarted(suite, suite->cases());
        }
    }

    bool doKill() override
    {
        killed = true;
        exec->kill();
        return true;
    }

private:

    void procFinished(int);
    void procError(QProcess::ProcessError);

    bool killed;
    CargoTestSuite* suite;
    QString caseName;
    ITestSuite::TestJobVerbosity verbosity;
    KDevelop::CommandExecutor* exec;
    QHash<QString, TestResult::TestCaseResult> caseResults;
};


void CargoRunTestsJob::procError( QProcess::ProcessError err )
{
    Q_UNUSED(err);

    if (killed)
    {
        return;
    }

    setError( FailedShownError );
    setErrorText( i18n( "Error running test command." ) );

    TestResult result;
    result.suiteResult = TestResult::Error;
    ICore::self()->testController()->notifyTestRunFinished(suite, result);

    emitResult();
}

void CargoRunTestsJob::procFinished(int code)
{
    if (killed)
    {
        return;
    }

    TestResult result;

    if (code != 0) {
        setError(FailedShownError);
        result.suiteResult = TestResult::Failed;
    } else {
        result.suiteResult = TestResult::Passed;
    }

    for (auto it = caseResults.constBegin(); it != caseResults.constEnd(); ++it)
    {
        result.testCaseResults.insert(it.key(), it.value());
    }

    ICore::self()->testController()->notifyTestRunFinished(suite, result);

    emitResult();
}

KJob* CargoTestSuite::launchCases(const QStringList & testCases, KDevelop::ITestSuite::TestJobVerbosity verbosity)
{
    /*
     * Rust test executable have no way of specifying a list of test cases to run.
     * Either all test cases, or only a single test case can be specified at a time.
     *
     * To work around this, when a list of tests is specified, we run one job per test case,
     * and run exactly one test case in each job.
     */

    Q_UNUSED(verbosity);
    // We do not want to run multiple verbose jobs at the same time

    QList<KJob*> jobs;
    for (auto& testCase : testCases)
    {
        jobs << launchCase(testCase, Silent);
    }
    return new ExecuteCompositeJob(ICore::self()->runController(), jobs);
}

KJob * CargoTestSuite::launchCase(const QString& testCase, KDevelop::ITestSuite::TestJobVerbosity verbosity)
{
    return new CargoRunTestsJob(this, testCase, verbosity);
}

KJob * CargoTestSuite::launchAllCases(KDevelop::ITestSuite::TestJobVerbosity verbosity)
{
    return new CargoRunTestsJob(this, QString(), verbosity);
}

CargoFindTestsJob::CargoFindTestsJob(CargoPlugin* plugin, KDevelop::ProjectBaseItem* item)
 : KJob(plugin)
 , plugin(plugin)
 , killed(false)
{
    setCapabilities( Killable );

    project = item->project();
    QString projectName = item->project()->name();
    builddir = plugin->buildDirectory( item ).toLocalFile();

    QString title = i18n("Find tests for Cargo project %1", projectName);
    setObjectName(title);
}

void CargoFindTestsJob::start()
{
    QDir testDir(builddir + "/target/debug");

    if (!testDir.exists())
    {
        setError( TargetsDirDoesNotExist );
        setErrorText( i18n( "The targets directory %1 does not exist", testDir.path() ) );
        emitResult();
        return;
    }

    executors.clear();
    numExecutorsFinished = 0;

    QDir::Filters filters = QDir::Files | QDir::Executable;
    for (auto info : testDir.entryInfoList(filters))
    {
        /*
         * Test executable built by cargo are named
         * <crate_name>-<hash>, where crate_name never includes dashes
         * but may include underscores.
         *
         * We thus try to split each executable named into the (crate_name, hash)
         * pair, then ignore the hash and use only the crate name as the
         * test suite name.
         */

        QStringList fileNameParts = info.fileName().split('-');
        if (fileNameParts.size() != 2)
        {
            continue;
        }
        QString suiteName = fileNameParts.first();
        QString executable = info.absoluteFilePath();

        auto exec = new KDevelop::CommandExecutor( executable, this );

        qCDebug(KDEV_CARGO) << "Finding tests in executable" << executable << "in dir" << builddir << ", suite" << suiteName;

        exec->setArguments(QStringList() << QStringLiteral("--list"));
        exec->setWorkingDirectory(builddir);

        connect(exec, &CommandExecutor::completed, this, [this, suiteName, executable](int code){
            procFinished(suiteName, executable, code);
        } );
        connect(exec, &CommandExecutor::failed, this, [this, suiteName](QProcess::ProcessError error){
            procError(suiteName, error);
        });

        connect(exec, &CommandExecutor::receivedStandardOutput, this, [this, suiteName](const QStringList& output) {
            addSuiteCases(suiteName, output);
        });

        exec->start();
        executors.append(exec);

        /*
         * We separately track the list of ignored test cases.
         * This is needed for the ability to run individual ignored test cases.
         */

        exec = new KDevelop::CommandExecutor( executable, this );
        exec->setArguments(QStringList() << QStringLiteral("--list") << QStringLiteral("--ignored"));
        exec->setWorkingDirectory(builddir);

        connect(exec, &CommandExecutor::completed, this, [this, suiteName, executable](int code){
            procFinished(suiteName, executable, code);
        } );
        connect(exec, &CommandExecutor::failed, this, [this, suiteName](QProcess::ProcessError error){
            procError(suiteName, error);
        });

        connect(exec, &CommandExecutor::receivedStandardOutput, this, [this, suiteName](const QStringList& output) {
            addIgnoredCases(suiteName, output);
        });

        exec->start();
        executors.append(exec);
    }
}

bool CargoFindTestsJob::doKill()
{
    killed = true;
    for (auto exec : executors)
    {
        exec->kill();
    }
    return true;
}

void CargoFindTestsJob::procFinished(const QString& suiteName, const QString& executable, int)
{
    if (killed)
    {
        return;
    }

    qCDebug(KDEV_CARGO) << "Proc finished" << suiteName;
    numExecutorsFinished++;

    if (suiteCases.contains(suiteName))
    {
        QStringList all = suiteCases[suiteName];
        QStringList ignored = ignoredCases.value(suiteName);

        CargoTestSuite* suite = new CargoTestSuite(suiteName, Path(executable), all, ignored, project);
        plugin->core()->testController()->addTestSuite(suite);
    }

    if (numExecutorsFinished == executors.size())
    {
        emitResult();
    }
}

void CargoFindTestsJob::procError(const QString& suiteName, QProcess::ProcessError err)
{
    if (killed)
    {
        return;
    }

    qCDebug(KDEV_CARGO) << "Proc error" << suiteName << err;
    numExecutorsFinished++;

    if (numExecutorsFinished == executors.size())
    {
        emitResult();
    }
}

void CargoFindTestsJob::addSuiteCases(const QString& suiteName, const QStringList& lines)
{
    qCDebug(KDEV_CARGO) << "Received lines for suite" << suiteName;

    if (!suiteCases.contains(suiteName))
    {
        suiteCases.insert(suiteName, QStringList());
    }

    for (const auto& line : lines)
    {
        QStringList elements = line.split(QStringLiteral(": "));
        if (elements.size() == 2 && elements[1] == QStringLiteral("test"))
        {
            qCDebug(KDEV_CARGO) << "Adding case" << elements[0] << "to suite" << suiteName;

            suiteCases[suiteName] << elements[0];
        }
    }
}

void CargoFindTestsJob::addIgnoredCases(const QString& suiteName, const QStringList& lines)
{
    qCDebug(KDEV_CARGO) << "Received ignored lines for suite" << suiteName;

    if (!ignoredCases.contains(suiteName))
    {
        ignoredCases.insert(suiteName, QStringList());
    }

    for (const auto& line : lines)
    {
        QStringList elements = line.split(QStringLiteral(": "));
        if (elements.size() == 2 && elements[1] == QStringLiteral("test"))
        {
            qCDebug(KDEV_CARGO) << "Adding ignored case" << elements[0] << "to suite" << suiteName;

            ignoredCases[suiteName] << elements[0];
        }
    }
}

#include "cargofindtestsjob.moc"
