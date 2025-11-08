/*
 Copyright (C) 2025 Kristian Duske

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

#include "Notifier.h"
#include "NotifierConnection.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace tb::mdl
{
class Map;
}

namespace tb::ui
{
class MapDocument;

struct PrefabEntry
{
  std::filesystem::path path;
  std::string name;
  std::chrono::system_clock::time_point lastModified;
};

class PrefabLibrary
{
private:
  MapDocument& m_document;
  std::filesystem::path m_prefabRoot;
  std::vector<PrefabEntry> m_entries;
  bool m_enabled = false;
  NotifierConnection m_notifierConnection;

public:
  Notifier<const std::vector<PrefabEntry>&> entriesDidChangeNotifier;
  Notifier<bool> availabilityDidChangeNotifier;

  explicit PrefabLibrary(MapDocument& document);
  ~PrefabLibrary();

  bool enabled() const;
  const std::filesystem::path& prefabRoot() const;
  const std::vector<PrefabEntry>& entries() const;

  void refresh();
  std::filesystem::path thumbnailRoot() const;
  std::filesystem::path prefabPathForName(const std::string& name) const;

private:
  void connectObservers();
  void mapStateChanged(mdl::Map&);
  void mapWasCleared(mdl::Map&);

  void updatePrefabRoot();
  void rebuild();

  std::filesystem::path defaultPrefabRoot() const;

  static std::string displayNameForPath(const std::filesystem::path& path);
  static std::chrono::system_clock::time_point convertFileTime(
    const std::filesystem::file_time_type& time);
};

} // namespace tb::ui
