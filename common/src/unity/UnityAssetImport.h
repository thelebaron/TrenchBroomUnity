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

#pragma once

#include "Result.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace tb::unity
{

/**
 * Copying images into the Unity project and removing them again, so textures and decals
 * can be managed from TrenchBroom. Unity imports new files when it next refreshes; the
 * texture folders and the decal catalog then pick them up.
 */

/**
 * The folder decal images are imported into, one subfolder per category, resolved
 * against the Unity project path. Empty without a Unity project.
 */
std::filesystem::path decalFolder();

/**
 * The decal categories: the subfolders of the decal folder, sorted.
 */
std::vector<std::string> decalCategories(const std::filesystem::path& decalFolder);

/**
 * Whether the path is inside the Unity project.
 */
bool isInUnityProject(const std::filesystem::path& path);

/**
 * A path inside the Unity project as Unity names assets ("Assets/..."), or the path
 * itself if it isn't inside the project.
 */
std::filesystem::path unityAssetPath(const std::filesystem::path& path);

/**
 * Which files an import copies where, and which of them would replace existing files.
 */
struct ImportPlan
{
  std::vector<std::pair<std::filesystem::path, std::filesystem::path>> copies;
  std::vector<std::filesystem::path> conflicts;
  // dropped files that aren't images, which are left out
  std::vector<std::filesystem::path> skipped;
};

ImportPlan planImageImport(
  const std::vector<std::filesystem::path>& sources,
  const std::filesystem::path& targetFolder);

/**
 * Copies the plan's files, replacing existing ones if replace is set and skipping them
 * otherwise. Returns the files written.
 */
Result<std::vector<std::filesystem::path>> copyImages(
  const ImportPlan& plan, bool replace);

/**
 * A folder name for a new category or texture folder: letters, digits, spaces, '-' and
 * '_' only, so Unity, Quake tools and every file system accept it.
 */
bool isValidFolderName(const std::string& name);

Result<std::filesystem::path> createSubfolder(
  const std::filesystem::path& parent, const std::string& name);

/**
 * Moves the file and its Unity .meta file to the recycle bin, so a removal can be undone
 * from there.
 */
Result<void> removeUnityAsset(const std::filesystem::path& file);

} // namespace tb::unity
