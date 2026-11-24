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
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Picking.h"
#include "mdl/Map_Selection.h"
#include "mdl/PickResult.h"
#include "mdl/WorldNode.h"
#include "ui/DecalStampTool.h"
#include "unity/UnityDecals.h"

#include "kdl/result.h"

#include "vm/approx.h"
#include "vm/vec_io.h" // IWYU pragma: keep

#include <set>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

mdl::EntityNode* addDecal(
  mdl::Map& map,
  const std::string& angles,
  const std::string& size,
  const std::string& origin = "0 0 64")
{
  auto* node = new mdl::EntityNode{mdl::Entity{{
    {mdl::EntityPropertyKeys::Classname, unity::DecalClassName},
    {mdl::EntityPropertyKeys::Origin, origin},
    {mdl::EntityPropertyKeys::Angles, angles},
    {unity::DecalPropertyKeys::Decal, "markings/stripe"},
    {unity::DecalPropertyKeys::Size, size},
  }}};
  REQUIRE_FALSE(mdl::addNodes(map, {{mdl::parentForNodes(map), {node}}}).empty());
  return node;
}

std::string propertyOf(const mdl::EntityNode& node, const std::string& key)
{
  const auto* value = node.entity().property(key);
  return value ? *value : std::string{};
}

} // namespace

TEST_CASE("Decal placement on a wall", "[decal-stamp]")
{
  // a wall facing -X
  const auto placement = decalPlacement({0, 0, 64}, {-1, 0, 0}, {1, 0, 0}, 0.0);
  CHECK(placement.origin == vm::vec3d{0, 0, 64});
  CHECK(placement.forward == vm::approx{vm::vec3d{1, 0, 0}});
  CHECK(placement.up == vm::approx{vm::vec3d{0, 0, 1}});

  SECTION("roll turns clockwise as seen looking at the wall")
  {
    // looking along +X with Z up, the viewer's right is -Y
    const auto rolled = decalPlacement({0, 0, 64}, {-1, 0, 0}, {1, 0, 0}, 90.0);
    CHECK(rolled.up == vm::approx{vm::vec3d{0, -1, 0}});
  }
}

TEST_CASE("Decal placement on a floor follows the view", "[decal-stamp]")
{
  const auto placement = decalPlacement({0, 0, 0}, {0, 0, 1}, {0, 1, -1}, 0.0);
  CHECK(placement.forward == vm::approx{vm::vec3d{0, 0, -1}});
  CHECK(placement.up == vm::approx{vm::vec3d{0, 1, 0}});

  SECTION("looking straight down falls back to a fixed axis")
  {
    const auto straightDown = decalPlacement({0, 0, 0}, {0, 0, 1}, {0, 0, -1}, 0.0);
    CHECK(straightDown.up == vm::approx{vm::vec3d{1, 0, 0}});
  }

  SECTION("an oblique view snaps to the closest world axis")
  {
    const auto oblique = decalPlacement({0, 0, 0}, {0, 0, 1}, {-0.3, 0.9, -1}, 0.0);
    CHECK(oblique.up == vm::approx{vm::vec3d{0, 1, 0}});
  }
}

TEST_CASE("Decal roll is measured from the reference up", "[decal-stamp]")
{
  const auto forward = vm::vec3d{1, 0, 0};
  const auto reference = vm::vec3d{0, 0, 1};
  CHECK(decalRoll(forward, reference, reference) == vm::approx{0.0});
  // clockwise looking along +X with Z up turns up towards -Y
  CHECK(decalRoll(forward, {0, -1, 0}, reference) == vm::approx{90.0});
  CHECK(decalRoll(forward, {0, 1, 0}, reference) == vm::approx{-90.0});

  // a placement's roll reads back
  const auto placement = decalPlacement({0, 0, 0}, {-1, 0, 0}, {1, 0, 0}, 30.0);
  CHECK(
    decalRoll(placement.forward, placement.up, decalReferenceUp({-1, 0, 0}, {1, 0, 0}))
    == vm::approx{30.0});
}

TEST_CASE("Moved decals keep their turn", "[decal-stamp]")
{
  SECTION("along the same wall")
  {
    const auto up = vm::normalize(vm::vec3d{0, -1, 1});
    CHECK(movedDecalUp(up, {-1, 0, 0}, {1, 0, 0}) == vm::approx{up});
  }

  SECTION("around a corner onto the next wall")
  {
    // up tilted towards the corner, which the new wall (facing -Y) continues along +X
    const auto up = vm::normalize(vm::vec3d{0, 1, 1});
    CHECK(movedDecalUp(up, {0, -1, 0}, {0, 1, 0}) == vm::approx{vm::vec3d{0, 0, 1}});
  }

  SECTION("onto a floor where the old up points straight into it")
  {
    CHECK(
      movedDecalUp({0, 0, 1}, {0, 0, 1}, {0, 1, -1})
      == vm::approx{decalReferenceUp({0, 0, 1}, {0, 1, -1})});
  }
}

TEST_CASE("Constrained drags keep to one screen axis", "[decal-stamp]")
{
  CHECK(constrainDrag({10, 3}) == vm::vec2d{10, 0});
  CHECK(constrainDrag({-2, -7}) == vm::vec2d{0, -7});
}

TEST_CASE("Dragging scales decals", "[decal-stamp]")
{
  const auto start = vm::vec2d{32, 16};

  SECTION("uniformly, growing to the right and up")
  {
    const auto right = draggedDecalSize(start, {100, 0}, false);
    CHECK(right.x() > start.x());
    CHECK(right.x() / right.y() == vm::approx{2.0});
    CHECK(draggedDecalSize(start, {0, -100}, false) == vm::approx{right});
    CHECK(draggedDecalSize(start, {-100, 0}, false).x() < start.x());
  }

  SECTION("stretching width and height separately")
  {
    const auto wider = draggedDecalSize(start, {100, 0}, true);
    CHECK(wider.x() > start.x());
    CHECK(wider.y() == vm::approx{start.y()});

    const auto taller = draggedDecalSize(start, {0, -100}, true);
    CHECK(taller.x() == vm::approx{start.x()});
    CHECK(taller.y() > start.y());
  }

  SECTION("never to nothing")
  {
    const auto tiny = draggedDecalSize(start, {-5000, 5000}, false);
    CHECK(tiny.x() >= 1.0);
    CHECK(tiny.y() >= 1.0);
  }
}

TEST_CASE("Rotation snaps to steps", "[decal-stamp]")
{
  CHECK(snapRoll(30.0, 45.0) == vm::approx{45.0});
  CHECK(snapRoll(20.0, 45.0) == vm::approx{0.0});
  CHECK(snapRoll(-100.0, 45.0) == vm::approx{-90.0});
}

TEST_CASE("Rotating selected decals", "[decal-stamp]")
{
  auto fixture = mdl::MapFixture{};
  fixture.create();
  auto& map = fixture.map();

  auto* decal = addDecal(map, "0 0 0", "32 16");
  mdl::selectNodes(map, {decal});

  CHECK(rotateSelectedDecals(map, 15.0));
  CHECK(propertyOf(*decal, mdl::EntityPropertyKeys::Angles) == "0 0 15");

  CHECK(rotateSelectedDecals(map, -30.0));
  CHECK(propertyOf(*decal, mdl::EntityPropertyKeys::Angles) == "0 0 -15");

  map.undoCommand();
  CHECK(propertyOf(*decal, mdl::EntityPropertyKeys::Angles) == "0 0 15");

  SECTION("nothing to rotate without a selected decal")
  {
    mdl::deselectAll(map);
    CHECK_FALSE(rotateSelectedDecals(map, 15.0));
  }
}

TEST_CASE("Scaling selected decals", "[decal-stamp]")
{
  auto fixture = mdl::MapFixture{};
  fixture.create();
  auto& map = fixture.map();

  auto* decal = addDecal(map, "0 0 0", "32 16");
  mdl::selectNodes(map, {decal});

  CHECK(scaleSelectedDecals(map, 2.0));
  CHECK(propertyOf(*decal, unity::DecalPropertyKeys::Size) == "64 32");

  map.undoCommand();
  CHECK(propertyOf(*decal, unity::DecalPropertyKeys::Size) == "32 16");
}

TEST_CASE("The stamp remembers its transform offset", "[decal-stamp]")
{
  auto fixture = mdl::MapFixture{};
  fixture.create();
  auto tool = DecalStampTool{fixture.map()};

  CHECK_FALSE(tool.hasTransformOffset());

  tool.setDecal("markings/stripe");
  tool.setRoll(30.0);
  tool.setStretch({2.0, 1.5});
  CHECK(tool.hasTransformOffset());

  SECTION("choosing another decal keeps the offset")
  {
    tool.setDecal("markings/arrow");
    CHECK(tool.roll() == 30.0);
    CHECK(tool.stretch() == vm::vec2d{2.0, 1.5});
  }

  SECTION("resetting drops the turn and the stretch")
  {
    tool.resetTransformOffset();
    CHECK(tool.roll() == 0.0);
    CHECK(tool.stretch() == vm::vec2d{1.0, 1.0});
    CHECK_FALSE(tool.hasTransformOffset());
  }
}

TEST_CASE("Clicks cycle through stacked decals", "[decal-stamp]")
{
  auto fixture = mdl::MapFixture{};
  fixture.create();
  auto& map = fixture.map();

  // a wall whose west face is at x = 0, with three decals on it and one behind it
  auto builder = mdl::BrushBuilder{map.world()->mapFormat(), map.worldBounds()};
  auto* wall = new mdl::BrushNode{
    builder.createCuboid(vm::bbox3d{{0, -64, 0}, {32, 64, 128}}, "material")
    | kdl::value()};
  REQUIRE_FALSE(mdl::addNodes(map, {{mdl::parentForNodes(map), {wall}}}).empty());

  auto* a = addDecal(map, "0 0 0", "32 16");
  auto* b = addDecal(map, "0 0 0", "64 64");
  auto* c = addDecal(map, "0 0 0", "16 16", "0 4 64");
  auto* behind = addDecal(map, "0 0 0", "32 16", "40 0 64");

  auto pickResult = mdl::PickResult::byDistance();
  mdl::pick(map, vm::ray3d{{-100, 0, 64}, {1, 0, 0}}, pickResult);
  const auto stack = decalsUnderCursor(pickResult);

  SECTION("decals behind the wall are not in the stack")
  {
    CHECK(
      std::set<mdl::EntityNode*>{stack.begin(), stack.end()}
      == std::set<mdl::EntityNode*>{a, b, c});
    CHECK(std::ranges::find(stack, behind) == stack.end());
  }

  SECTION("the first click selects the nearest")
  {
    CHECK(nextDecalInStack(stack, {}) == stack.front());
    CHECK(nextDecalInStack(stack, {behind}) == stack.front());
  }

  SECTION("repeated clicks visit every decal and come back")
  {
    for (auto* start : stack)
    {
      auto visited = std::set<mdl::EntityNode*>{};
      auto* current = start;
      for (size_t i = 0; i < stack.size(); ++i)
      {
        current = nextDecalInStack(stack, {current});
        visited.insert(current);
      }
      CHECK(current == start);
      CHECK(visited.size() == stack.size());
    }
  }

  SECTION("a stack that a selected decal isn't in starts again")
  {
    CHECK(nextDecalInStack({b, c}, {a}) == b);
  }

  SECTION("an empty stack selects nothing")
  {
    CHECK(nextDecalInStack({}, {a}) == nullptr);
  }
}

} // namespace tb::ui
