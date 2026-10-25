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

#include "MiscPreferencePane.h"

#include <QBoxLayout>
#include <QCheckBox>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "ui/FormWithSectionsLayout.h"
#include "ui/ViewConstants.h"

#include "kdl/set_temp.h"

namespace tb::ui
{

MiscPreferencePane::MiscPreferencePane(QWidget* parent)
  : PreferencePane{parent}
{
  createGui();
}

void MiscPreferencePane::createGui()
{
  auto* widget = createMiscPreferences();

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(QMargins{});
  layout->setSpacing(0);
  layout->addSpacing(LayoutConstants::NarrowVMargin);
  layout->addWidget(widget, 1);
  layout->addSpacing(LayoutConstants::MediumVMargin);
  setLayout(layout);
}

QWidget* MiscPreferencePane::createMiscPreferences()
{
  m_autoSetDefaultEntityPropertiesCheckBox = new QCheckBox{};
  connect(
    m_autoSetDefaultEntityPropertiesCheckBox,
    &QCheckBox::checkStateChanged,
    this,
    [&](const auto state) {
      if (m_disableNotifiers)
      {
        return;
      }

      const auto value = state == Qt::Checked;
      auto& prefs = PreferenceManager::instance();
      prefs.set(Preferences::AutoSetDefaultEntityProperties, value);
    });

  auto* layout = new FormWithSectionsLayout{};
  layout->setContentsMargins(
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin);
  layout->setVerticalSpacing(LayoutConstants::WideVMargin);

  layout->addSection("Entities");
  layout->addRow(
    "Automatically set default entity properties",
    m_autoSetDefaultEntityPropertiesCheckBox);

  auto* widget = new QWidget{};
  widget->setLayout(layout);
  return widget;
}

bool MiscPreferencePane::canResetToDefaults()
{
  return true;
}

void MiscPreferencePane::doResetToDefaults()
{
  auto& prefs = PreferenceManager::instance();
  prefs.resetToDefault(Preferences::AutoSetDefaultEntityProperties);
}

void MiscPreferencePane::updateControls()
{
  const auto disableNotifiers = kdl::set_temp{m_disableNotifiers, true};
  m_autoSetDefaultEntityPropertiesCheckBox->setChecked(
    pref(Preferences::AutoSetDefaultEntityProperties));
}

bool MiscPreferencePane::validate()
{
  return true;
}

} // namespace tb::ui
