/*
 Copyright (C) 2026 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "UnityIntegration.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringList>

#include "Logger.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "io/PathQt.h"
#include "ui/MapFrame.h"

#include <system_error>

namespace tb::ui::unity
{

QString findUnityExecutable()
{
  auto exe = QStandardPaths::findExecutable("unity");
  if (!exe.isEmpty())
  {
    return exe;
  }

#ifdef _WIN32
  exe = QStandardPaths::findExecutable("unity.exe");
  if (!exe.isEmpty())
  {
    return exe;
  }

  const auto localAppData =
    QProcessEnvironment::systemEnvironment().value("LOCALAPPDATA");
  if (!localAppData.isEmpty())
  {
    const auto defaultCli = QDir{localAppData}.filePath("Unity/bin/unity.exe");
    if (QFileInfo::exists(defaultCli))
    {
      return defaultCli;
    }
  }
#endif

  return "unity";
}

bool isPathInsideProject(
  const std::filesystem::path& filePath, const std::filesystem::path& unityProjectPath)
{
  if (filePath.empty() || unityProjectPath.empty())
  {
    return false;
  }

  try
  {
    std::error_code ec;
    auto normFile = std::filesystem::weakly_canonical(filePath, ec);
    if (ec)
    {
      normFile = filePath.lexically_normal();
    }
    auto normProj = std::filesystem::weakly_canonical(unityProjectPath, ec);
    if (ec)
    {
      normProj = unityProjectPath.lexically_normal();
    }

    const auto rel = normFile.lexically_relative(normProj);
    if (rel.empty() || rel.is_absolute() || rel == ".")
    {
      return false;
    }

    const auto firstElem = rel.begin()->string();
    return firstElem != "..";
  }
  catch (...)
  {
    return false;
  }
}

QString toUnityAssetPath(
  const std::filesystem::path& filePath, const std::filesystem::path& unityProjectPath)
{
  if (!isPathInsideProject(filePath, unityProjectPath))
  {
    return {};
  }

  std::error_code ec;
  auto normFile = std::filesystem::weakly_canonical(filePath, ec);
  if (ec)
  {
    normFile = filePath.lexically_normal();
  }
  auto normProj = std::filesystem::weakly_canonical(unityProjectPath, ec);
  if (ec)
  {
    normProj = unityProjectPath.lexically_normal();
  }

  const auto rel = normFile.lexically_relative(normProj);
  return QString::fromStdString(rel.generic_string());
}

void testUnityConnection(
  const std::filesystem::path& projectPath,
  std::function<void(bool success, const QString& message)> callback)
{
  if (projectPath.empty())
  {
    callback(false, QObject::tr("Unity project directory is not set."));
    return;
  }

  const auto unityExe = findUnityExecutable();
  auto* process = new QProcess{};
  QStringList args;
  args << "status"
       << "--project" << io::pathAsQString(projectPath) << "--json";

  QObject::connect(
    process,
    QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
    [process, callback](int /*exitCode*/, QProcess::ExitStatus exitStatus) {
      const auto output = process->readAllStandardOutput();
      const auto errOutput = process->readAllStandardError();
      process->deleteLater();

      if (exitStatus != QProcess::NormalExit)
      {
        callback(false, QObject::tr("Unity CLI terminated abnormally."));
        return;
      }

      QJsonParseError parseError;
      const auto doc = QJsonDocument::fromJson(output, &parseError);
      if (doc.isNull() || !doc.isObject())
      {
        const auto rawMsg =
          QString::fromUtf8(output.isEmpty() ? errOutput : output).trimmed();
        callback(
          false,
          rawMsg.isEmpty() ? QObject::tr("Failed to read status from Unity CLI.")
                           : rawMsg);
        return;
      }

      const auto root = doc.object();
      const auto success = root.value("success").toBool();
      const auto dataObj = root.value("data").toObject();
      const auto instances = dataObj.value("instances").toArray();

      if (!success || instances.isEmpty())
      {
        const auto errors = root.value("errors").toArray();
        QString msg;
        if (!errors.isEmpty())
        {
          msg = errors.first().toObject().value("message").toString();
        }
        if (msg.isEmpty())
        {
          msg = QObject::tr("No running Unity Editor instance found for this project.");
        }
        callback(false, msg);
        return;
      }

      const auto instance = instances.first().toObject();
      const auto version = instance.value("version").toString();
      const auto pid = instance.value("pid").toInteger();
      const auto port = instance.value("port").toInteger();
      const auto state = instance.value("state").toString();

      callback(
        true,
        QObject::tr("Connected: Unity %1 (PID %2, port %3, state: %4)")
          .arg(version, QString::number(pid), QString::number(port), state));
    });

  QObject::connect(
    process,
    &QProcess::errorOccurred,
    [process, callback](QProcess::ProcessError /*error*/) {
      const auto errStr = process->errorString();
      process->deleteLater();
      callback(false, QObject::tr("Could not run Unity CLI: %1").arg(errStr));
    });

  process->start(unityExe, args);
}

void triggerUnityReimport(const std::filesystem::path& mapPath, MapFrame* frame)
{
  if (!pref(Preferences::UnityReimportOnSave))
  {
    return;
  }

  const auto projectPath = pref(Preferences::UnityProjectPath);
  if (projectPath.empty())
  {
    return;
  }

  QPointer<MapFrame> framePtr{frame};

  if (!isPathInsideProject(mapPath, projectPath))
  {
    if (framePtr)
    {
      framePtr->logger().info()
        << "Map " << mapPath.string()
        << " is outside configured Unity project path; skipping Unity reimport.";
    }
    return;
  }

  const auto relativeAssetPath = toUnityAssetPath(mapPath, projectPath);
  if (
    !relativeAssetPath.startsWith("Assets/")
    && !relativeAssetPath.startsWith("Packages/"))
  {
    if (framePtr)
    {
      framePtr->logger().warn()
        << "Map " << mapPath.string()
        << " is not located under Assets/ or Packages/; skipping Unity reimport.";
    }
    return;
  }

  const auto unityExe = findUnityExecutable();
  auto* process = new QProcess{};
  QStringList args;
  args << "command"
       << "--project-path" << io::pathAsQString(projectPath) << "eval"
       << "--code"
       << QString(
            "UnityEditor.AssetDatabase.ImportAsset(\"%1\", "
            "UnityEditor.ImportAssetOptions.ForceUpdate);")
            .arg(relativeAssetPath);

  if (framePtr)
  {
    framePtr->logger().info() << "Triggering Unity reimport for "
                              << relativeAssetPath.toStdString() << "...";
  }

  QObject::connect(
    process,
    QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
    [process, mapPath, relativeAssetPath, framePtr](
      int exitCode, QProcess::ExitStatus exitStatus) {
      const auto stdoutData =
        QString::fromUtf8(process->readAllStandardOutput()).trimmed();
      const auto stderrData =
        QString::fromUtf8(process->readAllStandardError()).trimmed();
      process->deleteLater();

      if (!framePtr)
      {
        return;
      }

      if (exitStatus == QProcess::NormalExit && exitCode == 0)
      {
        framePtr->logger().info()
          << "Unity reimport finished for " << relativeAssetPath.toStdString();

        if (pref(Preferences::UnityRebakeSubsceneOnSave))
        {
          triggerUnitySubsceneRebake(mapPath, framePtr.data());
        }
      }
      else
      {
        framePtr->logger().warn()
          << "Unity reimport failed for " << relativeAssetPath.toStdString()
          << " (exit code " << exitCode << "): "
          << (stderrData.isEmpty() ? stdoutData.toStdString() : stderrData.toStdString());
      }
    });

  QObject::connect(
    process,
    &QProcess::errorOccurred,
    [process, relativeAssetPath, framePtr](QProcess::ProcessError /*error*/) {
      const auto errStr = process->errorString();
      process->deleteLater();
      if (framePtr)
      {
        framePtr->logger().warn()
          << "Failed to launch Unity CLI for " << relativeAssetPath.toStdString() << ": "
          << errStr.toStdString();
      }
    });

  process->start(unityExe, args);
}

void triggerUnitySubsceneRebake(const std::filesystem::path& mapPath, MapFrame* frame)
{
  const auto projectPath = pref(Preferences::UnityProjectPath);
  if (projectPath.empty())
  {
    return;
  }

  QPointer<MapFrame> framePtr{frame};

  const auto mapStem = QString::fromStdString(mapPath.stem().string());

  const auto unityExe = findUnityExecutable();
  auto* process = new QProcess{};
  QStringList args;
  args << "command"
       << "--project-path" << io::pathAsQString(projectPath) << "rebake_subscene";
  if (!mapStem.isEmpty())
  {
    args << "--map" << mapStem;
  }

  if (framePtr)
  {
    framePtr->logger().info() << "Triggering Unity SubScene rebake for map '"
                              << mapStem.toStdString() << "'...";
  }

  QObject::connect(
    process,
    QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
    [process, mapStem, framePtr](int exitCode, QProcess::ExitStatus exitStatus) {
      const auto stdoutData =
        QString::fromUtf8(process->readAllStandardOutput()).trimmed();
      const auto stderrData =
        QString::fromUtf8(process->readAllStandardError()).trimmed();
      process->deleteLater();

      if (!framePtr)
      {
        return;
      }

      if (exitStatus == QProcess::NormalExit && exitCode == 0)
      {
        framePtr->logger().info()
          << "Unity SubScene rebake finished: " << stdoutData.toStdString();
      }
      else
      {
        framePtr->logger().warn()
          << "Unity SubScene rebake failed (exit code " << exitCode << "): "
          << (stderrData.isEmpty() ? stdoutData.toStdString() : stderrData.toStdString());
      }
    });

  QObject::connect(
    process,
    &QProcess::errorOccurred,
    [process, mapStem, framePtr](QProcess::ProcessError /*error*/) {
      const auto errStr = process->errorString();
      process->deleteLater();
      if (framePtr)
      {
        framePtr->logger().warn()
          << "Failed to launch Unity CLI for SubScene rebake ('" << mapStem.toStdString()
          << "'): " << errStr.toStdString();
      }
    });

  process->start(unityExe, args);
}

} // namespace tb::ui::unity
