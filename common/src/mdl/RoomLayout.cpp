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

#include "RoomLayout.h"

#include "mdl/Brush.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/Group.h"
#include "mdl/GroupNode.h"
#include "mdl/Layer.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_NodeLocking.h"
#include "mdl/Map_NodeVisibility.h"
#include "mdl/Map_Nodes.h"
#include "mdl/NodeContents.h"
#include "mdl/PatchNode.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"

#include "kdl/overload.h"
#include "kdl/vector_utils.h"

#include "vm/mat.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

namespace tb::mdl
{

namespace RoomLayoutLayers
{
const std::string Walls = "walls";
const std::string Floors = "floors";
const std::string Ceilings = "ceilings";
} // namespace RoomLayoutLayers

namespace
{

LayerNode* findOrCreateLayer(Map& map, const std::string& name)
{
  if (auto* layerNode = findLayer(map, name))
  {
    return layerNode;
  }

  auto layer = Layer{name};

  // Sort it at the bottom of the list, like layers created in the layer editor.
  const auto customLayers = map.world()->customLayersUserSorted();
  layer.setSortIndex(
    !customLayers.empty() ? customLayers.back()->layer().sortIndex() + 1 : 0);

  auto* layerNode = new LayerNode{std::move(layer)};
  if (addNodes(map, {{map.world(), {layerNode}}}).empty())
  {
    return nullptr;
  }
  return layerNode;
}

/** The brushes the layout layers contain directly. */
std::vector<BrushNode*> layoutBrushes(const Map& map)
{
  auto result = std::vector<BrushNode*>{};
  for (const auto* name :
       {&RoomLayoutLayers::Walls, &RoomLayoutLayers::Floors, &RoomLayoutLayers::Ceilings})
  {
    if (const auto* layerNode = findLayer(map, *name))
    {
      for (auto* child : layerNode->children())
      {
        if (auto* brushNode = dynamic_cast<BrushNode*>(child))
        {
          result.push_back(brushNode);
        }
      }
    }
  }
  return result;
}

/** Removes from candidates every brush that matches one of expected (each at most once).
 */
template <typename Candidate, typename GetBrush>
std::vector<Candidate> matching(
  const std::vector<Candidate>& candidates,
  const std::vector<Brush>& expected,
  const GetBrush& getBrush)
{
  auto used = std::vector<bool>(expected.size(), false);
  auto result = std::vector<Candidate>{};
  for (const auto& candidate : candidates)
  {
    for (size_t i = 0; i < expected.size(); ++i)
    {
      if (!used[i] && roomBrushMatches(getBrush(candidate), expected[i]))
      {
        used[i] = true;
        result.push_back(candidate);
        break;
      }
    }
  }
  return result;
}

const std::string& layerNameForSurface(const RoomSurface surface)
{
  switch (surface)
  {
  case RoomSurface::Floor:
    return RoomLayoutLayers::Floors;
  case RoomSurface::Ceiling:
    return RoomLayoutLayers::Ceilings;
  case RoomSurface::Wall:
    return RoomLayoutLayers::Walls;
  }
  return RoomLayoutLayers::Walls;
}

bool surfaceLayerHidden(const Map& map, const RoomSurface surface)
{
  const auto* layerNode = findLayer(map, layerNameForSurface(surface));
  return layerNode && !layerNode->visible();
}

Result<std::vector<Brush>> layoutBrushesFor(const Map& map, const RoomRecipe& layout)
{
  if (layout.spaces.empty())
  {
    return std::vector<Brush>{};
  }
  return buildRoomBrushes(map, layout, vm::mat4x4d::identity());
}

Result<std::vector<std::pair<Brush, RoomSurface>>> layoutBrushesBySurfaceFor(
  const Map& map, const RoomRecipe& layout)
{
  if (layout.spaces.empty())
  {
    return std::vector<std::pair<Brush, RoomSurface>>{};
  }
  return buildRoomBrushesBySurface(map, layout, vm::mat4x4d::identity());
}

} // namespace

LayerNode* findLayer(const Map& map, const std::string& name)
{
  for (auto* layerNode : map.world()->allLayers())
  {
    if (layerNode->name() == name)
    {
      return layerNode;
    }
  }
  return nullptr;
}

Result<RoomRecipe> readRoomLayout(const Map& map)
{
  const auto* wallsLayer = findLayer(map, RoomLayoutLayers::Walls);
  if (!wallsLayer || !hasRoomRecipe(wallsLayer->layer()))
  {
    return RoomRecipe{};
  }
  return readRoomRecipe(wallsLayer->layer());
}

Result<std::vector<Brush>> roomLayoutPreview(const Map& map, const RoomRecipe& newLayout)
{
  auto newBrushes = layoutBrushesBySurfaceFor(map, newLayout);
  if (newBrushes.is_error())
  {
    return std::get<Error>(newBrushes.error());
  }

  auto visibleNewBrushes = std::vector<Brush>{};
  for (auto& [brush, surface] : newBrushes.value())
  {
    if (!surfaceLayerHidden(map, surface))
    {
      visibleNewBrushes.push_back(std::move(brush));
    }
  }

  auto oldLayout = readRoomLayout(map);
  auto oldBrushes = oldLayout.is_success()
                      ? layoutBrushesFor(map, oldLayout.value())
                      : Result<std::vector<Brush>>{std::vector<Brush>{}};
  if (oldBrushes.is_error())
  {
    return visibleNewBrushes;
  }

  // Only show what changes; brushes the layout already has are drawn by the map anyway.
  auto indices = std::vector<size_t>{};
  for (size_t i = 0; i < visibleNewBrushes.size(); ++i)
  {
    indices.push_back(i);
  }
  const auto unchanged =
    matching(indices, oldBrushes.value(), [&](const auto i) -> const Brush& {
      return visibleNewBrushes[i];
    });

  auto result = std::vector<Brush>{};
  for (size_t i = 0; i < visibleNewBrushes.size(); ++i)
  {
    if (std::ranges::find(unchanged, i) == unchanged.end())
    {
      result.push_back(visibleNewBrushes[i]);
    }
  }
  return result;
}

Result<void> updateRoomLayout(
  Map& map, const RoomRecipe& layout, const RoomLightOptions& lightOptions)
{
  auto oldLayout = readRoomLayout(map);
  if (oldLayout.is_error())
  {
    return std::get<Error>(oldLayout.error());
  }
  auto oldBrushes = layoutBrushesFor(map, oldLayout.value());
  if (oldBrushes.is_error())
  {
    return std::get<Error>(oldBrushes.error());
  }

  auto newBrushes = layout.spaces.empty()
                      ? Result<std::vector<std::pair<Brush, RoomSurface>>>{std::vector<
                          std::pair<Brush, RoomSurface>>{}}
                      : buildRoomBrushesBySurface(map, layout, vm::mat4x4d::identity());
  if (newBrushes.is_error())
  {
    return std::get<Error>(newBrushes.error());
  }

  auto transaction = Transaction{map, "Update Room Layout"};

  auto* wallsLayer = layout.spaces.empty()
                       ? findLayer(map, RoomLayoutLayers::Walls)
                       : findOrCreateLayer(map, RoomLayoutLayers::Walls);
  auto* floorsLayer = layout.spaces.empty()
                        ? findLayer(map, RoomLayoutLayers::Floors)
                        : findOrCreateLayer(map, RoomLayoutLayers::Floors);
  auto* ceilingsLayer = layout.spaces.empty()
                          ? findLayer(map, RoomLayoutLayers::Ceilings)
                          : findOrCreateLayer(map, RoomLayoutLayers::Ceilings);
  if (!layout.spaces.empty() && (!wallsLayer || !floorsLayer || !ceilingsLayer))
  {
    transaction.cancel();
    return Error{"Could not create the room layout layers"};
  }

  // What the previous layout generated; anything else in the layers stays.
  auto nodesToRemove = kdl::vec_static_cast<Node*>(matching(
    layoutBrushes(map), oldBrushes.value(), [](const auto* brushNode) -> const Brush& {
      return brushNode->brush();
    }));
  if (wallsLayer)
  {
    for (auto* child : wallsLayer->children())
    {
      if (isGeneratedRoomLight(child))
      {
        nodesToRemove.push_back(child);
      }
    }
  }

  auto nodesToAdd = std::map<Node*, std::vector<Node*>>{};
  for (auto& [brush, surface] : newBrushes.value())
  {
    auto* layer = surface == RoomSurface::Floor     ? floorsLayer
                  : surface == RoomSurface::Ceiling ? ceilingsLayer
                                                    : wallsLayer;
    nodesToAdd[layer].push_back(new BrushNode{std::move(brush)});
  }
  if (!layout.spaces.empty())
  {
    auto lights = createRoomLightNodes(layout, vm::mat4x4d::identity(), lightOptions);
    if (!lights.empty())
    {
      auto& wallNodes = nodesToAdd[wallsLayer];
      wallNodes.insert(wallNodes.end(), lights.begin(), lights.end());
    }
  }

  if (!nodesToAdd.empty())
  {
    const auto addedNodes = addNodes(map, nodesToAdd);
    if (addedNodes.empty())
    {
      transaction.cancel();
      return Error{"Could not add the room layout's brushes"};
    }

    // addNodes forces newly added children to Shown/Unlocked when their parent layer is
    // hidden or locked; downgrade them back to Inherited so layer state is preserved.
    downgradeShownToInherit(map, addedNodes);
    downgradeUnlockedToInherit(map, addedNodes);
  }
  if (!nodesToRemove.empty())
  {
    removeNodes(map, nodesToRemove);
  }

  if (wallsLayer)
  {
    auto layer = wallsLayer->layer();
    if (layout.spaces.empty())
    {
      removeRoomRecipe(layer);
    }
    else
    {
      writeRoomRecipe(layer, layout);
    }
    if (!updateNodeContents(
          map, "Update Room Layout", {{wallsLayer, NodeContents{std::move(layer)}}}))
    {
      transaction.cancel();
      return Error{"Could not store the room layout"};
    }
  }

  if (!transaction.commit())
  {
    return Error{"Could not commit the room layout"};
  }
  return kdl::void_success;
}

namespace
{

constexpr auto DollhouseEpsilon = 0.01;
constexpr auto DollhouseProbe = 0.5;

bool overlaps1D(
  const double aMin, const double aMax, const double bMin, const double bMax)
{
  return std::min(aMax, bMax) - std::max(aMin, bMin) > DollhouseEpsilon;
}

bool strictlyContains(const vm::bbox3d& box, const vm::vec3d& point)
{
  return point.x() > box.min.x() + DollhouseEpsilon
         && point.x() < box.max.x() - DollhouseEpsilon
         && point.y() > box.min.y() + DollhouseEpsilon
         && point.y() < box.max.y() - DollhouseEpsilon
         && point.z() > box.min.z() + DollhouseEpsilon
         && point.z() < box.max.z() - DollhouseEpsilon;
}

vm::bbox3d transformSpace(const vm::bbox3d& space, const vm::mat4x4d& transformation)
{
  auto builder = vm::bbox3d::builder{};
  space.for_each_vertex(
    [&](const auto& corner) { builder.add(transformation * corner); });
  return builder.bounds();
}

bool hasAdjacentSpaceOnSide(
  const std::vector<vm::bbox3d>& spaces,
  const vm::bbox3d& wall,
  const size_t axis,
  const int dir)
{
  const auto other = 1u - axis;
  const auto probe = dir > 0 ? wall.max[axis] + DollhouseProbe
                             : wall.min[axis] - DollhouseProbe;

  for (const auto& space : spaces)
  {
    if (
      overlaps1D(wall.min.z(), wall.max.z(), space.min.z(), space.max.z())
      && overlaps1D(wall.min[other], wall.max[other], space.min[other], space.max[other])
      && probe > space.min[axis] && probe < space.max[axis])
    {
      return true;
    }
  }
  return false;
}

std::optional<bool> shouldPeelAlongAxis(
  const std::vector<vm::bbox3d>& spaces,
  const vm::bbox3d& wall,
  const size_t axis,
  const vm::vec3d& cameraPosition)
{
  const auto negSide = hasAdjacentSpaceOnSide(spaces, wall, axis, -1);
  const auto posSide = hasAdjacentSpaceOnSide(spaces, wall, axis, +1);

  if (posSide && !negSide)
  {
    // Exterior wall on the -axis side of a room.
    return cameraPosition[axis] < wall.max[axis] - DollhouseEpsilon;
  }
  if (negSide && !posSide)
  {
    // Exterior wall on the +axis side of a room.
    return cameraPosition[axis] > wall.min[axis] + DollhouseEpsilon;
  }
  if (negSide && posSide)
  {
    // Interior partition wall dividing two rooms: keep visible so floorplans stay clear.
    return false;
  }
  return std::nullopt;
}

bool shouldPeelWall(
  const std::vector<vm::bbox3d>& spaces,
  const vm::bbox3d& wall,
  const vm::vec3d& cameraPosition)
{
  if (spaces.empty())
  {
    return false;
  }

  const auto size = wall.size();
  if (size.x() < size.y() - DollhouseEpsilon)
  {
    if (const auto peel = shouldPeelAlongAxis(spaces, wall, 0u, cameraPosition))
    {
      return *peel;
    }
  }
  else if (size.y() < size.x() - DollhouseEpsilon)
  {
    if (const auto peel = shouldPeelAlongAxis(spaces, wall, 1u, cameraPosition))
    {
      return *peel;
    }
  }
  else
  {
    const auto peelX = shouldPeelAlongAxis(spaces, wall, 0u, cameraPosition);
    const auto peelY = shouldPeelAlongAxis(spaces, wall, 1u, cameraPosition);
    if (peelX && peelY)
    {
      return *peelX || *peelY;
    }
    if (peelX)
    {
      return *peelX;
    }
    if (peelY)
    {
      return *peelY;
    }
  }

  // Fallback for ceiling-level wall caps (e.g. above full-height doorways) and diagonal
  // outer corner columns.
  for (const auto& space : spaces)
  {
    if (
      wall.min.z() >= space.max.z() - DollhouseEpsilon
      && overlaps1D(wall.min.x(), wall.max.x(), space.min.x(), space.max.x())
      && overlaps1D(wall.min.y(), wall.max.y(), space.min.y(), space.max.y()))
    {
      return cameraPosition.z() > wall.min.z() + DollhouseEpsilon;
    }

    if (overlaps1D(wall.min.z(), wall.max.z(), space.min.z(), space.max.z()))
    {
      const auto touchesNegX = std::abs(wall.max.x() - space.min.x()) <= DollhouseEpsilon;
      const auto touchesPosX = std::abs(wall.min.x() - space.max.x()) <= DollhouseEpsilon;
      const auto touchesNegY = std::abs(wall.max.y() - space.min.y()) <= DollhouseEpsilon;
      const auto touchesPosY = std::abs(wall.min.y() - space.max.y()) <= DollhouseEpsilon;

      if ((touchesNegX || touchesPosX) && (touchesNegY || touchesPosY))
      {
        const auto outsideX = touchesNegX
                                ? cameraPosition.x() < wall.max.x() - DollhouseEpsilon
                                : cameraPosition.x() > wall.min.x() + DollhouseEpsilon;
        const auto outsideY = touchesNegY
                                ? cameraPosition.y() < wall.max.y() - DollhouseEpsilon
                                : cameraPosition.y() > wall.min.y() + DollhouseEpsilon;
        if (outsideX && outsideY)
        {
          return true;
        }
      }
    }
  }

  return false;
}

bool shouldPeelCeiling(const vm::bbox3d& ceiling, const vm::vec3d& cameraPosition)
{
  return cameraPosition.z() > ceiling.min.z() + DollhouseEpsilon;
}

} // namespace

std::unordered_set<const BrushNode*> collectDollhouseHiddenBrushes(
  const Map& map, const vm::vec3d& cameraPosition)
{
  auto result = std::unordered_set<const BrushNode*>{};
  const auto* world = map.world();
  if (!world)
  {
    return result;
  }

  auto allSpaces = std::vector<vm::bbox3d>{};

  // 1. Collect room layout spaces and candidate brushes from layout layers.
  auto layoutSpaces = std::vector<vm::bbox3d>{};
  if (auto layout = readRoomLayout(map); layout.is_success())
  {
    layoutSpaces = std::move(layout).value().spaces;
    allSpaces.insert(allSpaces.end(), layoutSpaces.begin(), layoutSpaces.end());
  }

  auto ceilingBrushes = std::vector<const BrushNode*>{};
  auto layoutWallBrushes = std::vector<const BrushNode*>{};

  if (const auto* ceilingsLayer = findLayer(map, RoomLayoutLayers::Ceilings))
  {
    for (const auto* child : ceilingsLayer->children())
    {
      if (const auto* brushNode = dynamic_cast<const BrushNode*>(child);
          brushNode && !brushNode->selected())
      {
        ceilingBrushes.push_back(brushNode);
      }
    }
  }

  if (const auto* wallsLayer = findLayer(map, RoomLayoutLayers::Walls))
  {
    for (const auto* child : wallsLayer->children())
    {
      if (const auto* brushNode = dynamic_cast<const BrushNode*>(child);
          brushNode && !brushNode->selected())
      {
        layoutWallBrushes.push_back(brushNode);
      }
    }
  }

  // 2. Collect room groups (Box / Floor mode rooms) and classify their brushes.
  struct RoomGroupCandidates
  {
    std::vector<vm::bbox3d> spaces;
    std::vector<const BrushNode*> walls;
  };
  auto roomGroups = std::vector<RoomGroupCandidates>{};

  world->accept(kdl::overload(
    [](auto&& thisLambda, const WorldNode* worldNode) {
      worldNode->visitChildren(thisLambda);
    },
    [](auto&& thisLambda, const LayerNode* layerNode) {
      layerNode->visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, const GroupNode* groupNode) {
      if (isRoom(groupNode->group()))
      {
        if (auto recipe = readRoomRecipe(groupNode->group());
            recipe.is_success() && !recipe.value().spaces.empty())
        {
          const auto& transform = groupNode->group().transformation();
          auto candidate = RoomGroupCandidates{};
          for (const auto& space : recipe.value().spaces)
          {
            candidate.spaces.push_back(transformSpace(space, transform));
          }
          allSpaces.insert(
            allSpaces.end(), candidate.spaces.begin(), candidate.spaces.end());

          for (const auto* child : groupNode->children())
          {
            if (const auto* brushNode = dynamic_cast<const BrushNode*>(child);
                brushNode && !brushNode->selected())
            {
              const auto& bounds = brushNode->logicalBounds();
              auto isFloor = false;
              auto isCeiling = false;
              for (const auto& space : candidate.spaces)
              {
                if (
                  overlaps1D(bounds.min.x(), bounds.max.x(), space.min.x(), space.max.x())
                  && overlaps1D(
                    bounds.min.y(), bounds.max.y(), space.min.y(), space.max.y()))
                {
                  if (bounds.max.z() <= space.min.z() + DollhouseEpsilon)
                  {
                    isFloor = true;
                  }
                  else if (bounds.min.z() >= space.max.z() - DollhouseEpsilon)
                  {
                    isCeiling = true;
                  }
                }
              }

              if (isFloor)
              {
                continue;
              }
              if (isCeiling)
              {
                ceilingBrushes.push_back(brushNode);
              }
              else
              {
                candidate.walls.push_back(brushNode);
              }
            }
          }

          roomGroups.push_back(std::move(candidate));
        }
      }
      else
      {
        groupNode->visitChildren(thisLambda);
      }
    },
    [](const EntityNode*) {},
    [](const BrushNode*) {},
    [](const PatchNode*) {}));

  // When the camera flies inside any room interior, show the full room unmodified.
  for (const auto& space : allSpaces)
  {
    if (strictlyContains(space, cameraPosition))
    {
      return result;
    }
  }

  for (const auto* brushNode : ceilingBrushes)
  {
    if (shouldPeelCeiling(brushNode->logicalBounds(), cameraPosition))
    {
      result.insert(brushNode);
    }
  }

  for (const auto* brushNode : layoutWallBrushes)
  {
    if (shouldPeelWall(layoutSpaces, brushNode->logicalBounds(), cameraPosition))
    {
      result.insert(brushNode);
    }
  }

  for (const auto& group : roomGroups)
  {
    for (const auto* brushNode : group.walls)
    {
      if (shouldPeelWall(group.spaces, brushNode->logicalBounds(), cameraPosition))
      {
        result.insert(brushNode);
      }
    }
  }

  return result;
}

} // namespace tb::mdl
