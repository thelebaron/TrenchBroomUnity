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

#include "Result.h"

#include "kdl/reflection_decl.h"

#include "vm/bbox.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace tb::mdl
{
class Group;
class GroupNode;
class Map;
class Node;

enum class RoomSurface
{
  Wall,
  Floor,
  Ceiling,
};

std::ostream& operator<<(std::ostream& lhs, RoomSurface rhs);

/**
 * Everything needed to rebuild a room. A room is stored as a group whose properties hold
 * this recipe, so it survives saving, undo / redo and copy / paste.
 *
 * Spaces and doors are stored in the group's local coordinate system, i.e. relative to
 * the group's transformation at the time they were added.
 */
struct RoomRecipe
{
  /** The open interior volumes. Overlapping or touching spaces form one room. */
  std::vector<vm::bbox3d> spaces;
  /** Volumes that are cut out of the walls (but never out of floors or ceilings). */
  std::vector<vm::bbox3d> doors;
  int wallThickness = 16;
  bool generateFloor = true;
  bool generateCeiling = true;
  std::string wallMaterial;
  std::string floorMaterial;
  std::string ceilingMaterial;

  kdl_reflect_decl(
    RoomRecipe,
    spaces,
    doors,
    wallThickness,
    generateFloor,
    generateCeiling,
    wallMaterial,
    floorMaterial,
    ceilingMaterial);
};

struct RoomShellBrush
{
  vm::bbox3d bounds;
  RoomSurface surface;

  kdl_reflect_decl(RoomShellBrush, bounds, surface);
};

/**
 * Computes the solid shell around the union of the recipe's spaces: every point within
 * the wall thickness of a space that is not inside any space is solid. Walls between
 * touching or overlapping spaces therefore disappear, height differences between spaces
 * produce steps, and door volumes are cut out of the walls.
 *
 * The solid volume is packed into a small number of axis-aligned boxes. Each box is
 * tagged as floor, ceiling or wall so that it can receive the matching material.
 */
Result<std::vector<RoomShellBrush>> buildRoomShell(const RoomRecipe& recipe);

bool isRoom(const Group& group);
Result<RoomRecipe> readRoomRecipe(const Group& group);
void writeRoomRecipe(Group& group, const RoomRecipe& recipe);

/** Returns the room group that contains the given node, or the node itself if it is a
 * room. */
GroupNode* findRoom(Node* node);

std::vector<GroupNode*> collectRooms(const Map& map);

/**
 * Converts world space bounds into the room's local coordinate system. Fails if the room
 * has been rotated by an angle that is not a multiple of 90 degrees.
 */
Result<vm::bbox3d> roomLocalBounds(const GroupNode& room, const vm::bbox3d& worldBounds);

/** Returns the world space bounds of a room's shell (its spaces plus wall thickness). */
Result<std::vector<vm::bbox3d>> roomWorldShellBounds(const GroupNode& room);

/**
 * Counts the brushes in the room that no longer match what its stored recipe would
 * generate, i.e. brushes that were edited, retextured or added by hand.
 */
size_t countModifiedRoomBrushes(const Map& map, const GroupNode& room);

enum class ModifiedRoomBrushes
{
  Replace,
  Keep,
};

/**
 * Creates a new room group under the given parent and fills it with the shell for the
 * given recipe. The recipe's coordinates are world coordinates.
 */
Result<GroupNode*> createRoom(Map& map, Node* parent, const RoomRecipe& recipe);

/**
 * Replaces the generated brushes of the given room with the shell for the given recipe
 * and stores the recipe on the room. Brushes that do not match the room's previous
 * recipe are removed or kept depending on the given policy. Other children such as
 * entities are always kept.
 */
Result<void> rebuildRoom(
  Map& map,
  GroupNode& room,
  const RoomRecipe& recipe,
  ModifiedRoomBrushes modifiedBrushes);

} // namespace tb::mdl
