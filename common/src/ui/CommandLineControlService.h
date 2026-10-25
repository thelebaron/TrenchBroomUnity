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

#include "Result.h"
#include "mdl/MapFormat.h"

#include "vm/bbox.h"
#include "vm/vec.h"

#include <filesystem>
#include <string>
#include <vector>

namespace tb::ui
{
class TrenchBroomApp;

struct CommandLineControlMapState
{
  bool hasMap = false;
  std::filesystem::path path;
  std::string gameName;
  mdl::MapFormat mapFormat = mdl::MapFormat::Unknown;
  bool modified = false;
};

struct CommandLineBrushGeometry
{
  /**
   * Points whose convex hull defines the brush.
   */
  std::vector<vm::vec3d> vertices;
};

struct CommandLineCreatedBrush
{
  std::string id;
  vm::bbox3d bounds;
};

struct CommandLineCameraState
{
  vm::vec3f position;
  vm::vec3f direction;
};

struct CommandLineViewportCapture
{
  std::filesystem::path path;
  int width = 0;
  int height = 0;
};

/**
 * Application-facing, transport-independent operations for live command-line control.
 *
 * These operations run against the active TrenchBroom document and use the normal map
 * model so edits appear in the UI and participate in undo/redo. Call them on the GUI
 * thread; a future command transport must dispatch requests to that thread.
 */
class CommandLineControlService
{
private:
  TrenchBroomApp& m_app;

public:
  explicit CommandLineControlService(TrenchBroomApp& app);

  CommandLineControlMapState activeMapState() const;

  Result<CommandLineControlMapState> newQuakeMap(
    mdl::MapFormat mapFormat = mdl::MapFormat::Standard, bool discardChanges = false);
  Result<CommandLineControlMapState> loadQuakeMap(
    const std::filesystem::path& path,
    mdl::MapFormat mapFormat = mdl::MapFormat::Unknown,
    bool discardChanges = false);
  Result<void> saveMap();
  Result<std::filesystem::path> saveMapAs(
    const std::filesystem::path& path, bool overwrite = false);

  Result<std::vector<CommandLineCreatedBrush>> createBrushes(
    const std::vector<CommandLineBrushGeometry>& brushes);

  Result<CommandLineCameraState> moveCamera(
    const vm::vec3d& translation,
    double yawDegrees = 0.0,
    double pitchDegrees = 0.0);
  Result<CommandLineCameraState> frameGeometry();
  Result<CommandLineViewportCapture> captureViewport(
    const std::filesystem::path& path, bool overwrite = false);
};

} // namespace tb::ui
