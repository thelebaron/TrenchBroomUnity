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

#include "io/ImageFileSystem.h"
#include "io/VirtualFileSystem.h"

#include <filesystem>
#include <string>
#include <vector>

namespace tb
{
class Logger;
}
namespace tb::mdl
{
struct GameConfig;
struct MaterialConfig;
} // namespace tb::mdl
namespace tb::unity
{
inline constexpr auto TextureFolderPath = "Unity::TextureFolderPath";
struct FolderTexture
{
  std::filesystem::path path;
  std::filesystem::path folder;
};
struct TextureFolderScan
{
  std::vector<FolderTexture> textures;
  std::vector<std::filesystem::path> directories;
  std::vector<std::string> errors;
  // Paths of textures whose names may not survive Quake WAD export.
  std::vector<std::string> warnings;
};
bool usesTextureFolders(const mdl::GameConfig& config);
bool isTextureImage(const std::filesystem::path& path);
bool isTextureFolderPreference(const std::filesystem::path& path);
// Always scanned first, relative to the Unity project path.
inline const auto DefaultTextureFolder = std::filesystem::path{"Assets/Content/Textures"};
// The default folder followed by the additional folders, resolved against the Unity
// project path.
std::vector<std::filesystem::path> textureFolders();
TextureFolderScan scanTextureFolders(const std::vector<std::filesystem::path>& folders);
mdl::MaterialConfig textureMaterialConfig(const mdl::GameConfig& config);
std::vector<io::VirtualMountPointId> mountTextureFolders(
  io::VirtualFileSystem& fs, const std::filesystem::path& root, Logger& logger);

class TextureFolderFileSystem : public io::ImageFileSystemBase
{
private:
  std::vector<FolderTexture> m_textures;

public:
  explicit TextureFolderFileSystem(std::vector<FolderTexture> textures);

private:
  Result<void> doReadDirectory() override;
};
} // namespace tb::unity
