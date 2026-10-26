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

#include "TextureFolderWatcher.h"

#include "PreferenceManager.h"
#include "io/PathQt.h"
#include "mdl/Game.h"
#include "mdl/Map.h"
#include "mdl/Map_Assets.h"
#include "unity/UnityTextureFolders.h"

#include <system_error>

namespace tb::unity
{
TextureFolderWatcher::TextureFolderWatcher(mdl::Map& map)
  : m_map{map}
{
  m_reloadTimer.setSingleShot(true);
  m_reloadTimer.setInterval(250);
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
    m_reloadTimer.start();
  });
  connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() {
    m_reloadTimer.start();
  });
  connect(&m_reloadTimer, &QTimer::timeout, this, &TextureFolderWatcher::refresh);
  m_connections += map.mapWasCreatedNotifier.connect([this](auto&) { updateWatches(); });
  m_connections += map.mapWasLoadedNotifier.connect([this](auto&) { updateWatches(); });
  m_connections += map.mapWasClearedNotifier.connect([this](auto&) { updateWatches(); });
  m_connections +=
    map.materialCollectionsDidChangeNotifier.connect([this]() { updateWatches(); });
  m_connections += PreferenceManager::instance().preferenceDidChangeNotifier.connect(
    [this](const auto& path) {
      if (isTextureFolderPreference(path))
      {
        m_reloadTimer.start();
      }
    });
  updateWatches();
}
void TextureFolderWatcher::updateWatches()
{
  m_reloadTimer.stop();
  const auto files = m_watcher.files();
  const auto directories = m_watcher.directories();
  if (!files.empty())
  {
    m_watcher.removePaths(files);
  }
  if (!directories.empty())
  {
    m_watcher.removePaths(directories);
  }
  if (!m_map.world() || !m_map.game() || !usesTextureFolders(m_map.game()->config()))
  {
    return;
  }
  const auto folders = textureFolders();
  const auto scan = scanTextureFolders(folders);
  auto paths = QStringList{};
  for (const auto& directory : scan.directories)
  {
    paths.push_back(io::pathAsQString(directory));
  }
  // Watch all source images, including ambiguous ones, so deleting a duplicate repairs
  // it.
  for (const auto& directory : scan.directories)
  {
    auto ec = std::error_code{};
    for (auto it = std::filesystem::directory_iterator{directory, ec};
         !ec && it != std::filesystem::directory_iterator{};
         it.increment(ec))
    {
      if (it->is_regular_file(ec) && isTextureImage(it->path()))
      {
        paths.push_back(io::pathAsQString(it->path()));
      }
    }
  }
  for (auto folder : folders)
  {
    auto ec = std::error_code{};
    while (!folder.empty() && !std::filesystem::is_directory(folder, ec))
    {
      const auto parent = folder.parent_path();
      if (parent == folder)
      {
        break;
      }
      folder = parent;
    }
    if (!folder.empty())
    {
      paths.push_back(io::pathAsQString(folder));
    }
  }
  paths.removeDuplicates();
  if (!paths.empty())
  {
    m_watcher.addPaths(paths);
  }
}
void TextureFolderWatcher::refresh()
{
  if (m_map.world())
  {
    mdl::reloadMaterialCollections(m_map);
  }
  else
  {
    updateWatches();
  }
}
} // namespace tb::unity
