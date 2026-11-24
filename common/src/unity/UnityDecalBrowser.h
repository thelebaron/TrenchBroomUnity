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

#include <QImage>
#include <QString>

#include "NotifierConnection.h"
#include "ui/TabBook.h"

#include <filesystem>
#include <string>
#include <unordered_map>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;

namespace tb::ui
{
class MapDocument;
class Splitter;
} // namespace tb::ui

namespace tb::unity
{

/**
 * The Decals inspector tab: the categories of the Unity decal libraries on the left, and
 * the decals of the chosen one on the right, as tiles cut from their atlases, with a
 * search. Clicking a decal arms the stamp tool with it.
 */
class UnityDecalBrowser : public ui::TabBookPage
{
  Q_OBJECT
private:
  ui::MapDocument& m_document;
  NotifierConnection m_notifierConnection;
  // Atlas images by atlas id, loaded once per library change.
  std::unordered_map<std::string, QImage> m_atlasImages;

  QLineEdit* m_searchBox = nullptr;
  ui::Splitter* m_splitter = nullptr;
  QListWidget* m_categoryList = nullptr;
  QListWidget* m_list = nullptr;
  QLabel* m_messageLabel = nullptr;
  QStackedWidget* m_stack = nullptr;

public:
  explicit UnityDecalBrowser(ui::MapDocument& document, QWidget* parent = nullptr);
  ~UnityDecalBrowser() override;

signals:
  void decalStampRequested(const QString& decalId);

protected:
  // images dropped from the file browser are imported as decals
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  void createGui();
  void reloadLibrary();
  void rebuildCategories();
  void rebuildList();
  void showMessage(const QString& message);
  void itemClicked(QListWidgetItem* item);
  void showContextMenu(const QPoint& position);
  void showCategoryContextMenu(const QPoint& position);
  /**
   * The decal's own image if it has one, else its atlas.
   */
  std::filesystem::path imagePath(const std::string& decalId) const;
  std::string currentCategory() const;
  /**
   * The category under the drop position if the drop is on the category list, else the
   * current one.
   */
  std::string dropCategory(const QPoint& position) const;
};

} // namespace tb::unity
