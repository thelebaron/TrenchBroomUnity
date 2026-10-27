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
#include <vector>

namespace tb::mdl
{
class Entity;
struct EntityDefinition;
struct GameConfig;
class Map;
} // namespace tb::mdl

namespace tb::unity
{

/**
 * Unity exports the prefabs TrenchBroom can place into <Unity project>/TrenchBroom:
 * entities.fgd (entity definitions with preview models), models/, thumbnails/ and
 * catalog.json, which is written last and lists every prefab.
 */
inline constexpr auto ExportFolderName = "TrenchBroom";
inline constexpr auto CatalogFileName = "catalog.json";
inline constexpr auto EntitiesFileName = "entities.fgd";
inline constexpr auto CatalogVersion = 1;

/**
 * The entity property holding the Unity prefab's asset GUID. The map importer finds the
 * prefab by it.
 */
inline constexpr auto PrefabGuidProperty = "_unity_prefab";

struct UnityCatalogPrefab
{
  std::string guid;
  std::string className;
  std::string displayName;
  // The prefab's folder under Assets, for example "Models/CommonObjects/Van".
  std::string category;
  std::filesystem::path thumbnail;

  bool operator==(const UnityCatalogPrefab&) const = default;
};

struct UnityCatalog
{
  std::vector<UnityCatalogPrefab> prefabs;
};

/**
 * The export folder in the Unity project, or empty if no Unity project is set.
 */
std::filesystem::path exportFolder();
std::filesystem::path catalogPath();

/**
 * Whether entity definitions come from the Unity export instead of the map's _tb_def
 * property, which is then ignored but left untouched.
 */
bool usesUnityEntityDefinitions(const mdl::GameConfig& config);
std::filesystem::path unityEntityDefinitionsPath();
bool isUnityEntityDefinitionPreference(const std::filesystem::path& path);

/**
 * Parses catalog.json. Thumbnail paths are resolved against the export folder.
 */
Result<UnityCatalog> parseCatalog(
  const std::string& json, const std::filesystem::path& exportFolder);
Result<UnityCatalog> loadCatalog();

/**
 * Whether the definition is a Unity prefab from the catalog. Its other properties are
 * the prefab's component fields: placing it doesn't copy their defaults, so a placed
 * prefab follows the prefab's values until they are changed in TrenchBroom.
 */
bool isUnityPrefab(const mdl::EntityDefinition& definition);

/**
 * Sets the prefab GUID on a new entity from its definition's default, whether or not
 * default properties are applied in general, so a placed prefab can always be found.
 */
void setPrefabGuid(const mdl::EntityDefinition& definition, mdl::Entity& entity);

/**
 * Brings existing entities in line with the catalog: entities whose class name is a
 * catalog prefab get its GUID, and entities whose prefab was renamed get its current
 * class name. Undoable. Returns the number of entities changed.
 */
size_t updatePrefabEntities(mdl::Map& map, const UnityCatalog& catalog);

/**
 * Loads the catalog and runs updatePrefabEntities, reporting the result in the map's log.
 */
void updatePrefabEntitiesFromCatalog(mdl::Map& map);

} // namespace tb::unity
