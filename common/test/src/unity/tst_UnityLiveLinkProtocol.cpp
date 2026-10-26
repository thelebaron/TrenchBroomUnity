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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "unity/UnityLiveLinkProtocol.h"

#include <variant>

#include <catch2/catch_test_macros.hpp>

namespace tb::unity
{
namespace
{
QJsonObject parseJsonLine(const QByteArray& bytes)
{
  REQUIRE(bytes.endsWith('\n'));
  REQUIRE(bytes.count('\n') == 1);
  return QJsonDocument::fromJson(bytes.chopped(1)).object();
}
} // namespace

TEST_CASE("Unity live link parses camera messages", "[unity-live-link]")
{
  const auto result = parseLiveLinkMessage(
    R"({"type":"camera","position":[64,-32,16],"direction":[2,0,0],"up":[0,0,3]})");
  REQUIRE(result.is_success());
  const auto* camera = std::get_if<LiveLinkCamera>(&result.value());
  REQUIRE(camera != nullptr);
  CHECK(camera->pose.position == vm::vec3f{64, -32, 16});
  // direction and up arrive normalized
  CHECK(camera->pose.direction == vm::vec3f{1, 0, 0});
  CHECK(camera->pose.up == vm::vec3f{0, 0, 1});
}

TEST_CASE("Unity live link rejects bad camera messages", "[unity-live-link]")
{
  CHECK(parseLiveLinkMessage(
          R"({"type":"camera","position":[0,0],"direction":[1,0,0],"up":[0,0,1]})")
          .is_error());
  CHECK(parseLiveLinkMessage(
          R"({"type":"camera","position":[0,0,0],"direction":[0,0,0],"up":[0,0,1]})")
          .is_error());
  CHECK(parseLiveLinkMessage(
          R"({"type":"camera","position":[0,"a",0],"direction":[1,0,0],"up":[0,0,1]})")
          .is_error());
  CHECK(parseLiveLinkMessage(R"({"type":"camera"})").is_error());
}

TEST_CASE("Unity live link parses control messages", "[unity-live-link]")
{
  const auto hello = parseLiveLinkMessage(R"({"type":"hello","version":1})");
  REQUIRE(hello.is_success());
  CHECK(std::get<LiveLinkHello>(hello.value()).version == 1);

  const auto request = parseLiveLinkMessage(R"({"type":"request_camera"})");
  REQUIRE(request.is_success());
  CHECK(std::holds_alternative<LiveLinkRequestCamera>(request.value()));

  const auto live = parseLiveLinkMessage(R"({"type":"live_camera","enabled":true})");
  REQUIRE(live.is_success());
  CHECK(std::get<LiveLinkLiveCamera>(live.value()).enabled);

  CHECK(parseLiveLinkMessage(R"({"type":"live_camera","enabled":1})").is_error());
  CHECK(parseLiveLinkMessage(R"({"type":"unknown"})").is_error());
  CHECK(parseLiveLinkMessage(R"([1,2,3])").is_error());
  CHECK(parseLiveLinkMessage("not json").is_error());
}

TEST_CASE("Unity live link serializes messages as JSON lines", "[unity-live-link]")
{
  const auto camera = parseJsonLine(serializeLiveLinkCamera(
    {{1, 2, 3}, {0, 1, 0}, {0, 0, 1}}, std::filesystem::path{"C:/maps/e1m1.map"}));
  CHECK(camera.value("type").toString() == "camera");
  CHECK(camera.value("map").toString() == "C:/maps/e1m1.map");
  CHECK(camera.value("position").toArray() == QJsonArray{1, 2, 3});
  CHECK(camera.value("direction").toArray() == QJsonArray{0, 1, 0});
  CHECK(camera.value("up").toArray() == QJsonArray{0, 0, 1});

  const auto hello = parseJsonLine(serializeLiveLinkHello({}, true));
  CHECK(hello.value("type").toString() == "hello");
  CHECK(hello.value("version").toInt() == LiveLinkProtocolVersion);
  CHECK(hello.value("live_camera").toBool());

  const auto live = parseJsonLine(serializeLiveLinkLiveCamera(false));
  CHECK(live.value("type").toString() == "live_camera");
  CHECK(live.value("enabled").toBool() == false);
}

TEST_CASE("Unity live link camera round trips", "[unity-live-link]")
{
  const auto pose = LiveLinkCameraPose{{128, 64, 32}, {0, 0, -1}, {1, 0, 0}};
  const auto bytes = serializeLiveLinkCamera(pose, {});
  const auto result = parseLiveLinkMessage(bytes.chopped(1));
  REQUIRE(result.is_success());
  CHECK(std::get<LiveLinkCamera>(result.value()).pose == pose);
}

} // namespace tb::unity
