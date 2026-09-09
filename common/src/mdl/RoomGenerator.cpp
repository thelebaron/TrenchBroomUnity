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

#include "RoomGenerator.h"

#include "mdl/AddRemoveNodesCommand.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Game.h"
#include "mdl/Layer.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/NodeContents.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"

#include <map>

namespace tb::mdl
{
namespace
{

LayerNode* findLayer(const Map& map, const std::string& name)
{
  const auto layers = map.world()->allLayers();
  for (auto* layer : layers)
  {
    if (layer->name() == name)
    {
      return layer;
    }
  }
  return nullptr;
}

LayerNode* findOrCreateLayer(Map& map, const std::string& name)
{
  if (auto* layer = findLayer(map, name))
  {
    return layer;
  }

  auto* layer = new LayerNode{Layer{name}};
  if (addNodes(map, {{map.world(), {layer}}}).empty())
  {
    delete layer;
    return nullptr;
  }
  return layer;
}

Result<BrushNode*> makeBrush(
  const BrushBuilder& builder,
  const vm::bbox3d& bounds,
  const std::string& material)
{
  auto result = builder.createCuboid(bounds, material);
  if (result.is_error())
  {
    return std::get<Error>(result.error());
  }
  return new BrushNode{std::move(result.value())};
}

Result<void> addBrush(
  const BrushBuilder& builder,
  const vm::bbox3d& bounds,
  const std::string& material,
  LayerNode* layer,
  std::map<Node*, std::vector<Node*>>& nodesToAdd,
  std::vector<BrushNode*>& generatedNodes)
{
  auto result = makeBrush(builder, bounds, material);
  if (result.is_error())
  {
    return std::get<Error>(result.error());
  }

  auto* brushNode = result.value();
  nodesToAdd[layer].push_back(brushNode);
  generatedNodes.push_back(brushNode);
  return kdl::void_success;
}

Result<void> addRoom(
  const BrushBuilder& builder,
  const RoomGenerationSettings& settings,
  const RoomGenerationSource& source,
  LayerNode* wallsLayer,
  LayerNode* ceilingsLayer,
  LayerNode* floorsLayer,
  std::map<Node*, std::vector<Node*>>& nodesToAdd,
  std::vector<BrushNode*>& generatedNodes)
{
  const auto thickness = static_cast<double>(settings.wallThickness);
  const auto min = source.bounds.min;
  const auto max = source.bounds.max;

  auto floorMin = min.z();
  auto ceilingMax = max.z();
  if (settings.mode == RoomGenerationMode::Floor)
  {
    floorMin = max.z();
    ceilingMax = floorMin + static_cast<double>(settings.wallHeight);
  }

  const auto wallBottom = floorMin;
  const auto wallTop = ceilingMax;
  const auto expandedMin = vm::vec3d{min.x() - thickness, min.y() - thickness, wallBottom};
  const auto expandedMax = vm::vec3d{max.x() + thickness, max.y() + thickness, wallTop};

  // The side walls include the corners. Front and back stop at the inner X bounds so
  // that every corner remains a simple, non-intersecting pair of brushes.
  const auto wallBounds = std::vector<vm::bbox3d>{
    vm::bbox3d{expandedMin, vm::vec3d{min.x(), expandedMax.y(), wallTop}},
    vm::bbox3d{vm::vec3d{max.x(), expandedMin.y(), wallBottom}, expandedMax},
    vm::bbox3d{vm::vec3d{min.x(), expandedMin.y(), wallBottom},
               vm::vec3d{max.x(), min.y(), wallTop}},
    vm::bbox3d{vm::vec3d{min.x(), max.y(), wallBottom},
               vm::vec3d{max.x(), expandedMax.y(), wallTop}}};

  for (const auto& bounds : wallBounds)
  {
    if (auto result = addBrush(
          builder,
          bounds,
          settings.wallMaterial,
          wallsLayer,
          nodesToAdd,
          generatedNodes);
        result.is_error())
    {
      return result;
    }
  }

  if (settings.mode == RoomGenerationMode::Box && settings.generateFloor)
  {
    if (auto result = addBrush(
          builder,
          vm::bbox3d{vm::vec3d{min.x(), min.y(), min.z() - thickness},
                     vm::vec3d{max.x(), max.y(), min.z()}},
          settings.floorMaterial,
          floorsLayer,
          nodesToAdd,
          generatedNodes);
        result.is_error())
    {
      return result;
    }
  }

  if (settings.generateCeiling)
  {
    if (auto result = addBrush(
          builder,
          vm::bbox3d{vm::vec3d{min.x(), min.y(), ceilingMax},
                     vm::vec3d{max.x(), max.y(), ceilingMax + thickness}},
          settings.ceilingMaterial,
          ceilingsLayer,
          nodesToAdd,
          generatedNodes);
        result.is_error())
    {
      return result;
    }
  }

  return kdl::void_success;
}

void setFloorMaterial(Map& map, BrushNode& floorNode, const std::string& material)
{
  auto brush = floorNode.brush();
  for (auto& face : brush.faces())
  {
    auto attributes = face.attributes();
    attributes.setMaterialName(material);
    face.setAttributes(attributes);
  }
  updateNodeContents(map, "Set Room Floor Material", {{&floorNode, NodeContents{std::move(brush)}}});
}

} // namespace

Result<RoomGenerationResult> generateRooms(
  Map& map,
  const RoomGenerationSettings& settings,
  const std::vector<RoomGenerationSource>& sources,
  const std::vector<BrushNode*>& previousGeneratedNodes)
{
  if (sources.empty())
  {
    return Error{"No valid room sources selected"};
  }

  if (settings.wallThickness <= 0 || settings.wallHeight <= 0)
  {
    return Error{"Room dimensions must be positive"};
  }

  auto* wallsLayer = findOrCreateLayer(map, "walls");
  auto* ceilingsLayer = settings.generateCeiling ? findOrCreateLayer(map, "ceilings") : nullptr;
  auto* floorsLayer =
    (settings.mode == RoomGenerationMode::Floor || settings.generateFloor)
      ? findOrCreateLayer(map, "floors")
      : nullptr;
  if (wallsLayer == nullptr || (settings.generateCeiling && ceilingsLayer == nullptr)
      || (floorsLayer == nullptr
          && (settings.mode == RoomGenerationMode::Floor || settings.generateFloor)))
  {
    return Error{"Could not create room output layers"};
  }

  const auto builder = BrushBuilder{
    map.world()->mapFormat(),
    map.worldBounds(),
    map.game()->config().faceAttribsConfig.defaults};

  auto transaction = Transaction{map, "Generate Rooms"};

  if (!previousGeneratedNodes.empty())
  {
    removeNodes(map, kdl::vec_static_cast<Node*>(previousGeneratedNodes));
  }

  auto sourceNodes = std::vector<BrushNode*>{};
  for (const auto& source : sources)
  {
    if (source.floorNode != nullptr)
    {
      sourceNodes.push_back(source.floorNode);
    }
  }

  if (settings.mode == RoomGenerationMode::Box)
  {
    if (!sourceNodes.empty())
    {
      removeNodes(map, kdl::vec_static_cast<Node*>(sourceNodes));
    }
  }
  else if (!sourceNodes.empty())
  {
    auto floorNodes = std::vector<Node*>{};
    floorNodes.reserve(sourceNodes.size());
    for (auto* sourceNode : sourceNodes)
    {
      floorNodes.push_back(sourceNode);
    }
    std::map<Node*, std::vector<Node*>> nodesToAdd{{floorsLayer, std::move(floorNodes)}};
    if (!reparentNodes(map, nodesToAdd))
    {
      transaction.cancel();
      return Error{"Could not move floor sources to the floors layer"};
    }
    for (auto* floorNode : sourceNodes)
    {
      setFloorMaterial(map, *floorNode, settings.floorMaterial);
    }
  }

  auto nodesToAdd = std::map<Node*, std::vector<Node*>>{};
  auto generatedNodes = std::vector<BrushNode*>{};
  for (const auto& source : sources)
  {
    if (auto result = addRoom(
          builder,
          settings,
          source,
          wallsLayer,
          ceilingsLayer,
          floorsLayer,
          nodesToAdd,
          generatedNodes);
        result.is_error())
    {
      transaction.cancel();
      return std::get<Error>(result.error());
    }
  }

  if (addNodes(map, nodesToAdd).empty())
  {
    transaction.cancel();
    return Error{"Could not add generated room brushes"};
  }

  if (!transaction.commit())
  {
    return Error{"Could not commit room generation"};
  }

  return RoomGenerationResult{std::move(generatedNodes), sources.size()};
}

} // namespace tb::mdl
