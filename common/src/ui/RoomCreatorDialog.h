/*
 Copyright (C) 2026 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#pragma once

#include "mdl/RoomGenerator.h"
#include "NotifierConnection.h"

#include <QDialog>

#include <vector>

class QCheckBox;
class QComboBox;
class QGroupBox;

namespace tb::mdl
{
class BrushNode;
class Node;
}

namespace tb::ui
{
class MapFrame;

class RoomCreatorDialog : public QDialog
{
  Q_OBJECT

private:
  MapFrame& m_frame;
  QComboBox* m_modeChoice = nullptr;
  QComboBox* m_wallThickness = nullptr;
  QComboBox* m_wallHeight = nullptr;
  QCheckBox* m_generateCeiling = nullptr;
  QCheckBox* m_generateFloor = nullptr;
  QGroupBox* m_materials = nullptr;
  QComboBox* m_wallMaterial = nullptr;
  QComboBox* m_ceilingMaterial = nullptr;
  QComboBox* m_floorMaterial = nullptr;

  std::vector<mdl::RoomGenerationSource> m_lastSources;
  std::vector<mdl::BrushNode*> m_generatedNodes;
  NotifierConnection m_notifierConnection;
  bool m_updating = false;

public:
  explicit RoomCreatorDialog(MapFrame& frame, QWidget* parent = nullptr);

  void gridDidChange();
  void mapDidChange();

private slots:
  void generate();
  void reset();
  void updateModeUi();
  void updateGridStep();

private:
  void createGui();
  void loadSettings();
  void saveSettings();
  void populateMaterials();
  void nodesWillBeRemoved(const std::vector<mdl::Node*>& nodes);

  std::vector<mdl::RoomGenerationSource> collectSources() const;
  bool isValidBox(const mdl::BrushNode& brush) const;
  mdl::RoomGenerationSettings settings() const;
  void populateDimensionChoice(QComboBox* choice, int value, bool thickness);
};

} // namespace tb::ui
