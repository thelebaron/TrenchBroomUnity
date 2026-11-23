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
#include "io/NodeWriter.h"
#include "io/TestParserStatus.h"
#include "io/WorldReader.h"
#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Group.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/NodeContents.h"
#include "mdl/RoomGenerator.h"
#include "mdl/WorldNode.h"

#include "kdl/task_manager.h"

#include "vm/bbox_io.h" // IWYU pragma: keep
#include "vm/mat_ext.h"
#include "vm/scalar.h"

#include <algorithm>
#include <ranges>
#include <sstream>

#include <catch2/catch_test_macros.hpp>

namespace tb::mdl
{
namespace
{

bool strictlyContains(const vm::bbox3d& box, const vm::vec3d& point)
{
  return point.x() > box.min.x() && point.x() < box.max.x() && point.y() > box.min.y()
         && point.y() < box.max.y() && point.z() > box.min.z() && point.z() < box.max.z();
}

size_t countBrushesAt(const std::vector<RoomShellBrush>& brushes, const vm::vec3d& point)
{
  return static_cast<size_t>(std::ranges::count_if(
    brushes, [&](const auto& brush) { return strictlyContains(brush.bounds, point); }));
}

std::optional<RoomSurface> surfaceAt(
  const std::vector<RoomShellBrush>& brushes, const vm::vec3d& point)
{
  for (const auto& brush : brushes)
  {
    if (strictlyContains(brush.bounds, point))
    {
      return brush.surface;
    }
  }
  return std::nullopt;
}

vm::bbox3d dilated(const vm::bbox3d& space, const RoomRecipe& recipe)
{
  const auto t = static_cast<double>(recipe.wallThickness);
  return {
    space.min - vm::vec3d{t, t, recipe.generateFloor ? t : 0.0},
    space.max + vm::vec3d{t, t, recipe.generateCeiling ? t : 0.0}};
}

/**
 * Samples the shell on an 8 unit lattice and checks that every sample is covered by
 * exactly one brush if it lies within the wall thickness of a space but outside all
 * spaces, and by no brush otherwise. This checks that the shell is sealed, has no
 * overlapping brushes and has no walls between connected spaces.
 */
void checkShellMatchesSpaces(
  const RoomRecipe& recipe, const std::vector<RoomShellBrush>& brushes)
{
  auto bounds = dilated(recipe.spaces.front(), recipe);
  for (const auto& space : recipe.spaces)
  {
    bounds = vm::merge(bounds, dilated(space, recipe));
  }
  bounds = bounds.expand(8.0);

  for (auto x = bounds.min.x() + 4.0; x < bounds.max.x(); x += 8.0)
  {
    for (auto y = bounds.min.y() + 4.0; y < bounds.max.y(); y += 8.0)
    {
      for (auto z = bounds.min.z() + 4.0; z < bounds.max.z(); z += 8.0)
      {
        const auto point = vm::vec3d{x, y, z};
        const auto inSpace = std::ranges::any_of(recipe.spaces, [&](const auto& space) {
          return strictlyContains(space, point);
        });
        const auto inShell = std::ranges::any_of(recipe.spaces, [&](const auto& space) {
          return strictlyContains(dilated(space, recipe), point);
        });

        const auto expected = inShell && !inSpace ? 1u : 0u;
        INFO(point);
        REQUIRE(countBrushesAt(brushes, point) == expected);
      }
    }
  }
}

size_t countSurfaces(
  const std::vector<RoomShellBrush>& brushes, const RoomSurface surface)
{
  return static_cast<size_t>(std::ranges::count_if(
    brushes, [&](const auto& brush) { return brush.surface == surface; }));
}

RoomRecipe makeRecipe(std::vector<vm::bbox3d> spaces, std::vector<vm::bbox3d> doors = {})
{
  auto recipe = RoomRecipe{};
  recipe.spaces = std::move(spaces);
  recipe.doors = std::move(doors);
  recipe.wallThickness = 16;
  recipe.wallMaterial = "wall";
  recipe.floorMaterial = "floor";
  recipe.ceilingMaterial = "ceiling";
  return recipe;
}

std::vector<BrushNode*> roomBrushes(const GroupNode& room)
{
  auto result = std::vector<BrushNode*>{};
  for (auto* child : room.children())
  {
    if (auto* brushNode = dynamic_cast<BrushNode*>(child))
    {
      result.push_back(brushNode);
    }
  }
  return result;
}

} // namespace

TEST_CASE("buildRoomShell")
{
  SECTION("Rejects invalid recipes")
  {
    CHECK(buildRoomShell(makeRecipe({})).is_error());

    auto recipe = makeRecipe({vm::bbox3d{{0, 0, 0}, {128, 128, 128}}});
    recipe.wallThickness = 0;
    CHECK(buildRoomShell(recipe).is_error());

    CHECK(buildRoomShell(makeRecipe({vm::bbox3d{{0, 0, 0}, {128, 0, 128}}})).is_error());
  }

  SECTION("Single box produces four walls, a floor and a ceiling")
  {
    const auto recipe = makeRecipe({vm::bbox3d{{0, 0, 0}, {128, 128, 128}}});
    const auto brushes = buildRoomShell(recipe).value();

    CHECK(brushes.size() == 6u);
    CHECK(countSurfaces(brushes, RoomSurface::Wall) == 4u);
    CHECK(countSurfaces(brushes, RoomSurface::Floor) == 1u);
    CHECK(countSurfaces(brushes, RoomSurface::Ceiling) == 1u);
    CHECK(surfaceAt(brushes, {64, 64, -8}) == RoomSurface::Floor);
    CHECK(surfaceAt(brushes, {64, 64, 136}) == RoomSurface::Ceiling);
    CHECK(surfaceAt(brushes, {-8, 64, 64}) == RoomSurface::Wall);
    checkShellMatchesSpaces(recipe, brushes);
  }

  SECTION("Touching boxes merge into one room without a wall between them")
  {
    const auto recipe = makeRecipe({
      vm::bbox3d{{0, 0, 0}, {128, 128, 128}},
      vm::bbox3d{{128, 0, 0}, {256, 128, 128}},
    });
    const auto brushes = buildRoomShell(recipe).value();

    // Same as a single 256 x 128 box.
    CHECK(brushes.size() == 6u);
    CHECK(countBrushesAt(brushes, {128, 64, 64}) == 0u);
    checkShellMatchesSpaces(recipe, brushes);
  }

  SECTION("Overlapping boxes form an L-shaped room")
  {
    const auto recipe = makeRecipe({
      vm::bbox3d{{0, 0, 0}, {256, 128, 128}},
      vm::bbox3d{{0, 64, 0}, {128, 256, 128}},
    });
    const auto brushes = buildRoomShell(recipe).value();

    checkShellMatchesSpaces(recipe, brushes);
    CHECK(countSurfaces(brushes, RoomSurface::Floor) == 2u);
    CHECK(countSurfaces(brushes, RoomSurface::Ceiling) == 2u);
    // An L-shaped outline has six sides.
    CHECK(countSurfaces(brushes, RoomSurface::Wall) <= 8u);
  }

  SECTION("Spaces at different heights are joined by a step")
  {
    const auto recipe = makeRecipe({
      vm::bbox3d{{0, 0, 0}, {128, 128, 128}},
      vm::bbox3d{{128, 0, 32}, {256, 128, 160}},
    });
    const auto brushes = buildRoomShell(recipe).value();

    checkShellMatchesSpaces(recipe, brushes);

    // The riser below the higher floor and the lintel above the lower ceiling are solid.
    CHECK(countBrushesAt(brushes, {136, 64, 8}) == 1u);
    CHECK(countBrushesAt(brushes, {120, 64, 152}) == 1u);
    // The opening between the two spaces is open.
    CHECK(countBrushesAt(brushes, {128, 64, 64}) == 0u);
  }

  SECTION("Disabling floor and ceiling leaves the room open")
  {
    auto recipe = makeRecipe({vm::bbox3d{{0, 0, 0}, {128, 128, 128}}});
    recipe.generateFloor = false;
    recipe.generateCeiling = false;
    const auto brushes = buildRoomShell(recipe).value();

    CHECK(brushes.size() == 4u);
    CHECK(countSurfaces(brushes, RoomSurface::Floor) == 0u);
    CHECK(countSurfaces(brushes, RoomSurface::Ceiling) == 0u);
    for (const auto& brush : brushes)
    {
      CHECK(brush.bounds.min.z() == 0.0);
      CHECK(brush.bounds.max.z() == 128.0);
    }
    checkShellMatchesSpaces(recipe, brushes);
  }

  SECTION("Doorway cuts the wall but leaves threshold, lintel and floor intact")
  {
    // The door box reaches below the floor; only the wall within the room height is cut.
    const auto recipe = makeRecipe(
      {vm::bbox3d{{0, 0, 0}, {256, 256, 128}}},
      {vm::bbox3d{{96, -32, -32}, {160, 32, 96}}});
    const auto brushes = buildRoomShell(recipe).value();

    // Opening in the wall at y in [-16, 0].
    CHECK(countBrushesAt(brushes, {128, -8, 48}) == 0u);
    // Lintel above the opening.
    CHECK(countBrushesAt(brushes, {128, -8, 112}) == 1u);
    // Threshold below the opening, in line with the floor.
    CHECK(countBrushesAt(brushes, {128, -8, -8}) == 1u);
    // Floor under the doorway inside the room.
    CHECK(surfaceAt(brushes, {128, 16, -8}) == RoomSurface::Floor);
    // Wall next to the opening.
    CHECK(countBrushesAt(brushes, {64, -8, 48}) == 1u);

    // Left and right of the door, lintel and threshold.
    CHECK(countSurfaces(brushes, RoomSurface::Wall) == 7u);
  }

  SECTION("Doorway away from the walls has no effect")
  {
    const auto withoutDoor = makeRecipe({vm::bbox3d{{0, 0, 0}, {256, 256, 128}}});
    const auto withDoor = makeRecipe(
      {vm::bbox3d{{0, 0, 0}, {256, 256, 128}}},
      {vm::bbox3d{{64, 64, 0}, {128, 128, 64}}});

    CHECK(buildRoomShell(withDoor).value() == buildRoomShell(withoutDoor).value());
  }
}

TEST_CASE("RoomRecipe")
{
  SECTION("Round trips through group properties")
  {
    auto recipe = makeRecipe(
      {vm::bbox3d{{0, 0, 0}, {128, 128, 128}}, vm::bbox3d{{-64.5, 0, 0}, {0, 64, 96}}},
      {vm::bbox3d{{32, -32, 0}, {96, 16, 112}}});
    recipe.wallThickness = 24;
    recipe.generateCeiling = false;

    auto group = Group{"Room"};
    CHECK_FALSE(isRoom(group));
    CHECK(readRoomRecipe(group).is_error());

    writeRoomRecipe(group, recipe);
    CHECK(isRoom(group));
    CHECK(readRoomRecipe(group).value() == recipe);

    recipe.doors.clear();
    writeRoomRecipe(group, recipe);
    CHECK(group.property("_tb_room_doors") == nullptr);
    CHECK(readRoomRecipe(group).value() == recipe);
  }

  SECTION("Rejects malformed properties")
  {
    auto group = Group{"Room"};
    writeRoomRecipe(group, makeRecipe({vm::bbox3d{{0, 0, 0}, {128, 128, 128}}}));

    SECTION("Bad box")
    {
      group.setProperty("_tb_room_spaces", "0 0 0 128 128");
      CHECK(readRoomRecipe(group).is_error());
    }

    SECTION("Bad number")
    {
      group.setProperty("_tb_room_spaces", "0 0 0 128 abc 128");
      CHECK(readRoomRecipe(group).is_error());
    }

    SECTION("Bad thickness")
    {
      group.setProperty("_tb_room_thickness", "0");
      CHECK(readRoomRecipe(group).is_error());
    }
  }

  SECTION("Group properties survive writing and reading the map")
  {
    const auto worldBounds = vm::bbox3d{8192.0};
    auto world = WorldNode{{}, {}, MapFormat::Standard};

    auto group = Group{"Room"};
    writeRoomRecipe(group, makeRecipe({vm::bbox3d{{0, 0, 0}, {128, 128, 128}}}));
    group.setProperty("custom_key", "custom value");
    auto* groupNode = new GroupNode{std::move(group)};
    world.defaultLayer()->addChild(groupNode);

    auto taskManager = kdl::task_manager{};
    auto str = std::stringstream{};
    auto writer = io::NodeWriter{world, str};
    writer.writeMap(taskManager);

    const auto mapText = str.str();
    auto status = io::TestParserStatus{};
    auto reader = io::WorldReader{mapText, MapFormat::Standard, {}};
    auto readWorld = reader.read(worldBounds, status, taskManager);
    REQUIRE(readWorld.is_success());

    auto* readLayer = readWorld.value()->defaultLayer();
    REQUIRE(readLayer->childCount() == 1u);
    auto* readGroup = dynamic_cast<GroupNode*>(readLayer->children().front());
    REQUIRE(readGroup != nullptr);

    CHECK(readGroup->group().properties() == groupNode->group().properties());
    CHECK(readGroup->group().property("_tb_name") == nullptr);
    CHECK(readGroup->group().property("_tb_id") == nullptr);
  }
}

TEST_CASE("Room creation and rebuilding")
{
  auto fixture = MapFixture{};
  auto& map = fixture.map();
  fixture.create();

  const auto recipe = makeRecipe({vm::bbox3d{{0, 0, 0}, {128, 128, 128}}});
  auto* room = createRoom(map, map.world()->defaultLayer(), recipe).value();

  SECTION("createRoom adds a room group with the shell")
  {
    CHECK(room->parent() == map.world()->defaultLayer());
    CHECK(isRoom(room->group()));
    CHECK(readRoomRecipe(room->group()).value() == recipe);
    CHECK(roomBrushes(*room).size() == 6u);
    CHECK(countModifiedRoomBrushes(map, *room) == 0u);
    CHECK(findRoom(roomBrushes(*room).front()) == room);
    CHECK(collectRooms(map) == std::vector<GroupNode*>{room});
  }

  SECTION("rebuildRoom replaces the shell and stores the new recipe")
  {
    auto extended = recipe;
    extended.spaces.push_back(vm::bbox3d{{128, 0, 0}, {256, 128, 128}});
    extended.wallThickness = 8;

    REQUIRE(rebuildRoom(map, *room, extended, ModifiedRoomBrushes::Replace).is_success());
    CHECK(readRoomRecipe(room->group()).value() == extended);
    CHECK(roomBrushes(*room).size() == 6u);
    CHECK(countModifiedRoomBrushes(map, *room) == 0u);
    CHECK(room->logicalBounds() == vm::bbox3d{{-8, -8, -8}, {264, 136, 136}});

    map.undoCommand();
    CHECK(readRoomRecipe(room->group()).value() == recipe);
    CHECK(roomBrushes(*room).size() == 6u);
    CHECK(room->logicalBounds() == vm::bbox3d{{-16, -16, -16}, {144, 144, 144}});

    map.redoCommand();
    CHECK(readRoomRecipe(room->group()).value() == extended);
    CHECK(roomBrushes(*room).size() == 6u);
  }

  SECTION("Hand edited brushes are detected and kept or replaced")
  {
    auto* wall = roomBrushes(*room).front();
    auto brush = wall->brush();
    for (auto& face : brush.faces())
    {
      auto attributes = face.attributes();
      attributes.setMaterialName("retextured");
      face.setAttributes(attributes);
    }
    REQUIRE(
      updateNodeContents(map, "Retexture", {{wall, NodeContents{std::move(brush)}}}));
    CHECK(countModifiedRoomBrushes(map, *room) == 1u);

    SECTION("Keep")
    {
      REQUIRE(rebuildRoom(map, *room, recipe, ModifiedRoomBrushes::Keep).is_success());
      const auto brushes = roomBrushes(*room);
      CHECK(brushes.size() == 7u);
      CHECK(std::ranges::find(brushes, wall) != brushes.end());
    }

    SECTION("Replace")
    {
      REQUIRE(rebuildRoom(map, *room, recipe, ModifiedRoomBrushes::Replace).is_success());
      CHECK(roomBrushes(*room).size() == 6u);
      CHECK(countModifiedRoomBrushes(map, *room) == 0u);
    }
  }

  SECTION("Moved rooms rebuild in place and convert new spaces to local coordinates")
  {
    selectNodes(map, {room});
    REQUIRE(translateSelection(map, {512, 0, 0}));
    deselectAll(map);

    CHECK(countModifiedRoomBrushes(map, *room) == 0u);

    const auto worldSpace = vm::bbox3d{{640, 0, 0}, {768, 128, 128}};
    const auto localSpace = roomLocalBounds(*room, worldSpace).value();
    CHECK(localSpace == vm::bbox3d{{128, 0, 0}, {256, 128, 128}});

    auto extended = recipe;
    extended.spaces.push_back(localSpace);
    REQUIRE(rebuildRoom(map, *room, extended, ModifiedRoomBrushes::Replace).is_success());
    CHECK(room->logicalBounds() == vm::bbox3d{{496, -16, -16}, {784, 144, 144}});

    const auto shells = roomWorldShellBounds(*room).value();
    CHECK(shells.front() == vm::bbox3d{{496, -16, -16}, {656, 144, 144}});
  }

  SECTION("Rooms rotated by an arbitrary angle reject new spaces")
  {
    selectNodes(map, {room});
    REQUIRE(rotateSelection(map, {64, 64, 64}, vm::vec3d{0, 0, 1}, vm::to_radians(45.0)));
    deselectAll(map);

    CHECK(roomLocalBounds(*room, vm::bbox3d{{0, 0, 0}, {64, 64, 64}}).is_error());
  }
}

} // namespace tb::mdl
