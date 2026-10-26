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

#include "mdl/MapFormat.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace tb
{
class Logger;
}
namespace tb::mdl
{
class Game;
}
namespace tb::ui
{
class MapFrame;
}

namespace tb::unity
{
using GameAndFormat = std::tuple<std::string, mdl::MapFormat>;
GameAndFormat defaultGameAndFormat();
GameAndFormat completeMapHeader(
  std::optional<std::string> gameName, mdl::MapFormat format);
std::unique_ptr<mdl::Game> createEmptyGame(const std::string& gameName, Logger& logger);
void markStartupMap(ui::MapFrame& frame, bool startup);
ui::MapFrame* untouchedStartupFrame(const std::vector<ui::MapFrame*>& frames);
} // namespace tb::unity