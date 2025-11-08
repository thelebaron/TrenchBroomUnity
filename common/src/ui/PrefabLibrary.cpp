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

#include "ui/PrefabLibrary.h"

#include "ui/MapDocument.h"

#include "mdl/Map.h"

#include "kdl/string_compare.h"
#include "kdl/string_format.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <ranges>
#include <system_error>
#include <utility>

namespace tb::ui
{
namespace
{
constexpr auto PrefabFolderName = std::string_view{"prefabs"};

bool hasMapExtension(const std::filesystem::path& path)
{
  const auto extension = path.extension().string();
  return kdl::ci::str_is_equal(extension, ".map");
}

} // namespace

PrefabLibrary::PrefabLibrary(MapDocument& document)
  : m_document{document}
{
  connectObservers();
  refresh();
}

PrefabLibrary::~PrefabLibrary() = default;

bool PrefabLibrary::enabled() const
{
  return m_enabled;
}

const std::filesystem::path& PrefabLibrary::prefabRoot() const
{
  return m_prefabRoot;
}

const std::vector<PrefabEntry>& PrefabLibrary::entries() const
{
  return m_entries;
}

void PrefabLibrary::refresh()
{
  updatePrefabRoot();
  rebuild();
}

std::filesystem::path PrefabLibrary::thumbnailRoot() const
{
  if (m_prefabRoot.empty())
  {
    return {};
  }

  return m_prefabRoot / "thumbnails";
}

std::filesystem::path PrefabLibrary::prefabPathForName(const std::string& name) const
{
  if (m_prefabRoot.empty())
  {
    return {};
  }

  auto sanitized = name;
  sanitized = kdl::str_trim(sanitized);
  if (sanitized.empty())
  {
    return {};
  }

  auto filename = sanitized;
  filename.append(".map");
  return m_prefabRoot / filename;
}

void PrefabLibrary::connectObservers()
{
  auto& map = m_document.map();

  m_notifierConnection +=
    map.mapWasCreatedNotifier.connect(this, &PrefabLibrary::mapStateChanged);
  m_notifierConnection +=
    map.mapWasLoadedNotifier.connect(this, &PrefabLibrary::mapStateChanged);
  m_notifierConnection +=
    map.mapWasSavedNotifier.connect(this, &PrefabLibrary::mapStateChanged);
  m_notifierConnection +=
    map.mapWasClearedNotifier.connect(this, &PrefabLibrary::mapWasCleared);
}

void PrefabLibrary::mapStateChanged(mdl::Map&)
{
  refresh();
}

void PrefabLibrary::mapWasCleared(mdl::Map&)
{
  m_prefabRoot.clear();
  const auto wasEnabled = std::exchange(m_enabled, false);
  if (wasEnabled)
  {
    availabilityDidChangeNotifier(false);
  }

  if (!m_entries.empty())
  {
    m_entries.clear();
    entriesDidChangeNotifier(m_entries);
  }
}

void PrefabLibrary::updatePrefabRoot()
{
  const auto root = defaultPrefabRoot();
  if (root == m_prefabRoot)
  {
    return;
  }

  m_prefabRoot = root;
  const auto nowEnabled = !m_prefabRoot.empty();
  if (m_enabled != nowEnabled)
  {
    m_enabled = nowEnabled;
    availabilityDidChangeNotifier(m_enabled);
  }
}

void PrefabLibrary::rebuild()
{
  m_entries.clear();

  if (!m_enabled || m_prefabRoot.empty())
  {
    entriesDidChangeNotifier(m_entries);
    return;
  }

  std::error_code ec;
  if (!std::filesystem::exists(m_prefabRoot, ec))
  {
    std::filesystem::create_directories(m_prefabRoot, ec);
    if (ec)
    {
      m_document.error() << "Failed to create prefab folder '" << m_prefabRoot << "': "
                         << ec.message();
      entriesDidChangeNotifier(m_entries);
      return;
    }
  }

  for (auto it = std::filesystem::directory_iterator(m_prefabRoot, ec);
       !ec && it != std::filesystem::directory_iterator{};
       ++it)
  {
    std::error_code statusError;
    const auto status = it->status(statusError);
    if (statusError || !std::filesystem::is_regular_file(status))
    {
      continue;
    }

    const auto& path = it->path();
    if (!hasMapExtension(path))
    {
      continue;
    }

    std::error_code timeError;
    const auto fileTime = std::filesystem::last_write_time(path, timeError);
    const auto lastModified = timeError ? std::chrono::system_clock::now()
                                        : convertFileTime(fileTime);

    m_entries.push_back(PrefabEntry{
      path,
      displayNameForPath(path),
      lastModified,
    });
  }

  if (ec)
  {
    m_document.error() << "Failed to enumerate prefabs in '" << m_prefabRoot << "': "
                       << ec.message();
    m_entries.clear();
  }

  std::ranges::sort(
    m_entries, [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });

  entriesDidChangeNotifier(m_entries);
}

std::filesystem::path PrefabLibrary::defaultPrefabRoot() const
{
  const auto& map = m_document.map();
  if (!map.persistent())
  {
    return {};
  }

  const auto mapPath = map.path();
  const auto parent = mapPath.parent_path();
  if (parent.empty())
  {
    return {};
  }

  return parent / PrefabFolderName;
}

std::string PrefabLibrary::displayNameForPath(const std::filesystem::path& path)
{
  return path.stem().string();
}

std::chrono::system_clock::time_point PrefabLibrary::convertFileTime(
  const std::filesystem::file_time_type& time)
{
  using namespace std::chrono;
  return time_point_cast<system_clock::duration>(
    time - std::filesystem::file_time_type::clock::now() + system_clock::now());
}

} // namespace tb::ui
