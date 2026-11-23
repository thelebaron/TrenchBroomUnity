/*
 Copyright (C) 2026 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#pragma once

#include <QDialog>

#include "NotifierConnection.h"
#include "mdl/RoomGenerator.h"

#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace tb::mdl
{
class BrushNode;
class GroupNode;
class Map;
struct SelectionChange;
} // namespace tb::mdl

namespace tb::ui
{
class GLContextManager;
class MapFrame;
class MaterialSwatchButton;

/**
 * The room style (wall settings, materials and lights, but no spaces, doors or clips)
 * from the Room Creator's saved settings. Empty materials fall back to the current one.
 */
mdl::RoomRecipe roomStyleFromPreferences(const mdl::Map& map);

/** Light settings for generated room lights, from the preferences. */
mdl::RoomLightOptions roomLightOptionsFromPreferences();

/**
 * Creates rooms from box brushes. A room is a group that stores its recipe (spaces,
 * doorways, wall thickness and materials), so it can be rebuilt, extended or given
 * doorways at any time, also after saving and reloading the map.
 */
class RoomCreatorDialog : public QDialog
{
  Q_OBJECT

private:
  struct RoomSelection
  {
    std::vector<mdl::GroupNode*> rooms;
    std::vector<mdl::BrushNode*> boxes;
  };

  MapFrame& m_frame;
  GLContextManager& m_contextManager;
  QComboBox* m_modeChoice = nullptr;
  QComboBox* m_wallThickness = nullptr;
  QComboBox* m_wallHeight = nullptr;
  QCheckBox* m_generateCeiling = nullptr;
  QCheckBox* m_generateFloor = nullptr;
  QGroupBox* m_materials = nullptr;
  MaterialSwatchButton* m_wallMaterial = nullptr;
  MaterialSwatchButton* m_ceilingMaterial = nullptr;
  MaterialSwatchButton* m_floorMaterial = nullptr;
  QLabel* m_status = nullptr;
  QPushButton* m_generateButton = nullptr;
  QPushButton* m_addDoorwayButton = nullptr;
  QPushButton* m_clearDoorwaysButton = nullptr;
  QCheckBox* m_generateLights = nullptr;
  QComboBox* m_lightSpacing = nullptr;
  QComboBox* m_corridorWidth = nullptr;
  QComboBox* m_corridorHeight = nullptr;
  QPushButton* m_connectButton = nullptr;
  QPushButton* m_drawRoomsButton = nullptr;
  QPushButton* m_clearLayoutButton = nullptr;
  QSpinBox* m_floorLevel = nullptr;
  QComboBox* m_doorWidth = nullptr;
  QComboBox* m_doorHeight = nullptr;

  NotifierConnection m_notifierConnection;
  bool m_updating = false;

public:
  RoomCreatorDialog(
    MapFrame& frame, GLContextManager& contextManager, QWidget* parent = nullptr);

  void gridDidChange();
  void mapDidChange();

  /** Keeps the Draw Rooms button in sync when the tool is switched on or off. */
  void roomDrawToolDidChange();

private slots:
  void generate();
  void addDoorway();
  void clearDoorways();
  void connectRooms();
  void toggleDrawRooms();
  void clearLayout();
  void floorLevelFromSelection();
  void reset();
  void updateModeUi();
  void updateGridStep();

private:
  void createGui();
  void connectObservers();
  void loadSettings();
  void saveSettings();
  void refreshMaterialSwatches();
  void selectionDidChange(const mdl::SelectionChange& change);
  void updateSelectionUi();
  void loadSettingsFromRoom(const mdl::RoomRecipe& recipe);

  RoomSelection resolveSelection() const;
  std::optional<vm::bbox3d> spaceForBox(const mdl::BrushNode& brush) const;
  mdl::RoomRecipe recipeFromUi() const;
  void applyUiSettings(mdl::RoomRecipe& recipe) const;
  std::optional<mdl::ModifiedRoomBrushes> askAboutModifiedBrushes(
    const std::vector<mdl::GroupNode*>& rooms);
  void selectRooms(const std::vector<mdl::GroupNode*>& rooms);
  void populateDimensionChoice(QComboBox* choice, int value, bool thickness);
};

} // namespace tb::ui
