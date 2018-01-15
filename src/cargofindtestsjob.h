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

#ifndef CARGOFINDTESTSJOB_H
#define CARGOFINDTESTSJOB_H

#include <outputview/outputjob.h>
#include <QProcess>
#include <QUrl>

class CargoPlugin;
namespace KDevelop
{
class ProjectBaseItem;
class CommandExecutor;
class OutputModel;
class IProject;
}



class CargoFindTestsJob : public KJob
{
Q_OBJECT
public:
    enum ErrorType {
        TargetsDirDoesNotExist = UserDefinedError,
    };

    CargoFindTestsJob(CargoPlugin*, KDevelop::ProjectBaseItem*);

    void start() override;
    bool doKill() override;

private slots:
    void procFinished(const QString& suiteName, const QString& executable, int);
    void procError(const QString& suiteName, QProcess::ProcessError);
private:
    void addSuiteCases(const QString& suiteName, const QStringList& lines);
    void addIgnoredCases(const QString& suiteName, const QStringList& lines);

    CargoPlugin* plugin;
    KDevelop::IProject* project;
    QString builddir;

    QList<KDevelop::CommandExecutor*> executors;
    int numExecutorsFinished;
    QHash<QString, QStringList> suiteCases;
    QHash<QString, QStringList> ignoredCases;

    bool killed;
    bool enabled;
};

#endif 
