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

#include "UnityTextureFolders.h"

#include "Logger.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "io/DiskIO.h"
#include "io/File.h"
#include "mdl/GameConfig.h"

#include "kdl/string_format.h"
#include "kdl/string_utils.h"

#include <algorithm>
#include <map>
#include <set>
#include <system_error>

namespace tb::unity
{
namespace
{
const auto ImageExtensions = std::vector<std::filesystem::path>{
  ".png", ".tga", ".bmp", ".jpg", ".jpeg", ".tif", ".tiff"};
} // namespace

bool isTextureImage(const std::filesystem::path& path)
{
  const auto extension =
    std::filesystem::path{kdl::str_to_lower(path.extension().string())};
  return std::find(ImageExtensions.begin(), ImageExtensions.end(), extension)
         != ImageExtensions.end();
}

bool usesTextureFolders(const mdl::GameConfig& config)
{
  return config.name == "Quake" && pref(Preferences::UnityTextureFoldersEnabled);
}
bool isTextureFolderPreference(const std::filesystem::path& path)
{
  return path == Preferences::UnityTextureFoldersEnabled.path()
         || path == Preferences::UnityAdditionalTextureFolders.path()
         || path == Preferences::UnityProjectPath.path();
}
std::vector<std::filesystem::path> textureFolders()
{
  auto folders = std::vector<std::filesystem::path>{DefaultTextureFolder};
  const auto& additional = pref(Preferences::UnityAdditionalTextureFolders);
  folders.insert(folders.end(), additional.begin(), additional.end());
  const auto project = pref(Preferences::UnityProjectPath);
  for (auto& folder : folders)
  {
    if (folder.is_relative() && !project.empty())
    {
      folder = project / folder;
    }
    folder = folder.lexically_normal();
  }
  return folders;
}

TextureFolderScan scanTextureFolders(const std::vector<std::filesystem::path>& folders)
{
  auto scan = TextureFolderScan{};
  auto texturesByName = std::map<std::string, std::vector<FolderTexture>>{};
  auto seenFiles = std::set<std::filesystem::path>{};
  for (const auto& folder : folders)
  {
    auto ec = std::error_code{};
    if (folder.is_relative())
    {
      scan.errors.push_back(
        "Texture folder '" + folder.string()
        + "' is relative, but no Unity project path is set");
      continue;
    }
    if (!std::filesystem::is_directory(folder, ec))
    {
      scan.errors.push_back("Texture folder is unavailable: " + folder.string());
      continue;
    }
    scan.directories.push_back(folder);
    // Only images directly inside each folder; subfolders are added as their own folders.
    auto it = std::filesystem::directory_iterator{folder, ec};
    const auto end = std::filesystem::directory_iterator{};
    while (it != end && !ec)
    {
      const auto path = it->path();
      if (it->is_regular_file(ec) && isTextureImage(path))
      {
        auto canonical = std::filesystem::weakly_canonical(path, ec);
        if (!ec && seenFiles.insert(canonical).second)
        {
          texturesByName[kdl::str_to_lower(path.stem().string())].push_back(
            {path, folder});
        }
      }
      if (!ec)
      {
        it.increment(ec);
      }
    }
    if (ec)
    {
      scan.errors.push_back(
        "Could not scan texture folder '" + folder.string() + "': " + ec.message());
    }
  }
  for (auto& [name, textures] : texturesByName)
  {
    if (textures.size() > 1)
    {
      auto error = "Duplicate texture name '" + name + "':";
      for (const auto& texture : textures)
      {
        error += "\n  " + texture.path.string();
      }
      scan.errors.push_back(std::move(error));
      continue;
    }
    const auto& texture = textures.front();
    // Case is ignored: Scopa and Quake tools both match texture names case-insensitively.
    const auto prefix =
      name.starts_with("*") || name.starts_with("sky")
      || (name.size() >= 2 && name[0] == '+' && name[1] >= '0' && name[1] <= '9');
    if (name.size() > 15 || prefix)
    {
      scan.warnings.push_back(texture.path.string());
    }
    scan.textures.push_back(texture);
  }
  std::sort(scan.directories.begin(), scan.directories.end());
  scan.directories.erase(
    std::unique(scan.directories.begin(), scan.directories.end()),
    scan.directories.end());
  return scan;
}

mdl::MaterialConfig textureMaterialConfig(const mdl::GameConfig& config)
{
  auto result = config.materialConfig;
  if (usesTextureFolders(config))
  {
    result.extensions = ImageExtensions;
    result.palette.clear();
  }
  return result;
}

TextureFolderFileSystem::TextureFolderFileSystem(std::vector<FolderTexture> textures)
  : m_textures{std::move(textures)}
{
}

Result<void> TextureFolderFileSystem::doReadDirectory()
{
  for (const auto& texture : m_textures)
  {
    addFile(
      texture.path.filename(),
      [path = texture.path]() -> Result<std::shared_ptr<io::File>> {
        return io::Disk::openFile(path) | kdl::transform([](auto file) {
                 return std::static_pointer_cast<io::File>(file);
               });
      });
  }
  return kdl::void_success;
}

std::vector<io::VirtualMountPointId> mountTextureFolders(
  io::VirtualFileSystem& fs, const std::filesystem::path& root, Logger& logger)
{
  const auto folders = textureFolders();
  const auto scan = scanTextureFolders(folders);
  for (const auto& error : scan.errors)
  {
    logger.error() << error;
  }
  if (!scan.warnings.empty())
  {
    logger.warn() << scan.warnings.size()
                  << " texture names may not survive Quake WAD export (longer than 15 "
                     "characters, or a reserved prefix such as *, +0 or sky)";
    for (const auto& warning : scan.warnings)
    {
      logger.debug() << "  " << warning;
    }
  }
  auto mounts = std::vector<io::VirtualMountPointId>{};
  for (const auto& folder : folders)
  {
    auto textures = std::vector<FolderTexture>{};
    for (const auto& texture : scan.textures)
    {
      if (texture.folder == folder)
      {
        textures.push_back(texture);
      }
    }
    if (textures.empty())
    {
      continue;
    }
    io::createImageFileSystem<TextureFolderFileSystem>(std::move(textures))
      | kdl::transform([&](auto folderFs) {
          folderFs->setMetadata({{TextureFolderPath, folder}});
          mounts.push_back(fs.mount(root, std::move(folderFs)));
        })
      | kdl::transform_error([&](auto error) { logger.error() << error.msg; });
  }
  return mounts;
}
} // namespace tb::unity
