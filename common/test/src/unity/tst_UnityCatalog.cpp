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

#include "MapFixture.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "TestUtils.h"
#include "mdl/Entity.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityNode.h"
#include "mdl/GameConfig.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Nodes.h"
#include "mdl/PropertyDefinition.h"
#include "unity/UnityCatalog.h"

#include <fmt/format.h>

#include <catch2/catch_test_macros.hpp>

namespace tb::unity
{
namespace
{
constexpr auto CatalogJson = R"({
  "version": 1,
  "prefabs": [
    {
      "guid": "59905e417919b994e9ff248cfd4913d1",
      "class_name": "van",
      "display_name": "Van",
      "category": "Models/CommonObjects/Van",
      "asset_path": "Assets/Models/CommonObjects/Van/Van.prefab",
      "model": "models/59905e417919b994e9ff248cfd4913d1.obj",
      "thumbnail": "thumbnails/59905e417919b994e9ff248cfd4913d1.png",
      "bounds_min": [-51.7, -23.2, 0.3],
      "bounds_max": [51.7, 23.2, 45.9]
    }
  ]
})";

constexpr auto VanGuid = "59905e417919b994e9ff248cfd4913d1";

mdl::EntityNode* addEntity(mdl::Map& map, std::vector<mdl::EntityProperty> properties)
{
  auto* node = new mdl::EntityNode{mdl::Entity{std::move(properties)}};
  REQUIRE_FALSE(mdl::addNodes(map, {{mdl::parentForNodes(map), {node}}}).empty());
  return node;
}

std::string propertyOf(const mdl::EntityNode& node, const std::string& key)
{
  const auto* value = node.entity().property(key);
  return value ? *value : std::string{};
}
} // namespace

TEST_CASE("Unity catalog parses prefabs", "[unity-catalog]")
{
  const auto catalog = parseCatalog(CatalogJson, "C:/project/TrenchBroom");
  REQUIRE(catalog.is_success());
  REQUIRE(catalog.value().prefabs.size() == 1u);

  const auto& van = catalog.value().prefabs.front();
  CHECK(van.guid == VanGuid);
  CHECK(van.className == "van");
  CHECK(van.displayName == "Van");
  CHECK(van.category == "Models/CommonObjects/Van");
  CHECK(
    van.thumbnail
    == std::filesystem::path{"C:/project/TrenchBroom/thumbnails/"
                             "59905e417919b994e9ff248cfd4913d1.png"}
         .lexically_normal());
}

TEST_CASE("Unity catalog parses decals", "[unity-catalog]")
{
  constexpr auto json = R"({
    "version": 1,
    "prefabs": [],
    "decal_atlases": [
      { "id": "markings", "texture": "decals/markings.png", "width": 256, "height": 128 }
    ],
    "decals": [
      {
        "id": "markings/stripe",
        "display_name": "Stripe",
        "category": "markings",
        "atlas": "markings",
        "rect": [0.25, 0.5, 0.125, 0.0625],
        "default_size": [32, 16]
      }
    ]
  })";

  const auto catalog = parseCatalog(json, "C:/project/TrenchBroom");
  REQUIRE(catalog.is_success());
  CHECK(
    catalog.value().decalAtlases
    == std::vector<UnityCatalogDecalAtlas>{
      {"markings",
       std::filesystem::path{"C:/project/TrenchBroom/decals/markings.png"}
         .lexically_normal()}});
  CHECK(
    catalog.value().decals
    == std::vector<UnityCatalogDecal>{
      {"markings/stripe",
       "Stripe",
       "markings",
       "markings",
       vm::vec4d{0.25, 0.5, 0.125, 0.0625},
       vm::vec2d{32, 16}}});
}

TEST_CASE("Unity catalog without decals has none", "[unity-catalog]")
{
  const auto catalog = parseCatalog(CatalogJson, "C:/project/TrenchBroom");
  REQUIRE(catalog.is_success());
  CHECK(catalog.value().decalAtlases.empty());
  CHECK(catalog.value().decals.empty());
}

TEST_CASE("Unity catalog rejects bad decals", "[unity-catalog]")
{
  constexpr auto atlases = R"("decal_atlases": [{"id": "a", "texture": "decals/a.png"}])";
  const auto withDecal = [&](const std::string& decal) {
    return parseCatalog(
      fmt::format(R"({{"version": 1, {}, "decals": [{}]}})", atlases, decal), {});
  };

  CHECK(
    withDecal(
      R"({"id": "a/x", "display_name": "X", "atlas": "a", "rect": [0, 0, 1, 1], "default_size": [8, 8]})")
      .is_success());
  // unknown atlas
  CHECK(
    withDecal(
      R"({"id": "b/x", "display_name": "X", "atlas": "b", "rect": [0, 0, 1, 1], "default_size": [8, 8]})")
      .is_error());
  // rect with too few numbers
  CHECK(
    withDecal(
      R"({"id": "a/x", "display_name": "X", "atlas": "a", "rect": [0, 0, 1], "default_size": [8, 8]})")
      .is_error());
  // missing id
  CHECK(
    withDecal(
      R"({"display_name": "X", "atlas": "a", "rect": [0, 0, 1, 1], "default_size": [8, 8]})")
      .is_error());
  // atlas without texture
  CHECK(parseCatalog(R"({"version": 1, "decal_atlases": [{"id": "a"}]})", {}).is_error());
}

TEST_CASE("Unity catalog rejects bad files", "[unity-catalog]")
{
  CHECK(parseCatalog("not json", {}).is_error());
  CHECK(parseCatalog(R"([1, 2])", {}).is_error());
  CHECK(parseCatalog(R"({"version": 2, "prefabs": []})", {}).is_error());
  CHECK(
    parseCatalog(R"({"version": 1, "prefabs": [{"guid": "x", "class_name": "y"}]})", {})
      .is_error());
  CHECK(parseCatalog(R"({"version": 1, "prefabs": []})", {}).is_success());
}

TEST_CASE("Unity entity definitions follow the preferences", "[unity-catalog]")
{
  auto quake = mdl::GameConfig{};
  quake.name = "Quake";
  auto other = mdl::GameConfig{};
  other.name = "Quake 2";

  auto project = TemporarilySetPref{
    Preferences::UnityProjectPath, std::filesystem::path{"C:/project"}};
  auto enabled = TemporarilySetPref{Preferences::UnityEntityDefinitionsEnabled, true};
  CHECK(usesUnityEntityDefinitions(quake));
  CHECK_FALSE(usesUnityEntityDefinitions(other));
  CHECK(
    unityEntityDefinitionsPath()
    == std::filesystem::path{"C:/project"} / "TrenchBroom" / "entities.fgd");

  SECTION("off when turned off")
  {
    auto disabled = TemporarilySetPref{Preferences::UnityEntityDefinitionsEnabled, false};
    CHECK_FALSE(usesUnityEntityDefinitions(quake));
  }

  SECTION("off without a Unity project")
  {
    auto noProject =
      TemporarilySetPref{Preferences::UnityProjectPath, std::filesystem::path{}};
    CHECK_FALSE(usesUnityEntityDefinitions(quake));
    CHECK(catalogPath().empty());
  }
}

TEST_CASE("Unity prefab GUID is set from the entity definition", "[unity-catalog]")
{
  auto definition = mdl::EntityDefinition{};
  definition.name = "van";

  SECTION("definitions with a prefab GUID set it")
  {
    definition.propertyDefinitions.push_back(
      {PrefabGuidProperty,
       mdl::PropertyValueTypes::String{std::string{VanGuid}},
       "Unity prefab GUID",
       "",
       true});
    auto entity = mdl::Entity{{{"classname", "van"}}};
    setPrefabGuid(definition, entity);
    REQUIRE(entity.property(PrefabGuidProperty) != nullptr);
    CHECK(*entity.property(PrefabGuidProperty) == VanGuid);
  }

  SECTION("other definitions leave the entity alone")
  {
    auto entity = mdl::Entity{{{"classname", "van"}}};
    setPrefabGuid(definition, entity);
    CHECK(entity.property(PrefabGuidProperty) == nullptr);
  }
}

TEST_CASE("Placed Unity prefabs follow the prefab's values", "[unity-catalog]")
{
  auto defaults = TemporarilySetPref{Preferences::AutoSetDefaultEntityProperties, true};
  auto fixture = mdl::MapFixture{};
  fixture.create();
  auto& map = fixture.map();

  const auto prefab = mdl::EntityDefinition{
    "item_can",
    Color{},
    "",
    {
      {PrefabGuidProperty,
       mdl::PropertyValueTypes::String{std::string{VanGuid}},
       "",
       "",
       true},
      {"chance_to_spawn", mdl::PropertyValueTypes::Float{0.1f}, "", ""},
    },
    mdl::PointEntityDefinition{vm::bbox3d{8.0}, {}, {}},
  };
  const auto plain = mdl::EntityDefinition{
    "light",
    Color{},
    "",
    {{"light", mdl::PropertyValueTypes::Integer{300}, "", ""}},
    mdl::PointEntityDefinition{vm::bbox3d{8.0}, {}, {}},
  };

  CHECK(isUnityPrefab(prefab));
  CHECK_FALSE(isUnityPrefab(plain));

  const auto* placedPrefab = mdl::createPointEntity(map, prefab, {0, 0, 0});
  REQUIRE(placedPrefab != nullptr);
  CHECK(propertyOf(*placedPrefab, PrefabGuidProperty) == VanGuid);
  CHECK(placedPrefab->entity().property("chance_to_spawn") == nullptr);

  // Other entities still get their defaults.
  const auto* placedLight = mdl::createPointEntity(map, plain, {0, 0, 0});
  REQUIRE(placedLight != nullptr);
  CHECK(propertyOf(*placedLight, "light") == "300");
}

TEST_CASE("Unity catalog updates existing prefab entities", "[unity-catalog]")
{
  const auto catalog = parseCatalog(CatalogJson, {});
  REQUIRE(catalog.is_success());

  auto fixture = mdl::MapFixture{};
  fixture.create();
  auto& map = fixture.map();

  auto* withoutGuid = addEntity(map, {{"classname", "van"}, {"origin", "0 0 0"}});
  auto* renamed = addEntity(
    map, {{"classname", "old_van"}, {PrefabGuidProperty, VanGuid}, {"origin", "0 0 0"}});
  auto* current = addEntity(
    map, {{"classname", "van"}, {PrefabGuidProperty, VanGuid}, {"origin", "0 0 0"}});
  auto* light = addEntity(map, {{"classname", "light"}, {"origin", "0 0 0"}});
  auto* unknownGuid = addEntity(
    map, {{"classname", "crate"}, {PrefabGuidProperty, "deleted"}, {"origin", "0 0 0"}});

  CHECK(updatePrefabEntities(map, catalog.value()) == 2u);
  CHECK(propertyOf(*withoutGuid, PrefabGuidProperty) == VanGuid);
  CHECK(propertyOf(*renamed, "classname") == "van");
  CHECK(propertyOf(*current, "classname") == "van");
  CHECK(propertyOf(*light, PrefabGuidProperty).empty());
  CHECK(propertyOf(*unknownGuid, "classname") == "crate");

  SECTION("nothing left to update the second time")
  {
    CHECK(updatePrefabEntities(map, catalog.value()) == 0u);
  }

  SECTION("undo restores the entities")
  {
    map.undoCommand();
    CHECK(propertyOf(*withoutGuid, PrefabGuidProperty).empty());
    CHECK(propertyOf(*renamed, "classname") == "old_van");
  }
}

} // namespace tb::unity
