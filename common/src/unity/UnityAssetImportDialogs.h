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
#include <string>
#include <vector>

class QMimeData;
class QWidget;

namespace tb::unity
{

/**
 * The image files among dropped URLs.
 */
std::vector<std::filesystem::path> droppedImageFiles(const QMimeData& mimeData);

/**
 * Asks for images to import.
 */
std::vector<std::filesystem::path> chooseImageFiles(
  QWidget* parent, const QString& title);

/**
 * Asks which texture folder the images go into (or makes a new one, which is added to the
 * texture folders in the preferences), asks before replacing existing textures, and
 * copies them. The texture folder watcher then reloads the materials.
 */
void importTexturesInteractively(
  QWidget* parent, const std::vector<std::filesystem::path>& files);

/**
 * Asks which decal category the images go into (or makes a new one), asks before
 * replacing existing decals, and copies them into the decal folder. Unity builds the
 * category's atlas and exports the catalog, which reloads the decal browser.
 */
void importDecalsInteractively(
  QWidget* parent,
  const std::vector<std::filesystem::path>& files,
  const std::string& preferredCategory = {});

/**
 * Asks to confirm, then moves the asset and its .meta file to the recycle bin.
 */
bool removeAssetInteractively(
  QWidget* parent, const std::filesystem::path& file, const QString& kind);

} // namespace tb::unity
