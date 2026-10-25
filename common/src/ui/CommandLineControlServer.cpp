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

#include "CommandLineControlServer.h"

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QString>

#include "Error.h"
#include "io/PathQt.h"
#include "mdl/MapFormat.h"
#include "ui/CommandLineControlService.h"

#include <cmath>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace tb::ui
{
namespace
{
constexpr auto MaxRequestSize = qsizetype{1024 * 1024};

QString protocolName(const mdl::MapFormat mapFormat)
{
  switch (mapFormat)
  {
  case mdl::MapFormat::Unknown:
    return "unknown";
  case mdl::MapFormat::Standard:
    return "standard";
  case mdl::MapFormat::Quake2:
    return "quake2";
  case mdl::MapFormat::Quake2_Valve:
    return "quake2-valve";
  case mdl::MapFormat::Valve:
    return "valve";
  case mdl::MapFormat::Hexen2:
    return "hexen2";
  case mdl::MapFormat::Daikatana:
    return "daikatana";
  case mdl::MapFormat::Quake3_Legacy:
    return "quake3-legacy";
  case mdl::MapFormat::Quake3_Valve:
    return "quake3-valve";
  case mdl::MapFormat::Quake3:
    return "quake3";
  }
  return "unknown";
}

std::optional<mdl::MapFormat> parseQuakeMapFormat(
  const QString& value, const bool allowAuto)
{
  if (value == "standard")
  {
    return mdl::MapFormat::Standard;
  }
  if (value == "valve")
  {
    return mdl::MapFormat::Valve;
  }
  if (allowAuto && value == "auto")
  {
    return mdl::MapFormat::Unknown;
  }
  return std::nullopt;
}

QJsonArray pointToJson(const vm::vec3d& point)
{
  return {point.x(), point.y(), point.z()};
}

QJsonObject mapStateToJson(const CommandLineControlMapState& state)
{
  return {
    {"has_map", state.hasMap},
    {"path", state.path.empty() ? QString{} : io::pathAsQString(state.path)},
    {"game", QString::fromStdString(state.gameName)},
    {"map_format", protocolName(state.mapFormat)},
    {"modified", state.modified},
  };
}

template <typename T>
QString resultErrorMessage(const Result<T>& result)
{
  return QString::fromStdString(std::get<Error>(result.error()).msg);
}

QJsonObject errorResponse(
  const QString& message, const QJsonValue& id = QJsonValue{QJsonValue::Undefined})
{
  auto response = QJsonObject{{"ok", false}, {"error", message}};
  if (!id.isUndefined())
  {
    response.insert("id", id);
  }
  return response;
}

QJsonObject successResponse(const QJsonValue& id, const QJsonValue& result)
{
  return {{"id", id}, {"ok", true}, {"result", result}};
}

bool optionalBoolean(
  const QJsonObject& object,
  const QString& key,
  const bool defaultValue,
  bool& value,
  QString& error)
{
  const auto jsonValue = object.value(key);
  if (jsonValue.isUndefined())
  {
    value = defaultValue;
    return true;
  }
  if (!jsonValue.isBool())
  {
    error = QString{"'%1' must be a boolean"}.arg(key);
    return false;
  }
  value = jsonValue.toBool();
  return true;
}

bool parseFiniteVector3(const QJsonValue& jsonValue, vm::vec3d& vector)
{
  if (!jsonValue.isArray())
  {
    return false;
  }

  const auto array = jsonValue.toArray();
  if (
    array.size() != 3 || !array[0].isDouble() || !array[1].isDouble()
    || !array[2].isDouble())
  {
    return false;
  }

  const auto x = array[0].toDouble();
  const auto y = array[1].toDouble();
  const auto z = array[2].toDouble();
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
  {
    return false;
  }

  vector = vm::vec3d{x, y, z};
  return true;
}

bool optionalFiniteNumber(
  const QJsonObject& object,
  const QString& key,
  const double defaultValue,
  double& value,
  QString& error)
{
  const auto jsonValue = object.value(key);
  if (jsonValue.isUndefined())
  {
    value = defaultValue;
    return true;
  }
  if (!jsonValue.isDouble() || !std::isfinite(jsonValue.toDouble()))
  {
    error = QString{"'%1' must be a finite number"}.arg(key);
    return false;
  }
  value = jsonValue.toDouble();
  return true;
}

QJsonObject cameraStateToJson(const CommandLineCameraState& state)
{
  return {
    {"position", pointToJson(vm::vec3d{state.position})},
    {"direction", pointToJson(vm::vec3d{state.direction})},
  };
}

QJsonObject executeRequest(CommandLineControlService& service, const QJsonObject& request)
{
  const auto id = request.value("id");
  if (!id.isString() && !id.isDouble())
  {
    return errorResponse("'id' must be a string or number");
  }

  const auto commandValue = request.value("command");
  if (!commandValue.isString())
  {
    return errorResponse("'command' must be a string", id);
  }
  const auto command = commandValue.toString();

  if (command == "status")
  {
    return successResponse(id, mapStateToJson(service.activeMapState()));
  }

  if (command == "new_map")
  {
    auto mapFormat = mdl::MapFormat::Standard;
    if (request.contains("map_format"))
    {
      const auto formatValue = request.value("map_format");
      if (!formatValue.isString())
      {
        return errorResponse("'map_format' must be 'standard' or 'valve'", id);
      }
      const auto parsedFormat = parseQuakeMapFormat(formatValue.toString(), false);
      if (!parsedFormat)
      {
        return errorResponse("'map_format' must be 'standard' or 'valve'", id);
      }
      mapFormat = *parsedFormat;
    }

    auto discardChanges = false;
    auto parseError = QString{};
    if (!optionalBoolean(request, "discard_changes", false, discardChanges, parseError))
    {
      return errorResponse(parseError, id);
    }

    const auto result = service.newQuakeMap(mapFormat, discardChanges);
    if (result.is_error())
    {
      return errorResponse(resultErrorMessage(result), id);
    }
    return successResponse(id, mapStateToJson(result.value()));
  }

  if (command == "load_map")
  {
    const auto pathValue = request.value("path");
    if (!pathValue.isString())
    {
      return errorResponse("'path' must be a string", id);
    }

    auto mapFormat = mdl::MapFormat::Unknown;
    if (request.contains("map_format"))
    {
      const auto formatValue = request.value("map_format");
      if (!formatValue.isString())
      {
        return errorResponse("'map_format' must be 'auto', 'standard', or 'valve'", id);
      }
      const auto parsedFormat = parseQuakeMapFormat(formatValue.toString(), true);
      if (!parsedFormat)
      {
        return errorResponse("'map_format' must be 'auto', 'standard', or 'valve'", id);
      }
      mapFormat = *parsedFormat;
    }

    auto discardChanges = false;
    auto parseError = QString{};
    if (!optionalBoolean(request, "discard_changes", false, discardChanges, parseError))
    {
      return errorResponse(parseError, id);
    }

    const auto path = io::pathFromQString(pathValue.toString());
    const auto result = service.loadQuakeMap(path, mapFormat, discardChanges);
    if (result.is_error())
    {
      return errorResponse(resultErrorMessage(result), id);
    }
    return successResponse(id, mapStateToJson(result.value()));
  }

  if (command == "save_map")
  {
    const auto result = service.saveMap();
    if (result.is_error())
    {
      return errorResponse(resultErrorMessage(result), id);
    }
    return successResponse(id, QJsonObject{});
  }

  if (command == "save_map_as")
  {
    const auto pathValue = request.value("path");
    if (!pathValue.isString())
    {
      return errorResponse("'path' must be a string", id);
    }

    auto overwrite = false;
    auto parseError = QString{};
    if (!optionalBoolean(request, "overwrite", false, overwrite, parseError))
    {
      return errorResponse(parseError, id);
    }

    const auto result =
      service.saveMapAs(io::pathFromQString(pathValue.toString()), overwrite);
    if (result.is_error())
    {
      return errorResponse(resultErrorMessage(result), id);
    }
    return successResponse(id, QJsonObject{{"path", io::pathAsQString(result.value())}});
  }

  if (command == "camera_move")
  {
    if (
      !request.contains("translation") && !request.contains("yaw_degrees")
      && !request.contains("pitch_degrees"))
    {
      return errorResponse(
        "'camera_move' needs translation, yaw_degrees, or pitch_degrees", id);
    }

    auto translation = vm::vec3d{};
    if (
      request.contains("translation")
      && !parseFiniteVector3(request.value("translation"), translation))
    {
      return errorResponse("'translation' must be a finite [x, y, z] array", id);
    }

    auto yawDegrees = 0.0;
    auto pitchDegrees = 0.0;
    auto parseError = QString{};
    if (
      !optionalFiniteNumber(request, "yaw_degrees", 0.0, yawDegrees, parseError)
      || !optionalFiniteNumber(request, "pitch_degrees", 0.0, pitchDegrees, parseError))
    {
      return errorResponse(parseError, id);
    }

    const auto result = service.moveCamera(translation, yawDegrees, pitchDegrees);
    if (result.is_error())
    {
      return errorResponse(resultErrorMessage(result), id);
    }
    return successResponse(id, cameraStateToJson(result.value()));
  }

  if (command == "frame_geometry")
  {
    const auto result = service.frameGeometry();
    if (result.is_error())
    {
      return errorResponse(resultErrorMessage(result), id);
    }
    return successResponse(id, cameraStateToJson(result.value()));
  }

  if (command == "capture_viewport")
  {
    const auto pathValue = request.value("path");
    if (!pathValue.isString())
    {
      return errorResponse("'path' must be a string", id);
    }

    auto overwrite = false;
    auto parseError = QString{};
    if (!optionalBoolean(request, "overwrite", false, overwrite, parseError))
    {
      return errorResponse(parseError, id);
    }

    const auto result =
      service.captureViewport(io::pathFromQString(pathValue.toString()), overwrite);
    if (result.is_error())
    {
      return errorResponse(resultErrorMessage(result), id);
    }

    const auto& capture = result.value();
    return successResponse(
      id,
      QJsonObject{
        {"path", io::pathAsQString(capture.path)},
        {"width", capture.width},
        {"height", capture.height},
      });
  }

  if (command == "create_brushes")
  {
    const auto brushesValue = request.value("brushes");
    if (!brushesValue.isArray())
    {
      return errorResponse("'brushes' must be an array", id);
    }

    auto brushes = std::vector<CommandLineBrushGeometry>{};
    const auto brushArray = brushesValue.toArray();
    brushes.reserve(static_cast<size_t>(brushArray.size()));
    for (qsizetype brushIndex = 0; brushIndex < brushArray.size(); ++brushIndex)
    {
      const auto brushValue = brushArray[brushIndex];
      if (!brushValue.isObject())
      {
        return errorResponse(
          QString{"'brushes[%1]' must be an object"}.arg(brushIndex), id);
      }

      const auto verticesValue = brushValue.toObject().value("vertices");
      if (!verticesValue.isArray())
      {
        return errorResponse(
          QString{"'brushes[%1].vertices' must be an array"}.arg(brushIndex), id);
      }

      auto geometry = CommandLineBrushGeometry{};
      const auto vertices = verticesValue.toArray();
      geometry.vertices.reserve(static_cast<size_t>(vertices.size()));
      for (qsizetype vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex)
      {
        const auto vertexValue = vertices[vertexIndex];
        if (!vertexValue.isArray() || vertexValue.toArray().size() != 3)
        {
          return errorResponse(
            QString{"'brushes[%1].vertices[%2]' must be a 3-number array"}
              .arg(brushIndex)
              .arg(vertexIndex),
            id);
        }

        const auto point = vertexValue.toArray();
        if (
          !point[0].isDouble() || !point[1].isDouble() || !point[2].isDouble()
          || !std::isfinite(point[0].toDouble()) || !std::isfinite(point[1].toDouble())
          || !std::isfinite(point[2].toDouble()))
        {
          return errorResponse(
            QString{"'brushes[%1].vertices[%2]' must contain finite numbers"}
              .arg(brushIndex)
              .arg(vertexIndex),
            id);
        }
        geometry.vertices.emplace_back(
          point[0].toDouble(), point[1].toDouble(), point[2].toDouble());
      }
      brushes.push_back(std::move(geometry));
    }

    const auto result = service.createBrushes(brushes);
    if (result.is_error())
    {
      return errorResponse(resultErrorMessage(result), id);
    }

    auto createdBrushes = QJsonArray{};
    for (const auto& brush : result.value())
    {
      createdBrushes.append(
        QJsonObject{
          {"id", QString::fromStdString(brush.id)},
          {"bounds",
           QJsonObject{
             {"min", pointToJson(brush.bounds.min)},
             {"max", pointToJson(brush.bounds.max)},
           }},
        });
    }
    return successResponse(id, createdBrushes);
  }

  return errorResponse(QString{"Unknown command: %1"}.arg(command), id);
}

QByteArray serializeResponse(const QJsonObject& response)
{
  auto bytes = QJsonDocument{response}.toJson(QJsonDocument::Compact);
  bytes.append('\n');
  return bytes;
}

} // namespace

class CommandLineControlServer::Impl
{
private:
  CommandLineControlService& m_service;
  // Buffers must outlive the server: destroying its sockets can emit disconnected.
  QHash<QLocalSocket*, QByteArray> m_clientBuffers;
  QLocalServer m_server;

public:
  explicit Impl(CommandLineControlService& service)
    : m_service{service}
  {
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    QObject::connect(&m_server, &QLocalServer::newConnection, &m_server, [this]() {
      acceptConnections();
    });
  }

  Result<void> listen(const std::string& socketName)
  {
    if (socketName.empty())
    {
      return Error{"The command-line control socket name must not be empty"};
    }
    if (!m_server.listen(QString::fromStdString(socketName)))
    {
      return Error{
        "Could not listen on command-line control socket '" + socketName
        + "': " + m_server.errorString().toStdString()};
    }
    return Result<void>{};
  }

private:
  void acceptConnections()
  {
    while (m_server.hasPendingConnections())
    {
      auto* socket = m_server.nextPendingConnection();
      if (!socket)
      {
        continue;
      }

      socket->setReadBufferSize(MaxRequestSize + 1);
      m_clientBuffers.insert(socket, QByteArray{});
      QObject::connect(socket, &QLocalSocket::readyRead, socket, [this, socket]() {
        readClient(socket);
      });
      QObject::connect(socket, &QLocalSocket::disconnected, socket, [this, socket]() {
        m_clientBuffers.remove(socket);
        socket->deleteLater();
      });
    }
  }

  void readClient(QLocalSocket* socket)
  {
    auto& buffer = m_clientBuffers[socket];
    buffer += socket->readAll();

    while (true)
    {
      const auto newline = buffer.indexOf('\n');
      if (newline < 0)
      {
        break;
      }
      if (newline > MaxRequestSize)
      {
        rejectOversizedRequest(socket);
        return;
      }

      const auto line = buffer.left(newline);
      buffer.remove(0, newline + 1);
      processLine(socket, line);
    }

    if (buffer.size() > MaxRequestSize)
    {
      rejectOversizedRequest(socket);
    }
  }

  void processLine(QLocalSocket* socket, const QByteArray& line)
  {
    auto parseError = QJsonParseError{};
    const auto document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
      const auto message = parseError.error == QJsonParseError::NoError
                             ? QString{"Each request must be a JSON object"}
                             : parseError.errorString();
      socket->write(serializeResponse(errorResponse(message)));
      return;
    }

    const auto request = document.object();
    const auto requestId = request.value("id");
    try
    {
      socket->write(serializeResponse(executeRequest(m_service, request)));
    }
    catch (const std::exception& e)
    {
      socket->write(
        serializeResponse(errorResponse(QString::fromUtf8(e.what()), requestId)));
    }
    catch (...)
    {
      socket->write(serializeResponse(
        errorResponse("Unknown error while handling request", requestId)));
    }
  }

  void rejectOversizedRequest(QLocalSocket* socket)
  {
    socket->write(serializeResponse(errorResponse("Request exceeds the 1 MiB limit")));
    socket->disconnectFromServer();
    m_clientBuffers.remove(socket);
  }
};

CommandLineControlServer::CommandLineControlServer(CommandLineControlService& service)
  : m_impl{std::make_unique<Impl>(service)}
{
}

CommandLineControlServer::~CommandLineControlServer() = default;

Result<void> CommandLineControlServer::listen(const std::string& socketName)
{
  return m_impl->listen(socketName);
}

} // namespace tb::ui
