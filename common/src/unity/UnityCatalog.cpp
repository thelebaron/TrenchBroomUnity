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

#include "UnityCatalog.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Error.h"
#include "Logger.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "io/DiskIO.h"
#include "mdl/Entity.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/GameConfig.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/NodeContents.h"
#include "mdl/NodeQueries.h"
#include "mdl/PropertyDefinition.h"
#include "mdl/WorldNode.h"

#include <fmt/format.h>

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace tb::unity
{
namespace
{

Result<std::string> requiredString(const QJsonObject& object, const char* key)
{
  const auto value = object.value(key);
  if (!value.isString() || value.toString().isEmpty())
  {
    return Error{fmt::format("Unity catalog prefab is missing '{}'", key)};
  }
  return value.toString().toStdString();
}

Result<UnityCatalogPrefab> parsePrefab(
  const QJsonObject& object, const std::filesystem::path& exportFolder)
{
  const auto guid = requiredString(object, "guid");
  const auto className = requiredString(object, "class_name");
  const auto displayName = requiredString(object, "display_name");
  const auto thumbnail = requiredString(object, "thumbnail");
  for (const auto* field : {&guid, &className, &displayName, &thumbnail})
  {
    if (field->is_error())
    {
      return std::get<Error>(field->error());
    }
  }

  return UnityCatalogPrefab{
    guid.value(),
    className.value(),
    displayName.value(),
    object.value("category").toString().toStdString(),
    (exportFolder / thumbnail.value()).lexically_normal(),
  };
}

} // namespace

std::filesystem::path exportFolder()
{
  const auto& project = pref(Preferences::UnityProjectPath);
  return project.empty() ? std::filesystem::path{} : project / ExportFolderName;
}

std::filesystem::path catalogPath()
{
  const auto folder = exportFolder();
  return folder.empty() ? folder : folder / CatalogFileName;
}

bool usesUnityEntityDefinitions(const mdl::GameConfig& config)
{
  return config.name == "Quake" && pref(Preferences::UnityEntityDefinitionsEnabled)
         && !pref(Preferences::UnityProjectPath).empty();
}

std::filesystem::path unityEntityDefinitionsPath()
{
  return exportFolder() / EntitiesFileName;
}

bool isUnityEntityDefinitionPreference(const std::filesystem::path& path)
{
  return path == Preferences::UnityEntityDefinitionsEnabled.path()
         || path == Preferences::UnityProjectPath.path();
}

Result<UnityCatalog> parseCatalog(
  const std::string& json, const std::filesystem::path& exportFolder)
{
  auto parseError = QJsonParseError{};
  const auto document =
    QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
  {
    return Error{
      "Unity catalog is not a JSON object: " + parseError.errorString().toStdString()};
  }

  const auto root = document.object();
  if (const auto version = root.value("version").toInt(); version != CatalogVersion)
  {
    return Error{fmt::format(
      "Unity catalog version {} is not supported; TrenchBroom reads version {}. Update "
      "TrenchBroom or the Unity package so they match",
      version,
      CatalogVersion)};
  }

  auto catalog = UnityCatalog{};
  for (const auto& value : root.value("prefabs").toArray())
  {
    auto prefab = parsePrefab(value.toObject(), exportFolder);
    if (prefab.is_error())
    {
      return std::get<Error>(prefab.error());
    }
    catalog.prefabs.push_back(std::move(prefab.value()));
  }
  return catalog;
}

Result<UnityCatalog> loadCatalog()
{
  const auto path = catalogPath();
  if (path.empty())
  {
    return Error{"No Unity project is set in the Unity preferences"};
  }
  auto json = io::Disk::withInputStream(path, [](auto& stream) {
    return std::string{std::istreambuf_iterator<char>{stream}, {}};
  });
  if (json.is_error())
  {
    return std::get<Error>(json.error());
  }
  return parseCatalog(json.value(), exportFolder());
}

void setPrefabGuid(const mdl::EntityDefinition& definition, mdl::Entity& entity)
{
  const auto property = std::ranges::find(
    definition.propertyDefinitions, PrefabGuidProperty, &mdl::PropertyDefinition::key);
  if (property == definition.propertyDefinitions.end())
  {
    return;
  }
  if (const auto guid = mdl::PropertyDefinition::defaultValue(*property))
  {
    entity.addOrUpdateProperty(PrefabGuidProperty, *guid);
  }
}

size_t updatePrefabEntities(mdl::Map& map, const UnityCatalog& catalog)
{
  auto byGuid = std::unordered_map<std::string, const UnityCatalogPrefab*>{};
  auto byClassName = std::unordered_map<std::string, const UnityCatalogPrefab*>{};
  for (const auto& prefab : catalog.prefabs)
  {
    byGuid.emplace(prefab.guid, &prefab);
    byClassName.emplace(prefab.className, &prefab);
  }

  auto updates = std::vector<std::pair<mdl::Node*, mdl::NodeContents>>{};
  const auto entityNodes = mdl::collectDescendants(
    std::vector<mdl::Node*>{map.world()}, [](mdl::EntityNode*) { return true; });
  for (auto* node : entityNodes)
  {
    auto* entityNode = static_cast<mdl::EntityNode*>(node);
    auto entity = entityNode->entity();
    const auto* guid = entity.property(PrefabGuidProperty);
    const auto* className = entity.property(mdl::EntityPropertyKeys::Classname);

    if (guid)
    {
      // Renamed prefab: same GUID, new class name.
      const auto prefab = byGuid.find(*guid);
      if (
        prefab == byGuid.end() || (className && *className == prefab->second->className))
      {
        continue;
      }
      entity.addOrUpdateProperty(
        mdl::EntityPropertyKeys::Classname, prefab->second->className);
    }
    else
    {
      // Placed before entities carried a GUID.
      const auto prefab = className ? byClassName.find(*className) : byClassName.end();
      if (prefab == byClassName.end())
      {
        continue;
      }
      entity.addOrUpdateProperty(PrefabGuidProperty, prefab->second->guid);
    }
    updates.emplace_back(entityNode, mdl::NodeContents{std::move(entity)});
  }

  const auto count = updates.size();
  if (count > 0)
  {
    mdl::updateNodeContents(
      map, "Update Prefab Entities from Catalog", std::move(updates));
  }
  return count;
}

void updatePrefabEntitiesFromCatalog(mdl::Map& map)
{
  auto catalog = loadCatalog();
  if (catalog.is_error())
  {
    map.logger().error() << "Could not update prefab entities: "
                         << std::get<Error>(catalog.error()).msg;
    return;
  }
  map.logger().info() << "Updated " << updatePrefabEntities(map, catalog.value())
                      << " prefab entities from the Unity catalog";
}

} // namespace tb::unity
