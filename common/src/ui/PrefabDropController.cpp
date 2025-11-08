#include "ui/PrefabDropController.h"

#include <QUrl>
#include <QString>

#include "mdl/Map.h"
#include "mdl/Map_CopyPaste.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_NodeVisibility.h"
#include "mdl/Map_Selection.h"
#include "mdl/PasteType.h"
#include "mdl/Transaction.h"
#include "ui/DropTracker.h"
#include "ui/MapDocument.h"
#include "ui/MapViewBase.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace tb::ui
{
namespace
{
constexpr auto PrefabPrefix = std::string_view{"prefab:"};
}

class PrefabDropController::PrefabDropTracker : public DropTracker
{
private:
  PrefabDropController& m_controller;
  std::string m_prefabSource;

public:
  PrefabDropTracker(PrefabDropController& controller, std::string prefabSource)
    : m_controller{controller}
    , m_prefabSource{std::move(prefabSource)}
  {
  }

  bool move(const InputState&) override { return true; }

  bool drop(const InputState& inputState) override
  {
    return m_controller.insertPrefab(m_prefabSource, inputState);
  }

  void leave(const InputState&) override {}
};

PrefabDropController::PrefabDropController(MapDocument& document, MapViewBase& mapView)
  : Tool{true}
  , m_document{document}
  , m_mapView{mapView}
{
}

Tool& PrefabDropController::tool()
{
  return *this;
}

const Tool& PrefabDropController::tool() const
{
  return *this;
}

bool PrefabDropController::shouldAcceptDrop(
  const InputState&, const std::string& payload) const
{
  return payload.rfind(PrefabPrefix, 0) == 0;
}

std::unique_ptr<DropTracker> PrefabDropController::acceptDrop(
  const InputState&, const std::string& payload)
{
  const auto path = decodePayload(payload);
  if (!path)
  {
    return nullptr;
  }

  const auto prefabSource = loadPrefabSource(*path);
  if (!prefabSource || prefabSource->empty())
  {
    return nullptr;
  }

  return std::unique_ptr<DropTracker>{new PrefabDropTracker{*this, *prefabSource}};
}

bool PrefabDropController::cancel()
{
  return false;
}

std::optional<std::filesystem::path> PrefabDropController::decodePayload(
  const std::string& payload) const
{
  if (payload.rfind(PrefabPrefix, 0) != 0)
  {
    return std::nullopt;
  }

  const auto encoded = QString::fromStdString(payload.substr(PrefabPrefix.size()));
  const auto url = QUrl{encoded};
  if (!url.isValid())
  {
    return std::nullopt;
  }

  const auto localFile = url.toLocalFile();
  if (localFile.isEmpty())
  {
    return std::nullopt;
  }

#if defined(_WIN32)
  return std::filesystem::path{localFile.toStdWString()};
#else
  return std::filesystem::path{localFile.toStdString()};
#endif
}

std::optional<std::string> PrefabDropController::loadPrefabSource(
  const std::filesystem::path& path) const
{
  auto stream = std::ifstream{path, std::ios::binary};
  if (!stream)
  {
    m_document.error() << "Unable to open prefab '" << path << "'";
    return std::nullopt;
  }

  auto source = std::string{
    std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
  return source;
}

bool PrefabDropController::insertPrefab(
  const std::string& prefabContents, const InputState& /* inputState */) const
{
  auto& map = m_document.map();
  const auto referenceBounds = map.referenceBounds();
  auto transaction = mdl::Transaction{map, "Insert Prefab"};

  const auto pasteResult = mdl::paste(map, prefabContents);
  if (pasteResult != mdl::PasteType::Node)
  {
    m_document.error() << "Prefab does not contain any nodes that can be pasted";
    transaction.cancel();
    return false;
  }

  auto nodes = map.selection().nodes;
  const auto selectionBounds = map.selectionBounds();
  if (nodes.empty() || !selectionBounds)
  {
    transaction.commit();
    return true;
  }

  mdl::hideNodes(map, nodes);
  const auto delta = m_mapView.pasteObjectsDelta(*selectionBounds, referenceBounds);
  mdl::showNodes(map, nodes);
  mdl::selectNodes(map, nodes);
  if (!mdl::translateSelection(map, delta))
  {
    m_document.error() << "Failed to position prefab after insertion";
    transaction.cancel();
    return false;
  }

  transaction.commit();
  return true;
}

} // namespace tb::ui
