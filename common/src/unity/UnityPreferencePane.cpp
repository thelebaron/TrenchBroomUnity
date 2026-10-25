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

#include "UnityPreferencePane.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "io/PathQt.h"
#include "ui/FormWithSectionsLayout.h"
#include "ui/MapDocument.h"
#include "ui/QtUtils.h"
#include "ui/ViewConstants.h"
#include "unity/UnityIntegration.h"

#include "kdl/set_temp.h"

namespace tb::unity
{

UnityPreferencePane::UnityPreferencePane(ui::MapDocument* document, QWidget* parent)
  : ui::PreferencePane{parent}
  , m_document{document}
{
  createGui();
}

void UnityPreferencePane::createGui()
{
  auto* widget = createUnityPreferences();

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(QMargins{});
  layout->setSpacing(0);
  layout->addSpacing(ui::LayoutConstants::NarrowVMargin);
  layout->addWidget(widget, 1);
  layout->addSpacing(ui::LayoutConstants::MediumVMargin);
  setLayout(layout);
}

QWidget* UnityPreferencePane::createUnityPreferences()
{
  auto* description = new QLabel{tr(
    "Assign a stable, non-empty identifier to every entity. Helpful when exporting maps "
    "to downstream tools that rely on consistent IDs (for example Unity).")};
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
    &UnityPreferencePane::updateClassIndicesClicked);

  m_unityReimportOnSaveCheckBox = new QCheckBox{};
  connect(
    m_unityReimportOnSaveCheckBox,
    &QCheckBox::checkStateChanged,
    this,
    [&](const auto state) {
      if (m_disableNotifiers)
      {
        return;
      }

      const auto value = state == Qt::Checked;
      auto& prefs = PreferenceManager::instance();
      prefs.set(Preferences::UnityReimportOnSave, value);
      m_unityRebakeSubsceneOnSaveCheckBox->setEnabled(value);
    });

  m_unityRebakeSubsceneOnSaveCheckBox = new QCheckBox{};
  connect(
    m_unityRebakeSubsceneOnSaveCheckBox,
    &QCheckBox::checkStateChanged,
    this,
    [&](const auto state) {
      if (m_disableNotifiers)
      {
        return;
      }

      const auto value = state == Qt::Checked;
      auto& prefs = PreferenceManager::instance();
      prefs.set(Preferences::UnityRebakeSubsceneOnSave, value);
    });

  m_unityProjectPathEdit = new QLineEdit{};
  connect(m_unityProjectPathEdit, &QLineEdit::editingFinished, this, [&]() {
    if (m_disableNotifiers)
    {
      return;
    }

    const auto pathStr = m_unityProjectPathEdit->text().trimmed();
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::UnityProjectPath, io::pathFromQString(pathStr));
  });

  m_unityProjectBrowseButton = new QPushButton{tr("...")};
  connect(
    m_unityProjectBrowseButton,
    &QPushButton::clicked,
    this,
    &UnityPreferencePane::chooseUnityProjectPathClicked);

  m_unityTestConnectionButton = new QPushButton{tr("Test Connection")};
  connect(
    m_unityTestConnectionButton,
    &QPushButton::clicked,
    this,
    &UnityPreferencePane::testUnityConnectionClicked);

  m_unityStatusLabel = new QLabel{};
  m_unityStatusLabel->setWordWrap(true);

  auto* unityPathLayout = new QHBoxLayout{};
  unityPathLayout->setContentsMargins(QMargins{});
  unityPathLayout->setSpacing(ui::LayoutConstants::MediumHMargin);
  unityPathLayout->addWidget(m_unityProjectPathEdit, 1);
  unityPathLayout->addWidget(m_unityProjectBrowseButton);

  auto* unityTestLayout = new QHBoxLayout{};
  unityTestLayout->setContentsMargins(QMargins{});
  unityTestLayout->setSpacing(ui::LayoutConstants::MediumHMargin);
  unityTestLayout->addWidget(m_unityTestConnectionButton);
  unityTestLayout->addWidget(m_unityStatusLabel, 1);

  auto* layout = new ui::FormWithSectionsLayout{};
  layout->setContentsMargins(
    ui::LayoutConstants::DialogOuterMargin,
    ui::LayoutConstants::DialogOuterMargin,
    ui::LayoutConstants::DialogOuterMargin,
    ui::LayoutConstants::DialogOuterMargin);
  layout->setVerticalSpacing(ui::LayoutConstants::WideVMargin);

  layout->addSection("Entities");
  layout->addRow(description);
  layout->addRow("Generate unique entity identifiers", m_generateEntityIdsCheckBox);
  layout->addRow("Generate unique entity class indices", m_generateClassIndicesCheckBox);
  layout->addRow("", m_updateClassIndicesButton);

  layout->addSection(tr("Unity Integration"));
  auto* unityDescription = new QLabel{tr(
    "Automatically trigger reimport in the Unity Editor when saving a map located inside "
    "the project.")};
  unityDescription->setWordWrap(true);
  layout->addRow(unityDescription);
  layout->addRow(tr("Trigger Unity reimport on save"), m_unityReimportOnSaveCheckBox);
  layout->addRow(
    tr("Trigger SubScene rebake on save"), m_unityRebakeSubsceneOnSaveCheckBox);
  layout->addRow(tr("Unity project directory"), unityPathLayout);
  layout->addRow("", unityTestLayout);

  auto* widget = new QWidget{};
  widget->setLayout(layout);
  return widget;
}

bool UnityPreferencePane::canResetToDefaults()
{
  return true;
}

void UnityPreferencePane::doResetToDefaults()
{
  auto& prefs = PreferenceManager::instance();
  prefs.resetToDefault(Preferences::GenerateEntityIds);
  prefs.resetToDefault(Preferences::GenerateClassIndices);
  prefs.resetToDefault(Preferences::UnityReimportOnSave);
  prefs.resetToDefault(Preferences::UnityRebakeSubsceneOnSave);
  prefs.resetToDefault(Preferences::UnityProjectPath);
}

void UnityPreferencePane::updateControls()
{
  const auto disableNotifiers = kdl::set_temp{m_disableNotifiers, true};
  m_generateEntityIdsCheckBox->setChecked(pref(Preferences::GenerateEntityIds));
  m_generateClassIndicesCheckBox->setChecked(pref(Preferences::GenerateClassIndices));
  m_unityReimportOnSaveCheckBox->setChecked(pref(Preferences::UnityReimportOnSave));
  m_unityRebakeSubsceneOnSaveCheckBox->setChecked(
    pref(Preferences::UnityRebakeSubsceneOnSave));
  m_unityRebakeSubsceneOnSaveCheckBox->setEnabled(
    m_unityReimportOnSaveCheckBox->isChecked());
  m_unityProjectPathEdit->setText(io::pathAsQString(pref(Preferences::UnityProjectPath)));
  m_unityStatusLabel->clear();
}

bool UnityPreferencePane::validate()
{
  return true;
}

void UnityPreferencePane::updateClassIndicesClicked()
{
  if (!m_document)
  {
    return;
  }

  m_document->rebuildClassIndexRegistry();
}

void UnityPreferencePane::chooseUnityProjectPathClicked()
{
  const auto currentPath = io::pathAsQString(pref(Preferences::UnityProjectPath));
  const auto startDir =
    !currentPath.isEmpty()
      ? currentPath
      : ui::fileDialogDefaultDirectory(ui::FileDialogDir::UnityProjectPath);
  const auto pathStr =
    QFileDialog::getExistingDirectory(this, tr("Unity Project Directory"), startDir);
  if (!pathStr.isEmpty())
  {
    ui::updateFileDialogDefaultDirectoryWithDirectory(
      ui::FileDialogDir::UnityProjectPath, pathStr);
    m_unityProjectPathEdit->setText(pathStr);
    auto& prefs = PreferenceManager::instance();
    prefs.set(Preferences::UnityProjectPath, io::pathFromQString(pathStr));
  }
}

void UnityPreferencePane::testUnityConnectionClicked()
{
  const auto pathStr = m_unityProjectPathEdit->text().trimmed();
  const auto projectPath = io::pathFromQString(pathStr);
  if (projectPath.empty())
  {
    m_unityStatusLabel->setText(tr("Please select a Unity project directory."));
    return;
  }

  m_unityTestConnectionButton->setEnabled(false);
  m_unityStatusLabel->setText(tr("Testing connection..."));

  QPointer<UnityPreferencePane> weakThis{this};
  unity::testUnityConnection(
    projectPath, [weakThis](bool /*success*/, const QString& message) {
      if (!weakThis)
      {
        return;
      }
      weakThis->m_unityTestConnectionButton->setEnabled(true);
      weakThis->m_unityStatusLabel->setText(message);
    });
}

} // namespace tb::unity
