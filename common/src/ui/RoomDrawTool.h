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

#pragma once

#include "mdl/RoomGenerator.h"
#include "ui/CreateBrushesToolBase.h"
#include "ui/ToolController.h"

#include "vm/bbox.h"
#include "vm/vec.h"

#include <memory>
#include <optional>

namespace tb::ui
{

/**
 * Draws rooms into the room layout. Each drag on the floor plane (at the room creator's
 * floor level) adds a space with the room creator's height. Overlapping spaces merge;
 * touching spaces get a wall with a doorway. While dragging, the tool previews the
 * brushes that will change.
 */
class RoomDrawTool : public CreateBrushesToolBase
{
private:
  std::optional<vm::bbox3d> m_space;

public:
  explicit RoomDrawTool(mdl::Map& map);

  double floorLevel() const;
  double roomHeight() const;

  /** The space spanned by a drag between the given points on the floor plane. */
  std::optional<vm::bbox3d> spaceFor(const vm::vec3d& start, const vm::vec3d& end) const;

  /** The current layout with the given space added, styled by the room creator. */
  mdl::RoomRecipe layoutWith(const vm::bbox3d& space) const;

  void update(const vm::bbox3d& space);
  void cancelDrawing();

private:
  bool doCommitBrushes() override;
  QWidget* doCreatePage(QWidget* parent) override;
};

class RoomDrawToolController : public ToolController
{
private:
  RoomDrawTool& m_tool;

public:
  explicit RoomDrawToolController(RoomDrawTool& tool);

  Tool& tool() override;
  const Tool& tool() const override;

  std::unique_ptr<GestureTracker> acceptMouseDrag(const InputState& inputState) override;
};

} // namespace tb::ui
