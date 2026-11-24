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

#include "mdl/Brush.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/MapFormat.h"
#include "unity/UnityDecals.h"

#include "kdl/result.h"

#include "vm/approx.h"
#include "vm/mat_ext.h"
#include "vm/plane.h"
#include "vm/scalar.h"
#include "vm/vec_io.h" // IWYU pragma: keep

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

namespace tb::unity
{
namespace
{

mdl::Brush cuboid(const vm::bbox3d& bounds)
{
  const auto builder = mdl::BrushBuilder{mdl::MapFormat::Standard, vm::bbox3d{8192.0}};
  return builder.createCuboid(bounds, "material") | kdl::value();
}

// A wall whose front face is the plane x = 0, facing -X.
mdl::Brush wall()
{
  return cuboid(vm::bbox3d{{0, -256, -256}, {16, 256, 256}});
}

DecalBox wallDecal(const vm::vec3d& origin, const vm::vec2d& size)
{
  // angles 0 0 0: forward +X into the wall, u axis -Y, v axis +Z
  return decalBox(origin, vm::mat4x4d::identity(), size);
}

bool containsPoint(const std::vector<vm::vec2d>& coords, const vm::vec2d& point)
{
  return std::ranges::any_of(
    coords, [&](const auto& c) { return c == vm::approx{point}; });
}

} // namespace

TEST_CASE("Decal box axes follow the entity rotation", "[unity-decals]")
{
  const auto box = wallDecal({0, 0, 64}, {32, 16});
  CHECK(box.forward == vm::approx{vm::vec3d{1, 0, 0}});
  CHECK(box.uAxis == vm::approx{vm::vec3d{0, -1, 0}});
  CHECK(box.vAxis == vm::approx{vm::vec3d{0, 0, 1}});

  SECTION("pitch 90 projects down")
  {
    const auto floor =
      decalBox({0, 0, 0}, vm::rotation_matrix(0.0, vm::to_radians(90.0), 0.0), {32, 32});
    CHECK(floor.forward == vm::approx{vm::vec3d{0, 0, -1}});
  }
}

TEST_CASE("Decal entity reads its properties", "[unity-decals]")
{
  const auto entity = mdl::Entity{{
    {mdl::EntityPropertyKeys::Classname, DecalClassName},
    {mdl::EntityPropertyKeys::Origin, "8 16 32"},
    {DecalPropertyKeys::Decal, "markings/stripe"},
    {DecalPropertyKeys::Size, "64 32"},
    {DecalPropertyKeys::Depth, "8"},
    {DecalPropertyKeys::Flip, "1 0"},
  }};

  CHECK(isDecalEntity(entity));
  const auto box = decalBox(entity, {16, 16});
  CHECK(box.origin == vm::vec3d{8, 16, 32});
  CHECK(box.size == vm::vec2d{64, 32});
  CHECK(box.depth == 8.0);
  CHECK(box.flipU);
  CHECK_FALSE(box.flipV);

  SECTION("missing size uses the default")
  {
    const auto bare = mdl::Entity{{
      {mdl::EntityPropertyKeys::Classname, DecalClassName},
    }};
    CHECK(decalBox(bare, {16, 8}).size == vm::vec2d{16, 8});
    CHECK(decalBox(bare, {16, 8}).depth == DefaultDecalDepth);
  }
}

TEST_CASE("Decal bounds cover the rotated box", "[unity-decals]")
{
  const auto box = wallDecal({0, 0, 64}, {32, 16});
  const auto bounds = decalBounds(box);
  CHECK(bounds.min == vm::approx{vm::vec3d{-8, -16, 56}});
  CHECK(bounds.max == vm::approx{vm::vec3d{8, 16, 72}});
}

TEST_CASE("Decal projects onto the face it covers", "[unity-decals]")
{
  const auto polygons = projectDecal(wallDecal({0, 0, 64}, {32, 16}), wall());

  // only the front face faces the decal; the side faces are clipped away or turned away
  REQUIRE(polygons.size() == 1u);
  const auto& polygon = polygons.front();
  CHECK(polygon.normal == vm::approx{vm::vec3d{-1, 0, 0}});
  CHECK(polygon.positions.size() == 4u);

  // wound so that the renderer draws it front facing, like upstream decals
  const auto plane =
    vm::from_points(polygon.positions[0], polygon.positions[1], polygon.positions[2]);
  REQUIRE(plane);
  CHECK(plane->normal == vm::approx{polygon.normal});
  for (const auto& position : polygon.positions)
  {
    CHECK(position.x() == vm::approx{0.0});
  }

  // the corners map to the corners of the decal: u runs along -Y, v along +Z
  CHECK(containsPoint(polygon.decalCoords, {0, 0}));
  CHECK(containsPoint(polygon.decalCoords, {1, 1}));
  const auto bottomLeft = std::ranges::find_if(polygon.positions, [](const auto& p) {
    return p == vm::approx{vm::vec3d{0, 16, 56}};
  });
  REQUIRE(bottomLeft != polygon.positions.end());
  const auto index = size_t(std::distance(polygon.positions.begin(), bottomLeft));
  CHECK(polygon.decalCoords[index] == vm::approx{vm::vec2d{0, 0}});
}

TEST_CASE("Decal is clipped at the edge of a face", "[unity-decals]")
{
  // half the decal hangs past the wall's top edge at z = 256
  const auto polygons = projectDecal(wallDecal({0, 0, 256}, {32, 32}), wall());
  REQUIRE(polygons.size() == 1u);
  for (const auto& coords : polygons.front().decalCoords)
  {
    CHECK(coords.y() <= 0.5 + 0.0001);
  }
}

TEST_CASE("Decal wraps around a convex corner", "[unity-decals]")
{
  // a block whose front face is x = 0 and whose top face is z = 0; the decal looks along
  // +X and -Z at once (pitch 45), so both faces face it at 45 degrees
  const auto block = cuboid(vm::bbox3d{{0, -64, -64}, {64, 64, 0}});
  auto box = decalBox(
    {0, 0, 0}, vm::rotation_matrix(0.0, vm::to_radians(45.0), 0.0), {32, 32}, 32);

  SECTION("both faces get part of it")
  {
    const auto polygons = projectDecal(box, block);
    CHECK(polygons.size() == 2u);
  }

  SECTION("faces turned past the fade angle get none")
  {
    box.angleFadeEnd = 30.0;
    CHECK(projectDecal(box, block).empty());
  }
}

TEST_CASE("Decal ignores faces outside its depth", "[unity-decals]")
{
  // the wall's front face is 32 units behind the box, which is 16 deep
  const auto polygons = projectDecal(wallDecal({-32, 0, 64}, {32, 16}), wall());
  CHECK(polygons.empty());
}

TEST_CASE("Flipped decals mirror their coordinates", "[unity-decals]")
{
  auto box = wallDecal({0, 0, 64}, {32, 16});
  box.flipU = true;
  const auto polygons = projectDecal(box, wall());
  REQUIRE(polygons.size() == 1u);

  const auto& polygon = polygons.front();
  const auto bottomLeft = std::ranges::find_if(polygon.positions, [](const auto& p) {
    return p == vm::approx{vm::vec3d{0, 16, 56}};
  });
  REQUIRE(bottomLeft != polygon.positions.end());
  const auto index = size_t(std::distance(polygon.positions.begin(), bottomLeft));
  CHECK(polygon.decalCoords[index] == vm::approx{vm::vec2d{1, 0}});
}

TEST_CASE("Atlas texture coordinates flip v to the image origin", "[unity-decals]")
{
  // rect origin at the bottom left in Unity, image origin at the top left
  const auto rect = vm::vec4d{0.25, 0.5, 0.5, 0.25};
  CHECK(atlasTextureCoords({0, 0}, rect) == vm::approx{vm::vec2f{0.25f, 0.5f}});
  CHECK(atlasTextureCoords({1, 1}, rect) == vm::approx{vm::vec2f{0.75f, 0.25f}});
}

TEST_CASE("Decal angles round trip through the entity rotation", "[unity-decals]")
{
  const auto check = [](const vm::vec3d& forward, const vm::vec3d& up) {
    const auto angles = decalAngles(forward, up);
    const auto rotation = vm::rotation_matrix(
      vm::to_radians(angles.z()), vm::to_radians(angles.x()), vm::to_radians(angles.y()));
    const auto box = decalBox({0, 0, 0}, rotation, {16, 16});
    CHECK(box.forward == vm::approx{vm::normalize(forward)});
    CHECK(box.vAxis == vm::approx{vm::normalize(up)});
  };

  check({1, 0, 0}, {0, 0, 1});
  check({0, 1, 0}, {0, 0, 1});
  check({0, 0, -1}, {1, 0, 0});
  check({0, 0, -1}, {0, 1, 0});
  check({0, 0, 1}, {1, 0, 0});
  check({1, 0, 0}, {0, -1, 0});
  check({1, 1, 0}, {0, 0, 1});
  check({1, 0, -1}, {1, 0, 1});
}

TEST_CASE("Decal entity bounds cover the projection box", "[unity-decals]")
{
  const auto node = mdl::EntityNode{mdl::Entity{{
    {mdl::EntityPropertyKeys::Classname, DecalClassName},
    {mdl::EntityPropertyKeys::Origin, "0 0 64"},
    {DecalPropertyKeys::Size, "64 32"},
  }}};
  CHECK(node.logicalBounds().contains(vm::bbox3d{{-8, -32, 48}, {8, 32, 80}}));
}

} // namespace tb::unity

namespace tb::unity
{

TEST_CASE("Decal appearance reads its properties", "[unity-decals]")
{
  CHECK(
    decalAppearance(mdl::Entity{{{mdl::EntityPropertyKeys::Classname, DecalClassName}}})
    == DecalAppearance{});

  const auto appearance = decalAppearance(
    mdl::Entity{{
      {mdl::EntityPropertyKeys::Classname, DecalClassName},
      {DecalPropertyKeys::Tint, "255 0 51"},
      {DecalPropertyKeys::Brightness, "0.5"},
      {DecalPropertyKeys::Invert, "1"},
    }});
  CHECK(appearance.tint == vm::approx{vm::vec3f{1.0f, 0.0f, 0.2f}});
  CHECK(appearance.brightness == 0.5f);
  CHECK(appearance.invert);
}

TEST_CASE("Decal appearance changes colors but not alpha", "[unity-decals]")
{
  auto appearance = DecalAppearance{};
  appearance.tint = {1.0f, 0.5f, 0.0f};

  SECTION("tint in RGBA order")
  {
    unsigned char pixel[] = {200, 200, 200, 128};
    applyDecalAppearance(pixel, 4, false, appearance);
    CHECK(pixel[0] == 200);
    CHECK(pixel[1] == 100);
    CHECK(pixel[2] == 0);
    CHECK(pixel[3] == 128);
  }

  SECTION("tint in BGRA order")
  {
    unsigned char pixel[] = {200, 200, 200, 128};
    applyDecalAppearance(pixel, 4, true, appearance);
    CHECK(pixel[2] == 200); // red
    CHECK(pixel[1] == 100);
    CHECK(pixel[0] == 0); // blue
    CHECK(pixel[3] == 128);
  }

  SECTION("invert, then brightness, clamped")
  {
    auto inverted = DecalAppearance{};
    inverted.invert = true;
    inverted.brightness = 2.0f;
    unsigned char pixel[] = {255, 200, 0, 255};
    applyDecalAppearance(pixel, 4, false, inverted);
    CHECK(pixel[0] == 0);
    CHECK(pixel[1] == 110);
    CHECK(pixel[2] == 255);
    CHECK(pixel[3] == 255);
  }
}

} // namespace tb::unity
