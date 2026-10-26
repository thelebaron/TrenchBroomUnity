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

#include <QEventLoop>
#include <QTimer>

#include "Logger.h"
#include "MapFixture.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "TestUtils.h"
#include "io/LoadMaterialCollections.h"
#include "io/TestEnvironment.h"
#include "io/TraversalMode.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Game.h"
#include "mdl/GameConfig.h"
#include "mdl/GameFactory.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Assets.h"
#include "mdl/MaterialCollection.h"
#include "mdl/MaterialManager.h"
#include "mdl/Resource.h"
#include "mdl/Texture.h"
#include "mdl/WorldNode.h"
#include "unity/TextureFolderWatcher.h"
#include "unity/UnityTextureFolders.h"
#include "unity/UnityWorkflow.h"

#include "kdl/task_manager.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::unity
{
TEST_CASE("Unity startup defaults explicitly select Standard", "[unity-workflow]")
{
  auto environment = io::TestEnvironment{};
  auto& factory = mdl::GameFactory::instance();
  factory.reset();
  REQUIRE(factory
            .initialize(
              {{std::filesystem::current_path() / "fixture/games"},
               environment.dir() / "games"})
            .is_success());
  auto gamePref = TemporarilySetPref{Preferences::UnityDefaultGame, QString{"Quake"}};
  auto formatPref =
    TemporarilySetPref{Preferences::UnityDefaultMapFormat, QString{"Valve"}};
  CHECK(defaultGameAndFormat() == GameAndFormat{"Quake", mdl::MapFormat::Standard});
  CHECK(
    completeMapHeader(std::nullopt, mdl::MapFormat::Unknown)
    == GameAndFormat{"Quake", mdl::MapFormat::Standard});
  CHECK(
    completeMapHeader("Quake", mdl::MapFormat::Valve)
    == GameAndFormat{"Quake", mdl::MapFormat::Valve});
  CHECK(
    completeMapHeader("Missing game", mdl::MapFormat::Unknown)
    == GameAndFormat{"Quake", mdl::MapFormat::Standard});
  auto logger = NullLogger{};
  auto tasks = kdl::task_manager{};
  auto map = mdl::Map{tasks, logger};
  REQUIRE(
    map
      .create(
        mdl::MapFormat::Standard, vm::bbox3d{8192.0}, createEmptyGame("Quake", logger))
      .is_success());
  REQUIRE(map.world());
  CHECK(map.world()->defaultLayer()->childCount() == 0);
  CHECK_FALSE(map.persistent());
  CHECK_FALSE(map.modified());
  CHECK_FALSE(factory.gameConfig("Quake").forceEmptyNewMap);
}

TEST_CASE(
  "Unity texture folders flatten recursive images and reject ambiguous names",
  "[unity-workflow]")
{
  auto environment = io::TestEnvironment{[](auto& env) {
    env.createDirectory("first/sub");
    env.createDirectory("second");
    env.createFile("first/sub/brick.png", "image");
    env.createFile("first/sub/metal.tga", "image");
    env.createFile("second/floor.bmp", "image");
    env.createFile("second/ignore.mat", "not an image");
  }};
  const auto folders = std::vector<std::filesystem::path>{
    environment.dir() / "first", environment.dir() / "second"};
  auto scan = scanTextureFolders(folders);
  REQUIRE(scan.errors.empty());
  REQUIRE(scan.textures.size() == 3);
  auto folderFs = io::createImageFileSystem<TextureFolderFileSystem>(scan.textures);
  REQUIRE(folderFs.is_success());
  const auto paths = folderFs.value()->find("", io::TraversalMode::Recursive);
  REQUIRE(paths.is_success());
  CHECK(paths.value().size() == 3);
  CHECK(folderFs.value()->openFile("brick.png").is_success());
  CHECK(folderFs.value()->openFile("sub/brick.png").is_error());

  SECTION("duplicates include both paths and neither candidate is loaded")
  {
    environment.createFile("second/BRICK.tga", "image");
    scan = scanTextureFolders(folders);
    REQUIRE(scan.errors.size() == 1);
    CHECK(scan.errors.front().find("first") != std::string::npos);
    CHECK(scan.errors.front().find("BRICK.tga") != std::string::npos);
    CHECK(scan.errors.front().find("brick.png") != std::string::npos);
    CHECK(scan.textures.size() == 2);
  }
  SECTION("overlapping roots do not duplicate the same file")
  {
    scan = scanTextureFolders({folders.front(), folders.front() / "sub"});
    CHECK(scan.errors.empty());
    CHECK(scan.textures.size() == 2);
  }
  SECTION("missing folder is an error")
  {
    scan = scanTextureFolders({environment.dir() / "missing"});
    CHECK(scan.errors.size() == 1);
    CHECK(scan.textures.empty());
  }
  SECTION("WAD name warnings do not prevent loading")
  {
    environment.createFile("first/UPPER.png", "image");
    environment.createFile("first/abcdefghijklmnop.png", "image");
    environment.createFile("first/+0anim.png", "image");
    environment.createFile("first/sky_test.png", "image");
    scan = scanTextureFolders(folders);
    CHECK(scan.errors.empty());
    CHECK(scan.warnings.size() == 4);
    CHECK(scan.textures.size() == 7);
  }
}

TEST_CASE(
  "Unity folder materials keep flat names, groups and native dimensions",
  "[unity-workflow]")
{
  auto environment =
    io::TestEnvironment{[](auto& env) { env.createDirectory("Textures/sub"); }};
  std::filesystem::copy_file(
    "fixture/test/io/Image/707x710.png", environment.dir() / "Textures/sub/brick.png");
  auto enabled = TemporarilySetPref{Preferences::UnityTextureFoldersEnabled, true};
  auto folders = TemporarilySetPref{
    Preferences::UnityTextureFolders,
    std::vector<std::filesystem::path>{environment.dir() / "Textures"}};
  auto logger = NullLogger{};
  auto tasks = kdl::task_manager{};
  auto fs = io::VirtualFileSystem{};
  const auto mounts = mountTextureFolders(fs, "textures", logger);
  REQUIRE(mounts.size() == 1);
  auto config = mdl::GameConfig{};
  config.name = "Quake";
  config.materialConfig.root = "textures";
  const auto result = io::loadMaterialCollections(
    fs,
    textureMaterialConfig(config),
    [](auto loader) {
      auto resource = std::make_shared<mdl::TextureResource>(std::move(loader));
      resource->loadSync();
      return resource;
    },
    tasks,
    logger);
  REQUIRE(result.is_success());
  REQUIRE(result.value().size() == 1);
  CHECK(result.value().front().path() == "Textures");
  const auto& materials = result.value().front().materials();
  REQUIRE(materials.size() == 1);
  CHECK(materials.front().name() == "brick");
  REQUIRE(materials.front().texture());
  CHECK(materials.front().texture()->width() == 707);
  CHECK(materials.front().texture()->height() == 710);
}

TEST_CASE(
  "Unity folder mode preserves map texture names and legacy wad properties",
  "[unity-workflow]")
{
  auto environment =
    io::TestEnvironment{[](auto& env) { env.createDirectory("Textures"); }};
  std::filesystem::copy_file(
    "fixture/test/io/Image/5x5.png", environment.dir() / "Textures/brick.png");
  environment.createFile("source.map", R"(// Game: Quake
// Format: Standard
{
"classname" "worldspawn"
"wad" "C:/old/path/legacy.wad"
{
( -64 -64 -16 ) ( -64 -63 -16 ) ( -64 -64 -15 ) brick 0 0 0 1 1
( -64 -64 -16 ) ( -64 -64 -15 ) ( -63 -64 -16 ) brick 0 0 0 1 1
( -64 -64 -16 ) ( -63 -64 -16 ) ( -64 -63 -16 ) brick 0 0 0 1 1
( 64 64 16 ) ( 64 65 16 ) ( 65 64 16 ) brick 0 0 0 1 1
( 64 64 16 ) ( 65 64 16 ) ( 64 64 17 ) brick 0 0 0 1 1
( 64 64 16 ) ( 64 64 17 ) ( 64 65 16 ) brick 0 0 0 1 1
}
}
)");
  auto enabled = TemporarilySetPref{Preferences::UnityTextureFoldersEnabled, true};
  auto folders = TemporarilySetPref{
    Preferences::UnityTextureFolders,
    std::vector<std::filesystem::path>{environment.dir() / "Textures"}};
  auto fixture = mdl::MapFixture{};
  fixture.load(
    environment.dir() / "source.map",
    {.mapFormat = mdl::MapFormat::Standard, .game = mdl::LoadGameFixture{"Quake"}});
  auto& map = fixture.map();
  CHECK(*map.world()->entity().property("wad") == "C:/old/path/legacy.wad");
  REQUIRE(map.materialManager().material("brick"));
  REQUIRE(map.world()->defaultLayer()->childCount() == 1);
  const auto* brush =
    dynamic_cast<mdl::BrushNode*>(map.world()->defaultLayer()->children().front());
  REQUIRE(brush);
  REQUIRE(brush->brush().faces().size() == 6);
  for (const auto& face : brush->brush().faces())
  {
    CHECK(face.attributes().materialName() == "brick");
  }
  REQUIRE(map.saveAs(environment.dir() / "saved.map").is_success());
  auto restored = mdl::MapFixture{};
  restored.load(
    environment.dir() / "saved.map",
    {.mapFormat = mdl::MapFormat::Standard, .game = mdl::LoadGameFixture{"Quake"}});
  REQUIRE(restored.map().saveAs(environment.dir() / "saved-again.map").is_success());
  CHECK(environment.loadFile("saved.map") == environment.loadFile("saved-again.map"));
  CHECK(*restored.map().world()->entity().property("wad") == "C:/old/path/legacy.wad");

  auto watcher = TextureFolderWatcher{map};
  auto reloads = 0;
  auto connection =
    map.materialCollectionsDidChangeNotifier.connect([&]() { ++reloads; });
  std::filesystem::copy_file(
    "fixture/test/io/Image/5x5.png", environment.dir() / "Textures/added.png");
  auto eventLoop = QEventLoop{};
  QTimer::singleShot(1200, &eventLoop, &QEventLoop::quit);
  eventLoop.exec();
  CHECK(reloads > 0);
  CHECK(map.materialManager().material("added") != nullptr);
  CHECK_FALSE(map.modified());
  CHECK(*map.world()->entity().property("wad") == "C:/old/path/legacy.wad");
}
} // namespace tb::unity
