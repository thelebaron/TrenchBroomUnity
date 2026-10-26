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
#include <QComboBox>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "io/PathQt.h"
#include "mdl/GameFactory.h"
#include "mdl/Map.h"
#include "mdl/Map_Assets.h"
#include "ui/FormWithSectionsLayout.h"
#include "ui/MapDocument.h"
#include "ui/QtUtils.h"
#include "ui/ViewConstants.h"
#include "unity/UnityIntegration.h"
#include "unity/UnityTextureFolders.h"
#include "unity/UnityWorkflow.h"

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
  auto* scroll = new QScrollArea{};
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(widget);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(QMargins{});
  layout->setSpacing(0);
  layout->addSpacing(ui::LayoutConstants::NarrowVMargin);
  layout->addWidget(scroll, 1);
  layout->addSpacing(ui::LayoutConstants::MediumVMargin);
  setLayout(layout);
}

QWidget* UnityPreferencePane::createUnityPreferences()
{
  m_defaultGameCombo = new QComboBox{};
  for (const auto& name : mdl::GameFactory::instance().gameList())
  {
    m_defaultGameCombo->addItem(QString::fromStdString(name));
  }
  m_defaultMapFormatCombo = new QComboBox{};
  connect(
    m_defaultGameCombo, &QComboBox::currentTextChanged, this, [this](const auto& name) {
      if (m_disableNotifiers)
      {
        return;
      }
      PreferenceManager::instance().set(Preferences::UnityDefaultGame, name);
      updateMapFormats();
      PreferenceManager::instance().set(
        Preferences::UnityDefaultMapFormat, m_defaultMapFormatCombo->currentText());
    });
  connect(
    m_defaultMapFormatCombo,
    &QComboBox::currentTextChanged,
    this,
    [this](const auto& format) {
      if (!m_disableNotifiers && !format.isEmpty())
      {
        PreferenceManager::instance().set(Preferences::UnityDefaultMapFormat, format);
      }
    });
  m_textureSourceCombo = new QComboBox{};
  m_textureSourceCombo->addItems({tr("Unity folders"), tr("WAD")});
  connect(
    m_textureSourceCombo, &QComboBox::currentIndexChanged, this, [this](const int index) {
      if (m_disableNotifiers)
      {
        return;
      }
      PreferenceManager::instance().set(
        Preferences::UnityTextureFoldersEnabled, index == 0);
      m_textureFoldersEdit->setEnabled(index == 0);
      m_textureFolderBrowseButton->setEnabled(index == 0);
    });
  m_defaultTextureFolderLabel = new QLabel{};
  m_defaultTextureFolderLabel->setWordWrap(true);
  m_defaultTextureFolderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_textureFoldersEdit = new QPlainTextEdit{};
  m_textureFoldersEdit->setMaximumHeight(85);
  connect(m_textureFoldersEdit, &QPlainTextEdit::textChanged, this, [this]() {
    if (m_disableNotifiers)
    {
      return;
    }
    auto folders = std::vector<std::filesystem::path>{};
    for (const auto& line : m_textureFoldersEdit->toPlainText().split('\n'))
    {
      if (!line.trimmed().isEmpty())
      {
        folders.push_back(io::pathFromQString(line.trimmed()));
      }
    }
    PreferenceManager::instance().set(
      Preferences::UnityAdditionalTextureFolders, std::move(folders));
  });
  m_textureFolderBrowseButton = new QPushButton{tr("Add Folder...")};
  connect(m_textureFolderBrowseButton, &QPushButton::clicked, this, [this]() {
    const auto project = pref(Preferences::UnityProjectPath);
    const auto folder = io::pathFromQString(QFileDialog::getExistingDirectory(
      this,
      tr("Texture Folder"),
      io::pathAsQString(project / DefaultTextureFolder)));
    if (!folder.empty())
    {
      // Store folders inside the Unity project relative to it, like the default.
      const auto relative = folder.lexically_relative(project);
      const auto insideProject =
        !project.empty() && !relative.empty() && *relative.begin() != "..";
      m_textureFoldersEdit->appendPlainText(
        io::pathAsQString(insideProject ? relative : folder));
    }
  });
  m_refreshTexturesButton = new QPushButton{tr("Refresh Textures")};
  m_refreshTexturesButton->setEnabled(m_document != nullptr);
  connect(m_refreshTexturesButton, &QPushButton::clicked, this, [this]() {
    if (m_document && m_document->map().world())
    {
      mdl::reloadMaterialCollections(m_document->map());
    }
  });
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
    updateDefaultTextureFolderLabel();
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

  // The project directory comes first: texture folders and reimport depend on it.
  layout->addSection(tr("Unity Project"));
  layout->addRow(tr("Unity project directory"), unityPathLayout);
  layout->addRow("", unityTestLayout);

  layout->addSection(tr("Startup"));
  layout->addRow(tr("Default game"), m_defaultGameCombo);
  layout->addRow(tr("Default map format"), m_defaultMapFormatCombo);
  auto* formatDescription =
    new QLabel{tr("Quake uses Standard format to match Scopa's texture coordinates.")};
  formatDescription->setWordWrap(true);
  layout->addRow(formatDescription);

  layout->addSection(tr("Textures"));
  layout->addRow(tr("Texture source"), m_textureSourceCombo);
  layout->addRow(tr("Default folder"), m_defaultTextureFolderLabel);
  layout->addRow(tr("Additional folders"), m_textureFoldersEdit);
  auto* folderDescription = new QLabel{tr(
    "The default folder is always used. List additional folders one per line. Only "
    "images directly inside each folder are used; add subfolders on their own lines. "
    "Relative folders are inside the Unity project. Duplicate names are reported in "
    "the editor log and are not loaded.")};
  folderDescription->setWordWrap(true);
  layout->addRow(folderDescription);
  auto* textureButtons = new QHBoxLayout{};
  textureButtons->addWidget(m_textureFolderBrowseButton);
  textureButtons->addWidget(m_refreshTexturesButton);
  layout->addRow("", textureButtons);

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
  prefs.resetToDefault(Preferences::UnityDefaultGame);
  prefs.resetToDefault(Preferences::UnityDefaultMapFormat);
  prefs.resetToDefault(Preferences::UnityTextureFoldersEnabled);
  prefs.resetToDefault(Preferences::UnityAdditionalTextureFolders);
}

void UnityPreferencePane::updateControls()
{
  const auto disableNotifiers = kdl::set_temp{m_disableNotifiers, true};
  const auto [game, format] = defaultGameAndFormat();
  m_defaultGameCombo->setCurrentText(QString::fromStdString(game));
  updateMapFormats();
  m_defaultMapFormatCombo->setCurrentText(
    QString::fromStdString(mdl::formatName(format)));
  const auto foldersEnabled = pref(Preferences::UnityTextureFoldersEnabled);
  m_textureSourceCombo->setCurrentIndex(foldersEnabled ? 0 : 1);
  m_textureFoldersEdit->setEnabled(foldersEnabled);
  m_textureFolderBrowseButton->setEnabled(foldersEnabled);
  auto folders = QStringList{};
  updateDefaultTextureFolderLabel();
  for (const auto& folder : pref(Preferences::UnityAdditionalTextureFolders))
  {
    folders.push_back(io::pathAsQString(folder));
  }
  m_textureFoldersEdit->setPlainText(folders.join('\n'));
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

void UnityPreferencePane::updateMapFormats()
{
  const auto disableNotifiers = kdl::set_temp{m_disableNotifiers, true};
  const auto game = m_defaultGameCombo->currentText().toStdString();
  m_defaultMapFormatCombo->clear();
  if (game == "Quake")
  {
    m_defaultMapFormatCombo->addItem("Standard");
  }
  else
  {
    for (const auto& format : mdl::GameFactory::instance().fileFormats(game))
    {
      m_defaultMapFormatCombo->addItem(QString::fromStdString(format));
    }
  }
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
    updateDefaultTextureFolderLabel();
  }
}

void UnityPreferencePane::updateDefaultTextureFolderLabel()
{
  const auto project = pref(Preferences::UnityProjectPath);
  m_defaultTextureFolderLabel->setText(
    project.empty()
      ? tr("Set a Unity project path to use %1")
          .arg(io::pathAsQString(DefaultTextureFolder))
      : io::pathAsQString((project / DefaultTextureFolder).lexically_normal()));
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
