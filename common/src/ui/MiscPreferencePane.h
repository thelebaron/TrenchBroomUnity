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
class QPushButton;

namespace tb::ui
{
class MapDocument;

class MiscPreferencePane : public PreferencePane
{
  Q_OBJECT

private:
  MapDocument* m_document = nullptr;
  QCheckBox* m_generateEntityIdsCheckBox = nullptr;
  QCheckBox* m_generateClassIndicesCheckBox = nullptr;
  QCheckBox* m_autoSetDefaultEntityPropertiesCheckBox = nullptr;
  QPushButton* m_updateClassIndicesButton = nullptr;
  bool m_disableNotifiers = false;

public:
  explicit MiscPreferencePane(MapDocument* document = nullptr, QWidget* parent = nullptr);

private:
  void createGui();
  QWidget* createMiscPreferences();
  void updateClassIndicesClicked();

  bool canResetToDefaults() override;
  void doResetToDefaults() override;
  void updateControls() override;
  bool validate() override;
};

} // namespace tb::ui
