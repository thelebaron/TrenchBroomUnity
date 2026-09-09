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

#include "MarqueeSelectTool.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>

#include "mdl/EditorContext.h"
#include "mdl/Hit.h"
#include "mdl/HitAdapter.h"
#include "mdl/HitFilter.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Picking.h"
#include "mdl/Map_Selection.h"
#include "mdl/ModelUtils.h"
#include "mdl/Node.h"
#include "mdl/PickResult.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "render/Camera.h"
#include "render/RenderContext.h"
#include "render/RenderService.h"
#include "ui/GestureTracker.h"
#include "ui/InputState.h"
#include "ui/ViewConstants.h"

#include "vm/distance.h"
#include "vm/intersection.h"
#include "vm/plane.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tb::ui
{
namespace
{

constexpr double SelectionPlaneDistance = 64.0;
constexpr double FreeformPointDistance = 4.0;

class SelectionShape
{
private:
  const render::Camera& m_camera;
  const bool m_freeform;
  const vm::vec2d m_start;
  vm::vec2d m_current;
  std::vector<vm::vec2d> m_points;

public:
  SelectionShape(
    const render::Camera& camera, const float x, const float y, const bool freeform)
    : m_camera{camera}
    , m_freeform{freeform}
    , m_start{toScreenPoint(x, y)}
    , m_current{m_start}
    , m_points{m_start}
  {
  }

  const render::Camera& camera() const { return m_camera; }

  void update(const float x, const float y)
  {
    const auto point = toScreenPoint(x, y);
    m_current = point;

    if (
      m_freeform
      && vm::squared_distance(point, m_points.back())
           >= FreeformPointDistance * FreeformPointDistance)
    {
      m_points.push_back(point);
    }
  }

  std::vector<vm::vec2d> polygon() const
  {
    if (!m_freeform)
    {
      const auto min = vm::min(m_start, m_current);
      const auto max = vm::max(m_start, m_current);
      return {
        {min.x(), min.y()},
        {min.x(), max.y()},
        {max.x(), max.y()},
        {max.x(), min.y()},
      };
    }

    auto result = m_points;
    if (result.empty() || result.back() != m_current)
    {
      result.push_back(m_current);
    }
    return result;
  }

  void render(
    render::RenderContext& renderContext, render::RenderBatch& renderBatch) const
  {
    const auto polygon2d = polygon();
    if (polygon2d.size() < 3)
    {
      return;
    }

    const auto plane = vm::plane3d{
      vm::vec3d{m_camera.defaultPoint(static_cast<float>(SelectionPlaneDistance))},
      vm::vec3d{m_camera.direction()}};

    auto polygon3d = std::vector<vm::vec3f>{};
    polygon3d.reserve(polygon2d.size());
    for (const auto& point : polygon2d)
    {
      const auto y = static_cast<double>(m_camera.viewport().height) - point.y();
      const auto ray =
        vm::ray3d{m_camera.pickRay(static_cast<float>(point.x()), float(y))};
      if (const auto distance = vm::intersect_ray_plane(ray, plane))
      {
        polygon3d.push_back(vm::vec3f{vm::point_at_distance(ray, *distance)});
      }
    }

    if (polygon3d.size() < 3)
    {
      return;
    }

    auto renderService = render::RenderService{renderContext, renderBatch};
    renderService.setForegroundColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
    renderService.setLineWidth(2.0f);
    renderService.renderPolygonOutline(polygon3d);

    renderService.setForegroundColor(Color(1.0f, 1.0f, 1.0f, 0.25f));
    renderService.renderFilledPolygon(polygon3d);
  }

private:
  vm::vec2d toScreenPoint(const float x, const float y) const
  {
    return {
      static_cast<double>(x),
      static_cast<double>(m_camera.viewport().height) - static_cast<double>(y)};
  }
};

bool pointOnSegment(const vm::vec2d& point, const vm::vec2d& start, const vm::vec2d& end)
{
  const auto cross = (point.x() - start.x()) * (end.y() - start.y())
                     - (point.y() - start.y()) * (end.x() - start.x());
  if (std::abs(cross) > 1e-6)
  {
    return false;
  }

  return point.x() >= std::min(start.x(), end.x()) - 1e-6
         && point.x() <= std::max(start.x(), end.x()) + 1e-6
         && point.y() >= std::min(start.y(), end.y()) - 1e-6
         && point.y() <= std::max(start.y(), end.y()) + 1e-6;
}

bool pointInPolygon(const vm::vec2d& point, const std::vector<vm::vec2d>& polygon)
{
  auto inside = false;
  for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
  {
    const auto& current = polygon[i];
    const auto& previous = polygon[j];
    if (pointOnSegment(point, previous, current))
    {
      return true;
    }

    if ((current.y() > point.y()) != (previous.y() > point.y()))
    {
      const auto x = (previous.x() - current.x()) * (point.y() - current.y())
                       / (previous.y() - current.y())
                     + current.x();
      if (point.x() < x)
      {
        inside = !inside;
      }
    }
  }
  return inside;
}

mdl::HitFilter isNodeSelectable(const mdl::EditorContext& editorContext)
{
  return [&](const auto& hit) {
    if (const auto* node = mdl::hitToNode(hit))
    {
      return editorContext.selectable(*mdl::findOutermostClosedGroupOrNode(node));
    }
    return false;
  };
}

bool isVisibleAtPoint(
  const mdl::Map& map,
  const mdl::Node& node,
  const render::Camera& camera,
  const vm::vec2d& point,
  const mdl::EditorContext& editorContext)
{
  const auto y = static_cast<double>(camera.viewport().height) - point.y();
  const auto pickRay = vm::ray3d{camera.pickRay(static_cast<float>(point.x()), float(y))};

  auto pickResult = mdl::PickResult::byDistance();
  mdl::pick(map, pickRay, pickResult);

  using namespace mdl::HitFilters;
  const auto hit =
    pickResult.first(type(mdl::nodeHitType()) && isNodeSelectable(editorContext));
  return hit.isMatch()
         && mdl::findOutermostClosedGroupOrNode(mdl::hitToNode(hit)) == &node;
}

class MarqueeSelectionDragTracker : public GestureTracker
{
private:
  MarqueeSelectTool& m_tool;
  SelectionShape m_shape;

public:
  MarqueeSelectionDragTracker(
    MarqueeSelectTool& tool, const InputState& inputState, const bool freeform)
    : m_tool{tool}
    , m_shape{inputState.camera(), inputState.mouseX(), inputState.mouseY(), freeform}
  {
  }

  bool update(const InputState& inputState) override
  {
    m_shape.update(inputState.mouseX(), inputState.mouseY());
    return true;
  }

  void end(const InputState& inputState) override
  {
    m_tool.select(
      m_shape.camera(),
      m_shape.polygon(),
      inputState.modifierKeysDown(ModifierKeys::CtrlCmd));
  }

  void cancel() override {}

  void render(
    const InputState&,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch) const override
  {
    m_shape.render(renderContext, renderBatch);
  }
};

class MarqueeSelectToolPage : public QWidget
{
private:
  MarqueeSelectTool& m_tool;

public:
  MarqueeSelectToolPage(MarqueeSelectTool& tool, QWidget* parent)
    : QWidget{parent}
    , m_tool{tool}
  {
    auto* selectThrough = new QCheckBox{tr("Select through")};
    selectThrough->setChecked(m_tool.selectThrough());
    connect(selectThrough, &QCheckBox::toggled, this, [this](const bool checked) {
      m_tool.setSelectThrough(checked);
    });

    auto* shapeLabel = new QLabel{tr("Shape")};
    auto* shape = new QComboBox{};
    shape->addItem(tr("Marquee"));
    shape->addItem(tr("Freeform"));
    shape->setCurrentIndex(
      m_tool.selectionMode() == MarqueeSelectTool::SelectionMode::Marquee ? 0 : 1);
    connect(
      shape,
      QOverload<int>::of(&QComboBox::currentIndexChanged),
      this,
      [this](const int index) {
        m_tool.setSelectionMode(
          index == 0 ? MarqueeSelectTool::SelectionMode::Marquee
                     : MarqueeSelectTool::SelectionMode::Freeform);
      });

    auto* layout = new QHBoxLayout{};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(LayoutConstants::MediumHMargin);
    layout->addWidget(selectThrough, 0, Qt::AlignVCenter);
    layout->addSpacing(LayoutConstants::WideHMargin);
    layout->addWidget(shapeLabel, 0, Qt::AlignVCenter);
    layout->addWidget(shape, 0, Qt::AlignVCenter);
    layout->addStretch(1);
    setLayout(layout);
  }
};

} // namespace

MarqueeSelectTool::MarqueeSelectTool(mdl::Map& map)
  : Tool{false}
  , m_map{map}
{
}

bool MarqueeSelectTool::selectThrough() const
{
  return m_selectThrough;
}

void MarqueeSelectTool::setSelectThrough(const bool selectThrough)
{
  m_selectThrough = selectThrough;
}

MarqueeSelectTool::SelectionMode MarqueeSelectTool::selectionMode() const
{
  return m_selectionMode;
}

void MarqueeSelectTool::setSelectionMode(const SelectionMode selectionMode)
{
  m_selectionMode = selectionMode;
}

void MarqueeSelectTool::select(
  const render::Camera& camera,
  const std::vector<vm::vec2d>& polygon,
  const bool toggleSelection)
{
  if (polygon.size() < 3 || !m_map.editorContext().canChangeSelection())
  {
    return;
  }

  const auto hasArea = [&]() {
    auto area = 0.0;
    for (size_t i = 0; i < polygon.size(); ++i)
    {
      const auto& current = polygon[i];
      const auto& next = polygon[(i + 1) % polygon.size()];
      area += current.x() * next.y() - next.x() * current.y();
    }
    return std::abs(area) > 1.0;
  };
  if (!hasArea())
  {
    return;
  }

  const auto& editorContext = m_map.editorContext();
  const auto selectableNodes =
    mdl::collectSelectableNodes(std::vector<mdl::Node*>{m_map.world()}, editorContext);
  auto nodesToSelect = std::vector<mdl::Node*>{};

  for (auto* node : selectableNodes)
  {
    const auto projected = camera.project(vm::vec3f{node->logicalBounds().center()});
    const auto point = vm::vec2d{projected.x(), projected.y()};
    if (
      projected.z() >= 0.0f && projected.z() <= 1.0f && pointInPolygon(point, polygon)
      && (m_selectThrough || isVisibleAtPoint(m_map, *node, camera, point, editorContext)))
    {
      nodesToSelect.push_back(node);
    }
  }

  auto transaction = mdl::Transaction{m_map, "Marquee Select"};
  if (toggleSelection)
  {
    if (m_map.selection().hasBrushFaces())
    {
      deselectAll(m_map);
    }

    auto nodesToDeselect = std::vector<mdl::Node*>{};
    auto nodesToAdd = std::vector<mdl::Node*>{};
    for (auto* node : nodesToSelect)
    {
      (node->selected() ? nodesToDeselect : nodesToAdd).push_back(node);
    }
    deselectNodes(m_map, nodesToDeselect);
    selectNodes(m_map, nodesToAdd);
  }
  else
  {
    deselectAll(m_map);
    selectNodes(m_map, nodesToSelect);
  }
  transaction.commit();
}

QWidget* MarqueeSelectTool::doCreatePage(QWidget* parent)
{
  return new MarqueeSelectToolPage{*this, parent};
}

MarqueeSelectToolController::MarqueeSelectToolController(MarqueeSelectTool& tool)
  : m_tool{tool}
{
}

Tool& MarqueeSelectToolController::tool()
{
  return m_tool;
}

const Tool& MarqueeSelectToolController::tool() const
{
  return m_tool;
}

std::unique_ptr<GestureTracker> MarqueeSelectToolController::acceptMouseDrag(
  const InputState& inputState)
{
  if (
    !inputState.mouseButtonsPressed(MouseButtons::Left)
    || !inputState.checkModifierKeys(
      ModifierKeyPressed::DontCare, ModifierKeyPressed::No, ModifierKeyPressed::No))
  {
    return nullptr;
  }

  return std::make_unique<MarqueeSelectionDragTracker>(
    m_tool,
    inputState,
    m_tool.selectionMode() == MarqueeSelectTool::SelectionMode::Freeform);
}

} // namespace tb::ui
