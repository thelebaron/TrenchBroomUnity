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

#include "UnityDecalBrowser.h"

#include <QBoxLayout>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>

#include "io/PathQt.h"
#include "mdl/Game.h"
#include "mdl/Map.h"
#include "ui/ImageViewerDialog.h"
#include "ui/MapDocument.h"
#include "ui/QtUtils.h"
#include "ui/ViewConstants.h"
#include "unity/UnityAssetImport.h"
#include "unity/UnityAssetImportDialogs.h"
#include "unity/UnityCatalog.h"
#include "unity/UnityDecalLibrary.h"

#include <algorithm>
#include <set>

namespace tb::unity
{
namespace
{
constexpr auto ThumbnailSize = 96;
constexpr auto DecalIdRole = Qt::UserRole;

bool matchesSearch(const UnityCatalogDecal& decal, const QString& search)
{
  return QString::fromStdString(decal.displayName).contains(search, Qt::CaseInsensitive)
         || QString::fromStdString(decal.id).contains(search, Qt::CaseInsensitive)
         || QString::fromStdString(decal.category).contains(search, Qt::CaseInsensitive);
}

/**
 * The decal's rectangle cut from its atlas, fitted into a square tile on a checkerboard
 * so transparent parts show.
 */
QPixmap thumbnail(const QImage& atlas, const UnityCatalogDecal& decal)
{
  auto tile = QPixmap{ThumbnailSize, ThumbnailSize};
  tile.fill(Qt::transparent);

  auto painter = QPainter{&tile};
  constexpr auto checker = 8;
  for (auto y = 0; y < ThumbnailSize; y += checker)
  {
    for (auto x = 0; x < ThumbnailSize; x += checker)
    {
      const auto dark = ((x + y) / checker) % 2 == 0;
      painter.fillRect(
        x, y, checker, checker, dark ? QColor{70, 70, 70} : QColor{95, 95, 95});
    }
  }

  if (!atlas.isNull())
  {
    // the rect's origin is at the bottom left, the image's at the top left
    const auto w = atlas.width();
    const auto h = atlas.height();
    const auto source = QRectF{
      decal.rect[0] * w,
      (1.0 - decal.rect[1] - decal.rect[3]) * h,
      decal.rect[2] * w,
      decal.rect[3] * h};
    const auto scale = std::min(
      ThumbnailSize / std::max(source.width(), 1.0),
      ThumbnailSize / std::max(source.height(), 1.0));
    const auto size = QSizeF{source.width() * scale, source.height() * scale};
    const auto target = QRectF{
      QPointF{
        (ThumbnailSize - size.width()) / 2.0, (ThumbnailSize - size.height()) / 2.0},
      size};
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(target, atlas, source);
  }
  return tile;
}
} // namespace

UnityDecalBrowser::UnityDecalBrowser(ui::MapDocument& document, QWidget* parent)
  : ui::TabBookPage{parent}
  , m_document{document}
{
  createGui();

  connect(m_searchBox, &QLineEdit::textChanged, this, &UnityDecalBrowser::rebuildList);
  connect(
    m_categoryCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &UnityDecalBrowser::rebuildList);
  connect(m_list, &QListWidget::itemClicked, this, &UnityDecalBrowser::itemClicked);
  connect(m_list, &QListWidget::itemDoubleClicked, this, [this](auto* item) {
    const auto path = imagePath(item->data(DecalIdRole).toString().toStdString());
    if (!path.empty())
    {
      ui::ImageViewerDialog::showImage(path, this);
    }
  });
  m_list->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(
    m_list,
    &QListWidget::customContextMenuRequested,
    this,
    &UnityDecalBrowser::showContextMenu);
  setAcceptDrops(true);

  auto& map = m_document.map();
  m_notifierConnection +=
    map.decalLibraryDidChangeNotifier.connect([this]() { reloadLibrary(); });
  m_notifierConnection +=
    map.mapWasLoadedNotifier.connect([this](auto&) { reloadLibrary(); });
  m_notifierConnection +=
    map.mapWasCreatedNotifier.connect([this](auto&) { reloadLibrary(); });

  reloadLibrary();
}

void UnityDecalBrowser::createGui()
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
  auto* importButton = new QPushButton{tr("Import...")};
  importButton->setToolTip(tr(
    "Copy images into the Unity project as decals; dropping images here does the "
    "same"));
  connect(importButton, &QPushButton::clicked, this, [this]() {
    importDecalsInteractively(
      this, chooseImageFiles(this, tr("Import Decals into Unity")), currentCategory());
  });

  controls->addWidget(m_searchBox, 1);
  controls->addWidget(m_categoryCombo, 1);
  controls->addWidget(importButton);

  m_list = new QListWidget{};
  m_list->setViewMode(QListView::IconMode);
  m_list->setIconSize(QSize{ThumbnailSize, ThumbnailSize});
  m_list->setGridSize(QSize{ThumbnailSize + 24, ThumbnailSize + 36});
  m_list->setResizeMode(QListView::Adjust);
  m_list->setMovement(QListView::Static);
  m_list->setWordWrap(true);
  m_list->setUniformItemSizes(true);
  m_list->setSelectionMode(QAbstractItemView::SingleSelection);

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

void UnityDecalBrowser::reloadLibrary()
{
  m_atlasImages.clear();

  const auto& map = m_document.map();
  if (!map.game() || !usesUnityEntityDefinitions(map.game()->config()))
  {
    showMessage(tr(
      "Decals need a Unity project and Entity definitions from Unity, both in the Unity "
      "preferences."));
    rebuildCategories();
    return;
  }

  const auto& library = map.decalLibrary();
  for (const auto& decal : library.decals())
  {
    if (!m_atlasImages.contains(decal.atlas))
    {
      const auto* path = library.atlasImage(decal.atlas);
      m_atlasImages.emplace(
        decal.atlas, path ? QImage{io::pathAsQString(*path)} : QImage{});
    }
  }

  rebuildCategories();
  if (!library.decals().empty())
  {
    m_stack->setCurrentWidget(m_list);
  }
  else
  {
    showMessage(tr(
      "The Unity catalog has no decals. Create a Decal Library asset in Unity (Junk > "
      "Decal Library) and export the catalog."));
  }
}

void UnityDecalBrowser::rebuildCategories()
{
  const auto current = m_categoryCombo->currentData().toString();

  auto categories = std::set<std::string>{};
  for (const auto& decal : m_document.map().decalLibrary().decals())
  {
    categories.insert(decal.category);
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

void UnityDecalBrowser::rebuildList()
{
  const auto search = m_searchBox->text().trimmed();
  const auto category = m_categoryCombo->currentData().toString().toStdString();

  m_list->clear();
  for (const auto& decal : m_document.map().decalLibrary().decals())
  {
    if (!category.empty() && decal.category != category)
    {
      continue;
    }
    if (!search.isEmpty() && !matchesSearch(decal, search))
    {
      continue;
    }

    const auto atlas = m_atlasImages.find(decal.atlas);
    auto* item = new QListWidgetItem{
      QIcon{thumbnail(atlas != m_atlasImages.end() ? atlas->second : QImage{}, decal)},
      QString::fromStdString(decal.displayName)};
    item->setData(DecalIdRole, QString::fromStdString(decal.id));
    item->setToolTip(QString::fromStdString(decal.id));
    m_list->addItem(item);
  }
}

void UnityDecalBrowser::showMessage(const QString& message)
{
  m_messageLabel->setText(message);
  m_stack->setCurrentWidget(m_messageLabel);
}

std::filesystem::path UnityDecalBrowser::imagePath(const std::string& decalId) const
{
  const auto& library = m_document.map().decalLibrary();
  const auto* decal = library.decal(decalId);
  if (!decal)
  {
    return {};
  }
  if (!decal->source.empty())
  {
    return decal->source;
  }
  const auto* atlas = library.atlasImage(decal->atlas);
  return atlas ? *atlas : std::filesystem::path{};
}

std::string UnityDecalBrowser::currentCategory() const
{
  return m_categoryCombo->currentData().toString().toStdString();
}

void UnityDecalBrowser::showContextMenu(const QPoint& position)
{
  auto menu = QMenu{this};
  if (const auto* item = m_list->itemAt(position))
  {
    const auto decalId = item->data(DecalIdRole).toString().toStdString();
    const auto path = imagePath(decalId);
    const auto* decal = m_document.map().decalLibrary().decal(decalId);

    menu.addAction(tr("Stamp"), this, [this, decalId]() {
      emit decalStampRequested(QString::fromStdString(decalId));
    });
    menu.addSeparator();
    auto* view = menu.addAction(tr("View Image"), this, [this, path]() {
      ui::ImageViewerDialog::showImage(path, this);
    });
    view->setEnabled(!path.empty());
    auto* edit = menu.addAction(tr("Open in Image Editor"), this, [path]() {
      ui::openInExternalImageEditor(path);
    });
    edit->setEnabled(!path.empty());
    auto* show = menu.addAction(
      tr("Show in Folder"), this, [path]() { ui::showInFileBrowser(path); });
    show->setEnabled(!path.empty());
    menu.addSeparator();

    auto* remove = menu.addAction(
      tr("Remove from Unity Project..."),
      this,
      [this, source = decal ? decal->source : std::filesystem::path{}]() {
        removeAssetInteractively(this, source, tr("Decal"));
      });
    remove->setEnabled(
      decal && !decal->source.empty() && isInUnityProject(decal->source));
    if (decal && decal->source.empty())
    {
      remove->setToolTip(
        tr("This decal is cut from a hand-made atlas; edit its Decal Library in Unity"));
    }
  }

  menu.addAction(tr("Import Decals into Unity..."), this, [this]() {
    importDecalsInteractively(
      this, chooseImageFiles(this, tr("Import Decals into Unity")), currentCategory());
  });
  menu.exec(m_list->viewport()->mapToGlobal(position));
}

void UnityDecalBrowser::dragEnterEvent(QDragEnterEvent* event)
{
  if (!droppedImageFiles(*event->mimeData()).empty())
  {
    event->acceptProposedAction();
  }
}

void UnityDecalBrowser::dropEvent(QDropEvent* event)
{
  const auto files = droppedImageFiles(*event->mimeData());
  if (!files.empty())
  {
    event->acceptProposedAction();
    importDecalsInteractively(this, files, currentCategory());
  }
}

void UnityDecalBrowser::itemClicked(QListWidgetItem* item)
{
  emit decalStampRequested(item->data(DecalIdRole).toString());
}

} // namespace tb::unity
