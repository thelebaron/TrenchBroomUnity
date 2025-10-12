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
#include <QLabel>
#include <QPushButton>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "kdl/set_temp.h"
#include "ui/FormWithSectionsLayout.h"
#include "ui/MapDocument.h"
#include "ui/ViewConstants.h"

namespace tb::ui
{

MiscPreferencePane::MiscPreferencePane(MapDocument* document, QWidget* parent)
  : PreferencePane{parent}
  , m_document{document}
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
  auto* description = new QLabel{tr(
    "Assign a stable, non-empty identifier to every entity. Helpful when exporting maps "
    "to downstream tools that rely on consistent IDs (for example Unity)." )};
  description->setWordWrap(true);

  m_generateEntityIdsCheckBox = new QCheckBox{};
  connect(
    m_generateEntityIdsCheckBox,
    &QCheckBox::checkStateChanged,
    this,
    [&](const auto state) {
      if (m_disableNotifiers)
      {
        return;
      }

      const auto value = state == Qt::Checked;
      auto& prefs = PreferenceManager::instance();
      prefs.set(Preferences::GenerateEntityIds, value);
    });

  m_generateClassIndicesCheckBox = new QCheckBox{};
  connect(
    m_generateClassIndicesCheckBox,
    &QCheckBox::checkStateChanged,
    this,
    [&](const auto state) {
      if (m_disableNotifiers)
      {
        return;
      }

      const auto value = state == Qt::Checked;
      auto& prefs = PreferenceManager::instance();
      prefs.set(Preferences::GenerateClassIndices, value);
    });

  m_updateClassIndicesButton = new QPushButton{tr("Update Current Map Entities")};
  m_updateClassIndicesButton->setEnabled(m_document != nullptr);
  connect(
    m_updateClassIndicesButton,
    &QPushButton::clicked,
    this,
    &MiscPreferencePane::updateClassIndicesClicked);

  auto* layout = new FormWithSectionsLayout{};
  layout->setContentsMargins(
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin,
    LayoutConstants::DialogOuterMargin);
  layout->setVerticalSpacing(LayoutConstants::WideVMargin);

  layout->addSection("Entities");
  layout->addRow(description);
  layout->addRow("Generate unique entity identifiers", m_generateEntityIdsCheckBox);
  layout->addRow(
    "Generate unique entity class indices", m_generateClassIndicesCheckBox);
  layout->addRow("", m_updateClassIndicesButton);

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
  prefs.resetToDefault(Preferences::GenerateEntityIds);
  prefs.resetToDefault(Preferences::GenerateClassIndices);
}

void MiscPreferencePane::updateControls()
{
  const auto disableNotifiers = kdl::set_temp{m_disableNotifiers, true};
  m_generateEntityIdsCheckBox->setChecked(pref(Preferences::GenerateEntityIds));
  m_generateClassIndicesCheckBox->setChecked(pref(Preferences::GenerateClassIndices));
}

bool MiscPreferencePane::validate()
{
  return true;
}

void MiscPreferencePane::updateClassIndicesClicked()
{
  if (!m_document)
  {
    return;
  }

  m_document->rebuildClassIndexRegistry();
}

} // namespace tb::ui
