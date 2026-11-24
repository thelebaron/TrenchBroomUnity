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

#include "UnitySceneCamera.h"

#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityProperties.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Node.h"
#include "mdl/NodeContents.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "unity/UnityDecals.h"

#include "vm/scalar.h"
#include "vm/vec.h"
#include "vm/vec_io.h"

#include <algorithm>
#include <cmath>

namespace tb::unity
{
namespace
{

bool holds(const mdl::Entity& entity, const std::vector<mdl::EntityProperty>& properties)
{
  return std::ranges::all_of(properties, [&](const auto& property) {
    const auto* value = entity.property(property.key());
    return value && *value == property.value();
  });
}

void collectSceneCameras(mdl::Node& node, std::vector<mdl::EntityNode*>& result)
{
  for (auto* child : node.children())
  {
    auto* entityNode = dynamic_cast<mdl::EntityNode*>(child);
    if (entityNode && isSceneCameraEntity(entityNode->entity()))
    {
      result.push_back(entityNode);
    }
    collectSceneCameras(*child, result);
  }
}

} // namespace

bool isSceneCameraEntity(const mdl::Entity& entity)
{
  return entity.classname() == SceneCameraClassName;
}

LiveLinkCameraPose sceneCameraPose(const mdl::Entity& entity)
{
  const auto* value = entity.property(mdl::EntityPropertyKeys::Angles);
  const auto angles = (value ? vm::parse<double, 3>(*value) : std::nullopt)
                        .value_or(vm::vec3d{0.0, 0.0, 0.0});

  // the inverse of decalAngles: yaw(Z) * pitch(Y) * roll(X), positive pitch down
  const auto pitch = vm::to_radians(angles.x());
  const auto yaw = vm::to_radians(angles.y());
  const auto roll = vm::to_radians(angles.z());
  const auto cp = std::cos(pitch), sp = std::sin(pitch);
  const auto cy = std::cos(yaw), sy = std::sin(yaw);
  const auto cr = std::cos(roll), sr = std::sin(roll);

  const auto forward = vm::vec3d{cy * cp, sy * cp, -sp};
  const auto up = vm::vec3d{cy * sp * cr + sy * sr, sy * sp * cr - cy * sr, cp * cr};

  return {
    vm::vec3f{entity.origin()},
    vm::vec3f{vm::normalize(forward)},
    vm::vec3f{vm::normalize(up)},
  };
}

std::vector<mdl::EntityProperty> sceneCameraProperties(const LiveLinkCameraPose& pose)
{
  return {
    {mdl::EntityPropertyKeys::Classname, SceneCameraClassName},
    {mdl::EntityPropertyKeys::Origin, decalPropertyValue(vm::vec3d{pose.position})},
    {mdl::EntityPropertyKeys::Angles,
     decalPropertyValue(decalAngles(vm::vec3d{pose.direction}, vm::vec3d{pose.up}))},
  };
}

std::vector<mdl::EntityNode*> sceneCameraNodes(mdl::Map& map)
{
  auto result = std::vector<mdl::EntityNode*>{};
  collectSceneCameras(*map.world(), result);
  return result;
}

bool updateSceneCamera(mdl::Map& map, const LiveLinkCameraPose& pose)
{
  const auto nodes = sceneCameraNodes(map);
  const auto properties = sceneCameraProperties(pose);
  if (nodes.size() == 1 && holds(nodes.front()->entity(), properties))
  {
    return false;
  }

  auto transaction = mdl::Transaction{map, "Update Scene Camera"};
  if (nodes.size() > 1)
  {
    mdl::removeNodes(map, std::vector<mdl::Node*>{std::next(nodes.begin()), nodes.end()});
  }

  if (nodes.empty())
  {
    auto* node = new mdl::EntityNode{mdl::Entity{properties}};
    if (mdl::addNodes(map, {{map.world()->defaultLayer(), {node}}}).empty())
    {
      transaction.cancel();
      return false;
    }
  }
  else if (!holds(nodes.front()->entity(), properties))
  {
    auto entity = nodes.front()->entity();
    for (const auto& property : properties)
    {
      entity.addOrUpdateProperty(property.key(), property.value());
    }
    if (!mdl::updateNodeContents(
          map,
          "Update Scene Camera",
          {{nodes.front(), mdl::NodeContents{std::move(entity)}}}))
    {
      transaction.cancel();
      return false;
    }
  }

  return transaction.commit();
}

} // namespace tb::unity
