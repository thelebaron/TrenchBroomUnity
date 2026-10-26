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

#include <algorithm>
#include <system_error>

namespace tb::unity
{
namespace
{
std::vector<WatchedImage> listImages(const std::vector<std::filesystem::path>& folders)
{
  auto images = std::vector<WatchedImage>{};
  for (const auto& folder : folders)
  {
    if (folder.is_relative())
    {
      continue;
    }
    auto ec = std::error_code{};
    for (auto it = std::filesystem::directory_iterator{folder, ec};
         !ec && it != std::filesystem::directory_iterator{};
         it.increment(ec))
    {
      if (it->is_regular_file(ec) && isTextureImage(it->path()))
      {
        images.push_back(
          {it->path(), it->last_write_time(ec), it->file_size(ec)});
      }
    }
  }
  std::sort(images.begin(), images.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.path < rhs.path;
  });
  return images;
}
} // namespace

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
        m_forceReload = true;
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
  m_images.clear();
  if (!m_map.world() || !m_map.game() || !usesTextureFolders(m_map.game()->config()))
  {
    return;
  }
  const auto folders = textureFolders();
  // Remember what the loaded materials were built from, so changes that don't touch
  // an image (for example Unity writing .meta files) don't trigger a reload.
  m_images = listImages(folders);
  auto paths = QStringList{};
  // Watch all source images, including ambiguous ones, so deleting a duplicate repairs
  // it.
  for (const auto& image : m_images)
  {
    paths.push_back(io::pathAsQString(image.path));
  }
  // Watch each folder, or its nearest existing parent so creating it is noticed.
  for (auto folder : folders)
  {
    if (folder.is_relative())
    {
      continue;
    }
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
  if (
    m_map.world()
    && (m_forceReload || listImages(textureFolders()) != m_images))
  {
    m_forceReload = false;
    mdl::reloadMaterialCollections(m_map);
  }
  else
  {
    // Files replaced by saving tools drop their watch, so add them again.
    updateWatches();
  }
}
} // namespace tb::unity
