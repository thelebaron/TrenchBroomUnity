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

#include "UnityAssetImportDialogs.h"

#include <QDir>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QUrl>

#include "Error.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "io/PathQt.h"
#include "unity/UnityAssetImport.h"
#include "unity/UnityTextureFolders.h"

#include <algorithm>
#include <optional>

namespace tb::unity
{
namespace
{

QString displayPath(const std::filesystem::path& path)
{
  return QDir::fromNativeSeparators(io::pathAsQString(unityAssetPath(path)));
}

/**
 * Asks which of the folders to use, or for the name of a new one. Returns nullopt if the
 * user cancels.
 */
std::optional<std::filesystem::path> chooseFolder(
  QWidget* parent,
  const QString& title,
  const QString& label,
  const std::vector<std::filesystem::path>& folders,
  const std::filesystem::path& newFolderParent,
  const QString& newFolderText,
  const std::filesystem::path& preferred)
{
  auto items = QStringList{};
  for (const auto& folder : folders)
  {
    items.push_back(displayPath(folder));
  }
  items.push_back(newFolderText);

  const auto current = std::ranges::find(folders, preferred);
  const auto currentIndex =
    current != folders.end() ? int(std::distance(folders.begin(), current)) : 0;

  auto ok = false;
  const auto choice =
    QInputDialog::getItem(parent, title, label, items, currentIndex, false, &ok);
  if (!ok)
  {
    return std::nullopt;
  }

  const auto index = items.indexOf(choice);
  if (index >= 0 && index < int(folders.size()))
  {
    return folders[size_t(index)];
  }

  const auto name = QInputDialog::getText(
                      parent,
                      title,
                      QObject::tr("Name (letters, digits, spaces, '-' and '_'):"),
                      QLineEdit::Normal,
                      {},
                      &ok)
                      .trimmed();
  if (!ok || name.isEmpty())
  {
    return std::nullopt;
  }

  const auto folder = createSubfolder(newFolderParent, name.toStdString());
  if (folder.is_error())
  {
    QMessageBox::warning(
      parent, title, QString::fromStdString(std::get<Error>(folder.error()).msg));
    return std::nullopt;
  }
  return folder.value();
}

/**
 * Copies the images into the folder, asking before replacing files. Returns whether
 * anything was copied.
 */
bool copyInteractively(
  QWidget* parent,
  const QString& title,
  const std::vector<std::filesystem::path>& files,
  const std::filesystem::path& folder)
{
  const auto plan = planImageImport(files, folder);

  auto replace = false;
  if (!plan.conflicts.empty())
  {
    auto names = QStringList{};
    for (const auto& conflict : plan.conflicts)
    {
      names.push_back(QString::fromStdString(conflict.filename().string()));
    }
    const auto answer = QMessageBox::question(
      parent,
      title,
      QObject::tr("%1 already exist in %2. Replace them?")
        .arg(names.join(", "))
        .arg(displayPath(folder)),
      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
      QMessageBox::No);
    if (answer == QMessageBox::Cancel)
    {
      return false;
    }
    replace = answer == QMessageBox::Yes;
  }

  const auto written = copyImages(plan, replace);
  if (written.is_error())
  {
    QMessageBox::warning(
      parent, title, QString::fromStdString(std::get<Error>(written.error()).msg));
    return false;
  }

  if (!plan.skipped.empty())
  {
    auto names = QStringList{};
    for (const auto& skipped : plan.skipped)
    {
      names.push_back(QString::fromStdString(skipped.filename().string()));
    }
    QMessageBox::information(
      parent,
      title,
      QObject::tr("These files are not images TrenchBroom can use and were left out: %1")
        .arg(names.join(", ")));
  }
  return !written.value().empty();
}

bool requireUnityProject(QWidget* parent, const QString& title)
{
  if (pref(Preferences::UnityProjectPath).empty())
  {
    QMessageBox::information(
      parent,
      title,
      QObject::tr("Set the Unity project directory in the Unity preferences first."));
    return false;
  }
  return true;
}

} // namespace

std::vector<std::filesystem::path> droppedImageFiles(const QMimeData& mimeData)
{
  auto result = std::vector<std::filesystem::path>{};
  for (const auto& url : mimeData.urls())
  {
    if (url.isLocalFile())
    {
      const auto path = io::pathFromQString(url.toLocalFile());
      if (isTextureImage(path))
      {
        result.push_back(path);
      }
    }
  }
  return result;
}

std::vector<std::filesystem::path> chooseImageFiles(QWidget* parent, const QString& title)
{
  const auto files = QFileDialog::getOpenFileNames(
    parent,
    title,
    {},
    QObject::tr("Images (*.png *.tga *.jpg *.jpeg *.bmp *.tif *.tiff);;All Files (*)"));

  auto result = std::vector<std::filesystem::path>{};
  for (const auto& file : files)
  {
    result.push_back(io::pathFromQString(file));
  }
  return result;
}

void importTexturesInteractively(
  QWidget* parent, const std::vector<std::filesystem::path>& files)
{
  const auto title = QObject::tr("Import Textures into Unity");
  if (files.empty() || !requireUnityProject(parent, title))
  {
    return;
  }

  const auto folders = textureFolders();
  const auto defaultFolder = folders.empty() ? std::filesystem::path{} : folders.front();
  const auto folder = chooseFolder(
    parent,
    title,
    QObject::tr("Copy %1 image(s) into texture folder:").arg(files.size()),
    folders,
    defaultFolder,
    QObject::tr("New folder in %1...").arg(displayPath(defaultFolder)),
    defaultFolder);
  if (!folder)
  {
    return;
  }

  // a new folder is only scanned once it is in the preferences
  if (std::ranges::find(folders, *folder) == folders.end())
  {
    auto additional = pref(Preferences::UnityAdditionalTextureFolders);
    additional.push_back(unityAssetPath(*folder));
    PreferenceManager::instance().set(
      Preferences::UnityAdditionalTextureFolders, additional);
  }

  copyInteractively(parent, title, files, *folder);
}

void importDecalsInteractively(
  QWidget* parent,
  const std::vector<std::filesystem::path>& files,
  const std::string& preferredCategory)
{
  const auto title = QObject::tr("Import Decals into Unity");
  if (files.empty() || !requireUnityProject(parent, title))
  {
    return;
  }

  const auto root = decalFolder();
  auto folders = std::vector<std::filesystem::path>{};
  for (const auto& category : decalCategories(root))
  {
    folders.push_back(root / category);
  }

  const auto folder = chooseFolder(
    parent,
    title,
    QObject::tr("Copy %1 image(s) into decal category:").arg(files.size()),
    folders,
    root,
    QObject::tr("New category..."),
    root / preferredCategory);
  if (!folder)
  {
    return;
  }

  if (copyInteractively(parent, title, files, *folder))
  {
    QMessageBox::information(
      parent,
      title,
      QObject::tr(
        "Copied into %1. Unity adds the decals to the category's atlas when it next "
        "refreshes (switch to Unity), then they appear in the Decals browser.")
        .arg(displayPath(*folder)));
  }
}

bool removeAssetInteractively(
  QWidget* parent, const std::filesystem::path& file, const QString& kind)
{
  const auto title = QObject::tr("Remove %1").arg(kind);
  const auto answer = QMessageBox::question(
    parent,
    title,
    QObject::tr(
      "Move %1 and its Unity .meta file to the recycle bin?\n\n%2\n\nMaps that use it "
      "will show it as missing until it is restored from the recycle bin.")
      .arg(kind.toLower())
      .arg(displayPath(file)),
    QMessageBox::Yes | QMessageBox::Cancel,
    QMessageBox::Cancel);
  if (answer != QMessageBox::Yes)
  {
    return false;
  }

  const auto result = removeUnityAsset(file);
  if (result.is_error())
  {
    QMessageBox::warning(
      parent, title, QString::fromStdString(std::get<Error>(result.error()).msg));
    return false;
  }
  return true;
}

} // namespace tb::unity
