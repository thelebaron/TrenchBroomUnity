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
#include "mdl/Grid.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/RoomLayout.h"
#include "ui/RoomDrawTool.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("RoomDrawTool")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.map();
  fixture.create();

  setPref(Preferences::RoomCreatorFloorLevel, 64);
  setPref(Preferences::RoomCreatorWallHeight, 128);

  auto tool = RoomDrawTool{map};
  const auto& grid = map.grid();

  SECTION("A drag spans a grid-snapped space on the floor level")
  {
    const auto space = tool.spaceFor({10, 10, 64}, {100, 70, 64});
    REQUIRE(space);
    const auto min = grid.snapDown(vm::vec3d{10, 10, 64});
    const auto max = grid.snapUp(vm::vec3d{100, 70, 64});
    CHECK(space->min == vm::vec3d{min.x(), min.y(), 64});
    CHECK(space->max == vm::vec3d{max.x(), max.y(), 192});
  }

  SECTION("A drag without area spans nothing")
  {
    CHECK_FALSE(tool.spaceFor({32, 32, 64}, {32, 128, 64}));
  }

  SECTION("Drawing adds spaces to the layout with auto doorways")
  {
    const auto first = vm::bbox3d{{0, 0, 64}, {256, 256, 192}};
    tool.update(first);
    tool.createBrushes();

    auto layout = mdl::readRoomLayout(map).value();
    CHECK(layout.spaces == std::vector{first});
    CHECK(layout.autoDoors);
    CHECK(mdl::findLayer(map, mdl::RoomLayoutLayers::Walls) != nullptr);

    const auto second = vm::bbox3d{{256, 64, 64}, {512, 192, 192}};
    tool.update(second);
    tool.createBrushes();

    layout = mdl::readRoomLayout(map).value();
    CHECK(layout.spaces == std::vector{first, second});

    map.undoCommand();
    CHECK(mdl::readRoomLayout(map).value().spaces == std::vector{first});
  }

  SECTION("Cancelling a drag adds nothing")
  {
    tool.update(vm::bbox3d{{0, 0, 64}, {256, 256, 192}});
    tool.cancelDrawing();
    tool.createBrushes();
    CHECK(mdl::readRoomLayout(map).value().spaces.empty());
  }

  setPref(Preferences::RoomCreatorFloorLevel, 0);
}

} // namespace tb::ui
