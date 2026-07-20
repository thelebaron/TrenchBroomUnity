/*
 Copyright (C) 2026 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#include "RoomCreatorDialog.h"

#include "Preferences.h"
#include "PreferenceManager.h"
#include "Logger.h"
#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Grid.h"
#include "mdl/Map.h"
#include "mdl/Map_Selection.h"
#include "mdl/Material.h"
#include "mdl/MaterialManager.h"
#include "mdl/Node.h"
#include "ui/MapFrame.h"
#include "ui/MapDocument.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>

namespace tb::ui
{
namespace
{
constexpr int DefaultWallThickness = 16;
constexpr int DefaultFloorHeight = 128;
constexpr int PlayerWidth = 32;

QComboBox* createMaterialChoice(QWidget* parent)
{
  auto* choice = new QComboBox{parent};
  choice->setEditable(true);
  choice->setInsertPolicy(QComboBox::NoInsert);
  return choice;
}
} // namespace

RoomCreatorDialog::RoomCreatorDialog(MapFrame& frame, QWidget* parent)
  : QDialog{parent}
  , m_frame{frame}
{
  setWindowTitle(tr("Room Creator"));
  setWindowFlag(Qt::Tool);
  setModal(false);
  createGui();
  populateMaterials();
  loadSettings();
  updateGridStep();
  updateModeUi();
  m_notifierConnection += m_frame.document().map().nodesWillBeRemovedNotifier.connect(
    this, &RoomCreatorDialog::nodesWillBeRemoved);
}

void RoomCreatorDialog::gridDidChange()
{
  updateGridStep();
}

void RoomCreatorDialog::mapDidChange()
{
  m_lastSources.clear();
  m_generatedNodes.clear();
  m_notifierConnection.disconnect();
  m_notifierConnection += m_frame.document().map().nodesWillBeRemovedNotifier.connect(
    this, &RoomCreatorDialog::nodesWillBeRemoved);
}

void RoomCreatorDialog::nodesWillBeRemoved(const std::vector<mdl::Node*>& nodes)
{
  for (auto* node : nodes)
  {
    if (auto* brush = dynamic_cast<mdl::BrushNode*>(node); brush != nullptr)
    {
      m_generatedNodes.erase(
        std::remove(m_generatedNodes.begin(), m_generatedNodes.end(), brush),
        m_generatedNodes.end());
      m_lastSources.erase(
        std::remove_if(
          m_lastSources.begin(),
          m_lastSources.end(),
          [brush](const auto& source) { return source.floorNode == brush; }),
        m_lastSources.end());
    }
  }
}

void RoomCreatorDialog::createGui()
{
  auto* layout = new QVBoxLayout{this};
  auto* form = new QFormLayout{};

  m_modeChoice = new QComboBox{this};
  m_modeChoice->addItem(tr("Box"), static_cast<int>(mdl::RoomGenerationMode::Box));
  m_modeChoice->addItem(tr("Floor"), static_cast<int>(mdl::RoomGenerationMode::Floor));
  form->addRow(tr("Mode"), m_modeChoice);

  m_wallThickness = new QSpinBox{this};
  m_wallThickness->setRange(1, 8192);
  form->addRow(tr("Wall thickness"), m_wallThickness);

  m_floorHeight = new QSpinBox{this};
  m_floorHeight->setRange(1, 8192);
  form->addRow(tr("Floor height"), m_floorHeight);

  m_generateCeiling = new QCheckBox{tr("Generate ceiling"), this};
  m_generateFloor = new QCheckBox{tr("Generate floor (Box mode)"), this};
  form->addRow(m_generateCeiling);
  form->addRow(m_generateFloor);
  layout->addLayout(form);

  m_materials = new QGroupBox{tr("Materials"), this};
  m_materials->setCheckable(true);
  m_materials->setChecked(true);
  auto* materialForm = new QFormLayout{m_materials};
  m_wallMaterial = createMaterialChoice(m_materials);
  m_ceilingMaterial = createMaterialChoice(m_materials);
  m_floorMaterial = createMaterialChoice(m_materials);
  materialForm->addRow(tr("Walls"), m_wallMaterial);
  materialForm->addRow(tr("Ceiling"), m_ceilingMaterial);
  materialForm->addRow(tr("Floor"), m_floorMaterial);
  layout->addWidget(m_materials);

  auto* buttons = new QDialogButtonBox{this};
  auto* resetButton = buttons->addButton(tr("Reset"), QDialogButtonBox::ResetRole);
  auto* generateButton = buttons->addButton(tr("Generate"), QDialogButtonBox::AcceptRole);
  layout->addWidget(buttons);

  connect(generateButton, &QPushButton::clicked, this, &RoomCreatorDialog::generate);
  connect(resetButton, &QPushButton::clicked, this, &RoomCreatorDialog::reset);
  connect(
    m_modeChoice,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &RoomCreatorDialog::updateModeUi);
  connect(m_wallThickness, &QSpinBox::valueChanged, this, &RoomCreatorDialog::saveSettings);
  connect(m_floorHeight, &QSpinBox::valueChanged, this, &RoomCreatorDialog::saveSettings);
  connect(m_generateCeiling, &QCheckBox::toggled, this, &RoomCreatorDialog::saveSettings);
  connect(m_generateFloor, &QCheckBox::toggled, this, &RoomCreatorDialog::saveSettings);
  connect(m_wallMaterial, &QComboBox::currentTextChanged, this, &RoomCreatorDialog::saveSettings);
  connect(
    m_ceilingMaterial, &QComboBox::currentTextChanged, this, &RoomCreatorDialog::saveSettings);
  connect(m_floorMaterial, &QComboBox::currentTextChanged, this, &RoomCreatorDialog::saveSettings);
}

void RoomCreatorDialog::populateMaterials()
{
  const auto& map = m_frame.document().map();
  const auto& materials = map.materialManager().materials();
  for (auto* material : materials)
  {
    const auto name = QString::fromStdString(material->name());
    m_wallMaterial->addItem(name);
    m_ceilingMaterial->addItem(name);
    m_floorMaterial->addItem(name);
  }

  const auto current = QString::fromStdString(map.currentMaterialName());
  for (auto* choice : {m_wallMaterial, m_ceilingMaterial, m_floorMaterial})
  {
    if (choice->findText(current) < 0)
    {
      choice->addItem(current);
    }
  }
}

void RoomCreatorDialog::loadSettings()
{
  m_updating = true;
  m_modeChoice->setCurrentIndex(m_modeChoice->findData(pref(Preferences::RoomCreatorMode)));
  m_wallThickness->setValue(pref(Preferences::RoomCreatorWallThickness));
  m_floorHeight->setValue(pref(Preferences::RoomCreatorFloorHeight));
  m_generateCeiling->setChecked(pref(Preferences::RoomCreatorGenerateCeiling));
  m_generateFloor->setChecked(pref(Preferences::RoomCreatorGenerateFloor));
  m_wallMaterial->setCurrentText(pref(Preferences::RoomCreatorWallMaterial));
  m_ceilingMaterial->setCurrentText(pref(Preferences::RoomCreatorCeilingMaterial));
  m_floorMaterial->setCurrentText(pref(Preferences::RoomCreatorFloorMaterial));
  m_updating = false;
}

void RoomCreatorDialog::saveSettings()
{
  if (m_updating)
  {
    return;
  }

  setPref(Preferences::RoomCreatorMode, m_modeChoice->currentData().toInt());
  setPref(Preferences::RoomCreatorWallThickness, m_wallThickness->value());
  setPref(Preferences::RoomCreatorFloorHeight, m_floorHeight->value());
  setPref(Preferences::RoomCreatorGenerateCeiling, m_generateCeiling->isChecked());
  setPref(Preferences::RoomCreatorGenerateFloor, m_generateFloor->isChecked());
  setPref(Preferences::RoomCreatorWallMaterial, m_wallMaterial->currentText());
  setPref(Preferences::RoomCreatorCeilingMaterial, m_ceilingMaterial->currentText());
  setPref(Preferences::RoomCreatorFloorMaterial, m_floorMaterial->currentText());
}

void RoomCreatorDialog::setSpinBoxToGrid(QSpinBox* spinBox, const int value)
{
  const auto gridSize = static_cast<int>(m_frame.document().map().grid().actualSize());
  const auto snapped = std::max(gridSize, (value / gridSize) * gridSize);
  spinBox->setSingleStep(gridSize);
  spinBox->setValue(snapped);
}

void RoomCreatorDialog::updateGridStep()
{
  if (m_updating)
  {
    return;
  }
  m_updating = true;
  setSpinBoxToGrid(m_wallThickness, m_wallThickness->value());
  setSpinBoxToGrid(m_floorHeight, m_floorHeight->value());
  m_updating = false;
  saveSettings();
}

void RoomCreatorDialog::updateModeUi()
{
  const auto mode = static_cast<mdl::RoomGenerationMode>(m_modeChoice->currentData().toInt());
  m_floorHeight->setEnabled(mode == mdl::RoomGenerationMode::Floor);
  m_generateFloor->setEnabled(mode == mdl::RoomGenerationMode::Box);
  if (!m_updating)
  {
    saveSettings();
  }
}

bool RoomCreatorDialog::isValidBox(const mdl::BrushNode& brush) const
{
  const auto& source = brush.brush();
  if (source.faceCount() != 6u)
  {
    return false;
  }

  for (const auto& face : source.faces())
  {
    const auto normal = face.normal();
    const auto axisComponents = std::array<double, 3>{
      std::abs(normal.x()), std::abs(normal.y()), std::abs(normal.z())};
    const auto maxComponent = std::max({axisComponents[0], axisComponents[1], axisComponents[2]});
    if (maxComponent < 0.999 || std::count_if(
                                    axisComponents.begin(),
                                    axisComponents.end(),
                                    [](const auto component) { return component > 0.001; })
                                  != 1)
    {
      return false;
    }
  }
  return true;
}

std::vector<mdl::RoomGenerationSource> RoomCreatorDialog::collectSources() const
{
  const auto mode = static_cast<mdl::RoomGenerationMode>(m_modeChoice->currentData().toInt());
  const auto maxFloorThickness = 2.0 * m_frame.document().map().grid().actualSize();
  auto sources = std::vector<mdl::RoomGenerationSource>{};
  for (auto* brush : m_frame.document().map().selection().brushes)
  {
    if (std::find(m_generatedNodes.begin(), m_generatedNodes.end(), brush) != m_generatedNodes.end()
        || !isValidBox(*brush))
    {
      continue;
    }

    const auto bounds = brush->brush().bounds();
    if (mode == mdl::RoomGenerationMode::Floor && bounds.size().z() > maxFloorThickness)
    {
      continue;
    }
    sources.push_back({bounds, mode == mdl::RoomGenerationMode::Floor ? brush : nullptr});
  }
  return sources;
}

mdl::RoomGenerationSettings RoomCreatorDialog::settings() const
{
  const auto materialName = [this](const QComboBox* choice) {
    const auto material = choice->currentText().trimmed();
    const auto selected = material.isEmpty()
                            ? QString::fromStdString(
                                m_frame.document().map().currentMaterialName())
                            : material;
    return selected.toStdString();
  };
  return {
    static_cast<mdl::RoomGenerationMode>(m_modeChoice->currentData().toInt()),
    m_wallThickness->value(),
    m_floorHeight->value(),
    m_generateCeiling->isChecked(),
    m_generateFloor->isChecked(),
    materialName(m_wallMaterial),
    materialName(m_ceilingMaterial),
    materialName(m_floorMaterial)};
}

void RoomCreatorDialog::generate()
{
  const auto currentSources = collectSources();
  const auto sources = currentSources.empty() ? m_lastSources : currentSources;
  if (sources.empty())
  {
    m_frame.logger().warn() << "Room creator: select one or more axis-aligned box brushes";
    return;
  }

  const auto currentSettings = settings();
  if (currentSettings.wallThickness > PlayerWidth)
  {
    m_frame.logger().warn() << "Room creator: wall thickness exceeds player width ("
                            << PlayerWidth << ")";
  }

  auto result = mdl::generateRooms(
    m_frame.document().map(), currentSettings, sources, m_generatedNodes);
  if (result.is_error())
  {
    m_frame.logger().error() << "Room creator: " << std::get<Error>(result.error()).msg;
    return;
  }

  m_generatedNodes = std::move(result.value().generatedNodes);
  m_lastSources = sources;
  if (currentSettings.mode == mdl::RoomGenerationMode::Box)
  {
    for (auto& source : m_lastSources)
    {
      source.floorNode = nullptr;
    }
  }
  m_frame.logger().info() << "Room creator: generated " << result.value().generatedRooms
                          << " room(s)";
}

void RoomCreatorDialog::reset()
{
  setPref(Preferences::RoomCreatorMode, 0);
  setPref(Preferences::RoomCreatorWallThickness, DefaultWallThickness);
  setPref(Preferences::RoomCreatorFloorHeight, DefaultFloorHeight);
  setPref(Preferences::RoomCreatorGenerateCeiling, true);
  setPref(Preferences::RoomCreatorGenerateFloor, true);
  setPref(Preferences::RoomCreatorWallMaterial, QString{});
  setPref(Preferences::RoomCreatorCeilingMaterial, QString{});
  setPref(Preferences::RoomCreatorFloorMaterial, QString{});
  loadSettings();
  updateGridStep();
  updateModeUi();
}

} // namespace tb::ui
