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

#include "NotifierConnection.h"
#include "ui/TabBook.h"

class QLabel;
class QPushButton;

namespace tb::mdl
{
struct SelectionChange;
class Map;
} // namespace tb::mdl

namespace tb::ui
{

class MapDocument;
class PrefabBrowser;

class PrefabInspector : public TabBookPage
{
  Q_OBJECT
private:
  MapDocument& m_document;
  PrefabBrowser* m_browser = nullptr;
  QLabel* m_descriptionLabel = nullptr;
  QPushButton* m_createButton = nullptr;

  NotifierConnection m_notifierConnection;

public:
  PrefabInspector(MapDocument& document, QWidget* parent = nullptr);
  ~PrefabInspector() override;

private:
  void createGui();
  void connectObservers();
  void updateActions();

  void libraryAvailabilityChanged(bool available);
  void selectionDidChange(const mdl::SelectionChange& change);
  void mapWasCleared(mdl::Map& map);

  void createPrefabFromSelection();
};

} // namespace tb::ui
