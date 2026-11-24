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
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "ui/DecalStampTool.h"
#include "unity/UnityDecals.h"

#include "vm/approx.h"
#include "vm/vec_io.h" // IWYU pragma: keep

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

mdl::EntityNode* addDecal(
  mdl::Map& map, const std::string& angles, const std::string& size)
{
  auto* node = new mdl::EntityNode{mdl::Entity{{
    {mdl::EntityPropertyKeys::Classname, unity::DecalClassName},
    {mdl::EntityPropertyKeys::Origin, "0 0 64"},
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

} // namespace tb::ui
