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

#include "DecalStampTool.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/HitAdapter.h"
#include "mdl/HitFilter.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Picking.h"
#include "mdl/Map_Selection.h"
#include "mdl/NodeContents.h"
#include "mdl/PickResult.h"
#include "mdl/Selection.h"
#include "mdl/Transaction.h"
#include "mdl/TransactionScope.h"
#include "render/Camera.h"
#include "render/RenderService.h"
#include "ui/GestureTracker.h"
#include "ui/InputState.h"
#include "unity/UnityDecalLibrary.h"

#include "vm/quat.h"
#include "vm/scalar.h"
#include "vm/vec_ext.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace tb::ui
{
namespace
{

constexpr auto RotationStep = 15.0;
constexpr auto RotationSnap = 45.0;
constexpr auto ScaleStep = 1.1;
// how much a pixel of dragging scales: 100 pixels scale by e
constexpr auto ScalePerPixel = 0.01;
// turning by dragging sideways, when the cursor starts too close to the decal's centre
constexpr auto DegreesPerPixel = 0.5;
constexpr auto MinimumDecalSize = 1.0;

/**
 * The component of v along the plane with the given normal, or nullopt if v is (nearly)
 * perpendicular to it.
 */
std::optional<vm::vec3d> projectOntoPlane(const vm::vec3d& v, const vm::vec3d& normal)
{
  const auto projected = v - normal * vm::dot(v, normal);
  return vm::squared_length(projected) > 0.0001 ? std::optional{vm::normalize(projected)}
                                                : std::nullopt;
}

double signedAngle(const vm::vec3d& from, const vm::vec3d& to, const vm::vec3d& axis)
{
  return vm::to_degrees(
    std::atan2(vm::dot(vm::cross(from, to), axis), vm::dot(from, to)));
}

/**
 * An angle in degrees wrapped into [-180, 180).
 */
double wrapDegrees(const double degrees)
{
  return degrees - 360.0 * std::floor((degrees + 180.0) / 360.0);
}

vm::vec2d defaultSize(const mdl::Map& map, const mdl::Entity& entity)
{
  const auto* decalId = entity.property(unity::DecalPropertyKeys::Decal);
  const auto* decal = decalId ? map.decalLibrary().decal(*decalId) : nullptr;
  return decal ? decal->defaultSize : unity::DefaultDecalSize;
}

void setDecalAxes(mdl::Entity& entity, const vm::vec3d& forward, const vm::vec3d& up)
{
  entity.addOrUpdateProperty(
    mdl::EntityPropertyKeys::Angles,
    unity::decalPropertyValue(unity::decalAngles(forward, up)));
}

void setDecalSize(mdl::Entity& entity, const vm::vec2d& size)
{
  entity.addOrUpdateProperty(
    unity::DecalPropertyKeys::Size, unity::decalPropertyValue(size));
}

/**
 * Applies the update to every selected decal entity as one command.
 */
template <typename Update>
bool updateSelectedDecals(mdl::Map& map, const std::string& commandName, Update update)
{
  auto nodesToUpdate = std::vector<std::pair<mdl::Node*, mdl::NodeContents>>{};
  for (auto* entityNode : map.selection().entities)
  {
    if (unity::isDecalEntity(entityNode->entity()))
    {
      auto entity = entityNode->entity();
      update(entity, unity::decalBox(entity, defaultSize(map, entity)));
      nodesToUpdate.emplace_back(entityNode, mdl::NodeContents{std::move(entity)});
    }
  }

  return !nodesToUpdate.empty()
         && mdl::updateNodeContents(map, commandName, std::move(nodesToUpdate));
}

std::vector<mdl::EntityNode*> selectedDecals(const mdl::Map& map)
{
  auto result = std::vector<mdl::EntityNode*>{};
  for (auto* entityNode : map.selection().entities)
  {
    if (unity::isDecalEntity(entityNode->entity()))
    {
      result.push_back(entityNode);
    }
  }
  return result;
}

double scrollSteps(const InputState& inputState)
{
  // Windows turns Alt+wheel into horizontal scrolling.
  const auto scroll =
    inputState.scrollY() != 0.0f ? inputState.scrollY() : inputState.scrollX();
  return scroll > 0.0f ? 1.0 : scroll < 0.0f ? -1.0 : 0.0;
}

vm::vec2d mousePosition(const InputState& inputState)
{
  return {double(inputState.mouseX()), double(inputState.mouseY())};
}

/**
 * A point in screen coordinates as the mouse reports them: origin at the top left.
 */
vm::vec2d screenPosition(const render::Camera& camera, const vm::vec3d& point)
{
  const auto projected = camera.project(vm::vec3f{point});
  return {
    double(projected.x()), double(camera.viewport().height) - double(projected.y())};
}

/**
 * The degrees a drag from start to current turns about the given screen centre, clockwise
 * on screen. Close to the centre the angle jumps around, so there sideways dragging turns
 * instead.
 */
double screenTurn(
  const vm::vec2d& center, const vm::vec2d& start, const vm::vec2d& current)
{
  constexpr auto minimumRadius = 24.0;
  if (vm::length(start - center) < minimumRadius)
  {
    return (current.x() - start.x()) * DegreesPerPixel;
  }

  const auto angle = [&](const vm::vec2d& p) {
    return vm::to_degrees(std::atan2(p.y() - center.y(), p.x() - center.x()));
  };
  return wrapDegrees(angle(current) - angle(start));
}

/**
 * Moves, scales or turns the selected decals while W, E or R is held and the left mouse
 * button drags, or the stamp when no decal is selected. One undoable command per drag.
 */
class DecalTransformTracker : public GestureTracker
{
private:
  struct Target
  {
    mdl::EntityNode* node;
    mdl::Entity startEntity;
    unity::DecalBox startBox;
    vm::vec2d screenOffset;
    vm::vec2d screenCenter;
    vm::vec3d referenceUp;
    double startRoll;
  };

  DecalStampTool& m_tool;
  DecalManipulation m_manipulation;
  vm::vec2d m_startMouse;
  vm::vec3d m_viewDirection;
  std::vector<Target> m_targets;

  // the stamp's settings, when no decal is selected
  double m_startRoll;
  vm::vec2d m_startStretch;
  std::optional<vm::vec2d> m_stampCenter;

public:
  DecalTransformTracker(
    DecalStampTool& tool,
    const DecalManipulation manipulation,
    const InputState& inputState,
    const std::vector<mdl::EntityNode*>& nodes)
    : m_tool{tool}
    , m_manipulation{manipulation}
    , m_startMouse{mousePosition(inputState)}
    , m_viewDirection{vm::vec3d{inputState.camera().direction()}}
    , m_startRoll{tool.roll()}
    , m_startStretch{tool.stretch()}
  {
    const auto& camera = inputState.camera();
    auto& map = m_tool.map();
    for (auto* node : nodes)
    {
      const auto& entity = node->entity();
      const auto box = unity::decalBox(entity, defaultSize(map, entity));
      const auto center = screenPosition(camera, box.origin);
      const auto referenceUp = decalReferenceUp(-box.forward, m_viewDirection);
      m_targets.push_back(
        Target{
          node,
          entity,
          box,
          center - m_startMouse,
          center,
          referenceUp,
          decalRoll(box.forward, box.vAxis, referenceUp)});
    }

    if (!m_targets.empty())
    {
      map.startTransaction(transactionName(), mdl::TransactionScope::LongRunning);
    }
    else if (const auto box = m_tool.previewBox())
    {
      m_stampCenter = screenPosition(camera, box->origin);
    }
  }

  bool update(const InputState& inputState) override
  {
    const auto mouse = mousePosition(inputState);
    const auto shift = inputState.modifierKeysDown(ModifierKeys::Shift);

    if (m_targets.empty())
    {
      updateStamp(mouse, shift);
      return true;
    }

    auto& map = m_tool.map();
    map.rollbackTransaction();

    auto nodesToUpdate = std::vector<std::pair<mdl::Node*, mdl::NodeContents>>{};
    for (const auto& target : m_targets)
    {
      auto entity = target.startEntity;
      switch (m_manipulation)
      {
      case DecalManipulation::Translate:
        translate(inputState, target, mouse, shift, entity);
        break;
      case DecalManipulation::Scale:
        setDecalSize(
          entity, draggedDecalSize(target.startBox.size, mouse - m_startMouse, shift));
        break;
      case DecalManipulation::Rotate:
        rotate(target, mouse, shift, entity);
        break;
      case DecalManipulation::None:
        break;
      }
      nodesToUpdate.emplace_back(target.node, mdl::NodeContents{std::move(entity)});
    }

    mdl::updateNodeContents(map, transactionName(), std::move(nodesToUpdate));
    return true;
  }

  void end(const InputState&) override
  {
    if (m_targets.empty())
    {
      return;
    }

    auto& map = m_tool.map();
    map.commitTransaction();

    // the next stamp continues where this decal left off
    const auto& node = *m_targets.front().node;
    m_tool.adoptFrom(
      unity::decalBox(node.entity(), defaultSize(map, node.entity())), m_viewDirection);
  }

  void cancel() override
  {
    if (m_targets.empty())
    {
      m_tool.setRoll(m_startRoll);
      m_tool.setStretch(m_startStretch);
      return;
    }

    m_tool.map().cancelTransaction();
  }

private:
  std::string transactionName() const
  {
    switch (m_manipulation)
    {
    case DecalManipulation::Translate:
      return "Move Decals";
    case DecalManipulation::Scale:
      return "Scale Decals";
    case DecalManipulation::Rotate:
      return "Rotate Decals";
    case DecalManipulation::None:
      break;
    }
    return "Transform Decals";
  }

  void updateStamp(const vm::vec2d& mouse, const bool shift)
  {
    switch (m_manipulation)
    {
    case DecalManipulation::Scale:
      m_tool.setStretch(draggedDecalSize(m_startStretch, mouse - m_startMouse, shift));
      break;
    case DecalManipulation::Rotate: {
      const auto turn = m_stampCenter ? screenTurn(*m_stampCenter, m_startMouse, mouse)
                                      : (mouse.x() - m_startMouse.x()) * DegreesPerPixel;
      const auto roll = m_startRoll + turn;
      m_tool.setRoll(shift ? snapRoll(roll, RotationSnap) : roll);
      break;
    }
    case DecalManipulation::Translate:
    case DecalManipulation::None:
      break;
    }
  }

  void translate(
    const InputState& inputState,
    const Target& target,
    const vm::vec2d& mouse,
    const bool shift,
    mdl::Entity& entity) const
  {
    using namespace mdl::HitFilters;

    const auto drag = mouse - m_startMouse;
    const auto cursor = m_startMouse + (shift ? constrainDrag(drag) : drag);
    const auto screenPoint = cursor + target.screenOffset;

    const auto ray = vm::ray3d{
      inputState.camera().pickRay(float(screenPoint.x()), float(screenPoint.y()))};
    auto pickResult = mdl::PickResult::byDistance();
    mdl::pick(m_tool.map(), ray, pickResult);

    const auto& hit = pickResult.first(type(mdl::BrushNode::BrushHitType));
    const auto faceHandle = mdl::hitToFaceHandle(hit);
    if (!faceHandle)
    {
      // off every surface: the decal stays where it was
      return;
    }

    const auto& normal = faceHandle->face().boundary().normal;
    entity.setOrigin(hit.hitPoint());
    setDecalAxes(
      entity, -normal, movedDecalUp(target.startBox.vAxis, normal, m_viewDirection));
  }

  void rotate(
    const Target& target,
    const vm::vec2d& mouse,
    const bool shift,
    mdl::Entity& entity) const
  {
    // clockwise on screen is clockwise looking along forward, unless the surface faces
    // away from the camera
    const auto facing = vm::dot(m_viewDirection, target.startBox.forward) >= 0.0;
    const auto turn = screenTurn(target.screenCenter, m_startMouse, mouse);
    const auto roll = target.startRoll + (facing ? turn : -turn);
    const auto snapped = shift ? snapRoll(roll, RotationSnap) : roll;

    const auto& forward = target.startBox.forward;
    const auto up = vm::quatd{forward, vm::to_radians(snapped)} * target.referenceUp;
    setDecalAxes(entity, forward, up);
  }
};

} // namespace

vm::vec3d decalReferenceUp(const vm::vec3d& faceNormal, const vm::vec3d& viewDirection)
{
  const auto normal = vm::normalize(faceNormal);
  const auto fallback =
    projectOntoPlane(vm::vec3d{1, 0, 0}, normal)
      .value_or(
        projectOntoPlane(vm::vec3d{0, 1, 0}, normal).value_or(vm::vec3d{0, 0, 1}));

  if (std::abs(normal.z()) <= 0.7)
  {
    // walls: world up
    return projectOntoPlane(vm::vec3d{0, 0, 1}, normal).value_or(fallback);
  }

  // floors and ceilings: the world axis closest to where the view looks along the surface
  const auto view = projectOntoPlane(viewDirection, normal);
  if (!view)
  {
    return fallback;
  }
  const auto axis = std::abs(view->x()) >= std::abs(view->y())
                      ? vm::vec3d{view->x() > 0.0 ? 1.0 : -1.0, 0, 0}
                      : vm::vec3d{0, view->y() > 0.0 ? 1.0 : -1.0, 0};
  return projectOntoPlane(axis, normal).value_or(fallback);
}

DecalPlacement decalPlacement(
  const vm::vec3d& point,
  const vm::vec3d& faceNormal,
  const vm::vec3d& viewDirection,
  const double roll)
{
  const auto forward = -vm::normalize(faceNormal);
  const auto up = decalReferenceUp(faceNormal, viewDirection);

  // a positive rotation about forward is clockwise when looking along it
  const auto rolledUp = vm::quatd{forward, vm::to_radians(roll)} * up;
  return DecalPlacement{point, forward, vm::normalize(rolledUp)};
}

double decalRoll(
  const vm::vec3d& forward, const vm::vec3d& up, const vm::vec3d& referenceUp)
{
  return signedAngle(referenceUp, up, vm::normalize(forward));
}

vm::vec3d movedDecalUp(
  const vm::vec3d& previousUp,
  const vm::vec3d& faceNormal,
  const vm::vec3d& viewDirection)
{
  return projectOntoPlane(previousUp, vm::normalize(faceNormal))
    .value_or(decalReferenceUp(faceNormal, viewDirection));
}

vm::vec2d constrainDrag(const vm::vec2d& delta)
{
  return std::abs(delta.x()) >= std::abs(delta.y()) ? vm::vec2d{delta.x(), 0.0}
                                                    : vm::vec2d{0.0, delta.y()};
}

vm::vec2d draggedDecalSize(
  const vm::vec2d& startSize, const vm::vec2d& drag, const bool nonUniform)
{
  // dragging up (negative y on screen) grows
  const auto size =
    nonUniform ? vm::vec2d{
                   startSize.x() * std::exp(drag.x() * ScalePerPixel),
                   startSize.y() * std::exp(-drag.y() * ScalePerPixel)}
               : startSize * std::exp((drag.x() - drag.y()) * ScalePerPixel);
  return vm::max(size, vm::vec2d{MinimumDecalSize, MinimumDecalSize});
}

double snapRoll(const double roll, const double step)
{
  return std::round(roll / step) * step;
}

bool onlyDecalsSelected(const mdl::Map& map)
{
  const auto& selection = map.selection();
  return !selection.nodes.empty()
         && std::ranges::all_of(selection.nodes, [](const auto* node) {
              const auto* entityNode = dynamic_cast<const mdl::EntityNode*>(node);
              return entityNode && unity::isDecalEntity(entityNode->entity());
            });
}

bool rotateSelectedDecals(mdl::Map& map, const double degrees)
{
  return updateSelectedDecals(
    map, "Rotate Decals", [&](mdl::Entity& entity, const unity::DecalBox& box) {
      setDecalAxes(
        entity, box.forward, vm::quatd{box.forward, vm::to_radians(degrees)} * box.vAxis);
    });
}

bool scaleSelectedDecals(mdl::Map& map, const double factor)
{
  return updateSelectedDecals(
    map, "Scale Decals", [&](mdl::Entity& entity, const unity::DecalBox& box) {
      setDecalSize(entity, box.size * factor);
    });
}

DecalStampTool::DecalStampTool(mdl::Map& map)
  : Tool{false}
  , m_map{map}
{
}

mdl::Map& DecalStampTool::map()
{
  return m_map;
}

void DecalStampTool::setDecal(std::string decalId)
{
  if (decalId != m_decalId)
  {
    m_decalId = std::move(decalId);
    m_stretch = {1.0, 1.0};
  }
  updatePage();
  refreshViews();
}

const std::string& DecalStampTool::decalId() const
{
  return m_decalId;
}

const unity::UnityCatalogDecal* DecalStampTool::decal() const
{
  return m_map.decalLibrary().decal(m_decalId);
}

vm::vec2d DecalStampTool::size() const
{
  const auto* catalogDecal = decal();
  const auto base = catalogDecal ? catalogDecal->defaultSize : unity::DefaultDecalSize;
  return {base.x() * m_stretch.x(), base.y() * m_stretch.y()};
}

double DecalStampTool::roll() const
{
  return m_roll;
}

void DecalStampTool::setRoll(const double roll)
{
  m_roll = wrapDegrees(roll);
  updatePage();
  refreshViews();
}

const vm::vec2d& DecalStampTool::stretch() const
{
  return m_stretch;
}

void DecalStampTool::setStretch(const vm::vec2d& stretch)
{
  m_stretch = stretch;
  updatePage();
  refreshViews();
}

void DecalStampTool::adoptFrom(const unity::DecalBox& box, const vm::vec3d& viewDirection)
{
  const auto referenceUp = decalReferenceUp(-box.forward, viewDirection);
  m_roll = wrapDegrees(decalRoll(box.forward, box.vAxis, referenceUp));
  if (const auto* catalogDecal = decal())
  {
    const auto& base = catalogDecal->defaultSize;
    m_stretch = {box.size.x() / base.x(), box.size.y() / base.y()};
  }
  updatePage();
}

DecalManipulation DecalStampTool::manipulation() const
{
  return m_manipulation;
}

void DecalStampTool::setManipulation(const DecalManipulation manipulation)
{
  m_manipulation = manipulation;
}

void DecalStampTool::updatePlacement(
  const mdl::PickResult& pickResult, const vm::vec3d& viewDirection)
{
  using namespace mdl::HitFilters;

  const auto& hit = pickResult.first(type(mdl::BrushNode::BrushHitType));
  if (const auto faceHandle = mdl::hitToFaceHandle(hit))
  {
    m_surface =
      Surface{hit.hitPoint(), faceHandle->face().boundary().normal, viewDirection};
  }
  else
  {
    m_surface = std::nullopt;
  }
}

void DecalStampTool::clearPlacement()
{
  m_surface = std::nullopt;
}

std::optional<unity::DecalBox> DecalStampTool::previewBox() const
{
  if (!m_surface || !decal())
  {
    return std::nullopt;
  }

  const auto placement =
    decalPlacement(m_surface->point, m_surface->normal, m_surface->viewDirection, m_roll);
  const auto angles = unity::decalAngles(placement.forward, placement.up);
  const auto rotation = vm::rotation_matrix(
    vm::to_radians(angles.z()), vm::to_radians(angles.x()), vm::to_radians(angles.y()));
  return unity::decalBox(placement.origin, rotation, size());
}

void DecalStampTool::rotate(const double degrees)
{
  setRoll(m_roll + degrees);
}

void DecalStampTool::scale(const double factor)
{
  setStretch(m_stretch * factor);
}

bool DecalStampTool::stamp()
{
  if (!m_surface || !decal())
  {
    return false;
  }

  const auto placement =
    decalPlacement(m_surface->point, m_surface->normal, m_surface->viewDirection, m_roll);
  auto* node = new mdl::EntityNode{mdl::Entity{unity::decalProperties(
    m_decalId,
    placement.origin,
    unity::decalAngles(placement.forward, placement.up),
    size())}};

  auto transaction = mdl::Transaction{m_map, "Stamp Decal"};
  if (mdl::addNodes(m_map, {{mdl::parentForNodes(m_map), {node}}}).empty())
  {
    transaction.cancel();
    return false;
  }

  // selected, so W, E and R adjust it straight away
  mdl::deselectAll(m_map);
  mdl::selectNodes(m_map, {node});
  return transaction.commit();
}

bool DecalStampTool::doDeactivate()
{
  m_surface = std::nullopt;
  m_manipulation = DecalManipulation::None;
  return true;
}

QWidget* DecalStampTool::doCreatePage(QWidget* parent)
{
  auto* page = new QWidget{parent};
  m_label = new QLabel{};

  auto* layout = new QHBoxLayout{};
  layout->setContentsMargins(QMargins{});
  layout->addWidget(m_label);
  layout->addStretch(1);
  page->setLayout(layout);

  updatePage();
  return page;
}

void DecalStampTool::updatePage()
{
  if (!m_label)
  {
    return;
  }

  const auto stamp =
    m_decalId.empty()
      ? QObject::tr("Decals: click a decal to select it.")
      : QObject::tr(
          "Stamp %1 (%2 x %3, %4°): click a surface to place, a decal to select.")
          .arg(QString::fromStdString(m_decalId))
          .arg(std::round(size().x()))
          .arg(std::round(size().y()))
          .arg(std::round(m_roll));
  m_label->setText(
    stamp
    + QObject::tr(
      " Hold W/E/R and drag to move/scale/turn the selection (or the stamp); "
      "Shift constrains, stretches, snaps. Esc to stop."));
}

DecalStampToolController::DecalStampToolController(DecalStampTool& tool)
  : m_tool{tool}
{
}

Tool& DecalStampToolController::tool()
{
  return m_tool;
}

const Tool& DecalStampToolController::tool() const
{
  return m_tool;
}

void DecalStampToolController::mouseMove(const InputState& inputState)
{
  m_tool.updatePlacement(
    inputState.pickResult(), vm::vec3d{inputState.camera().direction()});
  refreshViews();
}

bool DecalStampToolController::mouseClick(const InputState& inputState)
{
  using namespace mdl::HitFilters;

  if (
    !inputState.mouseButtonsPressed(MouseButtons::Left)
    || !inputState.modifierKeysPressed(ModifierKeys::None))
  {
    return false;
  }

  if (m_tool.manipulation() != DecalManipulation::None)
  {
    // a click while W, E or R is held is a drag that didn't move
    return true;
  }

  // clicking a decal selects it rather than stamping over it
  const auto& hit = inputState.pickResult().first(
    type(mdl::EntityNode::EntityHitType | mdl::BrushNode::BrushHitType));
  if (
    auto* entityNode = dynamic_cast<mdl::EntityNode*>(mdl::hitToNode(hit));
    entityNode && unity::isDecalEntity(entityNode->entity()))
  {
    auto& map = m_tool.map();
    mdl::deselectAll(map);
    mdl::selectNodes(map, {entityNode});
    return true;
  }

  m_tool.updatePlacement(
    inputState.pickResult(), vm::vec3d{inputState.camera().direction()});

  // without a stamp, clicks select as usual
  return m_tool.stamp();
}

void DecalStampToolController::mouseScroll(const InputState& inputState)
{
  const auto steps = scrollSteps(inputState);
  if (steps == 0.0)
  {
    return;
  }

  // selected decals are turned and scaled by DecalEditController
  if (!selectedDecals(m_tool.map()).empty())
  {
    return;
  }

  if (inputState.modifierKeysPressed(ModifierKeys::Alt | ModifierKeys::Shift))
  {
    m_tool.scale(std::pow(ScaleStep, steps));
  }
  else if (inputState.modifierKeysPressed(ModifierKeys::Alt))
  {
    m_tool.rotate(RotationStep * steps);
  }
}

std::unique_ptr<GestureTracker> DecalStampToolController::acceptMouseDrag(
  const InputState& inputState)
{
  const auto manipulation = m_tool.manipulation();
  if (
    manipulation == DecalManipulation::None
    || !inputState.mouseButtonsPressed(MouseButtons::Left))
  {
    return nullptr;
  }

  // selected decals are edited by DecalEditController; this only adjusts the stamp
  if (
    !selectedDecals(m_tool.map()).empty() || manipulation == DecalManipulation::Translate
    || !m_tool.decal())
  {
    return nullptr;
  }

  return std::make_unique<DecalTransformTracker>(
    m_tool, manipulation, inputState, std::vector<mdl::EntityNode*>{});
}

bool DecalStampToolController::cancel()
{
  // Escape deselects decals first and only then leaves the tool
  auto& map = m_tool.map();
  if (selectedDecals(map).empty())
  {
    return false;
  }
  mdl::deselectAll(map);
  return true;
}

void DecalStampToolController::render(
  const InputState&,
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch)
{
  // while decals are selected, the stamp outline would only get in the way of editing
  // them
  if (!selectedDecals(m_tool.map()).empty())
  {
    return;
  }

  const auto box = m_tool.previewBox();
  if (!box)
  {
    return;
  }

  const auto f = box->forward * (box->depth / 2.0);
  const auto u = box->uAxis * (box->size.x() / 2.0);
  const auto v = box->vAxis * (box->size.y() / 2.0);
  const auto o = box->origin;
  const auto p = [](const vm::vec3d& point) { return vm::vec3f{point}; };

  auto renderService = render::RenderService{renderContext, renderBatch};
  renderService.setForegroundColor(pref(Preferences::HandleColor));
  renderService.setShowOccludedObjectsTransparent();

  // the decal's outline on the surface, and the box it projects through
  renderService.setLineWidth(2.0f);
  renderService.renderPolygonOutline(
    {p(o - u - v), p(o + u - v), p(o + u + v), p(o - u + v)});
  renderService.setLineWidth(1.0f);
  auto edges = std::vector<vm::vec3f>{};
  for (const auto sf : {-1.0, 1.0})
  {
    const auto c = o + f * sf;
    edges.insert(
      edges.end(),
      {p(c - u - v),
       p(c + u - v),
       p(c + u - v),
       p(c + u + v),
       p(c + u + v),
       p(c - u + v),
       p(c - u + v),
       p(c - u - v)});
  }
  for (const auto& corner : {-u - v, u - v, u + v, -u + v})
  {
    edges.push_back(p(o - f + corner));
    edges.push_back(p(o + f + corner));
  }
  // which way is up
  edges.push_back(p(o));
  edges.push_back(p(o + v * 1.25));
  renderService.renderLines(edges);
}

DecalEditController::DecalEditController(DecalStampTool& stampTool)
  : Tool{true}
  , m_stampTool{stampTool}
{
}

Tool& DecalEditController::tool()
{
  return *this;
}

const Tool& DecalEditController::tool() const
{
  return *this;
}

void DecalEditController::mouseScroll(const InputState& inputState)
{
  auto& map = m_stampTool.map();
  const auto steps = scrollSteps(inputState);
  if (steps == 0.0 || selectedDecals(map).empty())
  {
    return;
  }

  if (inputState.modifierKeysPressed(ModifierKeys::Alt | ModifierKeys::Shift))
  {
    scaleSelectedDecals(map, std::pow(ScaleStep, steps));
  }
  else if (inputState.modifierKeysPressed(ModifierKeys::Alt))
  {
    rotateSelectedDecals(map, RotationStep * steps);
  }
}

std::unique_ptr<GestureTracker> DecalEditController::acceptMouseDrag(
  const InputState& inputState)
{
  using namespace mdl::HitFilters;

  if (!inputState.mouseButtonsPressed(MouseButtons::Left))
  {
    return nullptr;
  }

  auto& map = m_stampTool.map();
  const auto nodes = selectedDecals(map);
  if (nodes.empty())
  {
    return nullptr;
  }

  if (
    const auto manipulation = m_stampTool.manipulation();
    manipulation != DecalManipulation::None)
  {
    return std::make_unique<DecalTransformTracker>(
      m_stampTool, manipulation, inputState, nodes);
  }

  // a plain drag that starts on a selected decal slides it along the surfaces, like W
  if (!inputState.modifierKeysPressed(ModifierKeys::None))
  {
    return nullptr;
  }
  const auto& hit = inputState.pickResult().first(
    type(mdl::EntityNode::EntityHitType | mdl::BrushNode::BrushHitType));
  const auto* hitNode = mdl::hitToNode(hit);
  if (!hitNode || !hitNode->selected())
  {
    return nullptr;
  }
  if (
    const auto* entityNode = dynamic_cast<const mdl::EntityNode*>(hitNode);
    !entityNode || !unity::isDecalEntity(entityNode->entity()))
  {
    return nullptr;
  }

  return std::make_unique<DecalTransformTracker>(
    m_stampTool, DecalManipulation::Translate, inputState, nodes);
}

} // namespace tb::ui
