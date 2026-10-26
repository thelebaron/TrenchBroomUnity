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

#include "ui/PreferencePane.h"

class QCheckBox;
class QComboBox;
class QPlainTextEdit;
class QLabel;
class QLineEdit;
class QPushButton;

namespace tb::ui
{
class MapDocument;
} // namespace tb::ui

namespace tb::unity
{

class UnityPreferencePane : public ui::PreferencePane
{
  Q_OBJECT

private:
  ui::MapDocument* m_document = nullptr;
  QCheckBox* m_generateEntityIdsCheckBox = nullptr;
  QCheckBox* m_generateClassIndicesCheckBox = nullptr;
  QPushButton* m_updateClassIndicesButton = nullptr;

  QCheckBox* m_unityReimportOnSaveCheckBox = nullptr;
  QCheckBox* m_unityRebakeSubsceneOnSaveCheckBox = nullptr;
  QLineEdit* m_unityProjectPathEdit = nullptr;
  QPushButton* m_unityProjectBrowseButton = nullptr;
  QPushButton* m_unityTestConnectionButton = nullptr;
  QLabel* m_unityStatusLabel = nullptr;
  QComboBox* m_defaultGameCombo = nullptr;
  QComboBox* m_defaultMapFormatCombo = nullptr;
  QComboBox* m_textureSourceCombo = nullptr;
  QPlainTextEdit* m_textureFoldersEdit = nullptr;
  QPushButton* m_textureFolderBrowseButton = nullptr;
  QPushButton* m_refreshTexturesButton = nullptr;

  bool m_disableNotifiers = false;

public:
  explicit UnityPreferencePane(
    ui::MapDocument* document = nullptr, QWidget* parent = nullptr);

private:
  void createGui();
  QWidget* createUnityPreferences();
  void updateClassIndicesClicked();
  void chooseUnityProjectPathClicked();
  void testUnityConnectionClicked();
  void updateMapFormats();

  bool canResetToDefaults() override;
  void doResetToDefaults() override;
  void updateControls() override;
  bool validate() override;
};

} // namespace tb::unity
