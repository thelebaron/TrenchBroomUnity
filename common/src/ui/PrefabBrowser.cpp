#include "ui/PrefabBrowser.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QLocale>
#include <QMimeData>

#include "ui/MapDocument.h"
#include "ui/PrefabLibrary.h"
#include "ui/QtUtils.h"
#include "ui/ViewConstants.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace
{
QString formatTimestamp(const std::chrono::system_clock::time_point& timePoint)
{
  const auto seconds =
    std::chrono::duration_cast<std::chrono::seconds>(timePoint.time_since_epoch());
  const auto dateTime = QDateTime::fromSecsSinceEpoch(qint64(seconds.count()));
  return QLocale::system().toString(dateTime, QLocale::ShortFormat);
}

QString toQString(const std::filesystem::path& path)
{
#if defined(_WIN32)
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromStdString(path.string());
#endif
}

QString toDisplayPath(const std::filesystem::path& path)
{
  return QDir::toNativeSeparators(toQString(path));
}
} // namespace

namespace tb::ui
{

PrefabListWidget::PrefabListWidget(QWidget* parent)
  : QListWidget{parent}
{
  setSelectionMode(QAbstractItemView::SingleSelection);
  setDragEnabled(true);
  setUniformItemSizes(true);
  setAlternatingRowColors(true);
}

QMimeData* PrefabListWidget::mimeData(const QList<QListWidgetItem*> items) const
{
  if (items.isEmpty())
  {
    return nullptr;
  }

  const auto path = items.first()->data(Qt::UserRole).toString();
  if (path.isEmpty())
  {
    return nullptr;
  }

  const auto url = QUrl::fromLocalFile(path);
  auto* mime = new QMimeData{};
  mime->setText(QStringLiteral("prefab:%1").arg(url.toString(QUrl::FullyEncoded)));
  return mime;
}

void PrefabListWidget::startDrag(Qt::DropActions supportedActions)
{
  auto selected = selectedItems();
  if (selected.isEmpty())
  {
    return;
  }

  auto* data = mimeData(selected);
  if (data == nullptr)
  {
    return;
  }

  auto* drag = new QDrag{this};
  drag->setMimeData(data);
  drag->exec(Qt::CopyAction & supportedActions, Qt::CopyAction);
}

PrefabBrowser::PrefabBrowser(MapDocument& document, QWidget* parent)
  : QWidget{parent}
  , m_document{document}
  , m_library{document.prefabLibrary()}
{
  createGui();
  connectSignals();

  updateAvailability(m_library.enabled());
  entriesChanged(m_library.entries());
}

PrefabBrowser::~PrefabBrowser() = default;

void PrefabBrowser::refresh()
{
  m_library.refresh();
}

void PrefabBrowser::createGui()
{
  m_filterBox = createSearchBox();
  m_refreshButton = new QPushButton{tr("Refresh")};
  m_openFolderButton = new QPushButton{tr("Open Folder")};

  auto* controlsLayout = new QHBoxLayout{};
  controlsLayout->setContentsMargins(
    LayoutConstants::NarrowHMargin,
    LayoutConstants::NarrowVMargin,
    LayoutConstants::NarrowHMargin,
    LayoutConstants::NarrowVMargin);
  controlsLayout->setSpacing(LayoutConstants::NarrowHMargin);
  controlsLayout->addWidget(m_filterBox, 1);
  controlsLayout->addWidget(m_refreshButton, 0);
  controlsLayout->addWidget(m_openFolderButton, 0);

  m_listWidget = new PrefabListWidget{};
  m_emptyLabel = new QLabel{tr("No prefabs found in this map's prefab folder.")};
  m_emptyLabel->setAlignment(Qt::AlignCenter);
  m_emptyLabel->setVisible(false);

  auto* browserLayout = new QVBoxLayout{};
  browserLayout->setContentsMargins(0, 0, 0, 0);
  browserLayout->setSpacing(LayoutConstants::NarrowVMargin);
  browserLayout->addWidget(m_listWidget, 1);
  browserLayout->addWidget(m_emptyLabel, 0, Qt::AlignCenter);

  auto* browserPanel = new QWidget{};
  browserPanel->setLayout(browserLayout);

  m_disabledLabel = new QLabel{
    tr("Save the map to enable prefab creation and browsing.")};
  m_disabledLabel->setAlignment(Qt::AlignCenter);
  m_disabledLabel->setWordWrap(true);
  m_disabledLabel->setMargin(LayoutConstants::NarrowHMargin);

  m_stack = new QStackedWidget{};
  m_stack->addWidget(m_disabledLabel); // index 0
  m_stack->addWidget(browserPanel);    // index 1

  m_locationLabel = new QLabel{};
  m_locationLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
  m_locationLabel->setOpenExternalLinks(false);

  auto* outerLayout = new QVBoxLayout{};
  outerLayout->setContentsMargins(0, 0, 0, 0);
  outerLayout->setSpacing(0);
  outerLayout->addLayout(controlsLayout, 0);
  outerLayout->addWidget(m_stack, 1);
  outerLayout->addWidget(m_locationLabel, 0);

  setLayout(outerLayout);
}

void PrefabBrowser::connectSignals()
{
  connect(m_filterBox, &QLineEdit::textChanged, this, &PrefabBrowser::rebuildList);
  connect(m_refreshButton, &QPushButton::clicked, this, &PrefabBrowser::refresh);
  connect(
    m_openFolderButton, &QPushButton::clicked, this, &PrefabBrowser::openPrefabFolder);

  connect(m_listWidget, &QListWidget::itemDoubleClicked, this, [this](auto* item) {
    if (!item)
    {
      return;
    }

    const auto path = item->data(Qt::UserRole).toString();
    if (!path.isEmpty())
    {
      emit prefabActivated(std::filesystem::path{path.toStdString()});
    }
  });

  m_notifierConnection += m_library.entriesDidChangeNotifier.connect(
    this, &PrefabBrowser::entriesChanged);
  m_notifierConnection += m_library.availabilityDidChangeNotifier.connect(
    this, &PrefabBrowser::updateAvailability);
}

void PrefabBrowser::updateAvailability(const bool enabled)
{
  if (m_stack == nullptr)
  {
    return;
  }

  m_stack->setCurrentIndex(enabled ? 1 : 0);
  m_filterBox->setEnabled(enabled);
  m_refreshButton->setEnabled(enabled);
  m_openFolderButton->setEnabled(enabled);

  updateLocationLabel();

  if (enabled)
  {
    rebuildList();
  }
}

void PrefabBrowser::entriesChanged(const std::vector<PrefabEntry>& entries)
{
  m_entries = entries;
  rebuildList();
  updateLocationLabel();
}

void PrefabBrowser::rebuildList()
{
  if (m_listWidget == nullptr)
  {
    return;
  }

  const auto filter = filterText().toLower();
  const auto selectedItems = m_listWidget->selectedItems();
  const auto previousSelection =
    selectedItems.isEmpty() ? QString{} : selectedItems.first()->data(Qt::UserRole).toString();

  m_listWidget->clear();

  for (const auto& entry : m_entries)
  {
    const auto name = QString::fromStdString(entry.name);
    if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive))
    {
      continue;
    }

    auto* item = new QListWidgetItem(name, m_listWidget);
    const auto absolutePath = toQString(entry.path);
    item->setData(Qt::UserRole, absolutePath);
    item->setToolTip(
      tr("%1\nLast modified: %2")
        .arg(toDisplayPath(entry.path), formatTimestamp(entry.lastModified)));
  }

  if (!previousSelection.isEmpty())
  {
    const auto matches = m_listWidget->findItems(previousSelection, Qt::MatchExactly);
    if (!matches.isEmpty())
    {
      m_listWidget->setCurrentItem(matches.first());
    }
  }

  m_emptyLabel->setVisible(m_listWidget->count() == 0);
}

void PrefabBrowser::updateLocationLabel()
{
  if (!m_locationLabel)
  {
    return;
  }

  if (!m_library.enabled())
  {
    m_locationLabel->setText(
      tr("Prefabs are stored next to the map once it has been saved."));
    return;
  }

  const auto root = m_library.prefabRoot();
  if (root.empty())
  {
    m_locationLabel->clear();
    return;
  }

  m_locationLabel->setText(tr("Prefabs folder: %1").arg(toDisplayPath(root)));
}

void PrefabBrowser::openPrefabFolder()
{
  if (!m_library.enabled())
  {
    return;
  }

  const auto root = m_library.prefabRoot();
  if (root.empty())
  {
    return;
  }

  QDesktopServices::openUrl(QUrl::fromLocalFile(toQString(root)));
}

QString PrefabBrowser::filterText() const
{
  return m_filterBox != nullptr ? m_filterBox->text() : QString{};
}

} // namespace tb::ui
