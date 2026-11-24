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

#include "unity/UnityPlay.h"

#include "vm/vec.h"
#include "vm/vec_io.h" // IWYU pragma: keep

#include <catch2/catch_test_macros.hpp>

namespace tb::unity
{
namespace
{

bool near(const vm::vec3f& lhs, const vm::vec3f& rhs)
{
  return vm::length(lhs - rhs) < 0.0001f;
}

// looking down at the floor, towards +Y
const auto Camera = LiveLinkCameraPose{
  {0, -128, 128},
  vm::normalize(vm::vec3f{0, 1, -1}),
  vm::normalize(vm::vec3f{0, 1, 1}),
};

} // namespace

TEST_CASE("Playing from the camera puts the eyes at the camera", "[unity-play]")
{
  const auto start = playStartAtCamera(Camera);
  CHECK(start.anchor == LiveLinkPlayAnchor::Eye);
  CHECK(start.pose == Camera);
}

TEST_CASE("Playing on a floor stands on the point", "[unity-play]")
{
  const auto start = playStartOnSurface({32, 64, 0}, {0, 0, 1}, Camera);
  CHECK(start.anchor == LiveLinkPlayAnchor::Feet);
  CHECK(near(start.pose.position, {32, 64, 0}));
  // facing the camera's way along the floor, looking level
  CHECK(near(start.pose.direction, {0, 1, 0}));
  CHECK(near(start.pose.up, {0, 0, 1}));

  // a gentle ramp is still a floor
  CHECK(near(
    playStartOnSurface({0, 0, 0}, vm::normalize(vm::vec3f{1, 0, 2}), Camera)
      .pose.position,
    {0, 0, 0}));
}

TEST_CASE("Playing on a wall stands out from it", "[unity-play]")
{
  const auto start = playStartOnSurface({0, 256, 48}, {0, -1, 0}, Camera);
  CHECK(start.anchor == LiveLinkPlayAnchor::Feet);
  CHECK(near(start.pose.position, {0, 256 - (PlayStartRadius + PlayStartClearance), 48}));
  CHECK(near(start.pose.direction, {0, 1, 0}));
}

TEST_CASE("Playing under a ceiling hangs below it", "[unity-play]")
{
  const auto start = playStartOnSurface({0, 0, 256}, {0, 0, -1}, Camera);
  CHECK(near(start.pose.position, {0, 0, 256 - (PlayStartHeight + PlayStartClearance)}));
}

TEST_CASE(
  "Playing faces the top of the screen when looking straight down", "[unity-play]")
{
  const auto down = LiveLinkCameraPose{{0, 0, 256}, {0, 0, -1}, {-1, 0, 0}};
  CHECK(near(playStartOnSurface({0, 0, 0}, {0, 0, 1}, down).pose.direction, {-1, 0, 0}));

  const auto up = LiveLinkCameraPose{{0, 0, 0}, {0, 0, 1}, {0, 1, 0}};
  CHECK(near(playStartOnSurface({0, 0, 0}, {0, 0, 1}, up).pose.direction, {0, -1, 0}));
}

} // namespace tb::unity
