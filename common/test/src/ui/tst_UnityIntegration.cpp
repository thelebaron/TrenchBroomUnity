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

#include "ui/UnityIntegration.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui::unity
{

TEST_CASE("UnityIntegration")
{
  SECTION("findUnityExecutable")
  {
    const auto exe = findUnityExecutable();
    CHECK(!exe.isEmpty());
  }

  SECTION("isPathInsideProject")
  {
    CHECK(!isPathInsideProject("", ""));
    CHECK(!isPathInsideProject("C:/Games/Maps/e1m1.map", ""));
    CHECK(!isPathInsideProject("", "C:/Games"));

    const auto projectPath = std::filesystem::path{"C:/Projects/cyberjunk"};

    CHECK(isPathInsideProject(projectPath / "Assets" / "Maps" / "e1m1.map", projectPath));
    CHECK(isPathInsideProject(projectPath / "Assets" / "test.map", projectPath));
    CHECK(isPathInsideProject(projectPath / "Packages" / "test.map", projectPath));

    // Outside the project
    CHECK(!isPathInsideProject("C:/Quake/id1/maps/start.map", projectPath));
    CHECK(!isPathInsideProject("C:/Projects/other/Assets/test.map", projectPath));
    CHECK(!isPathInsideProject(projectPath, projectPath));
  }

  SECTION("toUnityAssetPath")
  {
    const auto projectPath = std::filesystem::path{"C:/Projects/cyberjunk"};

    CHECK(
      toUnityAssetPath(projectPath / "Assets" / "Maps" / "e1m1.map", projectPath)
      == "Assets/Maps/e1m1.map");
    CHECK(
      toUnityAssetPath(projectPath / "Assets" / "cube.map", projectPath)
      == "Assets/cube.map");
    CHECK(toUnityAssetPath("C:/Quake/id1/maps/start.map", projectPath).isEmpty());
    CHECK(toUnityAssetPath("", projectPath).isEmpty());
  }
}

} // namespace tb::ui::unity
