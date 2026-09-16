/*
 Copyright (C) 2026 TrenchBroom Contributors

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
#include "TestUtils.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Game.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Picking.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "render/OrthographicCamera.h"
#include "ui/GestureTracker.h"
#include "ui/InputState.h"
#include "ui/PaintRollerTool.h"
#include "ui/PickRequest.h"

#include "kdl/result.h"

#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

vm::vec2f projectToInput(const render::Camera& camera, const vm::vec3f& point)
{
  const auto projected = camera.project(point);
  return {
    projected.x(),
    static_cast<float>(camera.viewport().height) - projected.y(),
  };
}

void updatePickState(
  InputState& inputState, const render::Camera& camera, const mdl::Map& map)
{
  const auto pickRay =
    vm::ray3d{camera.pickRay(inputState.mouseX(), inputState.mouseY())};
  auto pickResult = mdl::PickResult{};
  pick(map, pickRay, pickResult);

  inputState.setPickRequest(PickRequest{pickRay, camera});
  inputState.setPickResult(std::move(pickResult));
}

bool faceHasMaterial(
  const mdl::BrushNode& brushNode, const vm::vec3d& normal, const std::string& material)
{
  const auto faceIndex = brushNode.brush().findFace(normal);
  return faceIndex
         && brushNode.brush().face(*faceIndex).attributes().materialName() == material;
}

} // namespace

TEST_CASE("PaintRollerTool")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.map();
  fixture.create();

  auto camera = render::OrthographicCamera{};
  camera.setViewport({0, 0, 256, 256});
  camera.moveTo({0, 0, 128});
  camera.setDirection({0, 0, -1}, {0, 1, 0});

  auto builder = mdl::BrushBuilder{
    map.world()->mapFormat(),
    map.worldBounds(),
    map.game()->config().faceAttribsConfig.defaults};
  auto* leftBrush = new mdl::BrushNode{
    builder.createCuboid(vm::bbox3d{{-80, -16, -16}, {-48, 16, 16}}, "original")
    | kdl::value()};
  auto* rightBrush = new mdl::BrushNode{
    builder.createCuboid(vm::bbox3d{{48, -16, -16}, {80, 16, 16}}, "original")
    | kdl::value()};
  addNodes(map, {{parentForNodes(map), {leftBrush, rightBrush}}});

  const auto leftPoint = projectToInput(camera, vm::vec3f{-64, 0, 0});
  const auto rightPoint = projectToInput(camera, vm::vec3f{64, 0, 0});

  auto tool = PaintRollerTool{map};
  auto controller = PaintRollerToolController{tool};
  REQUIRE(tool.activate());

  SECTION("click paints a face without changing the selection")
  {
    selectNodes(map, {leftBrush});
    map.setCurrentMaterialName("painted");

    auto inputState = InputState{leftPoint.x(), leftPoint.y()};
    updatePickState(inputState, camera, map);
    inputState.mouseDown(MouseButtons::Left);
    CHECK(controller.mouseClick(inputState));
    inputState.mouseUp(MouseButtons::Left);

    CHECK(faceHasMaterial(*leftBrush, {0, 0, 1}, "painted"));
    CHECK(faceHasMaterial(*rightBrush, {0, 0, 1}, "original"));
    CHECK(map.selection() == mdl::makeSelection({leftBrush}));
  }

  SECTION("drag paints each face under the cursor without changing the selection")
  {
    selectNodes(map, {leftBrush});
    map.setCurrentMaterialName("painted");

    auto inputState = InputState{leftPoint.x(), leftPoint.y()};
    updatePickState(inputState, camera, map);
    inputState.mouseDown(MouseButtons::Left);

    auto tracker = controller.acceptMouseDrag(inputState);
    REQUIRE(tracker != nullptr);

    inputState.mouseMove(
      rightPoint.x(),
      rightPoint.y(),
      rightPoint.x() - inputState.mouseX(),
      rightPoint.y() - inputState.mouseY());
    updatePickState(inputState, camera, map);
    CHECK(tracker->update(inputState));
    tracker->end(inputState);
    inputState.mouseUp(MouseButtons::Left);

    CHECK(faceHasMaterial(*leftBrush, {0, 0, 1}, "painted"));
    CHECK(faceHasMaterial(*rightBrush, {0, 0, 1}, "painted"));
    CHECK(map.selection() == mdl::makeSelection({leftBrush}));
    CHECK(map.undoCommandName() == "Paint Faces");
  }
}

} // namespace tb::ui
