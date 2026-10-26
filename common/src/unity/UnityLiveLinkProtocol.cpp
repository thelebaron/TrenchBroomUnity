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

#include "UnityLiveLinkProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Error.h"
#include "io/PathQt.h"

#include "vm/constants.h"
#include "vm/vec.h"

#include <cmath>
#include <optional>

namespace tb::unity
{
namespace
{

std::optional<vm::vec3f> parseVector(const QJsonValue& value)
{
  const auto array = value.toArray();
  if (array.size() != 3)
  {
    return std::nullopt;
  }

  auto result = vm::vec3f{};
  for (qsizetype i = 0; i < 3; ++i)
  {
    if (!array[i].isDouble())
    {
      return std::nullopt;
    }
    const auto component = static_cast<float>(array[i].toDouble());
    if (!std::isfinite(component))
    {
      return std::nullopt;
    }
    result[static_cast<size_t>(i)] = component;
  }
  return result;
}

std::optional<vm::vec3f> parseUnitVector(const QJsonValue& value)
{
  const auto vector = parseVector(value);
  if (!vector || vm::is_zero(*vector, vm::Cf::almost_zero()))
  {
    return std::nullopt;
  }
  return vm::normalize(*vector);
}

QJsonArray vectorToJson(const vm::vec3f& vector)
{
  return {vector.x(), vector.y(), vector.z()};
}

QByteArray serialize(const QJsonObject& message)
{
  auto bytes = QJsonDocument{message}.toJson(QJsonDocument::Compact);
  bytes.append('\n');
  return bytes;
}

Result<LiveLinkMessage> parseCamera(const QJsonObject& message)
{
  const auto position = parseVector(message.value("position"));
  const auto direction = parseUnitVector(message.value("direction"));
  const auto up = parseUnitVector(message.value("up"));
  if (!position || !direction || !up)
  {
    return Error{
      "Live link camera needs finite 'position', 'direction' and 'up' [x, y, z] "
      "arrays, and direction and up must not be zero"};
  }
  return LiveLinkCamera{{*position, *direction, *up}};
}

} // namespace

Result<LiveLinkMessage> parseLiveLinkMessage(const QByteArray& line)
{
  auto parseError = QJsonParseError{};
  const auto document = QJsonDocument::fromJson(line, &parseError);
  if (parseError.error != QJsonParseError::NoError)
  {
    return Error{
      "Live link message is not valid JSON: " + parseError.errorString().toStdString()};
  }
  if (!document.isObject())
  {
    return Error{"Live link message must be a JSON object"};
  }

  const auto message = document.object();
  const auto type = message.value("type").toString();
  if (type == "hello")
  {
    return LiveLinkHello{message.value("version").toInt()};
  }
  if (type == "camera")
  {
    return parseCamera(message);
  }
  if (type == "request_camera")
  {
    return LiveLinkRequestCamera{};
  }
  if (type == "live_camera")
  {
    if (!message.value("enabled").isBool())
    {
      return Error{"Live link 'live_camera' needs a boolean 'enabled'"};
    }
    return LiveLinkLiveCamera{message.value("enabled").toBool()};
  }
  return Error{"Unknown live link message type '" + type.toStdString() + "'"};
}

QByteArray serializeLiveLinkHello(
  const std::filesystem::path& mapPath, const bool liveCamera)
{
  return serialize({
    {"type", "hello"},
    {"version", LiveLinkProtocolVersion},
    {"map", io::pathAsQString(mapPath)},
    {"live_camera", liveCamera},
  });
}

QByteArray serializeLiveLinkCamera(
  const LiveLinkCameraPose& pose, const std::filesystem::path& mapPath)
{
  return serialize({
    {"type", "camera"},
    {"map", io::pathAsQString(mapPath)},
    {"position", vectorToJson(pose.position)},
    {"direction", vectorToJson(pose.direction)},
    {"up", vectorToJson(pose.up)},
  });
}

QByteArray serializeLiveLinkLiveCamera(const bool enabled)
{
  return serialize({{"type", "live_camera"}, {"enabled", enabled}});
}

} // namespace tb::unity
