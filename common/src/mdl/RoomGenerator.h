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

#include "vm/bbox.h"

#include <string>
#include <vector>

namespace tb::mdl
{
class BrushNode;
class Map;

enum class RoomGenerationMode
{
  Box,
  Floor
};

struct RoomGenerationSettings
{
  RoomGenerationMode mode = RoomGenerationMode::Box;
  int wallThickness = 16;
  int floorHeight = 128;
  bool generateCeiling = true;
  bool generateFloor = true;
  std::string wallMaterial;
  std::string ceilingMaterial;
  std::string floorMaterial;
};

/**
 * A source can either refer to a live floor brush or to a saved box bounds snapshot.
 * Box sources are converted to bounds before generation because the source brush is
 * replaced by the generated room shell.
 */
struct RoomGenerationSource
{
  vm::bbox3d bounds;
  BrushNode* floorNode = nullptr;
};

struct RoomGenerationResult
{
  std::vector<BrushNode*> generatedNodes;
  size_t generatedRooms = 0u;
};

/**
 * Generates room geometry for the given sources. The previous generated nodes are
 * removed first, and all changes are made in one undoable transaction.
 */
Result<RoomGenerationResult> generateRooms(
  Map& map,
  const RoomGenerationSettings& settings,
  const std::vector<RoomGenerationSource>& sources,
  const std::vector<BrushNode*>& previousGeneratedNodes);

} // namespace tb::mdl
