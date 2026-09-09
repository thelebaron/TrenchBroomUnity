/*
 Copyright (C) 2026 the TrenchBroom developers

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
#include "mdl/BrushNode.h"
#include "mdl/Game.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/WorldNode.h"
#include "render/OrthographicCamera.h"
#include "ui/GestureTracker.h"
#include "ui/InputState.h"
#include "ui/MarqueeSelectTool.h"
#include "ui/PickRequest.h"

#include "kdl/result.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

void createScene(
  mdl::Map& map,
  render::OrthographicCamera& camera,
  mdl::BrushNode*& leftBrush,
  mdl::BrushNode*& rightBrush)
{
  const auto viewport = render::Camera::Viewport{0, 0, 256, 256};
  camera.setViewport(viewport);
  camera.moveTo({0, 0, 128});
  camera.setDirection({0, 0, -1}, {0, 1, 0});

  auto builder = mdl::BrushBuilder{
    map.world()->mapFormat(),
    map.worldBounds(),
    map.game()->config().faceAttribsConfig.defaults};
  leftBrush = new mdl::BrushNode{
    builder.createCuboid(vm::bbox3d{{-80, -16, -16}, {-48, 16, 16}}, "material")
    | kdl::value()};
  rightBrush = new mdl::BrushNode{
    builder.createCuboid(vm::bbox3d{{48, -16, -16}, {80, 16, 16}}, "material")
    | kdl::value()};
  addNodes(map, {{parentForNodes(map), {leftBrush, rightBrush}}});
}

vm::vec2f projectToInput(const render::Camera& camera, const vm::vec3f& point)
{
  const auto projected = camera.project(point);
  return {
    projected.x(),
    static_cast<float>(camera.viewport().height) - projected.y(),
  };
}

std::vector<vm::vec2d> rectangle(
  const vm::vec2d& center, const double halfWidth, const double halfHeight)
{
  return {
    {center.x() - halfWidth, center.y() - halfHeight},
    {center.x() - halfWidth, center.y() + halfHeight},
    {center.x() + halfWidth, center.y() + halfHeight},
    {center.x() + halfWidth, center.y() - halfHeight},
  };
}

void drag(
  MarqueeSelectToolController& controller,
  const render::Camera& camera,
  const std::vector<vm::vec2f>& points)
{
  REQUIRE(points.size() >= 2);
  auto inputState = InputState{points.front().x(), points.front().y()};
  inputState.setPickRequest(
    PickRequest{
      vm::ray3d{camera.pickRay(inputState.mouseX(), inputState.mouseY())}, camera});
  inputState.mouseDown(MouseButtons::Left);

  auto tracker = controller.acceptMouseDrag(inputState);
  REQUIRE(tracker != nullptr);
  for (size_t i = 1; i < points.size(); ++i)
  {
    inputState.mouseMove(
      points[i].x(),
      points[i].y(),
      points[i].x() - inputState.mouseX(),
      points[i].y() - inputState.mouseY());
    REQUIRE(tracker->update(inputState));
  }
  tracker->end(inputState);
}

} // namespace

TEST_CASE("MarqueeSelectTool")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.map();
  fixture.create();

  auto camera = render::OrthographicCamera{};
  mdl::BrushNode* leftBrush = nullptr;
  mdl::BrushNode* rightBrush = nullptr;
  createScene(map, camera, leftBrush, rightBrush);
  auto tool = MarqueeSelectTool{map};
  auto controller = MarqueeSelectToolController{tool};
  REQUIRE(tool.activate());
  tool.setSelectThrough(true);

  const auto left = projectToInput(camera, vm::vec3f{-64, 0, 0});
  const auto leftScreen = camera.project(vm::vec3f{-64, 0, 0});
  const auto leftCenter = vm::vec2d{leftScreen.x(), leftScreen.y()};

  SECTION("center selects a partial bounds")
  {
    tool.setSelectionMode(MarqueeSelectTool::SelectionMode::Center);
    tool.select(camera, rectangle(leftCenter, 8.0, 24.0), false);

    CHECK(map.selection() == mdl::makeSelection({leftBrush}));
  }

  SECTION("enclosed requires the complete bounds")
  {
    tool.setSelectionMode(MarqueeSelectTool::SelectionMode::Enclosed);
    tool.select(camera, rectangle(leftCenter, 8.0, 24.0), false);
    CHECK(map.selection() == mdl::Selection{});

    tool.select(camera, rectangle(leftCenter, 24.0, 24.0), false);
    CHECK(map.selection() == mdl::makeSelection({leftBrush}));
  }

  SECTION("intersecting selects a partial bounds")
  {
    tool.setSelectionMode(MarqueeSelectTool::SelectionMode::Intersecting);
    tool.select(camera, rectangle(leftCenter + vm::vec2d{12.0, 0.0}, 4.0, 24.0), false);

    CHECK(map.selection() == mdl::makeSelection({leftBrush}));
  }

  SECTION("marquee")
  {
    drag(
      controller,
      camera,
      {
        {left.x() - 24, left.y() - 24},
        {left.x() + 24, left.y() + 24},
      });

    CHECK(map.selection() == mdl::makeSelection({leftBrush}));
  }

  SECTION("freeform")
  {
    tool.setShapeMode(MarqueeSelectTool::ShapeMode::Freeform);
    drag(
      controller,
      camera,
      {
        {left.x() - 24, left.y() - 24},
        {left.x() + 24, left.y() - 24},
        {left.x() + 24, left.y() + 24},
        {left.x() - 24, left.y() + 24},
      });

    CHECK(map.selection() == mdl::makeSelection({leftBrush}));
    CHECK_FALSE(rightBrush->selected());
  }
}

} // namespace tb::ui
