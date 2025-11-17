#include "ui/MaterialWadFilter.h"

#include "PreferenceManager.h"
#include "Preferences.h"
#include "mdl/EntityProperties.h"
#include "mdl/Map.h"
#include "mdl/Map_Assets.h"
#include "mdl/WorldNode.h"
#include "ui/MapDocument.h"

#include "kdl/string_utils.h"
#include "kdl/vector_utils.h"

#include <utility>

namespace tb::ui
{

MaterialWadFilter::MaterialWadFilter(MapDocument& document, QObject* parent)
  : QObject{parent}
  , m_document{document}
{
  loadHiddenFromPreferences();
  reloadCollections();
  connectObservers();
}

const std::vector<std::filesystem::path>& MaterialWadFilter::wadCollections() const
{
  return m_wadCollections;
}

const std::vector<std::filesystem::path>& MaterialWadFilter::hiddenWads() const
{
  return m_hiddenWads;
}

bool MaterialWadFilter::isWadHidden(const std::filesystem::path& wad) const
{
  return kdl::vec_contains(m_hiddenWads, wad);
}

void MaterialWadFilter::setWadHidden(const std::filesystem::path& wad, const bool hidden)
{
  auto updated = m_hiddenWads;

  if (hidden)
  {
    if (!kdl::vec_contains(updated, wad))
    {
      updated.push_back(wad);
    }
  }
  else
  {
    updated = kdl::vec_erase_if(std::move(updated), [&](const auto& entry) {
      return entry == wad;
    });
  }

  updated = kdl::vec_sort_and_remove_duplicates(std::move(updated));

  if (updated == m_hiddenWads)
  {
    return;
  }

  m_hiddenWads = updated;

  setPref(Preferences::MaterialBrowserHiddenWads, m_hiddenWads);
  emit hiddenWadsChanged();
}

void MaterialWadFilter::connectObservers()
{
  auto& map = m_document.map();
  m_notifierConnection += map.mapWasCreatedNotifier.connect(this, &MaterialWadFilter::reloadCollections);
  m_notifierConnection += map.mapWasLoadedNotifier.connect(this, &MaterialWadFilter::reloadCollections);
  m_notifierConnection += map.materialCollectionsDidChangeNotifier.connect(
    this, &MaterialWadFilter::reloadCollections);

  auto& prefs = PreferenceManager::instance();
  m_notifierConnection += prefs.preferenceDidChangeNotifier.connect(
    this, &MaterialWadFilter::preferenceDidChange);
}

void MaterialWadFilter::reloadCollections()
{
  const auto collections = gatherWadCollections();
  if (collections != m_wadCollections)
  {
    m_wadCollections = collections;
    emit wadCollectionsChanged();
  }
}

void MaterialWadFilter::loadHiddenFromPreferences()
{
  auto hidden = pref(Preferences::MaterialBrowserHiddenWads);
  hidden = kdl::vec_sort_and_remove_duplicates(std::move(hidden));
  if (hidden != m_hiddenWads)
  {
    m_hiddenWads = std::move(hidden);
    emit hiddenWadsChanged();
  }
}

void MaterialWadFilter::preferenceDidChange(const std::filesystem::path& path)
{
  if (path == Preferences::MaterialBrowserHiddenWads.path())
  {
    loadHiddenFromPreferences();
  }
}

std::vector<std::filesystem::path> MaterialWadFilter::gatherWadCollections() const
{
  auto wadCollections = std::vector<std::filesystem::path>{};

  const auto& map = m_document.map();
  if (const auto* world = map.world())
  {
    if (const auto* wadValue = world->entity().property(mdl::EntityPropertyKeys::Wad))
    {
      for (const auto& wad : kdl::str_split(*wadValue, ";"))
      {
        if (!wad.empty())
        {
          wadCollections.emplace_back(wad);
        }
      }
    }
  }

  const auto enabledCollections = mdl::enabledMaterialCollections(map);
  wadCollections.insert(wadCollections.end(), enabledCollections.begin(), enabledCollections.end());

  return kdl::vec_sort_and_remove_duplicates(std::move(wadCollections));
}

} // namespace tb::ui
