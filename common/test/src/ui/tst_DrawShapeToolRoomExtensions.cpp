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
#include "mdl/Group.h"
#include "mdl/GroupNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/RoomGenerator.h"
#include "mdl/Selection.h"
#include "ui/DrawShapeToolExtension.h"
#include "ui/DrawShapeToolExtensions.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("DrawShapeToolRoomExtension")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.map();
  fixture.create();

  auto parameters = ShapeParameters{};
  auto extension = DrawShapeToolRoomExtension{map};
  const auto interior = vm::bbox3d{{0, 0, 0}, {128, 128, 128}};

  SECTION("Preview shows the room shell")
  {
    CHECK(extension.createBrushes(interior, parameters).value().size() == 6u);
    CHECK(extension.createBrushes(vm::bbox3d{}, parameters).value().empty());
  }

  SECTION("Committing creates a selected room")
  {
    CHECK(extension.commitShape(interior, parameters));

    const auto rooms = mdl::collectRooms(map);
    REQUIRE(rooms.size() == 1u);
    CHECK(rooms.front()->selected());
    CHECK(
      mdl::readRoomRecipe(rooms.front()->group()).value().spaces
      == std::vector{interior});
  }

  SECTION("With a room selected, drawing extends it")
  {
    REQUIRE(extension.commitShape(interior, parameters));
    auto* room = mdl::collectRooms(map).front();

    const auto annex = vm::bbox3d{{128, 0, 0}, {256, 128, 128}};
    // The preview shows the merged room, which is still a single box.
    CHECK(extension.createBrushes(annex, parameters).value().size() == 6u);

    CHECK(extension.commitShape(annex, parameters));
    CHECK(mdl::collectRooms(map).size() == 1u);
    CHECK(mdl::readRoomRecipe(room->group()).value().spaces.size() == 2u);
  }

  SECTION("After deselecting, drawing creates another room")
  {
    REQUIRE(extension.commitShape(interior, parameters));
    mdl::deselectAll(map);

    CHECK(extension.commitShape(vm::bbox3d{{512, 0, 0}, {640, 128, 128}}, parameters));
    CHECK(mdl::collectRooms(map).size() == 2u);
  }
}

TEST_CASE("DrawShapeToolDoorwayExtension")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.map();
  fixture.create();

  auto parameters = ShapeParameters{};
  auto roomExtension = DrawShapeToolRoomExtension{map};
  auto doorwayExtension = DrawShapeToolDoorwayExtension{map};

  REQUIRE(roomExtension.commitShape(vm::bbox3d{{0, 0, 0}, {256, 256, 128}}, parameters));
  auto* room = mdl::collectRooms(map).front();
  mdl::deselectAll(map);

  SECTION("A box through a wall becomes a doorway")
  {
    const auto door = vm::bbox3d{{96, -32, 0}, {160, 32, 112}};
    CHECK(doorwayExtension.createBrushes(door, parameters).value().size() == 1u);
    CHECK(doorwayExtension.commitShape(door, parameters));
    CHECK(mdl::readRoomRecipe(room->group()).value().doors == std::vector{door});
  }

  SECTION("A box away from all rooms changes nothing")
  {
    CHECK(doorwayExtension.commitShape(
      vm::bbox3d{{1024, 1024, 0}, {1088, 1088, 112}}, parameters));
    CHECK(mdl::readRoomRecipe(room->group()).value().doors.empty());
  }
}

} // namespace tb::ui
