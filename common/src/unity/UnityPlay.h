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

#include "vm/vec.h"

namespace tb::unity
{

/**
 * Roughly the cyberjunk player's capsule in map units, used to keep a start clicked on a
 * wall or ceiling out of it. Unity places the player exactly; these only need to be
 * large enough.
 */
inline constexpr auto PlayStartRadius = 16.0f;
inline constexpr auto PlayStartHeight = 64.0f;
// Extra room between the player and the clicked surface.
inline constexpr auto PlayStartClearance = 8.0f;

/**
 * Starts playing with the player's eyes at the camera, looking where it looks.
 */
LiveLinkPlayStart playStartAtCamera(const LiveLinkCameraPose& camera);

/**
 * Starts playing on a clicked surface, facing the camera's horizontal direction and
 * looking level. On a floor the player stands on the point. On a wall the player stands
 * out from it, and under a ceiling the player hangs below it, so in both cases the player
 * falls to the floor beneath.
 */
LiveLinkPlayStart playStartOnSurface(
  const vm::vec3f& point, const vm::vec3f& normal, const LiveLinkCameraPose& camera);

} // namespace tb::unity
