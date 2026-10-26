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

#include "UnityPrefabBrowser.h"

#include <QBoxLayout>
#include <QComboBox>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPixmap>
#include <QStackedWidget>

#include "Error.h"
#include "io/PathQt.h"
#include "mdl/Game.h"
#include "mdl/Map.h"
#include "ui/MapDocument.h"
#include "ui/QtUtils.h"
#include "ui/ViewConstants.h"
#include "unity/UnityCatalogWatcher.h"

#include <algorithm>
#include <set>

namespace tb::unity
{
namespace
{
constexpr auto ThumbnailSize = 96;
constexpr auto ClassNameRole = Qt::UserRole;

bool matchesSearch(const UnityCatalogPrefab& prefab, const QString& search)
{
  return QString::fromStdString(prefab.displayName).contains(search, Qt::CaseInsensitive)
         || QString::fromStdString(prefab.className).contains(search, Qt::CaseInsensitive)
         || QString::fromStdString(prefab.category).contains(search, Qt::CaseInsensitive);
}
} // namespace

UnityPrefabListWidget::UnityPrefabListWidget(QWidget* parent)
  : QListWidget{parent}
{
  setViewMode(QListView::IconMode);
  setIconSize(QSize{ThumbnailSize, ThumbnailSize});
  setGridSize(QSize{ThumbnailSize + 24, ThumbnailSize + 36});
  setResizeMode(QListView::Adjust);
  setMovement(QListView::Static);
  setWordWrap(true);
  setUniformItemSizes(true);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setDragEnabled(true);
  setDragDropMode(QAbstractItemView::DragOnly);
}

QMimeData* UnityPrefabListWidget::mimeData(const QList<QListWidgetItem*>& items) const
{
  if (items.isEmpty())
  {
    return nullptr;
  }

  auto* mime = new QMimeData{};
  mime->setText("entity:" + items.first()->data(ClassNameRole).toString());
  return mime;
}

UnityPrefabBrowser::UnityPrefabBrowser(ui::MapDocument& document, QWidget* parent)
  : ui::TabBookPage{parent}
  , m_document{document}
{
  createGui();

  connect(m_searchBox, &QLineEdit::textChanged, this, &UnityPrefabBrowser::rebuildList);
  connect(
    m_categoryCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &UnityPrefabBrowser::rebuildList);
  connect(
    &document.unityCatalogWatcher(),
    &UnityCatalogWatcher::catalogChanged,
    this,
    &UnityPrefabBrowser::reloadCatalog);

  reloadCatalog();
}

void UnityPrefabBrowser::createGui()
{
  m_searchBox = ui::createSearchBox();
  m_categoryCombo = new QComboBox{};

  auto* controls = new QHBoxLayout{};
  controls->setContentsMargins(
    ui::LayoutConstants::NarrowHMargin,
    ui::LayoutConstants::NarrowVMargin,
    ui::LayoutConstants::NarrowHMargin,
    ui::LayoutConstants::NarrowVMargin);
  controls->setSpacing(ui::LayoutConstants::NarrowHMargin);
  controls->addWidget(m_searchBox, 1);
  controls->addWidget(m_categoryCombo, 1);

  m_list = new UnityPrefabListWidget{};

  m_messageLabel = new QLabel{};
  m_messageLabel->setAlignment(Qt::AlignCenter);
  m_messageLabel->setWordWrap(true);
  m_messageLabel->setMargin(ui::LayoutConstants::WideHMargin);

  m_stack = new QStackedWidget{};
  m_stack->addWidget(m_list);
  m_stack->addWidget(m_messageLabel);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addLayout(controls, 0);
  layout->addWidget(m_stack, 1);
  setLayout(layout);
}

void UnityPrefabBrowser::reloadCatalog()
{
  m_catalog = {};
  const auto& map = m_document.map();
  if (!map.game() || !usesUnityEntityDefinitions(map.game()->config()))
  {
    showMessage(tr(
      "Unity prefabs need a Unity project and Entity definitions from Unity, both in the "
      "Unity preferences."));
    rebuildCategories();
    return;
  }

  auto catalog = loadCatalog();
  if (catalog.is_success())
  {
    m_catalog = std::move(catalog.value());
  }
  else
  {
    showMessage(tr(
                  "Could not read the Unity catalog. Export it from Unity with Junk > "
                  "Trenchbroom > Export Prefab Catalog.\n\n%1")
                  .arg(QString::fromStdString(std::get<Error>(catalog.error()).msg)));
  }

  rebuildCategories();
  if (!m_catalog.prefabs.empty())
  {
    m_stack->setCurrentWidget(m_list);
  }
  else if (m_stack->currentWidget() == m_list)
  {
    showMessage(tr(
      "The Unity catalog has no prefabs. Add a [Prefab] component to a "
      "Unity prefab to make it placeable."));
  }
}

void UnityPrefabBrowser::rebuildCategories()
{
  const auto current = m_categoryCombo->currentData().toString();

  auto categories = std::set<std::string>{};
  for (const auto& prefab : m_catalog.prefabs)
  {
    categories.insert(prefab.category);
  }

  const auto blocker = QSignalBlocker{m_categoryCombo};
  m_categoryCombo->clear();
  m_categoryCombo->addItem(tr("All categories"), QString{});
  for (const auto& category : categories)
  {
    const auto name = QString::fromStdString(category);
    m_categoryCombo->addItem(name, name);
  }
  m_categoryCombo->setCurrentIndex(std::max(0, m_categoryCombo->findData(current)));

  rebuildList();
}

void UnityPrefabBrowser::rebuildList()
{
  const auto search = m_searchBox->text().trimmed();
  const auto category = m_categoryCombo->currentData().toString().toStdString();

  m_list->clear();
  for (const auto& prefab : m_catalog.prefabs)
  {
    if (!category.empty() && prefab.category != category)
    {
      continue;
    }
    if (!search.isEmpty() && !matchesSearch(prefab, search))
    {
      continue;
    }

    auto* item = new QListWidgetItem{
      QIcon{QPixmap{io::pathAsQString(prefab.thumbnail)}},
      QString::fromStdString(prefab.displayName)};
    item->setData(ClassNameRole, QString::fromStdString(prefab.className));
    item->setToolTip(QString::fromStdString(prefab.className + "\n" + prefab.category));
    m_list->addItem(item);
  }
}

void UnityPrefabBrowser::showMessage(const QString& message)
{
  m_messageLabel->setText(message);
  m_stack->setCurrentWidget(m_messageLabel);
}

} // namespace tb::unity
