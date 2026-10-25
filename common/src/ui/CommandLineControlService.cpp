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

#include "CommandLineControlService.h"

#include "TrenchBroomApp.h"
#include "io/DiskIO.h"
#include "io/PathInfo.h"
#include "io/PathQt.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushNode.h"
#include "mdl/GameFactory.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/WorldNode.h"
#include "render/Camera.h"
#include "ui/FrameManager.h"
#include "ui/MapDocument.h"
#include "ui/MapFrame.h"
#include "ui/MapView3D.h"
#include "ui/MapViewBase.h"

#include "vm/scalar.h"

#include <QImage>

#include <cmath>
#include <ranges>
#include <system_error>
#include <utility>
#include <variant>

namespace tb::ui
{
namespace
{
bool isQuakeMapFormat(const mdl::MapFormat mapFormat)
{
  return mapFormat == mdl::MapFormat::Standard || mapFormat == mdl::MapFormat::Valve;
}

bool isQuakeMap(const mdl::Map& map)
{
  return map.game() != nullptr && map.world() != nullptr
         && map.game()->config().name == "Quake"
         && isQuakeMapFormat(map.world()->mapFormat());
}

bool hasLoadedMap(const mdl::Map& map)
{
  return map.game() != nullptr && map.world() != nullptr;
}

bool hasUnsavedChanges(const mdl::Map& map)
{
  return hasLoadedMap(map) && map.modified();
}

} // namespace

CommandLineControlService::CommandLineControlService(TrenchBroomApp& app)
  : m_app{app}
{
}

CommandLineControlMapState CommandLineControlService::activeMapState() const
{
  auto result = CommandLineControlMapState{};
  auto* frame = m_app.m_frameManager->topFrame();
  if (!frame)
  {
    return result;
  }

  const auto& map = frame->document().map();
  if (!hasLoadedMap(map))
  {
    return result;
  }

  result.hasMap = true;
  result.path = map.path();
  result.gameName = map.game()->config().name;
  result.mapFormat = map.world()->mapFormat();
  result.modified = map.modified();
  return result;
}

Result<CommandLineControlMapState> CommandLineControlService::newQuakeMap(
  const mdl::MapFormat mapFormat, const bool discardChanges)
{
  if (!isQuakeMapFormat(mapFormat))
  {
    return Error{"New Quake maps must use Standard or Valve map format"};
  }

  auto* frame = m_app.m_frameManager->topFrame();
  auto createdFrame = false;
  if (!frame)
  {
    frame = m_app.m_frameManager->newFrame(m_app.m_taskManager);
    createdFrame = true;
  }

  auto& map = frame->document().map();
  if (hasUnsavedChanges(map) && !discardChanges)
  {
    if (createdFrame)
    {
      frame->close();
    }
    return Error{"The active map has unsaved changes"};
  }
  if (!frame->closeCompileDialog())
  {
    if (createdFrame)
    {
      frame->close();
    }
    return Error{"Could not close the active compile dialog"};
  }

  auto game = mdl::GameFactory::instance().createGame("Quake", frame->logger());
  if (!game)
  {
    if (createdFrame)
    {
      frame->close();
    }
    return Error{"The Quake game configuration is not available"};
  }

  if (
    const auto result =
      map.create(mapFormat, MapDocument::DefaultWorldBounds, std::move(game));
    result.is_error())
  {
    if (createdFrame)
    {
      frame->close();
    }
    return Error{std::get<Error>(result.error()).msg};
  }

  m_app.closeWelcomeWindow();
  return activeMapState();
}

Result<CommandLineControlMapState> CommandLineControlService::loadQuakeMap(
  const std::filesystem::path& path,
  const mdl::MapFormat mapFormat,
  const bool discardChanges)
{
  if (mapFormat != mdl::MapFormat::Unknown && !isQuakeMapFormat(mapFormat))
  {
    return Error{"Quake maps must use Standard or Valve map format"};
  }

  auto absPath = path;
  if (!absPath.is_absolute())
  {
    auto error = std::error_code{};
    absPath = std::filesystem::absolute(absPath, error).lexically_normal();
    if (error)
    {
      return Error{"Could not resolve the map path: " + error.message()};
    }
  }

  if (io::Disk::pathInfo(absPath) != io::PathInfo::File)
  {
    return Error{"Map file not found: " + absPath.string()};
  }

  auto* frame = m_app.m_frameManager->topFrame();
  auto createdFrame = false;
  if (!frame)
  {
    frame = m_app.m_frameManager->newFrame(m_app.m_taskManager);
    createdFrame = true;
  }

  auto& map = frame->document().map();
  if (hasUnsavedChanges(map) && !discardChanges)
  {
    if (createdFrame)
    {
      frame->close();
    }
    return Error{"The active map has unsaved changes"};
  }
  if (!frame->closeCompileDialog())
  {
    if (createdFrame)
    {
      frame->close();
    }
    return Error{"Could not close the active compile dialog"};
  }

  auto game = mdl::GameFactory::instance().createGame("Quake", frame->logger());
  if (!game)
  {
    if (createdFrame)
    {
      frame->close();
    }
    return Error{"The Quake game configuration is not available"};
  }

  if (
    const auto result =
      map.load(mapFormat, MapDocument::DefaultWorldBounds, std::move(game), absPath);
    result.is_error())
  {
    if (createdFrame)
    {
      frame->close();
    }
    return Error{std::get<Error>(result.error()).msg};
  }

  m_app.closeWelcomeWindow();
  return activeMapState();
}

Result<void> CommandLineControlService::saveMap()
{
  auto* frame = m_app.m_frameManager->topFrame();
  if (!frame)
  {
    return Error{"No active map"};
  }

  auto& map = frame->document().map();
  if (!isQuakeMap(map))
  {
    return Error{"The active document is not a Quake 1 map"};
  }
  if (!map.path().is_absolute())
  {
    return Error{"The active map has no save path; use saveMapAs"};
  }

  return map.save();
}

Result<std::filesystem::path> CommandLineControlService::saveMapAs(
  const std::filesystem::path& path, const bool overwrite)
{
  auto* frame = m_app.m_frameManager->topFrame();
  if (!frame)
  {
    return Error{"No active map"};
  }

  auto& map = frame->document().map();
  if (!isQuakeMap(map))
  {
    return Error{"The active document is not a Quake 1 map"};
  }
  if (path.empty())
  {
    return Error{"A destination path is required"};
  }

  auto normalizedPath = path;
  if (normalizedPath.extension().empty())
  {
    normalizedPath += ".map";
  }
  if (!normalizedPath.is_absolute())
  {
    auto error = std::error_code{};
    normalizedPath = std::filesystem::absolute(normalizedPath, error).lexically_normal();
    if (error)
    {
      return Error{"Could not resolve the destination path: " + error.message()};
    }
  }

  const auto samePath = normalizedPath == map.path().lexically_normal();
  if (!samePath && io::Disk::pathInfo(normalizedPath) == io::PathInfo::File && !overwrite)
  {
    return Error{"The destination file already exists; set overwrite to replace it"};
  }

  if (const auto result = map.saveAs(normalizedPath); result.is_error())
  {
    return Error{std::get<Error>(result.error()).msg};
  }
  return normalizedPath;
}

Result<std::vector<CommandLineCreatedBrush>> CommandLineControlService::createBrushes(
  const std::vector<CommandLineBrushGeometry>& brushGeometries)
{
  auto* frame = m_app.m_frameManager->topFrame();
  if (!frame)
  {
    return Error{"No active map"};
  }

  auto& map = frame->document().map();
  if (!isQuakeMap(map))
  {
    return Error{"The active document is not a Quake 1 map"};
  }
  if (brushGeometries.empty())
  {
    return Error{"At least one brush is required"};
  }

  const auto builder = mdl::BrushBuilder{
    map.world()->mapFormat(),
    map.worldBounds(),
    map.game()->config().faceAttribsConfig.defaults};
  auto brushes = std::vector<mdl::Brush>{};
  brushes.reserve(brushGeometries.size());
  for (size_t i = 0; i < brushGeometries.size(); ++i)
  {
    const auto& geometry = brushGeometries[i];
    if (geometry.vertices.size() < 4)
    {
      return Error{"Brush " + std::to_string(i) + " needs at least four vertices"};
    }
    if (std::ranges::any_of(geometry.vertices, [](const auto& vertex) {
          return !std::isfinite(vertex.x()) || !std::isfinite(vertex.y())
                 || !std::isfinite(vertex.z());
        }))
    {
      return Error{"Brush " + std::to_string(i) + " contains a non-finite coordinate"};
    }

    auto brushResult = builder.createBrush(geometry.vertices, map.currentMaterialName());
    if (brushResult.is_error())
    {
      return Error{
        "Could not create brush " + std::to_string(i) + ": "
        + std::get<Error>(brushResult.error()).msg};
    }
    brushes.push_back(std::move(brushResult).value());
  }

  auto brushNodes = std::vector<mdl::BrushNode*>{};
  auto nodes = std::vector<mdl::Node*>{};
  brushNodes.reserve(brushes.size());
  nodes.reserve(brushes.size());
  for (auto& brush : brushes)
  {
    auto* node = new mdl::BrushNode{std::move(brush)};
    brushNodes.push_back(node);
    nodes.push_back(node);
  }

  const auto addedNodes = mdl::addNodes(map, {{mdl::parentForNodes(map), nodes}});
  if (addedNodes.size() != brushNodes.size())
  {
    return Error{"Could not add brushes to the active map"};
  }

  mdl::selectNodes(map, addedNodes);

  auto result = std::vector<CommandLineCreatedBrush>{};
  result.reserve(brushNodes.size());
  for (const auto* node : brushNodes)
  {
    result.push_back({node->linkId(), node->logicalBounds()});
  }
  return result;
}

Result<CommandLineCameraState> CommandLineControlService::moveCamera(
  const vm::vec3d& translation, const double yawDegrees, const double pitchDegrees)
{
  if (
    !std::isfinite(translation.x()) || !std::isfinite(translation.y())
    || !std::isfinite(translation.z()) || !std::isfinite(yawDegrees)
    || !std::isfinite(pitchDegrees))
  {
    return Error{"Camera movement values must be finite numbers"};
  }

  auto* frame = m_app.m_frameManager->topFrame();
  if (!frame || !isQuakeMap(frame->document().map()))
  {
    return Error{"No active Quake 1 map"};
  }

  auto* mapView = frame->findChild<MapView3D*>();
  if (!mapView)
  {
    return Error{"The active document has no 3D viewport"};
  }

  auto& camera = static_cast<MapViewBase*>(mapView)->camera();
  const auto translationf = vm::vec3f{translation};
  const auto yaw = static_cast<float>(vm::to_radians(yawDegrees));
  const auto pitch = static_cast<float>(vm::to_radians(pitchDegrees));
  if (
    !std::isfinite(translationf.x()) || !std::isfinite(translationf.y())
    || !std::isfinite(translationf.z()) || !std::isfinite(yaw) || !std::isfinite(pitch))
  {
    return Error{"Camera movement values exceed the supported range"};
  }

  camera.moveBy(translationf);
  camera.rotate(yaw, pitch);
  return CommandLineCameraState{camera.position(), camera.direction()};
}

Result<CommandLineCameraState> CommandLineControlService::frameGeometry()
{
  auto* frame = m_app.m_frameManager->topFrame();
  if (!frame || !isQuakeMap(frame->document().map()))
  {
    return Error{"No active Quake 1 map"};
  }

  auto* mapView = frame->findChild<MapView3D*>();
  if (!mapView)
  {
    return Error{"The active document has no 3D viewport"};
  }
  if (!mapView->frameMapGeometry())
  {
    return Error{"The active map has no geometry to frame"};
  }

  const auto& camera = static_cast<MapViewBase*>(mapView)->camera();
  return CommandLineCameraState{camera.position(), camera.direction()};
}

Result<CommandLineViewportCapture> CommandLineControlService::captureViewport(
  const std::filesystem::path& path, const bool overwrite)
{
  if (path.empty())
  {
    return Error{"A screenshot destination path is required"};
  }

  auto screenshotPath = path;
  if (screenshotPath.extension().empty())
  {
    screenshotPath += ".png";
  }
  else if (
    QString::fromStdString(screenshotPath.extension().string())
      .compare(".png", Qt::CaseInsensitive)
    != 0)
  {
    return Error{"Viewport screenshots must use the .png extension"};
  }
  if (!screenshotPath.is_absolute())
  {
    auto error = std::error_code{};
    screenshotPath = std::filesystem::absolute(screenshotPath, error).lexically_normal();
    if (error)
    {
      return Error{"Could not resolve the screenshot path: " + error.message()};
    }
  }
  if (io::Disk::pathInfo(screenshotPath) == io::PathInfo::File && !overwrite)
  {
    return Error{"The screenshot file already exists; set overwrite to replace it"};
  }

  auto* frame = m_app.m_frameManager->topFrame();
  if (!frame || !isQuakeMap(frame->document().map()))
  {
    return Error{"No active Quake 1 map"};
  }

  auto* mapView = frame->findChild<MapView3D*>();
  if (!mapView || !mapView->isVisible() || mapView->width() <= 0 || mapView->height() <= 0)
  {
    return Error{"The 3D viewport is not visible"};
  }

  const auto image = mapView->grabFramebuffer();
  if (image.isNull())
  {
    return Error{"Could not capture the 3D viewport framebuffer"};
  }
  if (!image.save(io::pathAsQString(screenshotPath), "PNG"))
  {
    return Error{"Could not save viewport screenshot to " + screenshotPath.string()};
  }

  return CommandLineViewportCapture{screenshotPath, image.width(), image.height()};
}

} // namespace tb::ui
