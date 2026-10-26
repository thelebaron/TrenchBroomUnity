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

#include "UnityWorkflow.h"

#include <QVariant>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "mdl/GameConfig.h"
#include "mdl/GameFactory.h"
#include "mdl/GameImpl.h"
#include "mdl/Map.h"
#include "ui/MapDocument.h"
#include "ui/MapFrame.h"

#include "kdl/vector_utils.h"

namespace tb::unity
{
GameAndFormat defaultGameAndFormat()
{
  const auto& factory = mdl::GameFactory::instance();
  auto gameName = pref(Preferences::UnityDefaultGame).toStdString();
  if (!kdl::vec_contains(factory.gameList(), gameName))
  {
    gameName = kdl::vec_contains(factory.gameList(), std::string{"Quake"}) ? "Quake"
               : factory.gameList().empty() ? "Quake"
                                            : factory.gameList().front();
  }
  auto format =
    mdl::formatFromName(pref(Preferences::UnityDefaultMapFormat).toStdString());
  const auto formats = factory.fileFormats(gameName);
  if (gameName == "Quake" || formats.empty())
  {
    format = mdl::MapFormat::Standard;
  }
  else if (!kdl::vec_contains(formats, mdl::formatName(format)))
  {
    format = kdl::vec_contains(formats, std::string{"Standard"})
               ? mdl::MapFormat::Standard
               : mdl::formatFromName(formats.front());
  }
  return {gameName, format};
}

GameAndFormat completeMapHeader(
  std::optional<std::string> gameName, mdl::MapFormat format)
{
  const auto [defaultGame, defaultFormat] = defaultGameAndFormat();
  const auto& factory = mdl::GameFactory::instance();
  if (!gameName || !kdl::vec_contains(factory.gameList(), *gameName))
  {
    gameName = defaultGame;
  }
  if (format == mdl::MapFormat::Unknown)
  {
    const auto formats = factory.fileFormats(*gameName);
    format = kdl::vec_contains(formats, mdl::formatName(defaultFormat)) ? defaultFormat
             : formats.empty() ? mdl::MapFormat::Standard
                               : mdl::formatFromName(formats.front());
  }
  return {*gameName, format};
}

std::unique_ptr<mdl::Game> createEmptyGame(const std::string& gameName, Logger& logger)
{
  const auto& factory = mdl::GameFactory::instance();
  auto config = factory.gameConfig(gameName);
  config.forceEmptyNewMap = true;
  return std::make_unique<mdl::GameImpl>(
    std::move(config), factory.gamePath(gameName), logger);
}

void markStartupMap(ui::MapFrame& frame, const bool startup)
{
  frame.setProperty("unityStartupMap", startup);
}

ui::MapFrame* untouchedStartupFrame(const std::vector<ui::MapFrame*>& frames)
{
  for (auto* frame : frames)
  {
    const auto& map = frame->document().map();
    if (
      frame->property("unityStartupMap").toBool() && map.world() && !map.persistent()
      && !map.modified())
    {
      return frame;
    }
  }
  return nullptr;
}
} // namespace tb::unity
