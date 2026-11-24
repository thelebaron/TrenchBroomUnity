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

#include "ui/Tool.h"
#include "ui/ToolController.h"
#include "unity/UnityDecals.h"

#include "vm/vec.h"

#include <optional>
#include <string>

class QLabel;

namespace tb::mdl
{
class Map;
class PickResult;
} // namespace tb::mdl

namespace tb::unity
{
struct UnityCatalogDecal;
}

namespace tb::ui
{

/**
 * Where a decal goes on a surface: its box centre on the surface, forward into it and up
 * along it.
 */
struct DecalPlacement
{
  vm::vec3d origin;
  vm::vec3d forward;
  vm::vec3d up;
};

/**
 * The up direction a decal on a face with the given normal has before it is turned: world
 * up on walls; on floors and ceilings the world axis closest to the view direction, so
 * the decal reads upright from where it is looked at and still lines up with the grid.
 */
vm::vec3d decalReferenceUp(const vm::vec3d& faceNormal, const vm::vec3d& viewDirection);

/**
 * The placement of a decal on a face with the given normal at the given point, turned by
 * roll degrees from its reference up: clockwise as seen looking at the surface.
 */
DecalPlacement decalPlacement(
  const vm::vec3d& point,
  const vm::vec3d& faceNormal,
  const vm::vec3d& viewDirection,
  double roll);

/**
 * How far the decal's up is turned from its reference up, in degrees, clockwise as seen
 * looking along forward.
 */
double decalRoll(
  const vm::vec3d& forward, const vm::vec3d& up, const vm::vec3d& referenceUp);

/**
 * The decal's up after moving it onto a face with the given normal: its previous up laid
 * onto the new face, so a decal keeps its turn while it slides across a surface and
 * around corners.
 */
vm::vec3d movedDecalUp(
  const vm::vec3d& previousUp,
  const vm::vec3d& faceNormal,
  const vm::vec3d& viewDirection);

/**
 * A mouse drag constrained to whichever of the horizontal and vertical screen axes it
 * moves along more.
 */
vm::vec2d constrainDrag(const vm::vec2d& delta);

/**
 * The size after scaling by a mouse drag in pixels (x right, y down). Uniform scaling
 * grows with drags right and up; non-uniform scaling stretches the width with horizontal
 * drags and the height with vertical ones.
 */
vm::vec2d draggedDecalSize(
  const vm::vec2d& startSize, const vm::vec2d& drag, bool nonUniform);

/**
 * A roll in degrees, snapped to the nearest multiple of step degrees.
 */
double snapRoll(double roll, double step);

/**
 * Whether the selection is one or more decal entities and nothing else. Then W, E and R
 * and dragging edit the decals instead of acting on brushes.
 */
bool onlyDecalsSelected(const mdl::Map& map);

/**
 * Turns the selected decal entities about their forward axis, clockwise as seen looking
 * at their surface. One undoable command; false if no decal is selected.
 */
bool rotateSelectedDecals(mdl::Map& map, double degrees);

/**
 * Scales the size of the selected decal entities. One undoable command; false if no decal
 * is selected.
 */
bool scaleSelectedDecals(mdl::Map& map, double factor);

/**
 * What holding W, E or R does to a left mouse drag while the decal tool is active.
 */
enum class DecalManipulation
{
  None,
  // W: slide across surfaces; Shift keeps to one screen axis
  Translate,
  // E: scale; Shift stretches width and height separately
  Scale,
  // R: turn; Shift snaps to 45 degrees
  Rotate,
};

/**
 * Places and edits atlas decals.
 *
 * Armed with a decal from the decal browser, the decal follows the surface under the
 * cursor and a click places and selects it. Clicking a decal selects it. Holding W, E or
 * R and dragging moves, scales or turns the selected decals (or the stamp, when none is
 * selected), Mari style. Alt+wheel and Alt+Shift+wheel turn and scale too. Escape stops.
 */
class DecalStampTool : public Tool
{
private:
  mdl::Map& m_map;
  std::string m_decalId;
  double m_roll = 0.0;
  vm::vec2d m_stretch = {1.0, 1.0};
  DecalManipulation m_manipulation = DecalManipulation::None;
  // The surface under the cursor: hit point, face normal and view direction.
  struct Surface
  {
    vm::vec3d point;
    vm::vec3d normal;
    vm::vec3d viewDirection;
  };
  std::optional<Surface> m_surface;
  QLabel* m_label = nullptr;

public:
  explicit DecalStampTool(mdl::Map& map);

  mdl::Map& map();

  void setDecal(std::string decalId);
  const std::string& decalId() const;
  const unity::UnityCatalogDecal* decal() const;

  /**
   * The size the next decal is stamped at.
   */
  vm::vec2d size() const;
  double roll() const;
  void setRoll(double roll);
  const vm::vec2d& stretch() const;
  void setStretch(const vm::vec2d& stretch);

  /**
   * Stamps continue where an edited decal left off: its turn and its size relative to the
   * decal's default size.
   */
  void adoptFrom(const unity::DecalBox& box, const vm::vec3d& viewDirection);

  DecalManipulation manipulation() const;
  void setManipulation(DecalManipulation manipulation);

  void updatePlacement(const mdl::PickResult& pickResult, const vm::vec3d& viewDirection);
  void clearPlacement();

  /**
   * The box the decal would be stamped with, if the cursor is over a surface.
   */
  std::optional<unity::DecalBox> previewBox() const;

  void rotate(double degrees);
  void scale(double factor);

  /**
   * Places the decal where the preview is and selects it. False if there is no preview.
   */
  bool stamp();

private:
  bool doActivate() override;
  bool doDeactivate() override;
  QWidget* doCreatePage(QWidget* parent) override;
  void updatePage();
};

class DecalStampToolController : public ToolController
{
private:
  DecalStampTool& m_tool;

public:
  explicit DecalStampToolController(DecalStampTool& tool);

  Tool& tool() override;
  const Tool& tool() const override;

  void mouseMove(const InputState& inputState) override;
  bool mouseClick(const InputState& inputState) override;
  void mouseScroll(const InputState& inputState) override;
  std::unique_ptr<GestureTracker> acceptMouseDrag(const InputState& inputState) override;
  bool cancel() override;

  void render(
    const InputState& inputState,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch) override;
};

/**
 * Edits selected decal entities whether or not the decal tool is active: dragging a
 * selected decal slides it across surfaces, holding W, E or R while dragging moves,
 * scales or turns the selected decals, and Alt+wheel and Alt+Shift+wheel turn and scale
 * them. It comes before the move tool in the chain, so decals never move like brushes.
 */
class DecalEditController : public ToolController, public Tool
{
private:
  DecalStampTool& m_stampTool;

public:
  explicit DecalEditController(DecalStampTool& stampTool);

  Tool& tool() override;
  const Tool& tool() const override;

  void mouseScroll(const InputState& inputState) override;
  std::unique_ptr<GestureTracker> acceptMouseDrag(const InputState& inputState) override;
};

} // namespace tb::ui
