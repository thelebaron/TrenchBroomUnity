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

#include "PreferenceManager.h"
#include "Preferences.h"
#include "TestUtils.h"
#include "unity/UnityAssetImport.h"

#include <chrono>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

namespace tb::unity
{
namespace
{

/**
 * A fresh folder under the system temp folder, removed again at the end.
 */
class TemporaryFolder
{
private:
  std::filesystem::path m_path;

public:
  TemporaryFolder()
    : m_path{
        std::filesystem::temp_directory_path()
        / ("tb-asset-import-"
           + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))}
  {
    std::filesystem::create_directories(m_path);
  }

  ~TemporaryFolder()
  {
    auto ec = std::error_code{};
    std::filesystem::remove_all(m_path, ec);
  }

  const std::filesystem::path& path() const { return m_path; }
};

void writeFile(const std::filesystem::path& path, const std::string& contents)
{
  std::filesystem::create_directories(path.parent_path());
  auto stream = std::ofstream{path, std::ios::binary};
  stream << contents;
}

std::string readFile(const std::filesystem::path& path)
{
  auto stream = std::ifstream{path, std::ios::binary};
  return std::string{std::istreambuf_iterator<char>{stream}, {}};
}

} // namespace

TEST_CASE("Image import plans copies and conflicts", "[unity-asset-import]")
{
  const auto folder = TemporaryFolder{};
  const auto source = folder.path() / "source";
  const auto target = folder.path() / "target";
  writeFile(source / "stripe.png", "new");
  writeFile(source / "panel.tga", "panel");
  writeFile(source / "notes.txt", "text");
  writeFile(target / "stripe.png", "old");

  const auto plan = planImageImport(
    {source / "stripe.png", source / "panel.tga", source / "notes.txt"}, target);
  CHECK(plan.copies.size() == 2u);
  CHECK(plan.conflicts == std::vector{target / "stripe.png"});
  CHECK(plan.skipped == std::vector{source / "notes.txt"});

  SECTION("skipping conflicts keeps the existing file")
  {
    const auto written = copyImages(plan, false);
    REQUIRE(written.is_success());
    CHECK(written.value() == std::vector{target / "panel.tga"});
    CHECK(readFile(target / "stripe.png") == "old");
  }

  SECTION("replacing conflicts overwrites them")
  {
    const auto written = copyImages(plan, true);
    REQUIRE(written.is_success());
    CHECK(written.value().size() == 2u);
    CHECK(readFile(target / "stripe.png") == "new");
  }
}

TEST_CASE("Folder names for categories are checked", "[unity-asset-import]")
{
  CHECK(isValidFolderName("markings"));
  CHECK(isValidFolderName("Wall Signs-2_b"));
  CHECK_FALSE(isValidFolderName(""));
  CHECK_FALSE(isValidFolderName("../escape"));
  CHECK_FALSE(isValidFolderName("a/b"));
  CHECK_FALSE(isValidFolderName("_generated"));
  CHECK_FALSE(isValidFolderName(".hidden"));
  CHECK_FALSE(isValidFolderName(" padded "));
}

TEST_CASE("Decal categories are the decal folder's subfolders", "[unity-asset-import]")
{
  const auto folder = TemporaryFolder{};
  std::filesystem::create_directories(folder.path() / "markings");
  std::filesystem::create_directories(folder.path() / "grime");
  std::filesystem::create_directories(folder.path() / "_Generated");
  writeFile(folder.path() / "loose.png", "x");

  CHECK(decalCategories(folder.path()) == std::vector<std::string>{"grime", "markings"});

  REQUIRE(createSubfolder(folder.path(), "signs").is_success());
  CHECK(createSubfolder(folder.path(), "../nope").is_error());
  CHECK(
    decalCategories(folder.path())
    == std::vector<std::string>{"grime", "markings", "signs"});
}

TEST_CASE("Assets are located in the Unity project", "[unity-asset-import]")
{
  auto project = TemporarilySetPref{
    Preferences::UnityProjectPath, std::filesystem::path{"C:/project"}};

  CHECK(isInUnityProject("C:/project/Assets/Content/Textures/a.png"));
  CHECK_FALSE(isInUnityProject("C:/elsewhere/a.png"));
  CHECK(
    unityAssetPath("C:/project/Assets/Content/Textures/a.png")
    == std::filesystem::path{"Assets/Content/Textures/a.png"});
  CHECK(
    decalFolder()
    == std::filesystem::path{"C:/project/Assets/Content/Decals"}.lexically_normal());

  SECTION("assets outside the project are not removed")
  {
    CHECK(removeUnityAsset("C:/elsewhere/a.png").is_error());
  }
}

} // namespace tb::unity
