#include "ui/PrefabInspector.h"

#include <QDir>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "mdl/Map_CopyPaste.h"
#include "mdl/Map.h"
#include "mdl/Selection.h"
#include "mdl/SelectionChange.h"
#include "ui/MapDocument.h"
#include "ui/PrefabBrowser.h"
#include "ui/PrefabLibrary.h"
#include "ui/ViewConstants.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace tb::ui
{

PrefabInspector::PrefabInspector(MapDocument& document, QWidget* parent)
  : TabBookPage{parent}
  , m_document{document}
{
  createGui();
  connectObservers();
  updateActions();
}

PrefabInspector::~PrefabInspector() = default;

void PrefabInspector::createGui()
{
  m_descriptionLabel = new QLabel{
    tr("Prefabs are reusable collections of brushes saved next to the map file.")};
  m_descriptionLabel->setWordWrap(true);

  m_browser = new PrefabBrowser{m_document};

  m_createButton = new QPushButton{tr("Create Prefab From Selection")};
  connect(m_createButton, &QPushButton::clicked, this, &PrefabInspector::createPrefabFromSelection);

  auto* buttonRow = new QHBoxLayout{};
  buttonRow->setContentsMargins(
    LayoutConstants::NarrowHMargin,
    LayoutConstants::NarrowVMargin,
    LayoutConstants::NarrowHMargin,
    LayoutConstants::NarrowVMargin);
  buttonRow->setSpacing(LayoutConstants::NarrowHMargin);
  buttonRow->addStretch(1);
  buttonRow->addWidget(m_createButton, 0);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(LayoutConstants::NarrowVMargin);
  layout->addWidget(m_descriptionLabel, 0);
  layout->addWidget(m_browser, 1);
  layout->addLayout(buttonRow);

  setLayout(layout);
}

void PrefabInspector::connectObservers()
{
  auto& map = m_document.map();
  m_notifierConnection += map.selectionDidChangeNotifier.connect(
    this, &PrefabInspector::selectionDidChange);
  m_notifierConnection +=
    map.mapWasClearedNotifier.connect(this, &PrefabInspector::mapWasCleared);
  m_notifierConnection +=
    m_document.prefabLibrary().availabilityDidChangeNotifier.connect(
      this, &PrefabInspector::libraryAvailabilityChanged);
}

void PrefabInspector::libraryAvailabilityChanged(bool)
{
  selectionDidChange(mdl::SelectionChange{});
}

void PrefabInspector::selectionDidChange(const mdl::SelectionChange&)
{
  updateActions();
}

void PrefabInspector::mapWasCleared(mdl::Map&)
{
  updateActions();
}

void PrefabInspector::updateActions()
{
  const auto hasSelection = m_document.map().selection().hasNodes();
  const auto prefabsEnabled = m_document.prefabLibrary().enabled();

  if (m_createButton)
  {
    m_createButton->setEnabled(prefabsEnabled && hasSelection);
  }
}

void PrefabInspector::createPrefabFromSelection()
{
  if (!m_createButton || !m_createButton->isEnabled())
  {
    return;
  }

  auto name = QInputDialog::getText(
    this, tr("Create Prefab"), tr("Prefab name:"), QLineEdit::Normal);

  name = name.trimmed();
  if (name.isEmpty())
  {
    return;
  }

  static const auto invalidChars = QStringLiteral(R"(\/:*?"<>|)");
  for (const auto ch : invalidChars)
  {
    if (name.contains(ch))
    {
      QMessageBox::warning(
        this,
        tr("Invalid Name"),
        tr("The prefab name cannot contain any of the following characters: %1")
          .arg(invalidChars));
      return;
    }
  }

  const auto path = m_document.prefabLibrary().prefabPathForName(name.toStdString());
  if (path.empty())
  {
    QMessageBox::warning(
      this,
      tr("Save Required"),
      tr("Save the map before creating prefabs."));
    return;
  }

  if (std::filesystem::exists(path))
  {
    const auto response = QMessageBox::question(
      this,
      tr("Overwrite Prefab"),
      tr("A prefab named '%1' already exists. Overwrite it?").arg(name));
    if (response != QMessageBox::Yes)
    {
      return;
    }
  }

  const auto prefabData = mdl::serializeSelectedNodes(m_document.map());
  if (prefabData.empty())
  {
    QMessageBox::warning(
      this,
      tr("Empty Selection"),
      tr("Select at least one brush or entity before creating a prefab."));
    return;
  }

  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec)
  {
    QMessageBox::critical(
      this,
      tr("Unable to create folder"),
      tr("Could not create the prefab directory:\n%1")
        .arg(QDir::toNativeSeparators(QString::fromStdString(path.parent_path().string()))));
    return;
  }

  auto stream = std::ofstream{path, std::ios::binary | std::ios::trunc};
  if (!stream)
  {
    QMessageBox::critical(
      this,
      tr("Write Error"),
      tr("Could not open '%1' for writing.")
        .arg(QDir::toNativeSeparators(QString::fromStdString(path.string()))));
    return;
  }
  stream << prefabData;
  stream.close();
  if (!stream)
  {
    QMessageBox::critical(
      this,
      tr("Write Error"),
      tr("Failed to finish writing '%1'.")
        .arg(QDir::toNativeSeparators(QString::fromStdString(path.string()))));
    return;
  }

  m_document.prefabLibrary().refresh();
  if (m_browser)
  {
    m_browser->refresh();
  }
}

} // namespace tb::ui
