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
#include <outputview/outputmodel.h>
#include <outputview/outputdelegate.h>
#include <outputview/filtereditem.h>
#include <outputview/outputfilteringstrategies.h>
#include <util/commandexecutor.h>
#include <project/projectmodel.h>
#include <language/duchain/indexeddeclaration.h>

#include "cargoplugin.h"
#include "debug.h"

using namespace KDevelop;

class CargoTestSuite : public KDevelop::ITestSuite
{
public:
    CargoTestSuite(const QString& suiteName, const Path& executable, const QStringList& cases, IProject* project);
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

    KJob * launchCase(const QString & testCase, KDevelop::ITestSuite::TestJobVerbosity verbosity) override;
    KJob * launchAllCases(KDevelop::ITestSuite::TestJobVerbosity verbosity) override;
    KJob * launchCases(const QStringList & testCases, KDevelop::ITestSuite::TestJobVerbosity verbosity) override;

private:
    QString m_suiteName;
    Path m_executable;
    QStringList m_cases;
    IProject* m_project;
};

CargoTestSuite::CargoTestSuite(const QString& suiteName, const Path& executable, const QStringList& cases, IProject* project)
: m_suiteName(suiteName)
, m_executable(executable)
, m_cases(cases)
, m_project(project)
{}

CargoTestSuite::~CargoTestSuite()
{}

class CargoRunTestsJob : public KDevelop::OutputJob
{
    Q_OBJECT
public:
    CargoRunTestsJob(CargoTestSuite* suite, const QString& caseName, KDevelop::ITestSuite::TestJobVerbosity verbosity)
    : killed(false)
    , suite(suite)
    , caseName(caseName)
    , verbosity(verbosity)
    {
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
        connect( exec, &CommandExecutor::receivedStandardOutput, model, &OutputModel::appendLines );

        model->appendLine( QStringLiteral("Test %1 %2").arg( suite->name() ).arg( caseName ) );
        exec->start();
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
};


void CargoRunTestsJob::procError( QProcess::ProcessError err )
{
    Q_UNUSED(err);
    if( !killed ) {
        setError( FailedShownError );
        setErrorText( i18n( "Error running test command." ) );
    }

    TestResult result;
    result.suiteResult = TestResult::Error;
    ICore::self()->testController()->notifyTestRunFinished(suite, result);

    emitResult();
}

void CargoRunTestsJob::procFinished(int code)
{
    TestResult result;

    if( code != 0 ) {
        setError( FailedShownError );
        result.suiteResult = TestResult::Failed;
        if (!caseName.isEmpty())
        {
            result.testCaseResults.insert(caseName, TestResult::Failed);
        }
    } else {
        result.suiteResult = TestResult::Passed;
        if (!caseName.isEmpty())
        {
            result.testCaseResults.insert(caseName, TestResult::Passed);
        }
        else
        {
            for (const auto& caseName : suite->cases())
            {
                result.testCaseResults.insert(caseName, TestResult::Passed);
            }
        }
    }

    ICore::self()->testController()->notifyTestRunFinished(suite, result);

    emitResult();
}

KJob* CargoTestSuite::launchCases(const QStringList & testCases, KDevelop::ITestSuite::TestJobVerbosity verbosity)
{
    return nullptr;
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

        connect(exec, &CommandExecutor::completed, [this, suiteName, executable](int code){
            procFinished(suiteName, executable, code);
        } );
        connect(exec, &CommandExecutor::failed, [this, suiteName](QProcess::ProcessError error){
            procError(suiteName, error);
        });

        connect(exec, &CommandExecutor::receivedStandardOutput, [this, suiteName](const QStringList& output) {
            addSuiteCases(suiteName, output);
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
    qCDebug(KDEV_CARGO) << "Proc finished" << suiteName;
    numExecutorsFinished++;

    if (suiteCases.contains(suiteName))
    {
        CargoTestSuite* suite = new CargoTestSuite(suiteName, Path(executable), suiteCases[suiteName], project);
        plugin->core()->testController()->addTestSuite(suite);
    }

    if (numExecutorsFinished == executors.size())
    {
        emitResult();
    }
}

void CargoFindTestsJob::procError(const QString& suiteName, QProcess::ProcessError err)
{
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

#include "cargofindtestsjob.moc"
