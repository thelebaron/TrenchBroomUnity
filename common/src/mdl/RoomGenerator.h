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
#include "vm/mat.h"
#include "vm/vec.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace tb::mdl
{
class Brush;
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
 * Spaces, doors and clips are stored in the group's local coordinate system, i.e.
 * relative to the group's transformation at the time they were added.
 */
struct RoomRecipe
{
  /** The open interior volumes. Overlapping or touching spaces form one room. */
  std::vector<vm::bbox3d> spaces;
  /** Volumes that are cut out of the walls (but never out of floors or ceilings). */
  std::vector<vm::bbox3d> doors;
  /**
   * Volumes in which no solid is generated at all, because another room already provides
   * it there. Used where a corridor meets the walls of the rooms it connects.
   */
  std::vector<vm::bbox3d> clips;
  int wallThickness = 16;
  bool generateFloor = true;
  bool generateCeiling = true;
  std::string wallMaterial;
  std::string floorMaterial;
  std::string ceilingMaterial;
  bool generateLights = false;
  int lightSpacing = 256;

  kdl_reflect_decl(
    RoomRecipe,
    spaces,
    doors,
    clips,
    wallThickness,
    generateFloor,
    generateCeiling,
    wallMaterial,
    floorMaterial,
    ceilingMaterial,
    generateLights,
    lightSpacing);
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
 * produce steps, door volumes are cut out of the walls and clip volumes are left empty.
 *
 * The solid volume is packed into a small number of axis-aligned boxes. Each box is
 * tagged as floor, ceiling or wall so that it can receive the matching material.
 */
Result<std::vector<RoomShellBrush>> buildRoomShell(const RoomRecipe& recipe);

/**
 * Builds the brushes for the given recipe, placed with the given transformation (the
 * room group's transformation).
 */
Result<std::vector<Brush>> buildRoomBrushes(
  const Map& map, const RoomRecipe& recipe, const vm::mat4x4d& transformation);

struct RoomLight
{
  vm::vec3d position;
  /** The light's reach in map units. */
  double range;

  kdl_reflect_decl(RoomLight, position, range);
};

/**
 * Places lights on a grid below the ceiling of each space, roughly lightSpacing apart.
 * Positions are in the recipe's (local) coordinate system.
 */
std::vector<RoomLight> planRoomLights(const RoomRecipe& recipe);

struct RoomLightOptions
{
  /** Map units to Unity meters, used for the light_unity range property. */
  double unitScale = 0.03125;
  double intensity = 1.0;
};

bool isRoom(const Group& group);
Result<RoomRecipe> readRoomRecipe(const Group& group);
void writeRoomRecipe(Group& group, const RoomRecipe& recipe);

/**
 * Returns the room group that contains the given node, or the node itself if it is a
 * room.
 */
GroupNode* findRoom(Node* node);

std::vector<GroupNode*> collectRooms(const Map& map);

/**
 * Converts world space bounds into the room's local coordinate system. Fails if the room
 * has been rotated by an angle that is not a multiple of 90 degrees.
 */
Result<vm::bbox3d> roomLocalBounds(const GroupNode& room, const vm::bbox3d& worldBounds);

/** Returns the world space bounds of a room's shell (its spaces plus wall thickness). */
Result<std::vector<vm::bbox3d>> roomWorldShellBounds(const GroupNode& room);

/** Returns the rooms whose walls the given world space box cuts through. */
std::vector<GroupNode*> roomsCrossedBy(
  const std::vector<GroupNode*>& rooms, const vm::bbox3d& worldBox);

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
 * Creates a new room group under the given parent and fills it with the shell (and
 * lights, if enabled) for the given recipe. The recipe's coordinates are world
 * coordinates.
 */
Result<GroupNode*> createRoom(
  Map& map,
  Node* parent,
  const RoomRecipe& recipe,
  const RoomLightOptions& lightOptions = {},
  const std::string& name = "Room");

/**
 * Replaces the generated brushes of the given room with the shell for the given recipe
 * and stores the recipe on the room. Brushes that do not match the room's previous
 * recipe are removed or kept depending on the given policy. Generated lights are always
 * replaced. Other children such as entities placed by hand are always kept.
 */
Result<void> rebuildRoom(
  Map& map,
  GroupNode& room,
  const RoomRecipe& recipe,
  ModifiedRoomBrushes modifiedBrushes,
  const RoomLightOptions& lightOptions = {});

// Corridors ///////////////////////////////////////////////////////////////////////////

/** A room's spaces in world coordinates, together with its wall settings. */
struct RoomFootprint
{
  std::vector<vm::bbox3d> spaces;
  double wallThickness = 16.0;
  bool generateFloor = true;
  bool generateCeiling = true;
};

Result<RoomFootprint> roomWorldFootprint(const GroupNode& room);

struct CorridorOptions
{
  double width = 64.0;
  double height = 128.0;
  double gridSize = 16.0;
  double wallThickness = 16.0;
  bool generateFloor = true;
  bool generateCeiling = true;
};

/** A corridor between two rooms, in world coordinates. */
struct CorridorPlan
{
  /** The corridor's spaces; empty if the rooms' walls already touch. */
  std::vector<vm::bbox3d> spaces;
  /** Where the corridor overlaps the rooms' walls; see RoomRecipe::clips. */
  std::vector<vm::bbox3d> clips;
  /** The doorway to cut into the first room. */
  vm::bbox3d doorA;
  /** The doorway to cut into the second room. */
  vm::bbox3d doorB;

  kdl_reflect_decl(CorridorPlan, spaces, clips, doorA, doorB);
};

/**
 * Finds the shortest straight corridor between the two rooms, or an L-shaped one if no
 * straight corridor fits. The corridor leaves each room through a wall, runs at the
 * higher of the two floors and never passes through either room.
 */
Result<CorridorPlan> planCorridor(
  const RoomFootprint& a, const RoomFootprint& b, const CorridorOptions& options);

/**
 * Connects the two rooms: cuts a doorway into each and, unless their walls already
 * touch, creates a corridor room between them styled by the given recipe (whose spaces,
 * doors and clips are ignored). Returns the corridor room, or nullptr if none was needed.
 */
Result<GroupNode*> connectRooms(
  Map& map,
  GroupNode& a,
  GroupNode& b,
  const RoomRecipe& corridorStyle,
  const CorridorOptions& options,
  ModifiedRoomBrushes modifiedBrushes,
  const RoomLightOptions& lightOptions = {});

} // namespace tb::mdl
