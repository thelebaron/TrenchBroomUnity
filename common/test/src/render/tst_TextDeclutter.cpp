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

#include "render/TextRenderer.h"

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

namespace tb::render
{

TEST_CASE("declutterLabels", "[text-declutter]")
{
  SECTION("Separate labels are all kept")
  {
    CHECK(
      declutterLabels({
        {{0, 0}, {50, 10}},
        {{60, 0}, {50, 10}},
        {{0, 20}, {50, 10}},
      })
      == std::vector<bool>{true, true, true});
  }

  SECTION("The first of overlapping labels wins")
  {
    CHECK(
      declutterLabels({
        {{0, 0}, {50, 10}},
        {{40, 5}, {50, 10}},
        {{100, 0}, {50, 10}},
      })
      == std::vector<bool>{true, false, true});
  }

  SECTION("Touching labels don't overlap")
  {
    CHECK(
      declutterLabels({
        {{0, 0}, {50, 10}},
        {{50, 0}, {50, 10}},
      })
      == std::vector<bool>{true, true});
  }

  SECTION("A dropped label doesn't block others")
  {
    // the second overlaps the first and the third, but only the first is drawn
    CHECK(
      declutterLabels({
        {{0, 0}, {50, 10}},
        {{45, 0}, {50, 10}},
        {{90, 0}, {50, 10}},
      })
      == std::vector<bool>{true, false, true});
  }

  SECTION("Labels spanning several cells and negative coordinates")
  {
    CHECK(
      declutterLabels(
        {
          {{-300, -10}, {400, 20}},
          {{80, 0}, {10, 10}},
          {{-500, -500}, {10, 10}},
          {{-150, 5}, {10, 10}},
        },
        64.0f)
      == std::vector<bool>{true, false, true, false});
  }

  SECTION("A crowd of identical labels keeps one")
  {
    const auto labels = std::vector<LabelRect>(1000, LabelRect{{10, 10}, {40, 12}});
    const auto keep = declutterLabels(labels);
    CHECK(std::count(keep.begin(), keep.end(), true) == 1);
    CHECK(keep.front());
  }
}

} // namespace tb::render
