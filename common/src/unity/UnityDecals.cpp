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

#include "UnityDecals.h"

#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/Entity.h"
#include "mdl/EntityProperties.h"
#include "mdl/EntityRotation.h"

#include "kdl/string_utils.h"

#include "vm/intersection.h"
#include "vm/plane.h"
#include "vm/scalar.h"
#include "vm/vec_ext.h"
#include "vm/vec_io.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace tb::unity
{
namespace
{

template <size_t S>
std::optional<vm::vec<double, S>> numbers(const mdl::Entity& entity, const char* key)
{
  const auto* value = entity.property(key);
  return value ? vm::parse<double, S>(*value) : std::nullopt;
}

/**
 * A number for an entity property: rounded so that floating point noise from rotating
 * axes doesn't end up in the map file.
 */
std::string propertyNumber(const double value)
{
  const auto rounded = std::round(value * 10000.0) / 10000.0;
  return kdl::str_to_string(rounded == 0.0 ? 0.0 : rounded);
}

template <size_t S>
std::string propertyNumbers(const vm::vec<double, S>& values)
{
  auto result = std::string{};
  for (size_t i = 0; i < S; ++i)
  {
    result += (i == 0 ? "" : " ") + propertyNumber(values[i]);
  }
  return result;
}

} // namespace

std::string decalPropertyValue(const vm::vec2d& values)
{
  return propertyNumbers(values);
}

std::string decalPropertyValue(const vm::vec3d& values)
{
  return propertyNumbers(values);
}

DecalAppearance decalAppearance(const mdl::Entity& entity)
{
  auto appearance = DecalAppearance{};
  if (const auto tint = numbers<3>(entity, DecalPropertyKeys::Tint))
  {
    appearance.tint =
      vm::vec3f{vm::clamp(*tint / 255.0, vm::vec3d{0, 0, 0}, vm::vec3d{1, 1, 1})};
  }
  if (const auto brightness = numbers<1>(entity, DecalPropertyKeys::Brightness))
  {
    appearance.brightness = float(std::max((*brightness)[0], 0.0));
  }
  if (const auto invert = numbers<1>(entity, DecalPropertyKeys::Invert))
  {
    appearance.invert = (*invert)[0] != 0.0;
  }
  return appearance;
}

unsigned char applyDecalAppearance(
  const unsigned char value, const float tint, const DecalAppearance& appearance)
{
  const auto base = appearance.invert ? 255.0f - float(value) : float(value);
  const auto result = base * tint * appearance.brightness;
  return static_cast<unsigned char>(std::clamp(std::round(result), 0.0f, 255.0f));
}

void applyDecalAppearance(
  unsigned char* pixels,
  const size_t size,
  const bool bgra,
  const DecalAppearance& appearance)
{
  const auto red = bgra ? 2u : 0u;
  const auto blue = bgra ? 0u : 2u;
  for (size_t i = 0; i + 3 < size; i += 4)
  {
    pixels[i + red] =
      applyDecalAppearance(pixels[i + red], appearance.tint.x(), appearance);
    pixels[i + 1] = applyDecalAppearance(pixels[i + 1], appearance.tint.y(), appearance);
    pixels[i + blue] =
      applyDecalAppearance(pixels[i + blue], appearance.tint.z(), appearance);
  }
}

bool isDecalEntity(const mdl::Entity& entity)
{
  return entity.classname() == DecalClassName;
}

DecalBox decalBox(const mdl::Entity& entity, const vm::vec2d& defaultSize)
{
  const auto size = numbers<2>(entity, DecalPropertyKeys::Size).value_or(defaultSize);
  const auto depth = numbers<1>(entity, DecalPropertyKeys::Depth)
                       .value_or(vm::vec<double, 1>{DefaultDecalDepth})[0];
  const auto flip =
    numbers<2>(entity, DecalPropertyKeys::Flip).value_or(vm::vec2d{0.0, 0.0});
  const auto angleFade = numbers<2>(entity, DecalPropertyKeys::AngleFade)
                           .value_or(vm::vec2d{30.0, DefaultDecalAngleFadeEnd});

  auto box = decalBox(entity.origin(), mdl::entityRotation(entity), size, depth);
  box.flipU = flip.x() != 0.0;
  box.flipV = flip.y() != 0.0;
  box.angleFadeEnd = angleFade.y();
  return box;
}

DecalBox decalBox(
  const vm::vec3d& origin,
  const vm::mat4x4d& rotation,
  const vm::vec2d& size,
  const double depth)
{
  return DecalBox{
    origin,
    vm::normalize(rotation * vm::vec3d{1, 0, 0}),
    vm::normalize(rotation * vm::vec3d{0, -1, 0}),
    vm::normalize(rotation * vm::vec3d{0, 0, 1}),
    vm::abs(size),
    std::abs(depth),
    false,
    false,
    DefaultDecalAngleFadeEnd,
  };
}

vm::bbox3d decalBounds(const DecalBox& box)
{
  const auto f = box.forward * (box.depth / 2.0);
  const auto u = box.uAxis * (box.size.x() / 2.0);
  const auto v = box.vAxis * (box.size.y() / 2.0);

  auto builder = vm::bbox3d::builder{};
  for (const auto sf : {-1.0, 1.0})
  {
    for (const auto su : {-1.0, 1.0})
    {
      for (const auto sv : {-1.0, 1.0})
      {
        builder.add(box.origin + f * sf + u * su + v * sv);
      }
    }
  }
  return builder.bounds();
}

std::vector<vm::vec3d> decalBoxEdges(const DecalBox& box)
{
  const auto f = box.forward * (box.depth / 2.0);
  const auto u = box.uAxis * (box.size.x() / 2.0);
  const auto v = box.vAxis * (box.size.y() / 2.0);
  const auto o = box.origin;

  auto edges = std::vector<vm::vec3d>{};
  for (const auto sf : {-1.0, 1.0})
  {
    const auto c = o + f * sf;
    const auto corners = std::array{c - u - v, c + u - v, c + u + v, c - u + v};
    for (size_t i = 0; i < 4; ++i)
    {
      edges.push_back(corners[i]);
      edges.push_back(corners[(i + 1) % 4]);
    }
  }
  for (const auto& corner : {-u - v, u - v, u + v, -u + v})
  {
    edges.push_back(o - f + corner);
    edges.push_back(o + f + corner);
  }
  return edges;
}

std::vector<DecalPolygon> projectDecal(const DecalBox& box, const mdl::Brush& brush)
{
  // The box's six planes, facing out, so clipping keeps what is inside.
  const auto halfExtents =
    vm::vec3d{box.depth / 2.0, box.size.x() / 2.0, box.size.y() / 2.0};
  const auto axes = std::array{box.forward, box.uAxis, box.vAxis};
  auto planes = std::vector<vm::plane3d>{};
  for (size_t i = 0; i < 3; ++i)
  {
    const auto center = vm::dot(box.origin, axes[i]);
    planes.emplace_back(center + halfExtents[i], axes[i]);
    planes.emplace_back(-center + halfExtents[i], -axes[i]);
  }

  const auto minFacing = std::cos(vm::to_radians(box.angleFadeEnd));

  auto result = std::vector<DecalPolygon>{};
  for (const auto& face : brush.faces())
  {
    const auto& normal = face.boundary().normal;
    if (vm::dot(normal, -box.forward) < minFacing)
    {
      continue;
    }

    auto positions = face.vertexPositions();
    for (const auto& plane : planes)
    {
      positions = vm::polygon_clip_by_plane(plane, positions.begin(), positions.end());
      if (positions.size() < 3)
      {
        break;
      }
    }
    if (positions.size() < 3)
    {
      continue;
    }

    // face vertices wind the other way from what the renderer draws as front facing
    std::ranges::reverse(positions);

    auto decalCoords = std::vector<vm::vec2d>{};
    decalCoords.reserve(positions.size());
    for (const auto& position : positions)
    {
      const auto relative = position - box.origin;
      auto s = vm::dot(relative, box.uAxis) / box.size.x() + 0.5;
      auto t = vm::dot(relative, box.vAxis) / box.size.y() + 0.5;
      decalCoords.emplace_back(box.flipU ? 1.0 - s : s, box.flipV ? 1.0 - t : t);
    }

    result.push_back(DecalPolygon{std::move(positions), std::move(decalCoords), normal});
  }
  return result;
}

vm::vec2f atlasTextureCoords(const vm::vec2d& decalCoords, const vm::vec4d& rect)
{
  const auto u = rect[0] + decalCoords.x() * rect[2];
  const auto v = rect[1] + decalCoords.y() * rect[3];
  return vm::vec2f{float(u), float(1.0 - v)};
}

std::vector<mdl::EntityProperty> decalProperties(
  const std::string& decalId,
  const vm::vec3d& origin,
  const vm::vec3d& angles,
  const vm::vec2d& size)
{
  return {
    {mdl::EntityPropertyKeys::Classname, DecalClassName},
    {mdl::EntityPropertyKeys::Origin, propertyNumbers(origin)},
    {mdl::EntityPropertyKeys::Angles, propertyNumbers(angles)},
    {DecalPropertyKeys::Decal, decalId},
    {DecalPropertyKeys::Size, propertyNumbers(size)},
  };
}

vm::vec3d decalAngles(const vm::vec3d& forward, const vm::vec3d& up)
{
  // TrenchBroom rotates "pitch yaw roll" as yaw(Z) * pitch(Y) * roll(X), positive pitch
  // down, so forward = (cy cp, sy cp, -sp) and up = (cy sp cr + sy sr, sy sp cr - cy sr,
  // cp cr).
  const auto f = vm::normalize(forward);
  const auto u = vm::normalize(up);

  if (std::abs(f.z()) > 1.0 - 1e-9)
  {
    // Straight down or up: yaw and roll turn about the same axis, so roll is 0.
    const auto pitch = f.z() < 0.0 ? 90.0 : -90.0;
    const auto yaw = f.z() < 0.0 ? std::atan2(u.y(), u.x()) : std::atan2(-u.y(), -u.x());
    return {pitch, vm::to_degrees(yaw), 0.0};
  }

  const auto pitch = std::asin(-f.z());
  const auto yaw = std::atan2(f.y(), f.x());
  const auto left = vm::cross(u, f);
  const auto roll = std::atan2(left.z(), u.z());
  return {vm::to_degrees(pitch), vm::to_degrees(yaw), vm::to_degrees(roll)};
}

} // namespace tb::unity
