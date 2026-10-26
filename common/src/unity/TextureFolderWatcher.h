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

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

#include "NotifierConnection.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace tb::mdl
{
class Map;
}
namespace tb::unity
{
struct WatchedImage
{
  std::filesystem::path path;
  std::filesystem::file_time_type lastWriteTime;
  std::uintmax_t size;

  bool operator==(const WatchedImage&) const = default;
};

class TextureFolderWatcher : public QObject
{
private:
  mdl::Map& m_map;
  QFileSystemWatcher m_watcher;
  QTimer m_reloadTimer;
  NotifierConnection m_connections;
  std::vector<WatchedImage> m_images;
  bool m_forceReload = false;

public:
  explicit TextureFolderWatcher(mdl::Map& map);

private:
  void updateWatches();
  void refresh();
};
} // namespace tb::unity