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

#include "PaintRollerTool.h"

#include "mdl/BrushFace.h"
#include "mdl/BrushFaceAttributes.h"
#include "mdl/BrushNode.h"
#include "mdl/ChangeBrushFaceAttributesRequest.h"
#include "mdl/EditorContext.h"
#include "mdl/HitAdapter.h"
#include "mdl/HitFilter.h"
#include "mdl/Map.h"
#include "mdl/Map_Brushes.h"
#include "mdl/TransactionScope.h"
#include "ui/GestureTracker.h"
#include "ui/InputState.h"

#include <utility>

namespace tb::ui
{
namespace
{

std::optional<mdl::BrushFaceHandle> findPaintFace(
  const InputState& inputState, const mdl::EditorContext& editorContext)
{
  using namespace mdl::HitFilters;

  const auto& hit = inputState.pickResult().first(type(mdl::BrushNode::BrushHitType));
  if (const auto pickedFaceHandle = mdl::hitToFaceHandle(hit))
  {
    if (editorContext.selectable(*pickedFaceHandle->node(), pickedFaceHandle->face()))
    {
      return pickedFaceHandle;
    }
  }

  return std::nullopt;
}

} // namespace

class PaintRollerDragTracker : public GestureTracker
{
private:
  PaintRollerToolController& m_controller;

public:
  explicit PaintRollerDragTracker(PaintRollerToolController& controller)
    : m_controller{controller}
  {
  }

  bool update(const InputState& inputState) override
  {
    m_controller.paint(inputState);
    return true;
  }

  void end(const InputState&) override { m_controller.finishDrag(); }

  void cancel() override { m_controller.cancelPaint(); }
};

PaintRollerTool::PaintRollerTool(mdl::Map& map)
  : Tool{false}
  , m_map{map}
{
}

bool PaintRollerTool::paint(
  const InputState& inputState,
  std::optional<mdl::BrushFaceHandle>& lastFace,
  std::string& lastMaterialName)
{
  const auto faceHandle = findPaintFace(inputState, m_map.editorContext());
  if (!faceHandle)
  {
    lastFace = std::nullopt;
    return false;
  }

  const auto& materialName = m_map.currentMaterialName();
  if (materialName == mdl::BrushFaceAttributes::NoMaterialName)
  {
    lastFace = *faceHandle;
    lastMaterialName = materialName;
    return false;
  }

  if (lastFace && *lastFace == *faceHandle && lastMaterialName == materialName)
  {
    return true;
  }

  if (faceHandle->face().attributes().materialName() != materialName)
  {
    auto request = mdl::ChangeBrushFaceAttributesRequest{};
    request.setMaterialName(materialName);
    mdl::setBrushFaceAttributes(m_map, {*faceHandle}, request);
  }

  lastFace = *faceHandle;
  lastMaterialName = materialName;
  return true;
}

PaintRollerToolController::PaintRollerToolController(PaintRollerTool& tool)
  : m_tool{tool}
{
}

Tool& PaintRollerToolController::tool()
{
  return m_tool;
}

const Tool& PaintRollerToolController::tool() const
{
  return m_tool;
}

void PaintRollerToolController::beginPaint(const InputState& inputState)
{
  if (!m_transactionActive)
  {
    m_tool.m_map.startTransaction("Paint Faces", mdl::TransactionScope::LongRunning);
    m_transactionActive = true;
    m_lastFace = std::nullopt;
    m_lastMaterialName.clear();
  }

  paint(inputState);
}

void PaintRollerToolController::commitPaint()
{
  if (m_transactionActive)
  {
    m_tool.m_map.commitTransaction();
    m_transactionActive = false;
  }
}

void PaintRollerToolController::cancelPaint()
{
  if (m_transactionActive)
  {
    m_tool.m_map.cancelTransaction();
    m_transactionActive = false;
  }

  m_leftClickPending = false;
}

void PaintRollerToolController::finishDrag()
{
  commitPaint();
  m_leftClickPending = false;
}

void PaintRollerToolController::paint(const InputState& inputState)
{
  m_tool.paint(inputState, m_lastFace, m_lastMaterialName);
}

void PaintRollerToolController::mouseDown(const InputState& inputState)
{
  m_leftClickPending = false;
  if (inputState.mouseButtonsPressed(MouseButtons::Left))
  {
    m_leftClickPending = true;
    beginPaint(inputState);
  }
}

void PaintRollerToolController::mouseUp(const InputState& inputState)
{
  if (inputState.mouseButtonsPressed(MouseButtons::Left))
  {
    commitPaint();
  }
}

bool PaintRollerToolController::mouseClick(const InputState& inputState)
{
  if (inputState.mouseButtonsPressed(MouseButtons::Left))
  {
    // Support callers that invoke mouseClick directly instead of sending the preceding
    // mouseDown event through the controller.
    if (m_leftClickPending)
    {
      commitPaint();
      m_leftClickPending = false;
    }
    else
    {
      auto lastFace = std::optional<mdl::BrushFaceHandle>{};
      auto lastMaterialName = std::string{};
      m_tool.paint(inputState, lastFace, lastMaterialName);
    }
    return true;
  }

  if (m_leftClickPending)
  {
    commitPaint();
    m_leftClickPending = false;
    return true;
  }

  return false;
}

bool PaintRollerToolController::mouseDoubleClick(const InputState& inputState)
{
  if (!inputState.mouseButtonsPressed(MouseButtons::Left))
  {
    return false;
  }

  if (!m_leftClickPending)
  {
    beginPaint(inputState);
  }
  commitPaint();
  m_leftClickPending = false;
  return true;
}

std::unique_ptr<GestureTracker> PaintRollerToolController::acceptMouseDrag(
  const InputState& inputState)
{
  if (!inputState.mouseButtonsPressed(MouseButtons::Left))
  {
    return nullptr;
  }

  if (!m_leftClickPending)
  {
    m_leftClickPending = true;
    beginPaint(inputState);
  }

  auto tracker = std::make_unique<PaintRollerDragTracker>(*this);
  tracker->update(inputState);
  return tracker;
}

bool PaintRollerToolController::cancel()
{
  if (m_transactionActive)
  {
    cancelPaint();
    return true;
  }

  return false;
}

} // namespace tb::ui
