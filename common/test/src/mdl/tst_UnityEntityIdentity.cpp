/*
 Copyright (C) 2026 the TrenchBroom contributors

 This file is part of TrenchBroom, licensed under the GNU General Public License,
 version 3 or later. See LICENSE.txt for details.
 */

#include "MapFixture.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "TestUtils.h"
#include "io/TestEnvironment.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_CopyPaste.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/PasteType.h"
#include "mdl/WorldNode.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tb::mdl
{
namespace
{
EntityNode* addEntity(Map& map, std::vector<EntityProperty> properties)
{
  auto* node = new EntityNode{Entity{std::move(properties)}};
  REQUIRE_FALSE(addNodes(map, {{parentForNodes(map), {node}}}).empty());
  return node;
}

std::string property(const EntityNode& node, const std::string& key)
{
  const auto* value = node.entity().property(key);
  REQUIRE(value != nullptr);
  return *value;
}
} // namespace

TEST_CASE("Fork entity identity lifecycle", "[fork][entity-identity]")
{
  auto ids = TemporarilySetPref{Preferences::GenerateEntityIds, true};
  auto indices = TemporarilySetPref{Preferences::GenerateClassIndices, true};
  auto fixture = MapFixture{};
  fixture.create();
  auto& map = fixture.map();
  auto* first = addEntity(map, {{"classname", "light"}, {"origin", "0 0 0"}});
  const auto firstId = property(*first, EntityPropertyKeys::UniqueId);
  const auto firstIndex = property(*first, EntityPropertyKeys::ClassIndex);
  REQUIRE_FALSE(firstId.empty());
  REQUIRE(firstIndex == "1");

  SECTION("duplication allocates new identity and undo redo preserves it")
  {
    selectNodes(map, {first});
    duplicateSelectedNodes(map);
    REQUIRE(map.selection().entities.size() == 1u);
    auto* duplicate = map.selection().entities.front();
    REQUIRE(duplicate != first);
    const auto duplicateId = property(*duplicate, EntityPropertyKeys::UniqueId);
    const auto duplicateIndex = property(*duplicate, EntityPropertyKeys::ClassIndex);
    CHECK(duplicateId != firstId);
    CHECK(duplicateIndex != firstIndex);

    map.undoCommand();
    CHECK(map.world()->defaultLayer()->childCount() == 1u);
    map.redoCommand();
    CHECK(property(*duplicate, EntityPropertyKeys::UniqueId) == duplicateId);
    CHECK(property(*duplicate, EntityPropertyKeys::ClassIndex) == duplicateIndex);
    CHECK(property(*first, EntityPropertyKeys::UniqueId) == firstId);
  }

  SECTION("copy paste can insert the same prefab repeatedly without identity collisions")
  {
    selectNodes(map, {first});
    const auto prefab = serializeSelectedNodes(map);
    REQUIRE_FALSE(prefab.empty());
    REQUIRE(paste(map, prefab) == PasteType::Node);
    REQUIRE(map.selection().entities.size() == 1u);
    const auto secondId =
      property(*map.selection().entities.front(), EntityPropertyKeys::UniqueId);
    const auto secondIndex =
      property(*map.selection().entities.front(), EntityPropertyKeys::ClassIndex);
    REQUIRE(paste(map, prefab) == PasteType::Node);
    REQUIRE(map.selection().entities.size() == 1u);
    const auto& third = *map.selection().entities.front();
    CHECK(property(third, EntityPropertyKeys::UniqueId) != firstId);
    CHECK(property(third, EntityPropertyKeys::UniqueId) != secondId);
    CHECK(property(third, EntityPropertyKeys::ClassIndex) != firstIndex);
    CHECK(property(third, EntityPropertyKeys::ClassIndex) != secondIndex);
  }

  SECTION("translation rotation and their undo preserve identity")
  {
    selectNodes(map, {first});
    REQUIRE(translateSelection(map, {32, 64, 0}));
    REQUIRE(rotateSelection(map, {0, 0, 0}, {0, 0, 1}, vm::to_radians(90.0)));
    CHECK(property(*first, EntityPropertyKeys::UniqueId) == firstId);
    CHECK(property(*first, EntityPropertyKeys::ClassIndex) == firstIndex);
    map.undoCommand();
    map.undoCommand();
    CHECK(first->entity().origin() == vm::vec3d{0, 0, 0});
    CHECK(property(*first, EntityPropertyKeys::UniqueId) == firstId);
    CHECK(property(*first, EntityPropertyKeys::ClassIndex) == firstIndex);
  }

  SECTION("deletion and undo restore the original identity")
  {
    removeNodes(map, {first});
    CHECK(map.world()->defaultLayer()->childCount() == 0u);
    map.undoCommand();
    REQUIRE(map.world()->defaultLayer()->childCount() == 1u);
    CHECK(property(*first, EntityPropertyKeys::UniqueId) == firstId);
    CHECK(property(*first, EntityPropertyKeys::ClassIndex) == firstIndex);
    auto* second = addEntity(map, {{"classname", "light"}});
    CHECK(property(*second, EntityPropertyKeys::UniqueId) != firstId);
    CHECK(property(*second, EntityPropertyKeys::ClassIndex) != firstIndex);
  }

  SECTION("class indices are independent for each classname")
  {
    auto* second = addEntity(map, {{"classname", "light"}});
    auto* otherClass = addEntity(map, {{"classname", "info_player_start"}});
    CHECK(property(*second, EntityPropertyKeys::ClassIndex) == "2");
    CHECK(property(*otherClass, EntityPropertyKeys::ClassIndex) == "1");
  }

  SECTION("worldspawn and editor containers do not receive entity identities")
  {
    CHECK_FALSE(map.world()->entity().hasProperty(EntityPropertyKeys::UniqueId));
    CHECK_FALSE(map.world()->entity().hasProperty(EntityPropertyKeys::ClassIndex));
    auto* group = new GroupNode{Group{"Prefab"}};
    auto* nested = new EntityNode{Entity{{{"classname", "light"}}}};
    group->addChild(nested);
    addNodes(map, {{parentForNodes(map), {group}}});
    CHECK_FALSE(property(*nested, EntityPropertyKeys::UniqueId).empty());
    CHECK(property(*nested, EntityPropertyKeys::ClassIndex) == "2");
  }
}

TEST_CASE("Fork imported entity identities", "[fork][entity-identity]")
{
  auto ids = TemporarilySetPref{Preferences::GenerateEntityIds, true};
  auto indices = TemporarilySetPref{Preferences::GenerateClassIndices, true};
  auto fixture = MapFixture{};
  fixture.create();
  auto& map = fixture.map();

  SECTION("valid imported identity is preserved and duplicates are repaired")
  {
    auto* original = addEntity(
      map,
      {{"classname", "light"}, {"_tb_unique_id", "existing-id"}, {"classindex", "7"}});
    auto* duplicate = addEntity(
      map,
      {{"classname", "light"}, {"_tb_unique_id", "existing-id"}, {"classindex", "7"}});
    CHECK(property(*original, EntityPropertyKeys::UniqueId) == "existing-id");
    CHECK(property(*original, EntityPropertyKeys::ClassIndex) == "7");
    CHECK_FALSE(property(*duplicate, EntityPropertyKeys::UniqueId).empty());
    CHECK(property(*duplicate, EntityPropertyKeys::UniqueId) != "existing-id");
    CHECK(property(*duplicate, EntityPropertyKeys::ClassIndex) != "7");
    map.rebuildClassIndexRegistry();
    CHECK(property(*original, EntityPropertyKeys::UniqueId) == "existing-id");
    CHECK(property(*original, EntityPropertyKeys::ClassIndex) == "7");
  }

  SECTION("empty ids and malformed class indices are replaced")
  {
    const auto invalidIndex = GENERATE("", "0", "-1", "abc", "1.5", "1extra");
    CAPTURE(invalidIndex);
    auto* node = addEntity(
      map, {{"classname", "light"}, {"_tb_unique_id", ""}, {"classindex", invalidIndex}});
    CHECK_FALSE(property(*node, EntityPropertyKeys::UniqueId).empty());
    CHECK(property(*node, EntityPropertyKeys::ClassIndex) == "1");
  }

  SECTION("save and reload retain identity in both supported Quake formats")
  {
    const auto format = GENERATE(MapFormat::Standard, MapFormat::Valve);
    fixture.create({.mapFormat = format});
    auto environment = io::TestEnvironment{};
    auto* node = addEntity(
      map,
      {{"classname", "light"}, {"_tb_unique_id", "persisted-id"}, {"classindex", "12"}});
    REQUIRE(map.saveAs(environment.dir() / "identity.map").is_success());
    CHECK(property(*node, EntityPropertyKeys::UniqueId) == "persisted-id");

    auto loaded = MapFixture{};
    loaded.load(environment.dir() / "identity.map", {.mapFormat = format});
    REQUIRE(loaded.map().world()->defaultLayer()->childCount() == 1u);
    auto* restored =
      dynamic_cast<EntityNode*>(loaded.map().world()->defaultLayer()->children().front());
    REQUIRE(restored != nullptr);
    CHECK(property(*restored, EntityPropertyKeys::UniqueId) == "persisted-id");
    CHECK(property(*restored, EntityPropertyKeys::ClassIndex) == "12");
    auto* next = addEntity(loaded.map(), {{"classname", "light"}});
    CHECK(property(*next, EntityPropertyKeys::UniqueId) != "persisted-id");
    CHECK(property(*next, EntityPropertyKeys::ClassIndex) != "12");
  }
}

TEST_CASE("Fork entity identity preferences", "[fork][entity-identity]")
{
  auto ids = TemporarilySetPref{Preferences::GenerateEntityIds, false};
  auto indices = TemporarilySetPref{Preferences::GenerateClassIndices, false};
  auto fixture = MapFixture{};
  fixture.create();
  auto& map = fixture.map();
  auto* node = addEntity(map, {{"classname", "light"}});
  REQUIRE_FALSE(node->entity().hasProperty(EntityPropertyKeys::UniqueId));
  REQUIRE_FALSE(node->entity().hasProperty(EntityPropertyKeys::ClassIndex));

  setPref(Preferences::GenerateEntityIds, true);
  PreferenceManager::instance().preferenceDidChangeNotifier(
    Preferences::GenerateEntityIds.path());
  setPref(Preferences::GenerateClassIndices, true);
  PreferenceManager::instance().preferenceDidChangeNotifier(
    Preferences::GenerateClassIndices.path());
  const auto id = property(*node, EntityPropertyKeys::UniqueId);
  CHECK_FALSE(id.empty());
  CHECK(property(*node, EntityPropertyKeys::ClassIndex) == "1");

  setPref(Preferences::GenerateEntityIds, false);
  PreferenceManager::instance().preferenceDidChangeNotifier(
    Preferences::GenerateEntityIds.path());
  setPref(Preferences::GenerateClassIndices, false);
  PreferenceManager::instance().preferenceDidChangeNotifier(
    Preferences::GenerateClassIndices.path());
  auto* next = addEntity(map, {{"classname", "light"}});
  CHECK_FALSE(next->entity().hasProperty(EntityPropertyKeys::UniqueId));
  CHECK_FALSE(next->entity().hasProperty(EntityPropertyKeys::ClassIndex));
  CHECK(property(*node, EntityPropertyKeys::UniqueId) == id);
  CHECK(property(*node, EntityPropertyKeys::ClassIndex) == "1");
}

} // namespace tb::mdl
