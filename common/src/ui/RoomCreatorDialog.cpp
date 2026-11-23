/*
 Copyright (C) 2026 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#include "RoomCreatorDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include "Logger.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Grid.h"
#include "mdl/GroupNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Material.h"
#include "mdl/MaterialManager.h"
#include "mdl/Node.h"
#include "mdl/RoomLayout.h"
#include "mdl/Selection.h"
#include "mdl/Transaction.h"
#include "ui/MapDocument.h"
#include "ui/MapFrame.h"
#include "ui/MaterialPickerDialog.h"

#include "kdl/vector_utils.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace tb::ui
{
namespace
{
constexpr int DefaultWallThickness = 16;
constexpr int DefaultWallHeight = 128;
constexpr int PlayerWidth = 32;
constexpr int BoxMode = 0;
constexpr int FloorMode = 1;

bool isAxisAlignedBox(const mdl::BrushNode& brushNode)
{
  const auto& brush = brushNode.brush();
  if (brush.faceCount() != 6u)
  {
    return false;
  }

  return std::ranges::all_of(brush.faces(), [](const auto& face) {
    const auto normal = face.normal();
    const auto components = std::array<double, 3>{
      std::abs(normal.x()), std::abs(normal.y()), std::abs(normal.z())};
    return std::ranges::count_if(components, [](const auto c) { return c > 0.001; }) == 1
           && std::ranges::max(components) > 0.999;
  });
}

/** Whether the two boxes share some volume (touching faces don't count). */
bool overlaps(const vm::bbox3d& lhs, const vm::bbox3d& rhs)
{
  constexpr auto Epsilon = 0.0001;
  for (size_t i = 0; i < 3; ++i)
  {
    if (lhs.min[i] >= rhs.max[i] - Epsilon || rhs.min[i] >= lhs.max[i] - Epsilon)
    {
      return false;
    }
  }
  return true;
}

QComboBox* createValueChoice(QWidget* parent, const std::vector<int>& values)
{
  auto* choice = new QComboBox{parent};
  for (const auto value : values)
  {
    choice->addItem(QString::number(value), value);
  }
  return choice;
}

/** Selects the given value, adding it to the choices if it isn't one of them. */
void selectValue(QComboBox* choice, const int value)
{
  if (choice->findData(value) < 0)
  {
    choice->addItem(QString::number(value), value);
  }
  choice->setCurrentIndex(choice->findData(value));
}

std::vector<mdl::Node*> toNodes(const std::vector<mdl::BrushNode*>& brushes)
{
  return kdl::vec_static_cast<mdl::Node*>(brushes);
}

} // namespace

RoomCreatorDialog::RoomCreatorDialog(
  MapFrame& frame, GLContextManager& contextManager, QWidget* parent)
  : QDialog{parent}
  , m_frame{frame}
  , m_contextManager{contextManager}
{
  setWindowTitle(tr("Room Creator"));
  setWindowFlag(Qt::Tool);
  setModal(false);
  createGui();
  loadSettings();
  updateGridStep();
  updateModeUi();
  connectObservers();
  updateSelectionUi();
}

void RoomCreatorDialog::gridDidChange()
{
  updateGridStep();
}

void RoomCreatorDialog::mapDidChange()
{
  m_notifierConnection.disconnect();
  connectObservers();
  loadSettings();
  updateGridStep();
  updateSelectionUi();
}

void RoomCreatorDialog::connectObservers()
{
  auto& map = m_frame.document().map();
  m_notifierConnection +=
    map.selectionDidChangeNotifier.connect(this, &RoomCreatorDialog::selectionDidChange);

  // Swatches show a placeholder until their materials have been loaded.
  m_notifierConnection += map.materialCollectionsDidChangeNotifier.connect(
    this, &RoomCreatorDialog::refreshMaterialSwatches);
  m_notifierConnection += map.resourcesWereProcessedNotifier.connect(
    [this](const auto&) { refreshMaterialSwatches(); });
}

void RoomCreatorDialog::selectionDidChange(const mdl::SelectionChange&)
{
  updateSelectionUi();
}

void RoomCreatorDialog::createGui()
{
  auto* layout = new QVBoxLayout{this};

  auto* draw = new QGroupBox{tr("Draw Rooms"), this};
  auto* drawLayout = new QVBoxLayout{draw};
  auto* drawButtons = new QHBoxLayout{};
  m_drawRoomsButton = new QPushButton{tr("Draw Rooms"), draw};
  m_drawRoomsButton->setCheckable(true);
  m_drawRoomsButton->setToolTip(tr(
    "Drag in the 3D or top view to draw rooms on the floor level below. Rooms that "
    "touch get a wall with a doorway; overlapping rooms merge into one space. "
    "Walls, floors and ceilings go into the walls, floors and ceilings layers."));
  m_clearLayoutButton = new QPushButton{tr("Clear Layout"), draw};
  m_clearLayoutButton->setToolTip(tr("Removes all drawn rooms."));
  drawButtons->addWidget(m_drawRoomsButton, 1);
  drawButtons->addWidget(m_clearLayoutButton);
  drawLayout->addLayout(drawButtons);

  auto* drawForm = new QFormLayout{};
  auto* floorRow = new QHBoxLayout{};
  m_floorLevel = new QSpinBox{draw};
  m_floorLevel->setRange(-65536, 65536);
  m_floorLevel->setToolTip(tr("Height of the floor that drawn rooms stand on"));
  auto* floorFromSelection = new QPushButton{tr("From Selection"), draw};
  floorFromSelection->setToolTip(tr("Use the bottom of the current selection"));
  floorRow->addWidget(m_floorLevel, 1);
  floorRow->addWidget(floorFromSelection);
  drawForm->addRow(tr("Floor level"), floorRow);
  m_doorWidth = createValueChoice(draw, {48, 64, 96, 128});
  m_doorHeight = createValueChoice(draw, {96, 112, 128, 160});
  drawForm->addRow(tr("Doorway width"), m_doorWidth);
  drawForm->addRow(tr("Doorway height"), m_doorHeight);
  drawLayout->addLayout(drawForm);
  layout->addWidget(draw);

  connect(
    m_drawRoomsButton, &QPushButton::clicked, this, &RoomCreatorDialog::toggleDrawRooms);
  connect(
    m_clearLayoutButton, &QPushButton::clicked, this, &RoomCreatorDialog::clearLayout);
  connect(
    floorFromSelection,
    &QPushButton::clicked,
    this,
    &RoomCreatorDialog::floorLevelFromSelection);
  connect(
    m_floorLevel,
    QOverload<int>::of(&QSpinBox::valueChanged),
    this,
    &RoomCreatorDialog::saveSettings);
  for (auto* choice : {m_doorWidth, m_doorHeight})
  {
    connect(
      choice,
      QOverload<int>::of(&QComboBox::currentIndexChanged),
      this,
      &RoomCreatorDialog::saveSettings);
  }
  auto* form = new QFormLayout{};

  m_modeChoice = new QComboBox{this};
  m_modeChoice->addItem(tr("Box"), BoxMode);
  m_modeChoice->addItem(tr("Floor"), FloorMode);
  m_modeChoice->setToolTip(tr(
    "Box: each selected brush is a room's interior.\n"
    "Floor: each selected brush is a floor; the interior rises from its top by the "
    "wall height."));
  form->addRow(tr("Mode"), m_modeChoice);

  m_wallThickness = new QComboBox{this};
  form->addRow(tr("Wall thickness"), m_wallThickness);

  m_wallHeight = new QComboBox{this};
  m_wallHeight->setToolTip(tr("Height of drawn rooms and of rooms made in Floor mode"));
  form->addRow(tr("Room height"), m_wallHeight);

  m_generateCeiling = new QCheckBox{tr("Generate ceiling"), this};
  m_generateFloor = new QCheckBox{tr("Generate floor"), this};
  form->addRow(m_generateCeiling);
  form->addRow(m_generateFloor);
  layout->addLayout(form);

  m_materials = new QGroupBox{tr("Materials"), this};
  m_materials->setCheckable(true);
  m_materials->setChecked(true);
  auto* materialForm = new QFormLayout{m_materials};
  auto& document = m_frame.document();
  m_wallMaterial = new MaterialSwatchButton{
    document, m_contextManager, tr("Wall Material"), m_materials};
  m_ceilingMaterial = new MaterialSwatchButton{
    document, m_contextManager, tr("Ceiling Material"), m_materials};
  m_floorMaterial = new MaterialSwatchButton{
    document, m_contextManager, tr("Floor Material"), m_materials};
  materialForm->addRow(tr("Walls"), m_wallMaterial);
  materialForm->addRow(tr("Ceiling"), m_ceilingMaterial);
  materialForm->addRow(tr("Floor"), m_floorMaterial);
  layout->addWidget(m_materials);

  auto* lights = new QGroupBox{tr("Lights"), this};
  auto* lightsForm = new QFormLayout{lights};
  m_generateLights = new QCheckBox{tr("Generate lights"), lights};
  m_generateLights->setToolTip(tr(
    "Places light_unity lights below the ceiling of each space. They are replaced "
    "whenever the room is rebuilt; remove a light's _tb_room_light key to keep it."));
  m_lightSpacing = createValueChoice(lights, {128, 192, 256, 384, 512});
  m_lightSpacing->setToolTip(tr("Approximate distance between lights"));
  lightsForm->addRow(m_generateLights);
  lightsForm->addRow(tr("Spacing"), m_lightSpacing);
  layout->addWidget(lights);

  auto* corridors = new QGroupBox{tr("Corridors"), this};
  auto* corridorForm = new QFormLayout{corridors};
  m_corridorWidth = createValueChoice(corridors, {48, 64, 96, 128, 192});
  m_corridorHeight = createValueChoice(corridors, {96, 112, 128, 160, 192});
  m_connectButton = new QPushButton{tr("Connect Rooms"), corridors};
  m_connectButton->setToolTip(tr(
    "Joins the two selected rooms with a straight or L-shaped corridor, using the "
    "wall, material and light settings above. Rooms whose walls touch just get a "
    "doorway."));
  corridorForm->addRow(tr("Width"), m_corridorWidth);
  corridorForm->addRow(tr("Height"), m_corridorHeight);
  corridorForm->addRow(m_connectButton);
  layout->addWidget(corridors);

  m_status = new QLabel{this};
  m_status->setWordWrap(true);
  layout->addWidget(m_status);

  auto* doorwayButtons = new QHBoxLayout{};
  m_addDoorwayButton = new QPushButton{tr("Add Doorway"), this};
  m_addDoorwayButton->setToolTip(
    tr("Turns the selected box brushes into doorways in the room walls they cross."));
  m_clearDoorwaysButton = new QPushButton{tr("Clear Doorways"), this};
  m_clearDoorwaysButton->setToolTip(tr("Removes all doorways from the selected rooms."));
  doorwayButtons->addWidget(m_addDoorwayButton);
  doorwayButtons->addWidget(m_clearDoorwaysButton);
  layout->addLayout(doorwayButtons);

  auto* buttons = new QDialogButtonBox{this};
  auto* resetButton = buttons->addButton(tr("Reset"), QDialogButtonBox::ResetRole);
  m_generateButton = buttons->addButton(tr("Generate"), QDialogButtonBox::AcceptRole);
  m_generateButton->setToolTip(tr(
    "Creates a room from the selected box brushes, or rebuilds the selected rooms "
    "with the current settings. Select a room together with box brushes to extend "
    "it."));
  layout->addWidget(buttons);

  connect(m_generateButton, &QPushButton::clicked, this, &RoomCreatorDialog::generate);
  connect(
    m_addDoorwayButton, &QPushButton::clicked, this, &RoomCreatorDialog::addDoorway);
  connect(
    m_clearDoorwaysButton,
    &QPushButton::clicked,
    this,
    &RoomCreatorDialog::clearDoorways);
  connect(resetButton, &QPushButton::clicked, this, &RoomCreatorDialog::reset);
  connect(m_connectButton, &QPushButton::clicked, this, &RoomCreatorDialog::connectRooms);
  connect(m_generateLights, &QCheckBox::toggled, this, &RoomCreatorDialog::saveSettings);
  for (auto* choice : {m_lightSpacing, m_corridorWidth, m_corridorHeight})
  {
    connect(
      choice,
      QOverload<int>::of(&QComboBox::currentIndexChanged),
      this,
      &RoomCreatorDialog::saveSettings);
  }
  connect(
    m_modeChoice,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &RoomCreatorDialog::updateModeUi);
  connect(
    m_wallThickness,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &RoomCreatorDialog::saveSettings);
  connect(
    m_wallHeight,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &RoomCreatorDialog::saveSettings);
  connect(m_generateCeiling, &QCheckBox::toggled, this, &RoomCreatorDialog::saveSettings);
  connect(m_generateFloor, &QCheckBox::toggled, this, &RoomCreatorDialog::saveSettings);
  for (auto* swatch : {m_wallMaterial, m_ceilingMaterial, m_floorMaterial})
  {
    connect(
      swatch,
      &MaterialSwatchButton::materialChanged,
      this,
      &RoomCreatorDialog::saveSettings);
  }
}

void RoomCreatorDialog::refreshMaterialSwatches()
{
  for (auto* swatch : {m_wallMaterial, m_ceilingMaterial, m_floorMaterial})
  {
    swatch->refresh();
  }
}

void RoomCreatorDialog::loadSettings()
{
  m_updating = true;
  m_modeChoice->setCurrentIndex(
    m_modeChoice->findData(pref(Preferences::RoomCreatorMode)));
  populateDimensionChoice(
    m_wallThickness, pref(Preferences::RoomCreatorWallThickness), true);
  populateDimensionChoice(m_wallHeight, pref(Preferences::RoomCreatorWallHeight), false);
  m_generateCeiling->setChecked(pref(Preferences::RoomCreatorGenerateCeiling));
  m_generateFloor->setChecked(pref(Preferences::RoomCreatorGenerateFloor));
  m_wallMaterial->setMaterialName(
    pref(Preferences::RoomCreatorWallMaterial).toStdString());
  m_ceilingMaterial->setMaterialName(
    pref(Preferences::RoomCreatorCeilingMaterial).toStdString());
  m_floorMaterial->setMaterialName(
    pref(Preferences::RoomCreatorFloorMaterial).toStdString());
  m_generateLights->setChecked(pref(Preferences::RoomCreatorGenerateLights));
  selectValue(m_lightSpacing, pref(Preferences::RoomCreatorLightSpacing));
  selectValue(m_corridorWidth, pref(Preferences::RoomCreatorCorridorWidth));
  selectValue(m_corridorHeight, pref(Preferences::RoomCreatorCorridorHeight));
  m_floorLevel->setValue(pref(Preferences::RoomCreatorFloorLevel));
  selectValue(m_doorWidth, pref(Preferences::RoomCreatorDoorWidth));
  selectValue(m_doorHeight, pref(Preferences::RoomCreatorDoorHeight));
  m_updating = false;
}

void RoomCreatorDialog::loadSettingsFromRoom(const mdl::RoomRecipe& recipe)
{
  // Show the room's own settings without overwriting the user's preferences.
  m_updating = true;
  populateDimensionChoice(m_wallThickness, recipe.wallThickness, true);
  m_generateCeiling->setChecked(recipe.generateCeiling);
  m_generateFloor->setChecked(recipe.generateFloor);
  m_wallMaterial->setMaterialName(recipe.wallMaterial);
  m_ceilingMaterial->setMaterialName(recipe.ceilingMaterial);
  m_floorMaterial->setMaterialName(recipe.floorMaterial);
  m_generateLights->setChecked(recipe.generateLights);
  selectValue(m_lightSpacing, recipe.lightSpacing);
  m_updating = false;
}

void RoomCreatorDialog::saveSettings()
{
  if (m_updating)
  {
    return;
  }

  setPref(Preferences::RoomCreatorMode, m_modeChoice->currentData().toInt());
  setPref(Preferences::RoomCreatorWallThickness, m_wallThickness->currentData().toInt());
  setPref(Preferences::RoomCreatorWallHeight, m_wallHeight->currentData().toInt());
  setPref(Preferences::RoomCreatorGenerateCeiling, m_generateCeiling->isChecked());
  setPref(Preferences::RoomCreatorGenerateFloor, m_generateFloor->isChecked());
  setPref(
    Preferences::RoomCreatorWallMaterial,
    QString::fromStdString(m_wallMaterial->materialName()));
  setPref(
    Preferences::RoomCreatorCeilingMaterial,
    QString::fromStdString(m_ceilingMaterial->materialName()));
  setPref(
    Preferences::RoomCreatorFloorMaterial,
    QString::fromStdString(m_floorMaterial->materialName()));
  setPref(Preferences::RoomCreatorGenerateLights, m_generateLights->isChecked());
  setPref(Preferences::RoomCreatorLightSpacing, m_lightSpacing->currentData().toInt());
  setPref(Preferences::RoomCreatorCorridorWidth, m_corridorWidth->currentData().toInt());
  setPref(Preferences::RoomCreatorFloorLevel, m_floorLevel->value());
  setPref(Preferences::RoomCreatorDoorWidth, m_doorWidth->currentData().toInt());
  setPref(Preferences::RoomCreatorDoorHeight, m_doorHeight->currentData().toInt());
  setPref(
    Preferences::RoomCreatorCorridorHeight, m_corridorHeight->currentData().toInt());
}

void RoomCreatorDialog::populateDimensionChoice(
  QComboBox* choice, const int value, const bool thickness)
{
  const auto gridSize =
    std::max(1, static_cast<int>(m_frame.document().map().grid().actualSize()));
  const auto snapped = std::max(gridSize, (value / gridSize) * gridSize);
  const auto maximum = thickness ? std::max(PlayerWidth, gridSize) : 512;
  const auto choices = thickness ? std::vector<int>{8, 16, 24, 32}
                                 : std::vector<int>{64, 96, 128, 160, 192, 256, 384, 512};

  choice->clear();
  for (const auto candidate : choices)
  {
    if (candidate >= gridSize && candidate <= maximum && candidate % gridSize == 0)
    {
      choice->addItem(QString::number(candidate), candidate);
    }
  }
  if (choice->findData(snapped) < 0)
  {
    choice->addItem(QString::number(snapped), snapped);
  }
  choice->setCurrentIndex(choice->findData(snapped));
}

void RoomCreatorDialog::updateGridStep()
{
  if (m_updating)
  {
    return;
  }
  m_updating = true;
  populateDimensionChoice(m_wallThickness, m_wallThickness->currentData().toInt(), true);
  populateDimensionChoice(m_wallHeight, m_wallHeight->currentData().toInt(), false);
  m_floorLevel->setSingleStep(
    std::max(1, static_cast<int>(m_frame.document().map().grid().actualSize())));
  m_updating = false;
  saveSettings();
}

void RoomCreatorDialog::updateModeUi()
{
  if (!m_updating)
  {
    saveSettings();
  }
}

void RoomCreatorDialog::updateSelectionUi()
{
  const auto selection = resolveSelection();
  const auto& rooms = selection.rooms;
  const auto& boxes = selection.boxes;

  auto doorCount = 0;
  auto spaceCount = 0;
  for (const auto* room : rooms)
  {
    if (const auto recipe = mdl::readRoomRecipe(room->group()); recipe.is_success())
    {
      doorCount += static_cast<int>(recipe.value().doors.size());
      spaceCount += static_cast<int>(recipe.value().spaces.size());
    }
  }

  if (rooms.size() == 1u && boxes.empty())
  {
    if (
      const auto recipe = mdl::readRoomRecipe(rooms.front()->group());
      recipe.is_success())
    {
      loadSettingsFromRoom(recipe.value());
    }
  }

  const auto roomCount = static_cast<int>(rooms.size());
  const auto boxCount = static_cast<int>(boxes.size());

  auto status = QString{};
  if (rooms.empty() && boxes.empty())
  {
    status = tr("Select box brushes to create a room, or select a room to rebuild it.");
  }
  else if (rooms.empty())
  {
    status = tr("Generate creates a room from %n box(es).", "", boxCount);
  }
  else if (boxes.empty())
  {
    status =
      tr("%n room(s) selected", "", roomCount) + QStringLiteral(": ")
      + tr("%n space(s), ", "", spaceCount) + tr("%n doorway(s).", "", doorCount)
      + QStringLiteral(" ") + tr("Generate rebuilds with the current settings.")
      + (rooms.size() == 2u ? QStringLiteral(" ") + tr("Connect Rooms joins them with a corridor.") : QString{});
  }
  else if (rooms.size() == 1u)
  {
    status = tr(
      "Generate adds %n box(es) to the selected room; Add Doorway cuts them "
      "into its walls instead.",
      "",
      boxCount);
  }
  else
  {
    status = tr("Select a single room to extend it with box brushes.");
  }
  m_status->setText(status);

  m_generateButton->setEnabled(
    !(rooms.empty() && boxes.empty()) && !(rooms.size() > 1u && !boxes.empty()));
  m_addDoorwayButton->setEnabled(!boxes.empty());
  m_clearDoorwaysButton->setEnabled(doorCount > 0);
  m_connectButton->setEnabled(rooms.size() == 2u && boxes.empty());
}

RoomCreatorDialog::RoomSelection RoomCreatorDialog::resolveSelection() const
{
  auto result = RoomSelection{};
  for (auto* node : m_frame.document().map().selection().nodes)
  {
    if (auto* room = mdl::findRoom(node))
    {
      if (std::ranges::find(result.rooms, room) == result.rooms.end())
      {
        result.rooms.push_back(room);
      }
    }
    else if (
      auto* brush = dynamic_cast<mdl::BrushNode*>(node);
      brush != nullptr && isAxisAlignedBox(*brush))
    {
      result.boxes.push_back(brush);
    }
  }
  return result;
}

std::optional<vm::bbox3d> RoomCreatorDialog::spaceForBox(
  const mdl::BrushNode& brush) const
{
  const auto bounds = brush.brush().bounds();
  if (m_modeChoice->currentData().toInt() == BoxMode)
  {
    return bounds;
  }

  const auto maxFloorThickness = 2.0 * m_frame.document().map().grid().actualSize();
  if (bounds.size().z() > maxFloorThickness)
  {
    return std::nullopt;
  }

  const auto height = static_cast<double>(m_wallHeight->currentData().toInt());
  return vm::bbox3d{
    {bounds.min.x(), bounds.min.y(), bounds.max.z()},
    {bounds.max.x(), bounds.max.y(), bounds.max.z() + height}};
}

mdl::RoomRecipe RoomCreatorDialog::recipeFromUi() const
{
  auto recipe = mdl::RoomRecipe{};
  applyUiSettings(recipe);
  return recipe;
}

void RoomCreatorDialog::applyUiSettings(mdl::RoomRecipe& recipe) const
{
  const auto materialName = [this](const MaterialSwatchButton* swatch) {
    const auto& material = swatch->materialName();
    return material.empty() ? m_frame.document().map().currentMaterialName() : material;
  };

  recipe.wallThickness = m_wallThickness->currentData().toInt();
  recipe.generateFloor = m_generateFloor->isChecked();
  recipe.generateCeiling = m_generateCeiling->isChecked();
  recipe.wallMaterial = materialName(m_wallMaterial);
  recipe.floorMaterial = materialName(m_floorMaterial);
  recipe.ceilingMaterial = materialName(m_ceilingMaterial);
  recipe.generateLights = m_generateLights->isChecked();
  recipe.lightSpacing = m_lightSpacing->currentData().toInt();
}

std::optional<mdl::ModifiedRoomBrushes> RoomCreatorDialog::askAboutModifiedBrushes(
  const std::vector<mdl::GroupNode*>& rooms)
{
  auto modified = size_t{0};
  for (const auto* room : rooms)
  {
    modified += mdl::countModifiedRoomBrushes(m_frame.document().map(), *room);
  }
  if (modified == 0u)
  {
    return mdl::ModifiedRoomBrushes::Replace;
  }

  auto box = QMessageBox{this};
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(tr("Room Creator"));
  box.setText(
    tr("%n brush(es) in the room were edited or added by hand.", "", int(modified)));
  box.setInformativeText(
    tr("Replace them with the rebuilt shell, or keep them alongside it?"));
  auto* replace = box.addButton(tr("Replace"), QMessageBox::DestructiveRole);
  auto* keep = box.addButton(tr("Keep"), QMessageBox::AcceptRole);
  box.addButton(QMessageBox::Cancel);
  box.setDefaultButton(keep);
  box.exec();

  if (box.clickedButton() == replace)
  {
    return mdl::ModifiedRoomBrushes::Replace;
  }
  if (box.clickedButton() == keep)
  {
    return mdl::ModifiedRoomBrushes::Keep;
  }
  return std::nullopt;
}

void RoomCreatorDialog::selectRooms(const std::vector<mdl::GroupNode*>& rooms)
{
  auto& map = m_frame.document().map();
  const auto* currentGroup = map.editorContext().currentGroup();
  auto nodes = std::vector<mdl::Node*>{};
  for (auto* room : rooms)
  {
    // Only nodes in the currently open group (or the top level) can be selected.
    if (room->containingGroup() == currentGroup)
    {
      nodes.push_back(room);
    }
  }
  if (!nodes.empty())
  {
    mdl::selectNodes(map, nodes);
  }
}

void RoomCreatorDialog::generate()
{
  auto& map = m_frame.document().map();
  const auto selection = resolveSelection();

  auto spaces = std::vector<vm::bbox3d>{};
  auto sourceBoxes = std::vector<mdl::BrushNode*>{};
  for (auto* box : selection.boxes)
  {
    if (const auto space = spaceForBox(*box))
    {
      spaces.push_back(*space);
      sourceBoxes.push_back(box);
    }
  }

  if (selection.rooms.empty())
  {
    if (spaces.empty())
    {
      m_frame.logger().warn()
        << "Room creator: select one or more axis-aligned box brushes"
        << (m_modeChoice->currentData().toInt() == FloorMode
              ? " (floors may be at most two grid units thick)"
              : "");
      return;
    }

    auto recipe = recipeFromUi();
    recipe.spaces = spaces;
    if (recipe.wallThickness > PlayerWidth)
    {
      m_frame.logger().warn() << "Room creator: wall thickness exceeds player width ("
                              << PlayerWidth << ")";
    }

    auto* parent = mdl::parentForNodes(map, toNodes(sourceBoxes));
    auto transaction = mdl::Transaction{map, "Create Room"};
    mdl::deselectAll(map);
    // Add the room before removing its sources so that their parent group can't become
    // empty, which would remove it.
    auto room = mdl::createRoom(map, parent, recipe, roomLightOptionsFromPreferences());
    if (room.is_error())
    {
      transaction.cancel();
      m_frame.logger().error() << "Room creator: " << std::get<Error>(room.error()).msg;
      return;
    }
    mdl::removeNodes(map, toNodes(sourceBoxes));
    selectRooms({room.value()});
    transaction.commit();

    m_frame.logger().info() << "Room creator: created a room from " << spaces.size()
                            << " space(s)";
    return;
  }

  if (!sourceBoxes.empty() && selection.rooms.size() > 1u)
  {
    m_frame.logger().warn() << "Room creator: select a single room to extend it";
    return;
  }

  const auto policy = askAboutModifiedBrushes(selection.rooms);
  if (!policy)
  {
    return;
  }

  auto transaction = mdl::Transaction{map, "Rebuild Room"};
  mdl::deselectAll(map);
  for (auto* room : selection.rooms)
  {
    auto recipeResult = mdl::readRoomRecipe(room->group());
    if (recipeResult.is_error())
    {
      transaction.cancel();
      m_frame.logger().error() << "Room creator: "
                               << std::get<Error>(recipeResult.error()).msg;
      return;
    }

    auto recipe = std::move(recipeResult).value();
    applyUiSettings(recipe);
    for (const auto& space : spaces)
    {
      auto localSpace = mdl::roomLocalBounds(*room, space);
      if (localSpace.is_error())
      {
        transaction.cancel();
        m_frame.logger().error()
          << "Room creator: " << std::get<Error>(localSpace.error()).msg;
        return;
      }
      recipe.spaces.push_back(localSpace.value());
    }

    if (
      auto result =
        mdl::rebuildRoom(map, *room, recipe, *policy, roomLightOptionsFromPreferences());
      result.is_error())
    {
      transaction.cancel();
      m_frame.logger().error() << "Room creator: " << std::get<Error>(result.error()).msg;
      return;
    }
  }

  if (!sourceBoxes.empty())
  {
    mdl::removeNodes(map, toNodes(sourceBoxes));
  }
  selectRooms(selection.rooms);
  transaction.commit();

  if (sourceBoxes.empty())
  {
    m_frame.logger().info() << "Room creator: rebuilt " << selection.rooms.size()
                            << " room(s)";
  }
  else
  {
    m_frame.logger().info() << "Room creator: added " << sourceBoxes.size()
                            << " space(s) to the room";
  }
}

void RoomCreatorDialog::addDoorway()
{
  auto& map = m_frame.document().map();
  const auto selection = resolveSelection();
  if (selection.boxes.empty())
  {
    m_frame.logger().warn()
      << "Room creator: select box brushes that cross a room's walls";
    return;
  }

  // Without a selected room, a doorway applies to every room whose walls it crosses, so
  // a doorway between two rooms opens both walls.
  const auto candidates =
    selection.rooms.empty() ? mdl::collectRooms(map) : selection.rooms;

  auto affectedRooms = std::vector<mdl::GroupNode*>{};
  auto recipes = std::vector<mdl::RoomRecipe>{};
  auto usedBoxes = std::vector<mdl::BrushNode*>{};
  for (auto* room : candidates)
  {
    auto recipeResult = mdl::readRoomRecipe(room->group());
    auto shells = mdl::roomWorldShellBounds(*room);
    if (recipeResult.is_error() || shells.is_error())
    {
      continue;
    }
    auto recipe = std::move(recipeResult).value();

    auto touched = false;
    for (auto* box : selection.boxes)
    {
      const auto bounds = box->brush().bounds();
      if (!std::ranges::any_of(
            shells.value(), [&](const auto& shell) { return overlaps(shell, bounds); }))
      {
        continue;
      }

      auto localDoor = mdl::roomLocalBounds(*room, bounds);
      if (localDoor.is_error())
      {
        m_frame.logger().warn()
          << "Room creator: " << std::get<Error>(localDoor.error()).msg;
        continue;
      }

      recipe.doors.push_back(localDoor.value());
      touched = true;
      if (std::ranges::find(usedBoxes, box) == usedBoxes.end())
      {
        usedBoxes.push_back(box);
      }
    }

    if (touched)
    {
      affectedRooms.push_back(room);
      recipes.push_back(std::move(recipe));
    }
  }

  if (affectedRooms.empty())
  {
    m_frame.logger().warn()
      << "Room creator: the selected boxes don't cross any room's walls";
    return;
  }

  const auto policy = askAboutModifiedBrushes(affectedRooms);
  if (!policy)
  {
    return;
  }

  auto transaction = mdl::Transaction{map, "Add Doorway"};
  mdl::deselectAll(map);
  for (size_t i = 0; i < affectedRooms.size(); ++i)
  {
    if (
      auto result = mdl::rebuildRoom(
        map, *affectedRooms[i], recipes[i], *policy, roomLightOptionsFromPreferences());
      result.is_error())
    {
      transaction.cancel();
      m_frame.logger().error() << "Room creator: " << std::get<Error>(result.error()).msg;
      return;
    }
  }
  mdl::removeNodes(map, toNodes(usedBoxes));
  selectRooms(affectedRooms);
  transaction.commit();

  m_frame.logger().info() << "Room creator: added " << usedBoxes.size()
                          << " doorway(s) to " << affectedRooms.size() << " room(s)";
  if (usedBoxes.size() < selection.boxes.size())
  {
    m_frame.logger().warn()
      << "Room creator: " << selection.boxes.size() - usedBoxes.size()
      << " box(es) don't cross any room's walls and were left as is";
  }
}

void RoomCreatorDialog::clearDoorways()
{
  auto& map = m_frame.document().map();
  const auto selection = resolveSelection();

  auto rooms = std::vector<mdl::GroupNode*>{};
  auto recipes = std::vector<mdl::RoomRecipe>{};
  for (auto* room : selection.rooms)
  {
    auto recipeResult = mdl::readRoomRecipe(room->group());
    if (recipeResult.is_success() && !recipeResult.value().doors.empty())
    {
      auto recipe = std::move(recipeResult).value();
      recipe.doors.clear();
      rooms.push_back(room);
      recipes.push_back(std::move(recipe));
    }
  }

  if (rooms.empty())
  {
    m_frame.logger().warn() << "Room creator: the selected rooms have no doorways";
    return;
  }

  const auto policy = askAboutModifiedBrushes(rooms);
  if (!policy)
  {
    return;
  }

  auto transaction = mdl::Transaction{map, "Clear Doorways"};
  mdl::deselectAll(map);
  for (size_t i = 0; i < rooms.size(); ++i)
  {
    if (
      auto result = mdl::rebuildRoom(
        map, *rooms[i], recipes[i], *policy, roomLightOptionsFromPreferences());
      result.is_error())
    {
      transaction.cancel();
      m_frame.logger().error() << "Room creator: " << std::get<Error>(result.error()).msg;
      return;
    }
  }
  selectRooms(rooms);
  transaction.commit();
}

void RoomCreatorDialog::reset()
{
  setPref(Preferences::RoomCreatorMode, FloorMode);
  setPref(Preferences::RoomCreatorWallThickness, DefaultWallThickness);
  setPref(Preferences::RoomCreatorWallHeight, DefaultWallHeight);
  setPref(Preferences::RoomCreatorGenerateCeiling, true);
  setPref(Preferences::RoomCreatorGenerateFloor, true);
  setPref(Preferences::RoomCreatorGenerateLights, false);
  setPref(Preferences::RoomCreatorLightSpacing, 256);
  setPref(Preferences::RoomCreatorCorridorWidth, 64);
  setPref(Preferences::RoomCreatorCorridorHeight, 128);
  setPref(Preferences::RoomCreatorFloorLevel, 0);
  setPref(Preferences::RoomCreatorDoorWidth, 64);
  setPref(Preferences::RoomCreatorDoorHeight, 112);
  loadSettings();
  updateGridStep();
  updateModeUi();
}

void RoomCreatorDialog::connectRooms()
{
  auto& map = m_frame.document().map();
  const auto selection = resolveSelection();
  if (selection.rooms.size() != 2u || !selection.boxes.empty())
  {
    m_frame.logger().warn() << "Room creator: select exactly two rooms to connect";
    return;
  }

  const auto policy = askAboutModifiedBrushes(selection.rooms);
  if (!policy)
  {
    return;
  }

  auto options = mdl::CorridorOptions{};
  options.width = static_cast<double>(m_corridorWidth->currentData().toInt());
  options.height = static_cast<double>(m_corridorHeight->currentData().toInt());
  options.gridSize = static_cast<double>(map.grid().actualSize());

  auto transaction = mdl::Transaction{map, "Connect Rooms"};
  mdl::deselectAll(map);
  auto corridor = mdl::connectRooms(
    map,
    *selection.rooms[0],
    *selection.rooms[1],
    recipeFromUi(),
    options,
    *policy,
    roomLightOptionsFromPreferences());
  if (corridor.is_error())
  {
    transaction.cancel();
    m_frame.logger().error() << "Room creator: " << std::get<Error>(corridor.error()).msg;
    return;
  }

  auto rooms = selection.rooms;
  if (corridor.value())
  {
    rooms.push_back(corridor.value());
  }
  selectRooms(rooms);
  transaction.commit();

  if (corridor.value())
  {
    m_frame.logger().info() << "Room creator: connected the rooms with a corridor";
  }
  else
  {
    m_frame.logger().info()
      << "Room creator: the rooms' walls touch, so they were joined with a doorway";
  }
}

void RoomCreatorDialog::roomDrawToolDidChange()
{
  const auto blocker = QSignalBlocker{m_drawRoomsButton};
  m_drawRoomsButton->setChecked(m_frame.roomDrawToolActive());
}

void RoomCreatorDialog::toggleDrawRooms()
{
  m_frame.toggleRoomDrawTool();
  roomDrawToolDidChange();
}

void RoomCreatorDialog::clearLayout()
{
  auto& map = m_frame.document().map();
  const auto layout = mdl::readRoomLayout(map);
  if (layout.is_error() || layout.value().spaces.empty())
  {
    m_frame.logger().info() << "Room creator: there are no drawn rooms to clear";
    return;
  }

  if (
    QMessageBox::question(
      this,
      tr("Room Creator"),
      tr(
        "Remove all %n drawn room(s)? You can undo this.",
        "",
        int(layout.value().spaces.size())))
    != QMessageBox::Yes)
  {
    return;
  }

  if (auto result = mdl::updateRoomLayout(map, mdl::RoomRecipe{}); result.is_error())
  {
    m_frame.logger().error() << "Room creator: " << std::get<Error>(result.error()).msg;
  }
}

void RoomCreatorDialog::floorLevelFromSelection()
{
  if (const auto& bounds = m_frame.document().map().selectionBounds())
  {
    m_floorLevel->setValue(static_cast<int>(std::lround(bounds->min.z())));
  }
  else
  {
    m_frame.logger().warn()
      << "Room creator: select something to take the floor level from";
  }
}

mdl::RoomRecipe roomStyleFromPreferences(const mdl::Map& map)
{
  const auto materialName = [&](Preference<QString>& preference) {
    const auto name = pref(preference).trimmed().toStdString();
    return name.empty() ? map.currentMaterialName() : name;
  };

  auto recipe = mdl::RoomRecipe{};
  recipe.wallThickness = std::max(1, pref(Preferences::RoomCreatorWallThickness));
  recipe.generateFloor = pref(Preferences::RoomCreatorGenerateFloor);
  recipe.generateCeiling = pref(Preferences::RoomCreatorGenerateCeiling);
  recipe.wallMaterial = materialName(Preferences::RoomCreatorWallMaterial);
  recipe.floorMaterial = materialName(Preferences::RoomCreatorFloorMaterial);
  recipe.ceilingMaterial = materialName(Preferences::RoomCreatorCeilingMaterial);
  recipe.generateLights = pref(Preferences::RoomCreatorGenerateLights);
  recipe.lightSpacing = pref(Preferences::RoomCreatorLightSpacing);
  return recipe;
}

mdl::RoomLightOptions roomLightOptionsFromPreferences()
{
  const auto unitScale = static_cast<double>(pref(Preferences::UnityLightUnitScale));
  return mdl::RoomLightOptions{unitScale > 0.0 ? unitScale : 0.03125, 1.0};
}

} // namespace tb::ui
