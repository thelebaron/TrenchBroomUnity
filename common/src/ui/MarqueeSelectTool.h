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

#pragma once

#include "ui/Tool.h"
#include "ui/ToolController.h"

#include "vm/vec.h"

#include <cstdint>
#include <memory>
#include <vector>

class QWidget;

namespace tb::mdl
{
class Map;
} // namespace tb::mdl

namespace tb::render
{
class Camera;
} // namespace tb::render

namespace tb::ui
{

class MarqueeSelectTool : public Tool
{
public:
  enum class SelectionMode
  {
    Center,
    Enclosed,
    Intersecting,
  };

  enum class ShapeMode
  {
    Marquee,
    Freeform,
  };

private:
  mdl::Map& m_map;
  bool m_selectThrough = false;
  SelectionMode m_selectionMode = SelectionMode::Center;
  ShapeMode m_shapeMode = ShapeMode::Marquee;

public:
  explicit MarqueeSelectTool(mdl::Map& map);

  bool selectThrough() const;
  void setSelectThrough(bool selectThrough);

  SelectionMode selectionMode() const;
  void setSelectionMode(SelectionMode selectionMode);

  ShapeMode shapeMode() const;
  void setShapeMode(ShapeMode shapeMode);

  void select(
    const render::Camera& camera,
    const std::vector<vm::vec2d>& polygon,
    bool toggleSelection);

protected:
  QWidget* doCreatePage(QWidget* parent) override;
};

class MarqueeSelectToolController : public ToolController
{
private:
  MarqueeSelectTool& m_tool;

public:
  explicit MarqueeSelectToolController(MarqueeSelectTool& tool);

  Tool& tool() override;
  const Tool& tool() const override;

  std::unique_ptr<GestureTracker> acceptMouseDrag(const InputState& inputState) override;
};

} // namespace tb::ui
