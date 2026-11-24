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

#include "UnityAssetImport.h"

#include <QFile>

#include "Error.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "io/PathQt.h"
#include "unity/UnityTextureFolders.h"

#include <fmt/format.h>

#include <algorithm>

namespace tb::unity
{
namespace
{

std::filesystem::path projectPath()
{
  return pref(Preferences::UnityProjectPath);
}

std::filesystem::path metaFile(const std::filesystem::path& file)
{
  auto meta = file;
  meta += ".meta";
  return meta;
}

bool isWithin(const std::filesystem::path& path, const std::filesystem::path& folder)
{
  const auto relative =
    path.lexically_normal().lexically_relative(folder.lexically_normal());
  return !relative.empty() && *relative.begin() != "..";
}

} // namespace

std::filesystem::path decalFolder()
{
  const auto project = projectPath();
  if (project.empty())
  {
    return {};
  }
  const auto& folder = pref(Preferences::UnityDecalFolder);
  return (folder.is_absolute() ? folder : project / folder).lexically_normal();
}

std::vector<std::string> decalCategories(const std::filesystem::path& decalFolder)
{
  auto result = std::vector<std::string>{};
  auto ec = std::error_code{};
  for (const auto& entry : std::filesystem::directory_iterator{decalFolder, ec})
  {
    const auto name = entry.path().filename().string();
    // Unity hides folders starting with '.' and the generated atlases live in '_' folders
    if (entry.is_directory(ec) && !name.starts_with('.') && !name.starts_with('_'))
    {
      result.push_back(name);
    }
  }
  std::ranges::sort(result);
  return result;
}

bool isInUnityProject(const std::filesystem::path& path)
{
  const auto project = projectPath();
  return !project.empty() && isWithin(path, project);
}

std::filesystem::path unityAssetPath(const std::filesystem::path& path)
{
  return isInUnityProject(path)
           ? path.lexically_normal().lexically_relative(projectPath().lexically_normal())
           : path;
}

ImportPlan planImageImport(
  const std::vector<std::filesystem::path>& sources,
  const std::filesystem::path& targetFolder)
{
  auto plan = ImportPlan{};
  for (const auto& source : sources)
  {
    if (!isTextureImage(source))
    {
      plan.skipped.push_back(source);
      continue;
    }

    const auto target = targetFolder / source.filename();
    auto ec = std::error_code{};
    if (std::filesystem::equivalent(source, target, ec))
    {
      // already in place
      continue;
    }

    plan.copies.emplace_back(source, target);
    if (std::filesystem::exists(target, ec))
    {
      plan.conflicts.push_back(target);
    }
  }
  return plan;
}

Result<std::vector<std::filesystem::path>> copyImages(
  const ImportPlan& plan, const bool replace)
{
  auto written = std::vector<std::filesystem::path>{};
  for (const auto& [source, target] : plan.copies)
  {
    const auto conflicting =
      std::ranges::find(plan.conflicts, target) != plan.conflicts.end();
    if (conflicting && !replace)
    {
      continue;
    }

    auto ec = std::error_code{};
    std::filesystem::create_directories(target.parent_path(), ec);
    std::filesystem::copy_file(
      source, target, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
      return Error{fmt::format(
        "Could not copy '{}' to '{}': {}",
        source.string(),
        target.string(),
        ec.message())};
    }
    written.push_back(target);
  }
  return written;
}

bool isValidFolderName(const std::string& name)
{
  return !name.empty() && name.size() <= 64 && name.front() != ' ' && name.back() != ' '
         && name.front() != '.' && name.front() != '_'
         && std::ranges::all_of(name, [](const char c) {
              return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                     || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
            });
}

Result<std::filesystem::path> createSubfolder(
  const std::filesystem::path& parent, const std::string& name)
{
  if (!isValidFolderName(name))
  {
    return Error{fmt::format(
      "'{}' is not a valid folder name; use letters, digits, spaces, '-' and '_'", name)};
  }

  const auto folder = parent / name;
  auto ec = std::error_code{};
  std::filesystem::create_directories(folder, ec);
  if (ec)
  {
    return Error{
      fmt::format("Could not create folder '{}': {}", folder.string(), ec.message())};
  }
  return folder;
}

Result<void> removeUnityAsset(const std::filesystem::path& file)
{
  if (!isInUnityProject(file))
  {
    return Error{fmt::format("'{}' is not inside the Unity project", file.string())};
  }

  for (const auto& path : {file, metaFile(file)})
  {
    auto ec = std::error_code{};
    if (!std::filesystem::exists(path, ec))
    {
      continue;
    }
    if (!QFile::moveToTrash(io::pathAsQString(path)))
    {
      return Error{fmt::format("Could not move '{}' to the recycle bin", path.string())};
    }
  }
  return Result<void>{};
}

} // namespace tb::unity
