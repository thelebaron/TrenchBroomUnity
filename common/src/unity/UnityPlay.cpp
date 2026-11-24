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

#include "UnityPlay.h"

#include "vm/constants.h"
#include "vm/vec.h"

namespace tb::unity
{
namespace
{

// Surfaces steeper than this count as walls: about 45 degrees.
constexpr auto FloorNormalZ = 0.7f;

vm::vec3f horizontal(const vm::vec3f& v)
{
  return vm::vec3f{v.x(), v.y(), 0.0f};
}

/**
 * The camera's facing along the floor. Looking straight up or down, the camera's up
 * (or down) axis points where the top (or bottom) of the screen is.
 */
vm::vec3f facing(const LiveLinkCameraPose& camera)
{
  for (const auto& candidate :
       {horizontal(camera.direction),
        horizontal(camera.direction.z() < 0.0f ? camera.up : -camera.up)})
  {
    if (!vm::is_zero(candidate, vm::Cf::almost_zero()))
    {
      return vm::normalize(candidate);
    }
  }
  return vm::vec3f{1, 0, 0};
}

} // namespace

LiveLinkPlayStart playStartAtCamera(const LiveLinkCameraPose& camera)
{
  return {camera, LiveLinkPlayAnchor::Eye};
}

LiveLinkPlayStart playStartOnSurface(
  const vm::vec3f& point, const vm::vec3f& normal, const LiveLinkCameraPose& camera)
{
  const auto n = vm::normalize(normal);
  auto feet = point;
  if (n.z() <= -FloorNormalZ)
  {
    feet = point - vm::vec3f{0, 0, PlayStartHeight + PlayStartClearance};
  }
  else if (n.z() < FloorNormalZ)
  {
    feet = point + vm::normalize(horizontal(n)) * (PlayStartRadius + PlayStartClearance);
  }

  return {{feet, facing(camera), vm::vec3f{0, 0, 1}}, LiveLinkPlayAnchor::Feet};
}

} // namespace tb::unity
