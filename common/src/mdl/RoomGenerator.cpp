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

#include "RoomGenerator.h"

#include "mdl/Brush.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Game.h"
#include "mdl/Group.h"
#include "mdl/GroupNode.h"
#include "mdl/LinkedGroupUtils.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/NodeContents.h"
#include "mdl/NodeQueries.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"

#include "kdl/reflection_impl.h"
#include "kdl/string_utils.h"
#include "kdl/vector_utils.h"

#include "vm/bbox_io.h" // IWYU pragma: keep
#include "vm/mat.h"
#include "vm/mat_ext.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ostream>

namespace tb::mdl
{

std::ostream& operator<<(std::ostream& lhs, const RoomSurface rhs)
{
  switch (rhs)
  {
  case RoomSurface::Wall:
    return lhs << "Wall";
  case RoomSurface::Floor:
    return lhs << "Floor";
  case RoomSurface::Ceiling:
    return lhs << "Ceiling";
  }
  return lhs;
}

kdl_reflect_impl(RoomRecipe);
kdl_reflect_impl(RoomShellBrush);

namespace
{

constexpr auto Epsilon = 0.0001;
constexpr auto BrushMatchEpsilon = 0.01;

namespace RoomKeys
{
const auto Version = std::string{"_tb_room"};
const auto Spaces = std::string{"_tb_room_spaces"};
const auto Doors = std::string{"_tb_room_doors"};
const auto WallThickness = std::string{"_tb_room_thickness"};
const auto Floor = std::string{"_tb_room_floor"};
const auto Ceiling = std::string{"_tb_room_ceiling"};
const auto WallMaterial = std::string{"_tb_room_wall_material"};
const auto FloorMaterial = std::string{"_tb_room_floor_material"};
const auto CeilingMaterial = std::string{"_tb_room_ceiling_material"};
} // namespace RoomKeys

const auto CurrentVersion = std::string{"1"};

// Solver //////////////////////////////////////////////////////////////////////////////

enum class CellState : std::uint8_t
{
  Empty,
  Interior,
  Wall,
  Floor,
  Ceiling,
};

std::vector<double> sortedUniqueCoords(std::vector<double> coords)
{
  std::ranges::sort(coords);
  auto result = std::vector<double>{};
  for (const auto coord : coords)
  {
    if (result.empty() || coord - result.back() > Epsilon)
    {
      result.push_back(coord);
    }
  }
  return result;
}

struct CellRange
{
  size_t begin;
  size_t end;
};

/**
 * Returns the range of cells covered by [min, max]. Cell i spans coords[i] to
 * coords[i + 1]. Both bounds must be (close to) one of the coordinates.
 */
CellRange cellRange(const std::vector<double>& coords, const double min, const double max)
{
  const auto index = [&](const double value) {
    return static_cast<size_t>(
      std::distance(coords.begin(), std::ranges::lower_bound(coords, value - Epsilon)));
  };
  const auto begin = index(min);
  const auto end = index(max);
  return {begin, std::max(begin, end)};
}

class CellGrid
{
private:
  std::vector<double> m_x;
  std::vector<double> m_y;
  std::vector<double> m_z;
  size_t m_nx;
  size_t m_ny;
  size_t m_nz;
  std::vector<CellState> m_cells;

public:
  CellGrid(std::vector<double> x, std::vector<double> y, std::vector<double> z)
    : m_x{std::move(x)}
    , m_y{std::move(y)}
    , m_z{std::move(z)}
    , m_nx{m_x.size() - 1}
    , m_ny{m_y.size() - 1}
    , m_nz{m_z.size() - 1}
    , m_cells(m_nx * m_ny * m_nz, CellState::Empty)
  {
  }

  size_t nx() const { return m_nx; }
  size_t ny() const { return m_ny; }
  size_t nz() const { return m_nz; }

  size_t index(const size_t x, const size_t y, const size_t z) const
  {
    return (x * m_ny + y) * m_nz + z;
  }

  CellState& at(const size_t x, const size_t y, const size_t z)
  {
    return m_cells[index(x, y, z)];
  }

  CellState at(const size_t x, const size_t y, const size_t z) const
  {
    return m_cells[index(x, y, z)];
  }

  double zMin(const size_t z) const { return m_z[z]; }
  double zMax(const size_t z) const { return m_z[z + 1]; }

  vm::bbox3d bounds(
    const size_t x0,
    const size_t y0,
    const size_t z0,
    const size_t x1,
    const size_t y1,
    const size_t z1) const
  {
    return {{m_x[x0], m_y[y0], m_z[z0]}, {m_x[x1 + 1], m_y[y1 + 1], m_z[z1 + 1]}};
  }

  template <typename F>
  void forEachCell(const vm::bbox3d& box, const F& f)
  {
    const auto xr = cellRange(m_x, box.min.x(), box.max.x());
    const auto yr = cellRange(m_y, box.min.y(), box.max.y());
    const auto zr = cellRange(m_z, box.min.z(), box.max.z());
    for (auto x = xr.begin; x < xr.end; ++x)
    {
      for (auto y = yr.begin; y < yr.end; ++y)
      {
        for (auto z = zr.begin; z < zr.end; ++z)
        {
          f(at(x, y, z));
        }
      }
    }
  }
};

bool isSolid(const CellState state)
{
  return state == CellState::Wall || state == CellState::Floor
         || state == CellState::Ceiling;
}

bool isValidBox(const vm::bbox3d& box)
{
  return box.max.x() - box.min.x() > Epsilon && box.max.y() - box.min.y() > Epsilon
         && box.max.z() - box.min.z() > Epsilon;
}

std::optional<vm::bbox3d> intersection(const vm::bbox3d& lhs, const vm::bbox3d& rhs)
{
  const auto min = vm::max(lhs.min, rhs.min);
  const auto max = vm::min(lhs.max, rhs.max);
  const auto box = vm::bbox3d{min, max};
  return isValidBox(box) ? std::optional{box} : std::nullopt;
}

vm::bbox3d dilate(const vm::bbox3d& space, const RoomRecipe& recipe)
{
  const auto t = static_cast<double>(recipe.wallThickness);
  return {
    space.min - vm::vec3d{t, t, recipe.generateFloor ? t : 0.0},
    space.max + vm::vec3d{t, t, recipe.generateCeiling ? t : 0.0}};
}

/**
 * A solid cell directly beneath an interior cell (within the wall thickness) is floor;
 * one directly above an interior cell is ceiling. Everything else is wall.
 */
void classifySolidCells(CellGrid& grid, const double thickness)
{
  for (size_t x = 0; x < grid.nx(); ++x)
  {
    for (size_t y = 0; y < grid.ny(); ++y)
    {
      for (size_t z = 0; z < grid.nz(); ++z)
      {
        if (!isSolid(grid.at(x, y, z)))
        {
          continue;
        }

        auto above = z + 1;
        while (above < grid.nz() && isSolid(grid.at(x, y, above)))
        {
          ++above;
        }
        if (
          above < grid.nz() && grid.at(x, y, above) == CellState::Interior
          && grid.zMin(z) >= grid.zMin(above) - thickness - Epsilon)
        {
          grid.at(x, y, z) = CellState::Floor;
          continue;
        }

        auto below = z;
        while (below > 0 && isSolid(grid.at(x, y, below - 1)))
        {
          --below;
        }
        if (
          below > 0 && grid.at(x, y, below - 1) == CellState::Interior
          && grid.zMax(z) <= grid.zMax(below - 1) + thickness + Epsilon)
        {
          grid.at(x, y, z) = CellState::Ceiling;
          continue;
        }

        grid.at(x, y, z) = CellState::Wall;
      }
    }
  }
}

RoomSurface toSurface(const CellState state)
{
  switch (state)
  {
  case CellState::Floor:
    return RoomSurface::Floor;
  case CellState::Ceiling:
    return RoomSurface::Ceiling;
  case CellState::Wall:
  case CellState::Empty:
  case CellState::Interior:
    break;
  }
  return RoomSurface::Wall;
}

/**
 * Packs the solid cells into boxes. Each box grows along Z first, then X, then Y. A box
 * only grows into a neighbouring slab whose run of matching cells ends exactly where the
 * box ends, so that full height wall columns are not chopped up by lower neighbours such
 * as door thresholds.
 */
std::vector<RoomShellBrush> packSolidCells(const CellGrid& grid)
{
  auto visited = std::vector<bool>(grid.nx() * grid.ny() * grid.nz(), false);
  const auto available =
    [&](const size_t x, const size_t y, const size_t z, const auto state) {
      return grid.at(x, y, z) == state && !visited[grid.index(x, y, z)];
    };

  auto result = std::vector<RoomShellBrush>{};
  for (size_t x = 0; x < grid.nx(); ++x)
  {
    for (size_t y = 0; y < grid.ny(); ++y)
    {
      for (size_t z = 0; z < grid.nz(); ++z)
      {
        const auto state = grid.at(x, y, z);
        if (!isSolid(state) || visited[grid.index(x, y, z)])
        {
          continue;
        }

        auto z1 = z;
        while (z1 + 1 < grid.nz() && available(x, y, z1 + 1, state))
        {
          ++z1;
        }

        // The column at (cx, cy) matches exactly if it holds the box's Z run and nothing
        // more of the same kind directly above or below.
        const auto columnMatches = [&](const size_t cx, const size_t cy) {
          for (auto cz = z; cz <= z1; ++cz)
          {
            if (!available(cx, cy, cz, state))
            {
              return false;
            }
          }
          return !(z > 0 && available(cx, cy, z - 1, state))
                 && !(z1 + 1 < grid.nz() && available(cx, cy, z1 + 1, state));
        };

        auto x1 = x;
        while (x1 + 1 < grid.nx() && columnMatches(x1 + 1, y))
        {
          ++x1;
        }

        const auto rowMatches = [&](const size_t cy) {
          for (auto cx = x; cx <= x1; ++cx)
          {
            if (!columnMatches(cx, cy))
            {
              return false;
            }
          }
          return !(x > 0 && columnMatches(x - 1, cy))
                 && !(x1 + 1 < grid.nx() && columnMatches(x1 + 1, cy));
        };

        auto y1 = y;
        while (y1 + 1 < grid.ny() && rowMatches(y1 + 1))
        {
          ++y1;
        }

        for (auto bx = x; bx <= x1; ++bx)
        {
          for (auto by = y; by <= y1; ++by)
          {
            for (auto bz = z; bz <= z1; ++bz)
            {
              visited[grid.index(bx, by, bz)] = true;
            }
          }
        }

        result.push_back({grid.bounds(x, y, z, x1, y1, z1), toSurface(state)});
      }
    }
  }
  return result;
}

// Recipe serialization ////////////////////////////////////////////////////////////////

std::string formatBoxes(const std::vector<vm::bbox3d>& boxes)
{
  auto parts = std::vector<std::string>{};
  parts.reserve(boxes.size());
  for (const auto& box : boxes)
  {
    parts.push_back(
      fmt::format(
        "{} {} {} {} {} {}",
        box.min.x(),
        box.min.y(),
        box.min.z(),
        box.max.x(),
        box.max.y(),
        box.max.z()));
  }
  return kdl::str_join(parts, ";");
}

Result<std::vector<vm::bbox3d>> parseBoxes(const std::string& key, const std::string& str)
{
  auto result = std::vector<vm::bbox3d>{};
  for (const auto& part : kdl::str_split(str, ";"))
  {
    auto values = std::vector<double>{};
    for (const auto& token : kdl::str_split(part, " "))
    {
      if (token.empty())
      {
        continue;
      }
      const auto value = kdl::str_to_double(token);
      if (!value)
      {
        return Error{fmt::format("Invalid number '{}' in room property {}", token, key)};
      }
      values.push_back(*value);
    }
    if (values.empty())
    {
      continue;
    }
    if (values.size() != 6u)
    {
      return Error{fmt::format("Invalid box '{}' in room property {}", part, key)};
    }
    result.push_back(
      vm::bbox3d{{values[0], values[1], values[2]}, {values[3], values[4], values[5]}});
  }
  return result;
}

std::string propertyOrEmpty(const Group& group, const std::string& key)
{
  const auto* value = group.property(key);
  return value ? *value : std::string{};
}

// Placing the shell in the map ////////////////////////////////////////////////////////

/** Whether the transformation maps axis-aligned boxes to axis-aligned boxes. */
bool preservesAxes(const vm::mat4x4d& transformation)
{
  for (size_t row = 0; row < 3; ++row)
  {
    auto nonZero = 0;
    for (size_t col = 0; col < 3; ++col)
    {
      const auto value = std::abs(transformation[col][row]);
      if (value > Epsilon)
      {
        if (std::abs(value - 1.0) > Epsilon)
        {
          return false;
        }
        ++nonZero;
      }
    }
    if (nonZero != 1)
    {
      return false;
    }
  }
  return true;
}

const std::string& materialFor(const RoomRecipe& recipe, const RoomSurface surface)
{
  switch (surface)
  {
  case RoomSurface::Floor:
    return recipe.floorMaterial;
  case RoomSurface::Ceiling:
    return recipe.ceilingMaterial;
  case RoomSurface::Wall:
    break;
  }
  return recipe.wallMaterial;
}

Result<std::vector<Brush>> buildRoomBrushes(
  const Map& map, const RoomRecipe& recipe, const vm::mat4x4d& transformation)
{
  auto shellResult = buildRoomShell(recipe);
  if (shellResult.is_error())
  {
    return std::get<Error>(shellResult.error());
  }

  const auto builder = BrushBuilder{
    map.world()->mapFormat(),
    map.worldBounds(),
    map.game()->config().faceAttribsConfig.defaults};
  const auto axisAligned = preservesAxes(transformation);

  auto result = std::vector<Brush>{};
  for (const auto& shellBrush : shellResult.value())
  {
    const auto& material = materialFor(recipe, shellBrush.surface);
    const auto bounds =
      axisAligned ? shellBrush.bounds.transform(transformation) : shellBrush.bounds;

    auto brushResult = builder.createCuboid(bounds, material);
    if (brushResult.is_error())
    {
      return std::get<Error>(brushResult.error());
    }

    auto brush = std::move(brushResult.value());
    if (!axisAligned)
    {
      if (
        auto transformResult = brush.transform(map.worldBounds(), transformation, false);
        transformResult.is_error())
      {
        return std::get<Error>(transformResult.error());
      }
    }
    result.push_back(std::move(brush));
  }
  return result;
}

bool brushMatches(const Brush& actual, const Brush& expected)
{
  if (
    actual.faceCount() != expected.faceCount()
    || !vm::is_equal(actual.bounds(), expected.bounds(), BrushMatchEpsilon))
  {
    return false;
  }

  const auto& material = expected.faces().front().attributes().materialName();
  return std::ranges::all_of(actual.faces(), [&](const auto& face) {
    return face.attributes().materialName() == material;
  });
}

/**
 * Splits the room's brush children into those that match the given expected brushes and
 * those that don't. Each expected brush matches at most one child.
 */
std::pair<std::vector<BrushNode*>, std::vector<BrushNode*>> matchRoomBrushes(
  const GroupNode& room, const std::vector<Brush>& expected)
{
  auto used = std::vector<bool>(expected.size(), false);
  auto matched = std::vector<BrushNode*>{};
  auto unmatched = std::vector<BrushNode*>{};

  for (auto* child : room.children())
  {
    auto* brushNode = dynamic_cast<BrushNode*>(child);
    if (brushNode == nullptr)
    {
      continue;
    }

    auto found = false;
    for (size_t i = 0; i < expected.size(); ++i)
    {
      if (!used[i] && brushMatches(brushNode->brush(), expected[i]))
      {
        used[i] = true;
        found = true;
        break;
      }
    }
    (found ? matched : unmatched).push_back(brushNode);
  }

  return {std::move(matched), std::move(unmatched)};
}

std::vector<Brush> expectedRoomBrushes(const Map& map, const GroupNode& room)
{
  auto recipe = readRoomRecipe(room.group());
  if (recipe.is_error())
  {
    return {};
  }

  auto brushes = buildRoomBrushes(map, recipe.value(), room.group().transformation());
  return brushes.is_success() ? std::move(brushes.value()) : std::vector<Brush>{};
}

std::vector<Node*> toBrushNodes(std::vector<Brush> brushes)
{
  auto result = std::vector<Node*>{};
  result.reserve(brushes.size());
  for (auto& brush : brushes)
  {
    result.push_back(new BrushNode{std::move(brush)});
  }
  return result;
}

} // namespace

Result<std::vector<RoomShellBrush>> buildRoomShell(const RoomRecipe& recipe)
{
  if (recipe.wallThickness <= 0)
  {
    return Error{"Wall thickness must be positive"};
  }
  if (recipe.spaces.empty())
  {
    return Error{"Room has no spaces"};
  }
  if (!std::ranges::all_of(recipe.spaces, isValidBox))
  {
    return Error{"Room spaces must have a positive size"};
  }

  const auto thickness = static_cast<double>(recipe.wallThickness);
  auto shells = std::vector<vm::bbox3d>{};
  for (const auto& space : recipe.spaces)
  {
    shells.push_back(dilate(space, recipe));
  }

  // Doors only cut walls within the height of a space they touch, so that a door box
  // that reaches into the floor or ceiling cannot open the room to the void.
  auto doorCuts = std::vector<vm::bbox3d>{};
  for (const auto& door : recipe.doors)
  {
    for (size_t i = 0; i < recipe.spaces.size(); ++i)
    {
      const auto& space = recipe.spaces[i];
      const auto& shell = shells[i];
      const auto limits = vm::bbox3d{
        {shell.min.x(), shell.min.y(), space.min.z()},
        {shell.max.x(), shell.max.y(), space.max.z()}};
      if (const auto cut = intersection(door, limits))
      {
        doorCuts.push_back(*cut);
      }
    }
  }

  auto xs = std::vector<double>{};
  auto ys = std::vector<double>{};
  auto zs = std::vector<double>{};
  for (const auto& boxes : {recipe.spaces, shells, doorCuts})
  {
    for (const auto& box : boxes)
    {
      xs.insert(xs.end(), {box.min.x(), box.max.x()});
      ys.insert(ys.end(), {box.min.y(), box.max.y()});
      zs.insert(zs.end(), {box.min.z(), box.max.z()});
    }
  }

  auto grid =
    CellGrid{sortedUniqueCoords(xs), sortedUniqueCoords(ys), sortedUniqueCoords(zs)};

  for (const auto& shell : shells)
  {
    grid.forEachCell(shell, [](auto& cell) { cell = CellState::Wall; });
  }
  for (const auto& space : recipe.spaces)
  {
    grid.forEachCell(space, [](auto& cell) { cell = CellState::Interior; });
  }

  classifySolidCells(grid, thickness);

  for (const auto& cut : doorCuts)
  {
    grid.forEachCell(cut, [](auto& cell) {
      if (cell == CellState::Wall)
      {
        cell = CellState::Empty;
      }
    });
  }

  return packSolidCells(grid);
}

bool isRoom(const Group& group)
{
  return group.property(RoomKeys::Version) != nullptr;
}

Result<RoomRecipe> readRoomRecipe(const Group& group)
{
  if (!isRoom(group))
  {
    return Error{fmt::format("Group '{}' is not a room", group.name())};
  }

  auto spaces = parseBoxes(RoomKeys::Spaces, propertyOrEmpty(group, RoomKeys::Spaces));
  if (spaces.is_error())
  {
    return std::get<Error>(spaces.error());
  }
  auto doors = parseBoxes(RoomKeys::Doors, propertyOrEmpty(group, RoomKeys::Doors));
  if (doors.is_error())
  {
    return std::get<Error>(doors.error());
  }

  const auto thickness = kdl::str_to_int(propertyOrEmpty(group, RoomKeys::WallThickness));
  if (!thickness || *thickness <= 0)
  {
    return Error{fmt::format("Room '{}' has an invalid wall thickness", group.name())};
  }

  return RoomRecipe{
    std::move(spaces.value()),
    std::move(doors.value()),
    *thickness,
    propertyOrEmpty(group, RoomKeys::Floor) != "0",
    propertyOrEmpty(group, RoomKeys::Ceiling) != "0",
    propertyOrEmpty(group, RoomKeys::WallMaterial),
    propertyOrEmpty(group, RoomKeys::FloorMaterial),
    propertyOrEmpty(group, RoomKeys::CeilingMaterial),
  };
}

void writeRoomRecipe(Group& group, const RoomRecipe& recipe)
{
  group.setProperty(RoomKeys::Version, CurrentVersion);
  group.setProperty(RoomKeys::Spaces, formatBoxes(recipe.spaces));
  if (recipe.doors.empty())
  {
    group.removeProperty(RoomKeys::Doors);
  }
  else
  {
    group.setProperty(RoomKeys::Doors, formatBoxes(recipe.doors));
  }
  group.setProperty(RoomKeys::WallThickness, std::to_string(recipe.wallThickness));
  group.setProperty(RoomKeys::Floor, recipe.generateFloor ? "1" : "0");
  group.setProperty(RoomKeys::Ceiling, recipe.generateCeiling ? "1" : "0");
  group.setProperty(RoomKeys::WallMaterial, recipe.wallMaterial);
  group.setProperty(RoomKeys::FloorMaterial, recipe.floorMaterial);
  group.setProperty(RoomKeys::CeilingMaterial, recipe.ceilingMaterial);
}

GroupNode* findRoom(Node* node)
{
  for (auto* current = node; current != nullptr; current = current->parent())
  {
    if (
      auto* groupNode = dynamic_cast<GroupNode*>(current);
      groupNode != nullptr && isRoom(groupNode->group()))
    {
      return groupNode;
    }
  }
  return nullptr;
}

std::vector<GroupNode*> collectRooms(const Map& map)
{
  return kdl::vec_static_cast<GroupNode*>(collectDescendants(
    std::vector<Node*>{map.world()},
    [](const GroupNode* groupNode) { return isRoom(groupNode->group()); }));
}

Result<vm::bbox3d> roomLocalBounds(const GroupNode& room, const vm::bbox3d& worldBounds)
{
  const auto& transformation = room.group().transformation();
  if (!preservesAxes(transformation))
  {
    return Error{fmt::format(
      "Room '{}' is rotated by an angle that is not a multiple of 90 degrees",
      room.name())};
  }

  const auto inverse = vm::invert(transformation);
  if (!inverse)
  {
    return Error{fmt::format("Room '{}' has an invalid transformation", room.name())};
  }
  return worldBounds.transform(*inverse);
}

Result<std::vector<vm::bbox3d>> roomWorldShellBounds(const GroupNode& room)
{
  auto recipe = readRoomRecipe(room.group());
  if (recipe.is_error())
  {
    return std::get<Error>(recipe.error());
  }

  const auto& transformation = room.group().transformation();
  auto result = std::vector<vm::bbox3d>{};
  for (const auto& space : recipe.value().spaces)
  {
    result.push_back(dilate(space, recipe.value()).transform(transformation));
  }
  return result;
}

size_t countModifiedRoomBrushes(const Map& map, const GroupNode& room)
{
  return matchRoomBrushes(room, expectedRoomBrushes(map, room)).second.size();
}

Result<GroupNode*> createRoom(Map& map, Node* parent, const RoomRecipe& recipe)
{
  auto brushes = buildRoomBrushes(map, recipe, vm::mat4x4d::identity());
  if (brushes.is_error())
  {
    return std::get<Error>(brushes.error());
  }

  auto group = Group{"Room"};
  writeRoomRecipe(group, recipe);

  auto* room = new GroupNode{std::move(group)};
  room->addChildren(toBrushNodes(std::move(brushes.value())));

  if (addNodes(map, {{parent, {room}}}).empty())
  {
    delete room;
    return Error{"Could not add room to the map"};
  }
  return room;
}

Result<void> rebuildRoom(
  Map& map,
  GroupNode& room,
  const RoomRecipe& recipe,
  const ModifiedRoomBrushes modifiedBrushes)
{
  auto brushes = buildRoomBrushes(map, recipe, room.group().transformation());
  if (brushes.is_error())
  {
    return std::get<Error>(brushes.error());
  }

  auto [matched, unmatched] = matchRoomBrushes(room, expectedRoomBrushes(map, room));
  auto nodesToRemove = kdl::vec_static_cast<Node*>(std::move(matched));
  if (modifiedBrushes == ModifiedRoomBrushes::Replace)
  {
    nodesToRemove = kdl::vec_concat(
      std::move(nodesToRemove), kdl::vec_static_cast<Node*>(std::move(unmatched)));
  }

  auto transaction = Transaction{map, "Rebuild Room"};

  // Add the new brushes before removing the old ones so the group never becomes empty,
  // which would cause it to be removed along with its last child.
  if (addNodes(map, {{&room, toBrushNodes(std::move(brushes.value()))}}).empty())
  {
    transaction.cancel();
    return Error{"Could not add room brushes"};
  }
  if (!nodesToRemove.empty())
  {
    removeNodes(map, nodesToRemove);
  }

  // Linked copies of the room receive the new brushes through the linked group update,
  // but each copy keeps its own group data, so the recipe is stored on all of them.
  auto nodesToUpdate = std::vector<std::pair<Node*, NodeContents>>{};
  for (auto* linkedRoom : collectGroupsWithLinkId({map.world()}, room.linkId()))
  {
    auto group = linkedRoom->group();
    writeRoomRecipe(group, recipe);
    nodesToUpdate.emplace_back(linkedRoom, NodeContents{std::move(group)});
  }
  if (!updateNodeContents(map, "Update Room", std::move(nodesToUpdate)))
  {
    transaction.cancel();
    return Error{"Could not store room recipe"};
  }

  if (!transaction.commit())
  {
    return Error{"Could not commit room update"};
  }
  return kdl::void_success;
}

} // namespace tb::mdl
