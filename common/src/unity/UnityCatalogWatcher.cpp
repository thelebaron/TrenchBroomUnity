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

#include "UnityCatalogWatcher.h"

#include "PreferenceManager.h"
#include "io/PathQt.h"
#include "mdl/Game.h"
#include "mdl/Map.h"
#include "mdl/Map_Assets.h"
#include "unity/UnityCatalog.h"

#include <system_error>

namespace tb::unity
{
namespace
{

std::optional<std::filesystem::file_time_type> lastWriteTime(
  const std::filesystem::path& path)
{
  auto ec = std::error_code{};
  const auto time = std::filesystem::last_write_time(path, ec);
  return ec ? std::nullopt : std::optional{time};
}

} // namespace

UnityCatalogWatcher::UnityCatalogWatcher(mdl::Map& map)
  : m_map{map}
{
  // Unity writes several files per export; wait for it to finish.
  m_reloadTimer.setSingleShot(true);
  m_reloadTimer.setInterval(250);
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
    m_reloadTimer.start();
  });
  connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() {
    m_reloadTimer.start();
  });
  connect(&m_reloadTimer, &QTimer::timeout, this, &UnityCatalogWatcher::refresh);

  // A new or opened map may use a different game; the browser checks again.
  m_connections += map.mapWasCreatedNotifier.connect([this](auto&) {
    updateWatches();
    emit catalogChanged();
  });
  m_connections += map.mapWasLoadedNotifier.connect([this](auto&) {
    updateWatches();
    emit catalogChanged();
  });
  m_connections += PreferenceManager::instance().preferenceDidChangeNotifier.connect(
    [this](const auto& path) {
      if (isUnityEntityDefinitionPreference(path))
      {
        // The map reloads its entity definitions itself; only the browser needs telling.
        updateWatches();
        emit catalogChanged();
      }
    });
  updateWatches();
}

void UnityCatalogWatcher::updateWatches()
{
  if (const auto paths = m_watcher.files() + m_watcher.directories(); !paths.empty())
  {
    m_watcher.removePaths(paths);
  }

  const auto catalog = catalogPath();
  m_catalogWriteTime = lastWriteTime(catalog);
  if (catalog.empty())
  {
    return;
  }

  // Watch the catalog, and the nearest existing folder above it so an export into a new
  // or deleted folder is noticed.
  if (m_catalogWriteTime)
  {
    m_watcher.addPath(io::pathAsQString(catalog));
  }
  auto ec = std::error_code{};
  auto folder = catalog.parent_path();
  while (!std::filesystem::is_directory(folder, ec) && folder.has_parent_path()
         && folder.parent_path() != folder)
  {
    folder = folder.parent_path();
  }
  m_watcher.addPath(io::pathAsQString(folder));
}

void UnityCatalogWatcher::refresh()
{
  const auto writeTime = lastWriteTime(catalogPath());
  const auto changed = writeTime != m_catalogWriteTime;
  // Unity replaces the file, which drops the watch on it.
  updateWatches();
  if (!changed)
  {
    return;
  }

  if (m_map.world() && m_map.game() && usesUnityEntityDefinitions(m_map.game()->config()))
  {
    mdl::reloadEntityDefinitions(m_map);
  }
  emit catalogChanged();
}

} // namespace tb::unity
