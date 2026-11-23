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
#include "mdl/Layer.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/NodeContents.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"

#include "kdl/vector_utils.h"

#include "vm/mat.h"

#include <algorithm>
#include <map>

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

Result<std::vector<Brush>> layoutBrushesFor(const Map& map, const RoomRecipe& layout)
{
  if (layout.spaces.empty())
  {
    return std::vector<Brush>{};
  }
  return buildRoomBrushes(map, layout, vm::mat4x4d::identity());
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
  auto newBrushes = layoutBrushesFor(map, newLayout);
  if (newBrushes.is_error())
  {
    return std::get<Error>(newBrushes.error());
  }

  auto oldLayout = readRoomLayout(map);
  auto oldBrushes = oldLayout.is_success()
                      ? layoutBrushesFor(map, oldLayout.value())
                      : Result<std::vector<Brush>>{std::vector<Brush>{}};
  if (oldBrushes.is_error())
  {
    return std::move(newBrushes).value();
  }

  // Only show what changes; brushes the layout already has are drawn by the map anyway.
  auto indices = std::vector<size_t>{};
  for (size_t i = 0; i < newBrushes.value().size(); ++i)
  {
    indices.push_back(i);
  }
  const auto unchanged =
    matching(indices, oldBrushes.value(), [&](const auto i) -> const Brush& {
      return newBrushes.value()[i];
    });

  auto result = std::vector<Brush>{};
  for (size_t i = 0; i < newBrushes.value().size(); ++i)
  {
    if (std::ranges::find(unchanged, i) == unchanged.end())
    {
      result.push_back(newBrushes.value()[i]);
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

  if (!nodesToAdd.empty() && addNodes(map, nodesToAdd).empty())
  {
    transaction.cancel();
    return Error{"Could not add the room layout's brushes"};
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

} // namespace tb::mdl
