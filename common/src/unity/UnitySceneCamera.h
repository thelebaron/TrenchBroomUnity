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

#include "unity/UnityLiveLinkProtocol.h"

#include <vector>

namespace tb::mdl
{
class Entity;
class EntityNode;
class EntityProperty;
class Map;
} // namespace tb::mdl

namespace tb::unity
{

/**
 * The scene camera: one point entity per map holding the TrenchBroom 3D camera's pose
 * from when the map was last saved. TrenchBroom moves its 3D camera there when the map
 * is opened, and Unity can read it to know where the level designer was looking, for
 * example to start playing from there.
 *
 * It stores its origin and "angles" (pitch yaw roll, positive pitch looks down, as for
 * decals). TrenchBroom neither draws nor picks it unless it is selected, since the 3D
 * camera already shows where it is, and it needs no entity definition.
 */
inline constexpr auto SceneCameraClassName = "scene_camera";

bool isSceneCameraEntity(const mdl::Entity& entity);

/**
 * The pose the entity stores. Missing or malformed angles look along +X.
 */
LiveLinkCameraPose sceneCameraPose(const mdl::Entity& entity);

/**
 * The properties of a scene camera with the given pose.
 */
std::vector<mdl::EntityProperty> sceneCameraProperties(const LiveLinkCameraPose& pose);

/**
 * The map's scene camera entities, in map order.
 */
std::vector<mdl::EntityNode*> sceneCameraNodes(mdl::Map& map);

/**
 * Makes the map's scene camera hold the given pose, as one undoable step: adds one to
 * the default layer if the map has none, and removes all but the first if it has more.
 * Does nothing if the scene camera already holds the pose. Returns whether the map
 * changed.
 */
bool updateSceneCamera(mdl::Map& map, const LiveLinkCameraPose& pose);

} // namespace tb::unity
