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

#pragma once

#include "mdl/BrushFaceHandle.h"
#include "ui/Tool.h"
#include "ui/ToolController.h"

#include <memory>
#include <optional>
#include <string>

namespace tb::mdl
{
class Map;
} // namespace tb::mdl

namespace tb::ui
{
class GestureTracker;
class InputState;
class PaintRollerDragTracker;

/**
 * Paints the currently selected material onto brush faces under the mouse cursor.
 *
 * The tool deliberately paints explicit face handles instead of using the map's current
 * selection. This keeps painting independent from, and invisible to, object selection.
 */
class PaintRollerTool : public Tool
{
private:
  mdl::Map& m_map;

  friend class PaintRollerToolController;

public:
  explicit PaintRollerTool(mdl::Map& map);

  bool paint(
    const InputState& inputState,
    std::optional<mdl::BrushFaceHandle>& lastFace,
    std::string& lastMaterialName);
};

class PaintRollerToolController : public ToolController
{
private:
  PaintRollerTool& m_tool;
  bool m_leftClickPending = false;
  bool m_transactionActive = false;
  std::optional<mdl::BrushFaceHandle> m_lastFace;
  std::string m_lastMaterialName;

  friend class PaintRollerDragTracker;

  void beginPaint(const InputState& inputState);
  void commitPaint();
  void cancelPaint();
  void finishDrag();
  void paint(const InputState& inputState);

public:
  explicit PaintRollerToolController(PaintRollerTool& tool);

  Tool& tool() override;
  const Tool& tool() const override;

  void mouseDown(const InputState& inputState) override;
  void mouseUp(const InputState& inputState) override;
  bool mouseClick(const InputState& inputState) override;
  bool mouseDoubleClick(const InputState& inputState) override;
  std::unique_ptr<GestureTracker> acceptMouseDrag(const InputState& inputState) override;

  bool cancel() override;
};

} // namespace tb::ui
