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

#include <QString>

#include <filesystem>
#include <functional>

namespace tb::ui
{
class MapFrame;

namespace unity
{

/**
 * Searches for the unity CLI executable.
 * Checks PATH first (for "unity" or "unity.exe"), then standard install paths
 * such as %LOCALAPPDATA%/Unity/bin/unity.exe.
 * Returns the resolved executable name/path, or "unity" as a fallback.
 */
QString findUnityExecutable();

/**
 * Checks whether filePath is located inside unityProjectPath.
 */
bool isPathInsideProject(
  const std::filesystem::path& filePath, const std::filesystem::path& unityProjectPath);

/**
 * Computes the forward-slashed project-relative asset path, e.g. "Assets/Maps/e1m1.map".
 * Returns an empty QString if filePath is not within unityProjectPath.
 */
QString toUnityAssetPath(
  const std::filesystem::path& filePath, const std::filesystem::path& unityProjectPath);

/**
 * Asynchronously checks whether the Unity Editor is running and connected for the given
 * project path. Invokes callback on the Qt event loop with success and a human-readable
 * status message.
 */
void testUnityConnection(
  const std::filesystem::path& projectPath,
  std::function<void(bool success, const QString& message)> callback);

/**
 * Triggers reimport of the map asset in Unity if auto-reimport is enabled in Preferences,
 * the Unity project path is configured, and mapPath resides within that project.
 * Runs asynchronously via QProcess and logs to the given MapFrame's logger if still
 * valid.
 */
void triggerUnityReimport(const std::filesystem::path& mapPath, MapFrame* frame);

/**
 * Triggers rebaking of the Unity Entity SubScene corresponding to mapPath
 * (or fallback to active MainScene) via the Unity CLI 'rebake_subscene' command.
 * Runs asynchronously via QProcess and logs to the given MapFrame's logger if still
 * valid.
 */
void triggerUnitySubsceneRebake(const std::filesystem::path& mapPath, MapFrame* frame);

} // namespace unity
} // namespace tb::ui
