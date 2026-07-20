/*
 Copyright (C) 2026 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#include "MapFixture.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/RoomGenerator.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::mdl
{
namespace
{
BrushNode* addBox(Map& map, const vm::bbox3d& bounds)
{
  auto result = BrushBuilder{map.world()->mapFormat(), map.worldBounds()}.createCuboid(bounds, "wall");
  REQUIRE_FALSE(result.is_error());
  auto* node = new BrushNode{std::move(result.value())};
  addNodes(map, {{parentForNodes(map), {node}}});
  return node;
}

const LayerNode* layerFor(const BrushNode& node)
{
  return dynamic_cast<const LayerNode*>(node.parent());
}
} // namespace

TEST_CASE("RoomGenerator")
{
  auto fixture = MapFixture{};
  fixture.create();
  auto& map = fixture.map();

  SECTION("box replaces source and creates layered shell")
  {
    auto* source = addBox(map, vm::bbox3d{vm::vec3d{0, 0, 0}, vm::vec3d{128, 128, 128}});
    const auto settings = RoomGenerationSettings{
      RoomGenerationMode::Box, 16, 128, true, true, "wall", "ceiling", "floor"};
    const auto result = generateRooms(map, settings, {{source->brush().bounds(), source}}, {});

    REQUIRE_FALSE(result.is_error());
    CHECK(result.value().generatedRooms == 1u);
    CHECK(result.value().generatedNodes.size() == 6u);
    CHECK(layerFor(*result.value().generatedNodes.front())->name() == "walls");
  }

  SECTION("floor keeps source as floor and uses configured height")
  {
    auto* source = addBox(map, vm::bbox3d{vm::vec3d{0, 0, 0}, vm::vec3d{128, 128, 16}});
    const auto settings = RoomGenerationSettings{
      RoomGenerationMode::Floor, 16, 128, true, false, "wall", "ceiling", "floor"};
    const auto result = generateRooms(map, settings, {{source->brush().bounds(), source}}, {});

    REQUIRE_FALSE(result.is_error());
    CHECK(result.value().generatedNodes.size() == 5u);
    REQUIRE(dynamic_cast<const LayerNode*>(source->parent()) != nullptr);
    CHECK(dynamic_cast<const LayerNode*>(source->parent())->name() == "floors");
    CHECK(result.value().generatedNodes.front()->brush().bounds().max.z() == 128.0);
  }
}

} // namespace tb::mdl
