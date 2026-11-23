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
#include "mdl/Brush.h"
#include "mdl/BrushNode.h"
#include "mdl/Grid.h"
#include "mdl/LayerNode.h"
#include "mdl/LockState.h"
#include "mdl/Map.h"
#include "mdl/Map_Layers.h"
#include "mdl/Map_NodeLocking.h"
#include "mdl/Map_Selection.h"
#include "mdl/RoomLayout.h"
#include "mdl/VisibilityState.h"
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

  SECTION("Drawing preserves hidden and locked layout layers")
  {
    const auto first = vm::bbox3d{{0, 0, 64}, {256, 256, 192}};
    tool.update(first);
    tool.createBrushes();

    auto* ceilingsLayer = mdl::findLayer(map, mdl::RoomLayoutLayers::Ceilings);
    auto* floorsLayer = mdl::findLayer(map, mdl::RoomLayoutLayers::Floors);
    REQUIRE(ceilingsLayer != nullptr);
    REQUIRE(floorsLayer != nullptr);
    REQUIRE_FALSE(ceilingsLayer->children().empty());

    mdl::hideLayers(map, {ceilingsLayer});
    mdl::lockNodes(map, {floorsLayer});
    REQUIRE_FALSE(ceilingsLayer->visible());
    REQUIRE(floorsLayer->locked());

    const auto second = vm::bbox3d{{256, 64, 64}, {512, 192, 192}};

    // Preview should omit brushes that belong to a hidden layout layer.
    const auto preview = mdl::roomLayoutPreview(map, tool.layoutWith(second));
    REQUIRE(preview.is_success());
    for (const auto& brush : preview.value())
    {
      CHECK(brush.bounds().min.z() < 192.0);
    }

    tool.update(second);
    tool.createBrushes();

    CHECK_FALSE(ceilingsLayer->visible());
    REQUIRE_FALSE(ceilingsLayer->children().empty());
    for (const auto* child : ceilingsLayer->children())
    {
      CHECK(child->visibilityState() == mdl::VisibilityState::Inherited);
      CHECK_FALSE(child->visible());
    }

    CHECK(floorsLayer->locked());
    REQUIRE_FALSE(floorsLayer->children().empty());
    for (const auto* child : floorsLayer->children())
    {
      CHECK(child->lockState() == mdl::LockState::Inherited);
      CHECK(child->locked());
    }
  }

  SECTION("Cancelling a drag adds nothing")
  {
    tool.update(vm::bbox3d{{0, 0, 64}, {256, 256, 192}});
    tool.cancelDrawing();
    tool.createBrushes();
    CHECK(mdl::readRoomLayout(map).value().spaces.empty());
  }

  SECTION("Dollhouse cutaway peels ceilings and camera-facing exterior walls")
  {
    const auto first = vm::bbox3d{{0, 0, 64}, {256, 256, 192}};
    tool.update(first);
    tool.createBrushes();

    const auto* wallsLayer = mdl::findLayer(map, mdl::RoomLayoutLayers::Walls);
    const auto* floorsLayer = mdl::findLayer(map, mdl::RoomLayoutLayers::Floors);
    const auto* ceilingsLayer = mdl::findLayer(map, mdl::RoomLayoutLayers::Ceilings);
    REQUIRE(wallsLayer != nullptr);
    REQUIRE(floorsLayer != nullptr);
    REQUIRE(ceilingsLayer != nullptr);

    const auto findWall = [&](auto predicate) -> const mdl::BrushNode* {
      for (const auto* child : wallsLayer->children())
      {
        if (const auto* brush = dynamic_cast<const mdl::BrushNode*>(child);
            brush && predicate(brush->logicalBounds()))
        {
          return brush;
        }
      }
      return nullptr;
    };

    const auto* southWall = findWall([](const auto& b) { return b.max.y() <= 0.01; });
    const auto* northWall = findWall([](const auto& b) { return b.min.y() >= 255.99; });
    const auto* westWall = findWall([](const auto& b) { return b.max.x() <= 0.01; });
    const auto* eastWall = findWall([](const auto& b) { return b.min.x() >= 255.99; });
    const auto* ceilingBrush =
      dynamic_cast<const mdl::BrushNode*>(ceilingsLayer->children().front());
    const auto* floorBrush =
      dynamic_cast<const mdl::BrushNode*>(floorsLayer->children().front());

    REQUIRE(southWall != nullptr);
    REQUIRE(northWall != nullptr);
    REQUIRE(westWall != nullptr);
    REQUIRE(eastWall != nullptr);
    REQUIRE(ceilingBrush != nullptr);
    REQUIRE(floorBrush != nullptr);

    // Looking from South-West above ceiling: peels ceiling + south & west walls only.
    {
      const auto hidden = mdl::collectDollhouseHiddenBrushes(map, {-200.0, -200.0, 300.0});
      CHECK(hidden.contains(ceilingBrush));
      CHECK(hidden.contains(southWall));
      CHECK(hidden.contains(westWall));
      CHECK_FALSE(hidden.contains(northWall));
      CHECK_FALSE(hidden.contains(eastWall));
      CHECK_FALSE(hidden.contains(floorBrush));
    }

    // Looking from North-East above ceiling: peels ceiling + north & east walls only.
    {
      const auto hidden = mdl::collectDollhouseHiddenBrushes(map, {500.0, 500.0, 300.0});
      CHECK(hidden.contains(ceilingBrush));
      CHECK(hidden.contains(northWall));
      CHECK(hidden.contains(eastWall));
      CHECK_FALSE(hidden.contains(southWall));
      CHECK_FALSE(hidden.contains(westWall));
      CHECK_FALSE(hidden.contains(floorBrush));
    }

    // Looking horizontally from South below ceiling: peels south wall only, keeps ceiling.
    {
      const auto hidden = mdl::collectDollhouseHiddenBrushes(map, {128.0, -200.0, 120.0});
      CHECK_FALSE(hidden.contains(ceilingBrush));
      CHECK(hidden.contains(southWall));
      CHECK_FALSE(hidden.contains(northWall));
      CHECK_FALSE(hidden.contains(westWall));
      CHECK_FALSE(hidden.contains(eastWall));
    }

    // Directly overhead: peels ceiling only, keeps all four walls.
    {
      const auto hidden = mdl::collectDollhouseHiddenBrushes(map, {128.0, 128.0, 400.0});
      CHECK(hidden.contains(ceilingBrush));
      CHECK_FALSE(hidden.contains(southWall));
      CHECK_FALSE(hidden.contains(northWall));
      CHECK_FALSE(hidden.contains(westWall));
      CHECK_FALSE(hidden.contains(eastWall));
    }

    // Camera inside the room: peels nothing.
    {
      const auto hidden = mdl::collectDollhouseHiddenBrushes(map, {128.0, 128.0, 128.0});
      CHECK(hidden.empty());
    }

    // Directly selecting a wall keeps it visible even from the camera-facing side.
    {
      mdl::selectNodes(map, {const_cast<mdl::BrushNode*>(southWall)});
      const auto hidden = mdl::collectDollhouseHiddenBrushes(map, {-200.0, -200.0, 300.0});
      CHECK_FALSE(hidden.contains(southWall));
      CHECK(hidden.contains(westWall));
      CHECK(hidden.contains(ceilingBrush));
      mdl::deselectAll(map);
    }

    // Touching room adds an interior partition wall which stays visible.
    const auto second = vm::bbox3d{{256, 0, 64}, {512, 256, 192}};
    tool.update(second);
    tool.createBrushes();

    const auto hidden = mdl::collectDollhouseHiddenBrushes(map, {-200.0, -200.0, 300.0});
    auto foundPartitionWall = false;
    for (const auto* child : wallsLayer->children())
    {
      if (const auto* brush = dynamic_cast<const mdl::BrushNode*>(child))
      {
        const auto& b = brush->logicalBounds();
        if (b.min.x() >= 255.99 && b.max.x() <= 272.01 && b.min.y() >= -0.01 && b.max.y() <= 256.01)
        {
          foundPartitionWall = true;
          CHECK_FALSE(hidden.contains(brush));
        }
      }
    }
    CHECK(foundPartitionWall);
  }

  setPref(Preferences::RoomCreatorFloorLevel, 0);
}

} // namespace tb::ui
