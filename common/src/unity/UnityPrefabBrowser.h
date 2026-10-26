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

#include <QListWidget>

#include "ui/TabBook.h"
#include "unity/UnityCatalog.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QMimeData;
class QStackedWidget;

namespace tb::ui
{
class MapDocument;
}

namespace tb::unity
{

/**
 * Drags a prefab the same way the Entity browser drags an entity definition, so the map
 * view places it with its definition's model, bounds and prefab GUID.
 */
class UnityPrefabListWidget : public QListWidget
{
public:
  explicit UnityPrefabListWidget(QWidget* parent = nullptr);

protected:
  QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
};

/**
 * The Unity inspector tab: the prefabs in the Unity catalog, with thumbnails, a category
 * filter and search.
 */
class UnityPrefabBrowser : public ui::TabBookPage
{
  Q_OBJECT
private:
  ui::MapDocument& m_document;
  UnityCatalog m_catalog;

  QLineEdit* m_searchBox = nullptr;
  QComboBox* m_categoryCombo = nullptr;
  UnityPrefabListWidget* m_list = nullptr;
  QLabel* m_messageLabel = nullptr;
  QStackedWidget* m_stack = nullptr;

public:
  explicit UnityPrefabBrowser(ui::MapDocument& document, QWidget* parent = nullptr);

private:
  void createGui();
  void reloadCatalog();
  void rebuildCategories();
  void rebuildList();
  void showMessage(const QString& message);
};

} // namespace tb::unity
