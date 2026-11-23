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
#include "mdl/RoomGenerator.h"

#include "vm/vec.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace tb::mdl
{
class Brush;
class BrushNode;
class LayerNode;
class Map;

/**
 * The room layout is everything drawn with the room creator's Draw Rooms tool. It is a
 * single room recipe (with auto doorways) stored on the "walls" layer. Its brushes are
 * not grouped; they go into the "walls", "floors" and "ceilings" layers so that, for
 * example, all ceilings can be hidden at once.
 */
namespace RoomLayoutLayers
{
extern const std::string Walls;
extern const std::string Floors;
extern const std::string Ceilings;
} // namespace RoomLayoutLayers

LayerNode* findLayer(const Map& map, const std::string& name);

/** Returns the current layout, or an empty recipe (no spaces) if there is none. */
Result<RoomRecipe> readRoomLayout(const Map& map);

/**
 * The brushes that adding the given space to the layout would create or change, i.e. the
 * new layout's brushes minus those the current layout already has. Used for previews.
 */
Result<std::vector<Brush>> roomLayoutPreview(const Map& map, const RoomRecipe& newLayout);

/**
 * Replaces the layout's generated brushes and lights with those for the given layout and
 * stores it. Brushes in the layout layers that the previous layout didn't generate
 * (added or edited by hand) are kept. An empty layout removes everything it generated.
 */
Result<void> updateRoomLayout(
  Map& map, const RoomRecipe& layout, const RoomLightOptions& lightOptions = {});

/**
 * Computes the set of room creator wall and ceiling brushes that should be peeled away
 * in dollhouse view for the given 3D camera position so the user can see inside rooms.
 * Returns an empty set when the camera is inside a room space.
 */
std::unordered_set<const BrushNode*> collectDollhouseHiddenBrushes(
  const Map& map, const vm::vec3d& cameraPosition);

} // namespace tb::mdl
