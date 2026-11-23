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

#include "RoomDrawTool.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>

#include "Logger.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "mdl/Brush.h"
#include "mdl/BrushNode.h"
#include "mdl/Grid.h"
#include "mdl/Map.h"
#include "mdl/RoomLayout.h"
#include "ui/HandleDragTracker.h"
#include "ui/InputState.h"
#include "ui/RoomCreatorDialog.h"

#include "kdl/range_to_vector.h"

#include "vm/intersection.h"
#include "vm/plane.h"
#include "vm/ray.h"

#include <ranges>

namespace tb::ui
{
namespace
{

class RoomDrawDragDelegate : public HandleDragTrackerDelegate
{
private:
  RoomDrawTool& m_tool;

public:
  explicit RoomDrawDragDelegate(RoomDrawTool& tool)
    : m_tool{tool}
  {
  }

  HandlePositionProposer start(
    const InputState&,
    const vm::vec3d& initialHandlePosition,
    const vm::vec3d& handleOffset) override
  {
    return makeHandlePositionProposer(
      makePlaneHandlePicker(vm::horizontal_plane(initialHandlePosition), handleOffset),
      makeIdentityHandleSnapper());
  }

  DragStatus update(
    const InputState&,
    const DragState& dragState,
    const vm::vec3d& proposedHandlePosition) override
  {
    if (
      const auto space =
        m_tool.spaceFor(dragState.initialHandlePosition, proposedHandlePosition))
    {
      m_tool.update(*space);
      m_tool.refreshViews();
    }
    return DragStatus::Continue;
  }

  void end(const InputState&, const DragState&) override { m_tool.createBrushes(); }

  void cancel(const DragState&) override { m_tool.cancelDrawing(); }

  void render(
    const InputState&,
    const DragState&,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch) const override
  {
    m_tool.render(renderContext, renderBatch);
  }
};

} // namespace

RoomDrawTool::RoomDrawTool(mdl::Map& map)
  : CreateBrushesToolBase{false, map}
{
}

double RoomDrawTool::floorLevel() const
{
  return static_cast<double>(pref(Preferences::RoomCreatorFloorLevel));
}

double RoomDrawTool::roomHeight() const
{
  return static_cast<double>(std::max(1, pref(Preferences::RoomCreatorWallHeight)));
}

std::optional<vm::bbox3d> RoomDrawTool::spaceFor(
  const vm::vec3d& start, const vm::vec3d& end) const
{
  const auto& grid = m_map.grid();
  const auto min = grid.snapDown(vm::min(start, end));
  const auto max = grid.snapUp(vm::max(start, end));
  if (max.x() - min.x() <= 0.0 || max.y() - min.y() <= 0.0)
  {
    return std::nullopt;
  }

  const auto floor = floorLevel();
  return vm::bbox3d{{min.x(), min.y(), floor}, {max.x(), max.y(), floor + roomHeight()}};
}

mdl::RoomRecipe RoomDrawTool::layoutWith(const vm::bbox3d& space) const
{
  auto current = mdl::readRoomLayout(m_map);

  auto layout = roomStyleFromPreferences(m_map);
  if (current.is_success())
  {
    layout.spaces = current.value().spaces;
    layout.doors = current.value().doors;
  }
  layout.spaces.push_back(space);
  layout.autoDoors = true;
  layout.doorWidth = pref(Preferences::RoomCreatorDoorWidth);
  layout.doorHeight = pref(Preferences::RoomCreatorDoorHeight);
  return layout;
}

void RoomDrawTool::update(const vm::bbox3d& space)
{
  m_space = space;
  auto preview = mdl::roomLayoutPreview(m_map, layoutWith(space));
  if (preview.is_error())
  {
    clearBrushes();
    return;
  }

  updateBrushes(
    std::move(preview).value() | std::views::transform([](auto brush) {
      return std::make_unique<mdl::BrushNode>(std::move(brush));
    })
    | kdl::to_vector);
}

void RoomDrawTool::cancelDrawing()
{
  m_space = std::nullopt;
  cancel();
}

bool RoomDrawTool::doCommitBrushes()
{
  if (!m_space)
  {
    return true;
  }

  const auto space = *m_space;
  m_space = std::nullopt;
  if (
    auto result =
      mdl::updateRoomLayout(m_map, layoutWith(space), roomLightOptionsFromPreferences());
    result.is_error())
  {
    m_map.logger().error() << "Could not draw room: "
                           << std::get<Error>(result.error()).msg;
  }
  return true;
}

QWidget* RoomDrawTool::doCreatePage(QWidget* parent)
{
  auto* page = new QWidget{parent};
  auto* label = new QLabel{QObject::tr(
    "Draw Rooms: drag in the 3D or top view. Rooms sit on the Room Creator's floor "
    "level; touching rooms get a doorway, overlapping rooms merge. Shift+Esc to stop.")};

  auto* layout = new QHBoxLayout{};
  layout->setContentsMargins(QMargins{});
  layout->addWidget(label);
  layout->addStretch(1);
  page->setLayout(layout);
  return page;
}

RoomDrawToolController::RoomDrawToolController(RoomDrawTool& tool)
  : m_tool{tool}
{
}

Tool& RoomDrawToolController::tool()
{
  return m_tool;
}

const Tool& RoomDrawToolController::tool() const
{
  return m_tool;
}

std::unique_ptr<GestureTracker> RoomDrawToolController::acceptMouseDrag(
  const InputState& inputState)
{
  if (!inputState.mouseButtonsPressed(MouseButtons::Left))
  {
    return nullptr;
  }

  if (!inputState.checkModifierKeys(
        ModifierKeyPressed::No, ModifierKeyPressed::No, ModifierKeyPressed::No))
  {
    return nullptr;
  }

  // Rooms are drawn on the floor plane; side views look along it and can't hit it.
  const auto floorPlane = vm::horizontal_plane(vm::vec3d{0, 0, m_tool.floorLevel()});
  const auto& pickRay = inputState.pickRay();
  const auto distance = vm::intersect_ray_plane(pickRay, floorPlane);
  if (!distance || *distance < 0.0)
  {
    return nullptr;
  }

  const auto point = vm::point_at_distance(pickRay, *distance);
  return createHandleDragTracker(RoomDrawDragDelegate{m_tool}, inputState, point, point);
}

} // namespace tb::ui
