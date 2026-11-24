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

#include "vm/bbox.h"
#include "vm/mat.h"
#include "vm/vec.h"

#include <string>
#include <vector>

namespace tb::mdl
{
class Brush;
class Entity;
class EntityProperty;
} // namespace tb::mdl

namespace tb::unity
{

/**
 * Atlas decals ("stamps"): point entities of class "decal" that project a rectangle of a
 * Unity decal library's atlas onto the surfaces behind them. The entity's origin is the
 * centre of the projection box, its forward axis points into the surface, and its up axis
 * is the decal's up. Unity imports them as URP decal projectors.
 */
inline constexpr auto DecalClassName = "decal";

namespace DecalPropertyKeys
{
inline constexpr auto Decal = "_decal";
inline constexpr auto Size = "size";
inline constexpr auto Depth = "depth";
inline constexpr auto Opacity = "opacity";
inline constexpr auto AngleFade = "angle_fade";
inline constexpr auto Flip = "flip";
inline constexpr auto Tint = "tint";
inline constexpr auto Brightness = "brightness";
inline constexpr auto Invert = "invert";
} // namespace DecalPropertyKeys

inline constexpr auto DefaultDecalDepth = 16.0;
// The size of a decal entity without a size property, as the entity definition defaults
// it.
inline const auto DefaultDecalSize = vm::vec2d{32.0, 32.0};
inline constexpr auto DefaultDecalAngleFadeEnd = 60.0;

bool isDecalEntity(const mdl::Entity& entity);

/**
 * How a decal's colors are changed: inverted first, then multiplied by the tint and the
 * brightness. Alpha is left alone.
 */
struct DecalAppearance
{
  // 0-1 per channel; the "tint" property stores 0-255 like other Quake colors
  vm::vec3f tint = {1.0f, 1.0f, 1.0f};
  float brightness = 1.0f;
  bool invert = false;

  bool operator==(const DecalAppearance&) const = default;
};

DecalAppearance decalAppearance(const mdl::Entity& entity);

/**
 * Applies the appearance to 32 bit pixels in RGBA order, or BGRA if bgra is set.
 */
void applyDecalAppearance(
  unsigned char* pixels, size_t size, bool bgra, const DecalAppearance& appearance);

/**
 * The color the appearance makes of an 8 bit channel value.
 */
unsigned char applyDecalAppearance(
  unsigned char value, float tint, const DecalAppearance& appearance);

/**
 * The projection box of a decal entity.
 */
struct DecalBox
{
  vm::vec3d origin;
  // Points into the surface.
  vm::vec3d forward;
  // The atlas rectangle's u axis: to the right when looking along forward.
  vm::vec3d uAxis;
  // The atlas rectangle's v axis.
  vm::vec3d vAxis;
  // Width (along uAxis) and height (along vAxis).
  vm::vec2d size;
  // Extent along forward, centred on the origin.
  double depth;
  bool flipU;
  bool flipV;
  // Faces turned further than this from facing the decal get none of it.
  double angleFadeEnd;
};

/**
 * Reads the decal entity's box. Missing or malformed properties use their defaults; a
 * missing size uses the given default size.
 */
DecalBox decalBox(const mdl::Entity& entity, const vm::vec2d& defaultSize);

/**
 * The box for the given origin and rotation; rotation maps +X to forward, -Y to the
 * u axis and +Z to the v axis, as TrenchBroom rotates point entities.
 */
DecalBox decalBox(
  const vm::vec3d& origin,
  const vm::mat4x4d& rotation,
  const vm::vec2d& size,
  double depth = DefaultDecalDepth);

vm::bbox3d decalBounds(const DecalBox& box);

/**
 * The twelve edges of the decal's projection box, as line segment pairs.
 */
std::vector<vm::vec3d> decalBoxEdges(const DecalBox& box);

/**
 * The part of a brush face inside a decal box, with decal coordinates per vertex:
 * (0, 0) at the box's bottom left and (1, 1) at its top right, with flips applied.
 */
struct DecalPolygon
{
  std::vector<vm::vec3d> positions;
  std::vector<vm::vec2d> decalCoords;
  vm::vec3d normal;
};

/**
 * Projects the decal onto every face of the brush that faces it closely enough.
 */
std::vector<DecalPolygon> projectDecal(const DecalBox& box, const mdl::Brush& brush);

/**
 * Maps decal coordinates into the atlas rectangle (normalized, origin at the bottom left
 * as Unity exports it) and returns texture coordinates as TrenchBroom samples images,
 * with the origin at the top left.
 */
vm::vec2f atlasTextureCoords(const vm::vec2d& decalCoords, const vm::vec4d& rect);

/**
 * Numbers for a decal property value, rounded so that floating point noise from rotating
 * axes doesn't end up in the map file.
 */
std::string decalPropertyValue(const vm::vec2d& values);
std::string decalPropertyValue(const vm::vec3d& values);

/**
 * The properties of a newly stamped decal.
 */
std::vector<mdl::EntityProperty> decalProperties(
  const std::string& decalId,
  const vm::vec3d& origin,
  const vm::vec3d& angles,
  const vm::vec2d& size);

/**
 * Pitch, yaw and roll for a decal whose forward and up are the given axes, in the form
 * the "angles" property stores them.
 */
vm::vec3d decalAngles(const vm::vec3d& forward, const vm::vec3d& up);

} // namespace tb::unity
