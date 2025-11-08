/*
 Copyright (C) 2025 Kristian Duske

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

#include "ui/Tool.h"
#include "ui/ToolController.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace tb::ui
{
class MapDocument;
class MapViewBase;

class PrefabDropController : public ToolController, public Tool
{
private:
  class PrefabDropTracker;

  MapDocument& m_document;
  MapViewBase& m_mapView;

public:
  PrefabDropController(MapDocument& document, MapViewBase& mapView);

private:
  Tool& tool() override;
  const Tool& tool() const override;

  bool shouldAcceptDrop(const InputState& inputState, const std::string& payload) const override;
  std::unique_ptr<DropTracker> acceptDrop(
    const InputState& inputState, const std::string& payload) override;

  bool cancel() override;

  std::optional<std::filesystem::path> decodePayload(const std::string& payload) const;
  std::optional<std::string> loadPrefabSource(const std::filesystem::path& path) const;
  bool insertPrefab(const std::string& prefabContents, const InputState& inputState) const;
};

} // namespace tb::ui
