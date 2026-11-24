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
#include "mdl/EditorContext.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "unity/UnitySceneCamera.h"

#include "vm/vec.h"
#include "vm/vec_io.h" // IWYU pragma: keep

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tb::unity
{
namespace
{

/**
 * A camera pose looking along the given direction with no roll, as TrenchBroom's 3D
 * camera keeps it.
 */
LiveLinkCameraPose poseLookingAt(const vm::vec3f& position, const vm::vec3f& direction)
{
  const auto d = vm::normalize(direction);
  const auto right = vm::normalize(vm::cross(d, vm::vec3f{0, 0, 1}));
  return {position, d, vm::cross(right, d)};
}

mdl::EntityNode* addEntity(mdl::Map& map, const LiveLinkCameraPose& pose)
{
  auto* node = new mdl::EntityNode{mdl::Entity{sceneCameraProperties(pose)}};
  REQUIRE_FALSE(mdl::addNodes(map, {{mdl::parentForNodes(map), {node}}}).empty());
  return node;
}

void checkPose(const LiveLinkCameraPose& actual, const LiveLinkCameraPose& expected)
{
  CHECK(vm::length(actual.position - expected.position) < 0.001f);
  CHECK(vm::length(actual.direction - expected.direction) < 0.0001f);
  CHECK(vm::length(actual.up - expected.up) < 0.0001f);
}

} // namespace

TEST_CASE("Scene camera properties hold the camera pose", "[unity-scene-camera]")
{
  const auto pose = GENERATE(
    poseLookingAt({0, 0, 0}, {1, 0, 0}),
    poseLookingAt({-80, -128, 96}, {80, 128, -96}),
    poseLookingAt({12.5f, 3.25f, -7}, {0, -1, 0.5f}),
    poseLookingAt({0, 0, 64}, {-1, 0.001f, 0}));

  const auto entity = mdl::Entity{sceneCameraProperties(pose)};
  CHECK(isSceneCameraEntity(entity));
  checkPose(sceneCameraPose(entity), pose);
}

TEST_CASE("Scene camera straight up or down keeps its up axis", "[unity-scene-camera]")
{
  const auto down = LiveLinkCameraPose{{0, 0, 0}, {0, 0, -1}, {0, 1, 0}};
  checkPose(sceneCameraPose(mdl::Entity{sceneCameraProperties(down)}), down);

  const auto up = LiveLinkCameraPose{{0, 0, 0}, {0, 0, 1}, {-1, 0, 0}};
  checkPose(sceneCameraPose(mdl::Entity{sceneCameraProperties(up)}), up);
}

TEST_CASE("Scene camera without angles looks along +X", "[unity-scene-camera]")
{
  const auto entity = mdl::Entity{{
    {mdl::EntityPropertyKeys::Classname, SceneCameraClassName},
    {mdl::EntityPropertyKeys::Origin, "1 2 3"},
  }};
  checkPose(sceneCameraPose(entity), LiveLinkCameraPose{{1, 2, 3}, {1, 0, 0}, {0, 0, 1}});
}

TEST_CASE("Updating the scene camera", "[unity-scene-camera]")
{
  auto fixture = mdl::MapFixture{};
  fixture.create();
  auto& map = fixture.map();

  const auto first = poseLookingAt({-80, -128, 96}, {80, 128, -96});
  const auto second = poseLookingAt({256, 0, 64}, {-1, 0, 0});

  SECTION("adds one to the default layer when the map has none")
  {
    REQUIRE(sceneCameraNodes(map).empty());
    CHECK(updateSceneCamera(map, first));

    const auto nodes = sceneCameraNodes(map);
    REQUIRE(nodes.size() == 1u);
    CHECK(nodes.front()->parent() == map.world()->defaultLayer());
    checkPose(sceneCameraPose(nodes.front()->entity()), first);

    SECTION("does nothing when the pose is unchanged")
    {
      const auto modificationCount = map.modificationCount();
      CHECK_FALSE(updateSceneCamera(map, first));
      CHECK(map.modificationCount() == modificationCount);
    }

    SECTION("moves the existing one")
    {
      CHECK(updateSceneCamera(map, second));
      REQUIRE(sceneCameraNodes(map) == nodes);
      checkPose(sceneCameraPose(nodes.front()->entity()), second);

      map.undoCommand();
      checkPose(sceneCameraPose(nodes.front()->entity()), first);
    }

    SECTION("undo removes it again")
    {
      map.undoCommand();
      CHECK(sceneCameraNodes(map).empty());
    }
  }

  SECTION("keeps only the first of several")
  {
    auto* kept = addEntity(map, first);
    addEntity(map, first);
    addEntity(map, second);
    REQUIRE(sceneCameraNodes(map).size() == 3u);

    CHECK(updateSceneCamera(map, second));
    CHECK(sceneCameraNodes(map) == std::vector<mdl::EntityNode*>{kept});
    checkPose(sceneCameraPose(kept->entity()), second);

    map.undoCommand();
    CHECK(sceneCameraNodes(map).size() == 3u);
    checkPose(sceneCameraPose(kept->entity()), first);
  }
}

TEST_CASE("Scene camera is only visible while selected", "[unity-scene-camera]")
{
  auto fixture = mdl::MapFixture{};
  fixture.create();
  auto& map = fixture.map();

  auto* node = addEntity(map, poseLookingAt({0, 0, 0}, {1, 0, 0}));
  CHECK_FALSE(map.editorContext().visible(*node));
  CHECK_FALSE(map.editorContext().selectable(*node));

  mdl::selectNodes(map, {node});
  CHECK(map.editorContext().visible(*node));
}

} // namespace tb::unity
