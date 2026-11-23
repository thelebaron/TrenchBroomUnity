/*
 Copyright (C) 2026 Kristian Duske

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

#include "MaterialPickerDialog.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "mdl/Map.h"
#include "mdl/Material.h"
#include "mdl/MaterialManager.h"
#include "mdl/Texture.h"
#include "ui/MapDocument.h"
#include "ui/MaterialBrowser.h"
#include "ui/QtUtils.h"

namespace tb::ui
{
namespace
{

constexpr int SwatchSize = 20;
constexpr int MaxLabelWidth = 200;

const mdl::Material* findMaterial(const MapDocument& document, const std::string& name)
{
  return name.empty() ? nullptr : document.map().materialManager().material(name);
}

QPixmap createSwatch(const mdl::Material* material)
{
  auto pixmap = QPixmap{SwatchSize, SwatchSize};
  pixmap.fill(Qt::transparent);

  auto painter = QPainter{&pixmap};
  const auto rect = QRect{0, 0, SwatchSize - 1, SwatchSize - 1};

  if (const auto* texture = material ? material->texture() : nullptr)
  {
    auto color = toQColor(texture->averageColor());
    color.setAlpha(255);
    painter.fillRect(rect, color);
  }
  else
  {
    // Missing or not yet loaded: a checkerboard with a red cross.
    const auto half = SwatchSize / 2;
    painter.fillRect(rect, QColor{200, 200, 200});
    painter.fillRect(QRect{0, 0, half, half}, QColor{140, 140, 140});
    painter.fillRect(QRect{half, half, half, half}, QColor{140, 140, 140});
    painter.setPen(QPen{QColor{200, 40, 40}, 2});
    painter.drawLine(3, 3, SwatchSize - 4, SwatchSize - 4);
    painter.drawLine(SwatchSize - 4, 3, 3, SwatchSize - 4);
  }

  painter.setPen(QColor{40, 40, 40});
  painter.drawRect(rect);
  return pixmap;
}

} // namespace

MaterialPickerDialog::MaterialPickerDialog(
  MapDocument& document,
  GLContextManager& contextManager,
  const std::string& initialMaterialName,
  const QString& title,
  QWidget* parent)
  : QDialog{parent}
{
  setWindowIconTB(this);
  setWindowTitle(title);

  m_browser = new MaterialBrowser{document, contextManager};

  // The browser follows the map's current material; show the slot's material instead.
  const auto* initialMaterial = findMaterial(document, initialMaterialName);
  m_browser->setSelectedMaterial(initialMaterial);
  if (initialMaterial)
  {
    m_browser->revealMaterial(initialMaterial);
  }

  auto* buttonBox = new QDialogButtonBox{};
  m_okButton = buttonBox->addButton(QDialogButtonBox::Ok);
  buttonBox->addButton(QDialogButtonBox::Cancel);
  m_okButton->setEnabled(initialMaterial != nullptr);

  connect(
    m_browser,
    &MaterialBrowser::materialSelected,
    this,
    &MaterialPickerDialog::materialSelected);
  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(QMargins{});
  layout->setSpacing(0);
  layout->addWidget(m_browser, 1);
  layout->addLayout(wrapDialogButtonBox(buttonBox), 0);
  insertTitleBarSeparator(layout);
  setLayout(layout);

  setMinimumSize(420, 480);
  resize(560, 640);
}

std::string MaterialPickerDialog::selectedMaterialName() const
{
  const auto* material = m_browser->selectedMaterial();
  return material ? material->name() : std::string{};
}

void MaterialPickerDialog::materialSelected(const mdl::Material* material)
{
  m_okButton->setEnabled(material != nullptr);
}

MaterialSwatchButton::MaterialSwatchButton(
  MapDocument& document,
  GLContextManager& contextManager,
  QString pickerTitle,
  QWidget* parent)
  : QToolButton{parent}
  , m_document{document}
  , m_contextManager{contextManager}
  , m_pickerTitle{std::move(pickerTitle)}
{
  setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  setIconSize(QSize{SwatchSize, SwatchSize});
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setPopupMode(QToolButton::MenuButtonPopup);

  auto* menu = new QMenu{this};
  auto* useCurrent = menu->addAction(tr("Use Current Material"));
  connect(menu, &QMenu::aboutToShow, this, [this, useCurrent]() {
    const auto& current = m_document.map().currentMaterialName();
    useCurrent->setText(
      tr("Use Current Material (%1)").arg(QString::fromStdString(current)));
    useCurrent->setEnabled(!current.empty());
  });
  connect(useCurrent, &QAction::triggered, this, [this]() {
    const auto current = m_document.map().currentMaterialName();
    if (!current.empty() && current != m_materialName)
    {
      setMaterialName(current);
      emit materialChanged(QString::fromStdString(m_materialName));
    }
  });
  setMenu(menu);

  connect(this, &QToolButton::clicked, this, &MaterialSwatchButton::pickMaterial);
  refresh();
}

const std::string& MaterialSwatchButton::materialName() const
{
  return m_materialName;
}

void MaterialSwatchButton::setMaterialName(const std::string& materialName)
{
  m_materialName = materialName;
  refresh();
}

void MaterialSwatchButton::refresh()
{
  const auto* material = findMaterial(m_document, m_materialName);
  setIcon(QIcon{createSwatch(material)});

  const auto name = m_materialName.empty() ? tr("(current material)")
                                           : QString::fromStdString(m_materialName);
  setText(fontMetrics().elidedText(name, Qt::ElideMiddle, MaxLabelWidth));

  if (m_materialName.empty())
  {
    setToolTip(tr("Uses the material selected in the material browser"));
  }
  else if (const auto* texture = material ? material->texture() : nullptr)
  {
    setToolTip(tr("%1 (%2 x %3)").arg(name).arg(texture->width()).arg(texture->height()));
  }
  else
  {
    setToolTip(tr("%1 (not found in the loaded materials)").arg(name));
  }
}

void MaterialSwatchButton::pickMaterial()
{
  auto dialog = MaterialPickerDialog{
    m_document, m_contextManager, m_materialName, m_pickerTitle, window()};
  if (dialog.exec() == QDialog::Accepted)
  {
    const auto name = dialog.selectedMaterialName();
    if (!name.empty() && name != m_materialName)
    {
      setMaterialName(name);
      emit materialChanged(QString::fromStdString(m_materialName));
    }
  }
}

} // namespace tb::ui
