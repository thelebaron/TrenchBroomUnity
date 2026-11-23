/*
 Copyright (C) 2023 Kristian Duske

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

#include "DrawShapeToolExtensions.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>

#include "Logger.h"
#include "mdl/BrushBuilder.h"
#include "mdl/EditorContext.h"
#include "mdl/Game.h"
#include "mdl/GroupNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/RoomGenerator.h"
#include "mdl/Selection.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "ui/QtUtils.h"
#include "ui/RoomCreatorDialog.h"

#include <algorithm>

namespace tb::ui
{

DrawShapeToolCuboidExtension::DrawShapeToolCuboidExtension(mdl::Map& map)
  : DrawShapeToolExtension{map}
{
}

const std::string& DrawShapeToolCuboidExtension::name() const
{
  static const auto name = std::string{"Cuboid"};
  return name;
}

const std::filesystem::path& DrawShapeToolCuboidExtension::iconPath() const
{
  static const auto path = std::filesystem::path{"ShapeTool_Cuboid.svg"};
  return path;
}

DrawShapeToolExtensionPage* DrawShapeToolCuboidExtension::createToolPage(
  ShapeParameters&, QWidget* parent)
{
  return new DrawShapeToolExtensionPage{parent};
}

Result<std::vector<mdl::Brush>> DrawShapeToolCuboidExtension::createBrushes(
  const vm::bbox3d& bounds, const ShapeParameters&) const
{
  const auto game = m_map.game();
  const auto builder = mdl::BrushBuilder{
    m_map.world()->mapFormat(),
    m_map.worldBounds(),
    game->config().faceAttribsConfig.defaults};

  return builder.createCuboid(bounds, m_map.currentMaterialName())
    .transform([](auto brush) { return std::vector{std::move(brush)}; });
}

namespace
{

bool hasVolume(const vm::bbox3d& bounds)
{
  const auto size = bounds.size();
  return size.x() > 0.0 && size.y() > 0.0 && size.z() > 0.0;
}

std::vector<mdl::GroupNode*> selectedRooms(const mdl::Map& map)
{
  auto result = std::vector<mdl::GroupNode*>{};
  for (auto* node : map.selection().nodes)
  {
    if (
      auto* room = mdl::findRoom(node);
      room && std::ranges::find(result, room) == result.end())
    {
      result.push_back(room);
    }
  }
  return result;
}

void selectRoom(mdl::Map& map, mdl::GroupNode* room)
{
  // Only nodes in the currently open group (or the top level) can be selected.
  if (room->containingGroup() == map.editorContext().currentGroup())
  {
    mdl::selectNodes(map, {room});
  }
}

/** The recipe of the given room with the given world space box added as a space. */
Result<mdl::RoomRecipe> extendedRecipe(
  const mdl::GroupNode& room, const vm::bbox3d& worldBounds)
{
  auto recipe = mdl::readRoomRecipe(room.group());
  if (recipe.is_error())
  {
    return std::get<Error>(recipe.error());
  }
  auto localBounds = mdl::roomLocalBounds(room, worldBounds);
  if (localBounds.is_error())
  {
    return std::get<Error>(localBounds.error());
  }

  auto result = std::move(recipe).value();
  result.spaces.push_back(localBounds.value());
  return result;
}

DrawShapeToolExtensionPage* createInfoPage(
  const QString& hint, const QString& details, QWidget* parent)
{
  auto* page = new DrawShapeToolExtensionPage{parent};
  auto* label = new QLabel{hint};
  label->setToolTip(details);
  // Insert before the page's trailing stretch, like
  // DrawShapeToolExtensionPage::addWidget.
  auto* layout = qobject_cast<QHBoxLayout*>(page->layout());
  layout->insertWidget(layout->count() - 1, label, 0, Qt::AlignVCenter);
  return page;
}

} // namespace

DrawShapeToolRoomExtension::DrawShapeToolRoomExtension(mdl::Map& map)
  : DrawShapeToolExtension{map}
{
}

const std::string& DrawShapeToolRoomExtension::name() const
{
  static const auto name = std::string{"Room"};
  return name;
}

const std::filesystem::path& DrawShapeToolRoomExtension::iconPath() const
{
  static const auto path = std::filesystem::path{"RoomCreator.svg"};
  return path;
}

DrawShapeToolExtensionPage* DrawShapeToolRoomExtension::createToolPage(
  ShapeParameters&, QWidget* parent)
{
  return createInfoPage(
    QObject::tr("Drag out a room; with a room selected, drag to extend it"),
    QObject::tr(
      "Drag out a room's interior. With a room selected, the box is added to "
      "that room; press Esc to deselect and start a new one. Walls, materials "
      "and lights use the Room Creator's settings."),
    parent);
}

Result<std::vector<mdl::Brush>> DrawShapeToolRoomExtension::createBrushes(
  const vm::bbox3d& bounds, const ShapeParameters&) const
{
  if (!hasVolume(bounds))
  {
    return std::vector<mdl::Brush>{};
  }

  if (const auto rooms = selectedRooms(m_map); rooms.size() == 1u)
  {
    auto* room = rooms.front();
    auto recipe = extendedRecipe(*room, bounds);
    if (recipe.is_error())
    {
      return std::get<Error>(recipe.error());
    }
    return mdl::buildRoomBrushes(m_map, recipe.value(), room->group().transformation());
  }

  auto recipe = roomStyleFromPreferences(m_map);
  recipe.spaces = {bounds};
  return mdl::buildRoomBrushes(m_map, recipe, vm::mat4x4d::identity());
}

bool DrawShapeToolRoomExtension::commitShape(
  const vm::bbox3d& bounds, const ShapeParameters&)
{
  if (!hasVolume(bounds))
  {
    return true;
  }

  const auto lightOptions = roomLightOptionsFromPreferences();
  const auto rooms = selectedRooms(m_map);
  auto transaction = mdl::Transaction{m_map, "Draw Room"};

  if (rooms.size() == 1u)
  {
    auto* room = rooms.front();
    auto recipe = extendedRecipe(*room, bounds);
    auto result =
      recipe.is_success()
        ? mdl::rebuildRoom(
            m_map, *room, recipe.value(), mdl::ModifiedRoomBrushes::Keep, lightOptions)
        : Result<void>{std::get<Error>(recipe.error())};
    if (result.is_error())
    {
      transaction.cancel();
      m_map.logger().error() << "Could not extend room: "
                             << std::get<Error>(result.error()).msg;
      return true;
    }
  }
  else
  {
    auto recipe = roomStyleFromPreferences(m_map);
    recipe.spaces = {bounds};

    mdl::deselectAll(m_map);
    auto room = mdl::createRoom(m_map, mdl::parentForNodes(m_map), recipe, lightOptions);
    if (room.is_error())
    {
      transaction.cancel();
      m_map.logger().error() << "Could not create room: "
                             << std::get<Error>(room.error()).msg;
      return true;
    }
    selectRoom(m_map, room.value());
  }

  transaction.commit();
  return true;
}

DrawShapeToolDoorwayExtension::DrawShapeToolDoorwayExtension(mdl::Map& map)
  : DrawShapeToolExtension{map}
{
}

const std::string& DrawShapeToolDoorwayExtension::name() const
{
  static const auto name = std::string{"Doorway"};
  return name;
}

const std::filesystem::path& DrawShapeToolDoorwayExtension::iconPath() const
{
  static const auto path = std::filesystem::path{"ShapeTool_Doorway.svg"};
  return path;
}

DrawShapeToolExtensionPage* DrawShapeToolDoorwayExtension::createToolPage(
  ShapeParameters&, QWidget* parent)
{
  return createInfoPage(
    QObject::tr("Drag a box through a wall to cut a doorway"),
    QObject::tr(
      "Drag a box through a room's wall to cut a doorway. Make it deeper than "
      "the wall. With rooms selected, only those are cut; otherwise every room "
      "whose walls the box crosses."),
    parent);
}

Result<std::vector<mdl::Brush>> DrawShapeToolDoorwayExtension::createBrushes(
  const vm::bbox3d& bounds, const ShapeParameters&) const
{
  if (!hasVolume(bounds))
  {
    return std::vector<mdl::Brush>{};
  }

  const auto builder = mdl::BrushBuilder{
    m_map.world()->mapFormat(),
    m_map.worldBounds(),
    m_map.game()->config().faceAttribsConfig.defaults};
  return builder.createCuboid(bounds, m_map.currentMaterialName())
    .transform([](auto brush) { return std::vector{std::move(brush)}; });
}

bool DrawShapeToolDoorwayExtension::commitShape(
  const vm::bbox3d& bounds, const ShapeParameters&)
{
  if (!hasVolume(bounds))
  {
    return true;
  }

  auto candidates = selectedRooms(m_map);
  if (candidates.empty())
  {
    candidates = mdl::collectRooms(m_map);
  }

  const auto rooms = mdl::roomsCrossedBy(candidates, bounds);
  if (rooms.empty())
  {
    m_map.logger().warn() << "The doorway doesn't cross any room's walls";
    return true;
  }

  const auto lightOptions = roomLightOptionsFromPreferences();
  auto transaction = mdl::Transaction{m_map, "Add Doorway"};
  for (auto* room : rooms)
  {
    auto recipe = mdl::readRoomRecipe(room->group());
    auto localDoor = mdl::roomLocalBounds(*room, bounds);
    if (recipe.is_error() || localDoor.is_error())
    {
      transaction.cancel();
      m_map.logger().error() << "Could not add doorway to room '" << room->name() << "'";
      return true;
    }

    auto newRecipe = std::move(recipe).value();
    newRecipe.doors.push_back(localDoor.value());
    if (
      auto result = mdl::rebuildRoom(
        m_map, *room, newRecipe, mdl::ModifiedRoomBrushes::Keep, lightOptions);
      result.is_error())
    {
      transaction.cancel();
      m_map.logger().error() << "Could not add doorway: "
                             << std::get<Error>(result.error()).msg;
      return true;
    }
  }
  transaction.commit();
  return true;
}

DrawShapeToolAxisAlignedShapeExtensionPage::DrawShapeToolAxisAlignedShapeExtensionPage(
  ShapeParameters& parameters, QWidget* parent)
  : DrawShapeToolExtensionPage{parent}
  , m_parameters{parameters}
{
  auto* axisLabel = new QLabel{tr("Axis: ")};
  auto* axisComboBox = new QComboBox{};
  axisComboBox->addItems({tr("X"), tr("Y"), tr("Z")});

  connect(
    axisComboBox,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    [&](const auto index) { m_parameters.setAxis(vm::axis::type(index)); });

  addWidget(axisLabel);
  addWidget(axisComboBox);

  const auto updateWidgets = [=, this]() {
    axisComboBox->setCurrentIndex(int(m_parameters.axis()));
  };
  updateWidgets();

  m_notifierConnection +=
    m_parameters.parametersDidChangeNotifier.connect(std::move(updateWidgets));
}

DrawShapeToolCircularShapeExtensionPage::DrawShapeToolCircularShapeExtensionPage(
  ShapeParameters& parameters, QWidget* parent)
  : DrawShapeToolAxisAlignedShapeExtensionPage{parameters, parent}
  , m_parameters{parameters}
{
  auto* numSidesLabel = new QLabel{tr("Number of Sides: ")};
  auto* numSidesBox = new QSpinBox{};
  numSidesBox->setRange(3, 96);

  auto* precisionBox = new QComboBox{};
  precisionBox->addItems({"12", "24", "48", "96"});

  auto* numSidesWidget = new QStackedWidget{};
  numSidesWidget->addWidget(numSidesBox);
  numSidesWidget->addWidget(precisionBox);

  auto* edgeAlignedCircleButton =
    createBitmapToggleButton("CircleEdgeAligned.svg", tr("Align edge to bounding box"));
  edgeAlignedCircleButton->setIconSize({24, 24});
  edgeAlignedCircleButton->setObjectName("toolButton_withBorder");

  auto* vertexAlignedCircleButton = createBitmapToggleButton(
    "CircleVertexAligned.svg", tr("Align vertices to bounding box"));
  vertexAlignedCircleButton->setIconSize({24, 24});
  vertexAlignedCircleButton->setObjectName("toolButton_withBorder");

  auto* scalableCircleButton =
    createBitmapToggleButton("CircleScalable.svg", tr("Scalable circle shape"));
  scalableCircleButton->setIconSize({24, 24});
  scalableCircleButton->setObjectName("toolButton_withBorder");

  auto* radiusModeButtonGroup = new QButtonGroup{};
  radiusModeButtonGroup->addButton(edgeAlignedCircleButton);
  radiusModeButtonGroup->addButton(vertexAlignedCircleButton);
  radiusModeButtonGroup->addButton(scalableCircleButton);

  connect(
    numSidesBox,
    QOverload<int>::of(&QSpinBox::valueChanged),
    this,
    [&](const auto numSides) {
      m_parameters.setCircleShape(
        std::visit(
          kdl::overload(
            [&](const mdl::EdgeAlignedCircle&) -> mdl::CircleShape {
              return mdl::EdgeAlignedCircle{size_t(numSides)};
            },
            [&](const mdl::VertexAlignedCircle&) -> mdl::CircleShape {
              return mdl::VertexAlignedCircle{size_t(numSides)};
            },
            [&](const mdl::ScalableCircle& circleShape) -> mdl::CircleShape {
              return circleShape;
            }),
          m_parameters.circleShape()));
    });
  connect(
    precisionBox,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    [&](const auto precision) {
      m_parameters.setCircleShape(
        std::visit(
          kdl::overload(
            [&](const mdl::ScalableCircle&) -> mdl::CircleShape {
              return mdl::ScalableCircle{size_t(precision)};
            },
            [](const auto& circleShape) -> mdl::CircleShape { return circleShape; }),
          m_parameters.circleShape()));
    });
  connect(edgeAlignedCircleButton, &QToolButton::clicked, this, [=, this]() {
    m_parameters.setCircleShape(
      mdl::convertCircleShape<mdl::EdgeAlignedCircle>(m_parameters.circleShape()));
  });
  connect(vertexAlignedCircleButton, &QToolButton::clicked, this, [=, this]() {
    m_parameters.setCircleShape(
      mdl::convertCircleShape<mdl::VertexAlignedCircle>(m_parameters.circleShape()));
  });
  connect(scalableCircleButton, &QToolButton::clicked, this, [=, this]() {
    m_parameters.setCircleShape(
      mdl::convertCircleShape<mdl::ScalableCircle>(m_parameters.circleShape()));
  });

  addWidget(numSidesLabel);
  addWidget(numSidesWidget);
  addWidget(edgeAlignedCircleButton);
  addWidget(vertexAlignedCircleButton);
  addWidget(scalableCircleButton);

  const auto updateWidgets = [=, this]() {
    std::visit(
      kdl::overload(
        [&](const mdl::EdgeAlignedCircle& circleShape) {
          numSidesBox->setValue(int(circleShape.numSides));
          numSidesWidget->setCurrentWidget(numSidesBox);
        },
        [&](const mdl::VertexAlignedCircle& circleShape) {
          numSidesBox->setValue(int(circleShape.numSides));
          numSidesWidget->setCurrentWidget(numSidesBox);
        },
        [&](const mdl::ScalableCircle& circleShape) {
          precisionBox->setCurrentIndex(int(circleShape.precision));
          numSidesWidget->setCurrentWidget(precisionBox);
        }),
      m_parameters.circleShape());

    edgeAlignedCircleButton->setChecked(
      std::holds_alternative<mdl::EdgeAlignedCircle>(m_parameters.circleShape()));
    vertexAlignedCircleButton->setChecked(
      std::holds_alternative<mdl::VertexAlignedCircle>(m_parameters.circleShape()));
    scalableCircleButton->setChecked(
      std::holds_alternative<mdl::ScalableCircle>(m_parameters.circleShape()));
  };
  updateWidgets();

  m_notifierConnection +=
    m_parameters.parametersDidChangeNotifier.connect(std::move(updateWidgets));
}

DrawShapeToolCylinderShapeExtensionPage::DrawShapeToolCylinderShapeExtensionPage(
  mdl::Map& map, ShapeParameters& parameters, QWidget* parent)
  : DrawShapeToolCircularShapeExtensionPage{parameters, parent}
  , m_parameters{parameters}
{
  auto* hollowCheckBox = new QCheckBox{tr("Hollow")};

  auto* thicknessLabel = new QLabel{tr("Thickness: ")};
  auto* thicknessBox = new QDoubleSpinBox{};
  thicknessBox->setEnabled(parameters.hollow());
  thicknessBox->setRange(1, 128);

  connect(hollowCheckBox, &QCheckBox::toggled, this, [&](const auto hollow) {
    m_parameters.setHollow(hollow);
  });
  connect(
    thicknessBox,
    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this,
    [&](const auto thickness) { m_parameters.setThickness(thickness); });

  addWidget(hollowCheckBox);
  addWidget(thicknessLabel);
  addWidget(thicknessBox);
  addApplyButton(map);

  const auto updateWidgets = [=, this]() {
    hollowCheckBox->setChecked(m_parameters.hollow());
    thicknessBox->setEnabled(m_parameters.hollow());
    thicknessBox->setValue(m_parameters.thickness());
  };
  updateWidgets();

  m_notifierConnection +=
    m_parameters.parametersDidChangeNotifier.connect(std::move(updateWidgets));
}

DrawShapeToolCylinderExtension::DrawShapeToolCylinderExtension(mdl::Map& map)
  : DrawShapeToolExtension{map}
{
}

const std::string& DrawShapeToolCylinderExtension::name() const
{
  static const auto name = std::string{"Cylinder"};
  return name;
}

const std::filesystem::path& DrawShapeToolCylinderExtension::iconPath() const
{
  static const auto path = std::filesystem::path{"ShapeTool_Cylinder.svg"};
  return path;
}

DrawShapeToolExtensionPage* DrawShapeToolCylinderExtension::createToolPage(
  ShapeParameters& parameters, QWidget* parent)
{
  return new DrawShapeToolCylinderShapeExtensionPage{m_map, parameters, parent};
}

Result<std::vector<mdl::Brush>> DrawShapeToolCylinderExtension::createBrushes(
  const vm::bbox3d& bounds, const ShapeParameters& parameters) const
{
  const auto game = m_map.game();
  const auto builder = mdl::BrushBuilder{
    m_map.world()->mapFormat(),
    m_map.worldBounds(),
    game->config().faceAttribsConfig.defaults};
  return parameters.hollow()
           ? builder.createHollowCylinder(
               bounds,
               parameters.thickness(),
               parameters.circleShape(),
               parameters.axis(),
               m_map.currentMaterialName())
           : builder
               .createCylinder(
                 bounds,
                 parameters.circleShape(),
                 parameters.axis(),
                 m_map.currentMaterialName())
               .transform([](auto brush) { return std::vector{std::move(brush)}; });
}

DrawShapeToolConeShapeExtensionPage::DrawShapeToolConeShapeExtensionPage(
  mdl::Map& map, ShapeParameters& parameters, QWidget* parent)
  : DrawShapeToolCircularShapeExtensionPage{parameters, parent}
  , m_parameters{parameters}
{
  addApplyButton(map);
}

DrawShapeToolConeExtension::DrawShapeToolConeExtension(mdl::Map& map)
  : DrawShapeToolExtension{map}
{
}

const std::string& DrawShapeToolConeExtension::name() const
{
  static const auto name = std::string{"Cone"};
  return name;
}

const std::filesystem::path& DrawShapeToolConeExtension::iconPath() const
{
  static const auto path = std::filesystem::path{"ShapeTool_Cone.svg"};
  return path;
}

DrawShapeToolExtensionPage* DrawShapeToolConeExtension::createToolPage(
  ShapeParameters& parameters, QWidget* parent)
{
  return new DrawShapeToolConeShapeExtensionPage{m_map, parameters, parent};
}

Result<std::vector<mdl::Brush>> DrawShapeToolConeExtension::createBrushes(
  const vm::bbox3d& bounds, const ShapeParameters& parameters) const
{
  const auto game = m_map.game();
  const auto builder = mdl::BrushBuilder{
    m_map.world()->mapFormat(),
    m_map.worldBounds(),
    game->config().faceAttribsConfig.defaults};
  return builder
    .createCone(
      bounds, parameters.circleShape(), parameters.axis(), m_map.currentMaterialName())
    .transform([](auto brush) { return std::vector{std::move(brush)}; });
}

DrawShapeToolIcoSphereShapeExtensionPage::DrawShapeToolIcoSphereShapeExtensionPage(
  mdl::Map& map, ShapeParameters& parameters, QWidget* parent)
  : DrawShapeToolExtensionPage{parent}
  , m_parameters{parameters}
{
  auto* accuracyLabel = new QLabel{tr("Accuracy: ")};
  auto* accuracyBox = new QSpinBox{};
  accuracyBox->setRange(0, 4);

  connect(
    accuracyBox,
    QOverload<int>::of(&QSpinBox::valueChanged),
    this,
    [&](const auto accuracy) { m_parameters.setAccuracy(size_t(accuracy)); });

  addWidget(accuracyLabel);
  addWidget(accuracyBox);
  addApplyButton(map);

  const auto updateWidgets = [=, this]() {
    accuracyBox->setValue(int(m_parameters.accuracy()));
  };
  updateWidgets();

  m_notifierConnection +=
    m_parameters.parametersDidChangeNotifier.connect(std::move(updateWidgets));
}

DrawShapeToolIcoSphereExtension::DrawShapeToolIcoSphereExtension(mdl::Map& map)
  : DrawShapeToolExtension{map}
{
}

const std::string& DrawShapeToolIcoSphereExtension::name() const
{
  static const auto name = std::string{"Spheroid (Icosahedron)"};
  return name;
}

const std::filesystem::path& DrawShapeToolIcoSphereExtension::iconPath() const
{
  static const auto path = std::filesystem::path{"ShapeTool_IcoSphere.svg"};
  return path;
}

DrawShapeToolExtensionPage* DrawShapeToolIcoSphereExtension::createToolPage(
  ShapeParameters& parameters, QWidget* parent)
{
  return new DrawShapeToolIcoSphereShapeExtensionPage{m_map, parameters, parent};
}

Result<std::vector<mdl::Brush>> DrawShapeToolIcoSphereExtension::createBrushes(
  const vm::bbox3d& bounds, const ShapeParameters& parameters) const
{
  const auto game = m_map.game();
  const auto builder = mdl::BrushBuilder{
    m_map.world()->mapFormat(),
    m_map.worldBounds(),
    game->config().faceAttribsConfig.defaults};

  return builder
    .createIcoSphere(bounds, parameters.accuracy(), m_map.currentMaterialName())
    .transform([](auto brush) { return std::vector{std::move(brush)}; });
}

DrawShapeToolUVSphereShapeExtensionPage::DrawShapeToolUVSphereShapeExtensionPage(
  mdl::Map& map, ShapeParameters& parameters, QWidget* parent)
  : DrawShapeToolCircularShapeExtensionPage{parameters, parent}
  , m_parameters{parameters}
{
  auto* numRingsLabel = new QLabel{tr("Number of Rings: ")};
  auto* numRingsBox = new QSpinBox{};
  numRingsBox->setRange(1, 256);

  auto* numRingsLayout = new QHBoxLayout{};
  numRingsLayout->setContentsMargins(QMargins{});
  numRingsLayout->setSpacing(LayoutConstants::MediumHMargin);
  numRingsLayout->addWidget(numRingsLabel);
  numRingsLayout->addWidget(numRingsBox);

  auto* numRingsWidget = new QWidget{};
  numRingsWidget->setLayout(numRingsLayout);

  connect(
    numRingsBox,
    QOverload<int>::of(&QSpinBox::valueChanged),
    this,
    [&](const auto numRings) { m_parameters.setNumRings(size_t(numRings)); });

  addWidget(numRingsWidget);
  addApplyButton(map);

  const auto updateWidgets = [=, this]() {
    numRingsWidget->setVisible(
      !std::holds_alternative<mdl::ScalableCircle>(m_parameters.circleShape()));
    numRingsBox->setValue(int(m_parameters.numRings()));
  };
  updateWidgets();

  m_notifierConnection +=
    m_parameters.parametersDidChangeNotifier.connect(std::move(updateWidgets));
}

DrawShapeToolUVSphereExtension::DrawShapeToolUVSphereExtension(mdl::Map& map)
  : DrawShapeToolExtension{map}
{
}

const std::string& DrawShapeToolUVSphereExtension::name() const
{
  static const auto name = std::string{"Spheroid (UV)"};
  return name;
}

const std::filesystem::path& DrawShapeToolUVSphereExtension::iconPath() const
{
  static const auto path = std::filesystem::path{"ShapeTool_UVSphere.svg"};
  return path;
}

DrawShapeToolExtensionPage* DrawShapeToolUVSphereExtension::createToolPage(
  ShapeParameters& parameters, QWidget* parent)
{
  return new DrawShapeToolUVSphereShapeExtensionPage{m_map, parameters, parent};
}

Result<std::vector<mdl::Brush>> DrawShapeToolUVSphereExtension::createBrushes(
  const vm::bbox3d& bounds, const ShapeParameters& parameters) const
{
  const auto game = m_map.game();
  const auto builder = mdl::BrushBuilder{
    m_map.world()->mapFormat(),
    m_map.worldBounds(),
    game->config().faceAttribsConfig.defaults};
  return builder
    .createUVSphere(
      bounds,
      parameters.circleShape(),
      parameters.numRings(),
      parameters.axis(),
      m_map.currentMaterialName())
    .transform([](auto brush) { return std::vector{std::move(brush)}; });
}

std::vector<std::unique_ptr<DrawShapeToolExtension>> createDrawShapeToolExtensions(
  mdl::Map& map)
{
  auto result = std::vector<std::unique_ptr<DrawShapeToolExtension>>{};
  result.push_back(std::make_unique<DrawShapeToolCuboidExtension>(map));
  result.push_back(std::make_unique<DrawShapeToolCylinderExtension>(map));
  result.push_back(std::make_unique<DrawShapeToolConeExtension>(map));
  result.push_back(std::make_unique<DrawShapeToolUVSphereExtension>(map));
  result.push_back(std::make_unique<DrawShapeToolIcoSphereExtension>(map));
  result.push_back(std::make_unique<DrawShapeToolRoomExtension>(map));
  result.push_back(std::make_unique<DrawShapeToolDoorwayExtension>(map));
  return result;
}

} // namespace tb::ui
