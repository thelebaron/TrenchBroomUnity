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

#include <QByteArray>

#include "Result.h"

#include "vm/vec.h"

#include <filesystem>
#include <variant>

namespace tb::unity
{

constexpr auto LiveLinkProtocolVersion = 2;

/**
 * A camera in Quake map units: Z is up, and direction and up are unit vectors.
 */
struct LiveLinkCameraPose
{
  vm::vec3f position;
  vm::vec3f direction;
  vm::vec3f up;

  bool operator==(const LiveLinkCameraPose&) const = default;
};

struct LiveLinkHello
{
  int version = 0;
};

struct LiveLinkCamera
{
  LiveLinkCameraPose pose;
};

struct LiveLinkRequestCamera
{
};

struct LiveLinkLiveCamera
{
  bool enabled = false;
};

/**
 * Unity reports entering (true) and leaving (false) play mode.
 */
struct LiveLinkPlayState
{
  bool playing = false;
};

/**
 * What a play start's position is: where the player's eyes go, or where the player
 * stands.
 */
enum class LiveLinkPlayAnchor
{
  Eye,
  Feet,
};

/**
 * Where Unity starts playing: the player is placed at the pose, facing its direction.
 */
struct LiveLinkPlayStart
{
  LiveLinkCameraPose pose;
  LiveLinkPlayAnchor anchor = LiveLinkPlayAnchor::Eye;

  bool operator==(const LiveLinkPlayStart&) const = default;
};

using LiveLinkMessage = std::variant<
  LiveLinkHello,
  LiveLinkCamera,
  LiveLinkRequestCamera,
  LiveLinkLiveCamera,
  LiveLinkPlayState>;

/**
 * Parses one JSON line sent by Unity. The line must not include the trailing newline.
 */
Result<LiveLinkMessage> parseLiveLinkMessage(const QByteArray& line);

// Each serialized message is one compact JSON object followed by a newline.
QByteArray serializeLiveLinkHello(const std::filesystem::path& mapPath, bool liveCamera);
QByteArray serializeLiveLinkCamera(
  const LiveLinkCameraPose& pose, const std::filesystem::path& mapPath);
QByteArray serializeLiveLinkLiveCamera(bool enabled);
QByteArray serializeLiveLinkPlay(
  const LiveLinkPlayStart& start, const std::filesystem::path& mapPath);
QByteArray serializeLiveLinkStop();

} // namespace tb::unity
