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
#include "ui/PrefabLibrary.h"

#include <QListWidget>
#include <QWidget>

class QMimeData;

#include <filesystem>
#include <vector>

class QLineEdit;
class QLabel;
class QPushButton;
class QStackedWidget;

namespace tb::ui
{
class MapDocument;
class PrefabListWidget;

class PrefabListWidget : public QListWidget
{
public:
  explicit PrefabListWidget(QWidget* parent = nullptr);

protected:
  QMimeData* mimeData(const QList<QListWidgetItem*> items) const;
  void startDrag(Qt::DropActions supportedActions) override;
};

class PrefabBrowser : public QWidget
{
  Q_OBJECT
private:
  MapDocument& m_document;
  PrefabLibrary& m_library;

  PrefabListWidget* m_listWidget = nullptr;
  QLineEdit* m_filterBox = nullptr;
  QLabel* m_disabledLabel = nullptr;
  QLabel* m_emptyLabel = nullptr;
  QLabel* m_locationLabel = nullptr;
  QPushButton* m_refreshButton = nullptr;
  QPushButton* m_openFolderButton = nullptr;
  QStackedWidget* m_stack = nullptr;

  std::vector<PrefabEntry> m_entries;
  NotifierConnection m_notifierConnection;

public:
  PrefabBrowser(MapDocument& document, QWidget* parent = nullptr);
  ~PrefabBrowser() override;

  void refresh();

signals:
  void prefabActivated(const std::filesystem::path& path);

private:
  void createGui();
  void connectSignals();

  void updateAvailability(bool enabled);
  void rebuildList();
  void updateLocationLabel();
  void openPrefabFolder();

  void entriesChanged(const std::vector<PrefabEntry>& entries);

  QString filterText() const;
};

} // namespace tb::ui
