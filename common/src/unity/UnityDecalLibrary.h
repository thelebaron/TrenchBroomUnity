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

#pragma once

#include "mdl/TextureResource.h"
#include "unity/UnityCatalog.h"
#include "unity/UnityDecals.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tb
{
class Logger;
}

namespace tb::mdl
{
class Material;
}

namespace tb::unity
{

/**
 * The decals of the Unity catalog with their atlases loaded as materials, for previewing
 * and stamping decal entities. Reloaded with the catalog; it never changes the map.
 */
class UnityDecalLibrary
{
public:
  using AddTextureResource = std::function<void(std::shared_ptr<mdl::TextureResource>)>;

private:
  struct Atlas;

  std::vector<UnityCatalogDecal> m_decals;
  std::unordered_map<std::string, size_t> m_decalIndices;
  std::unordered_map<std::string, std::unique_ptr<Atlas>> m_atlases;
  std::unordered_map<std::string, std::filesystem::path> m_atlasImages;
  // Tinted, brightened or inverted copies of atlases, made on first use.
  mutable std::unordered_map<std::string, std::unique_ptr<mdl::Material>> m_variants;
  AddTextureResource m_addTextureResource;

public:
  UnityDecalLibrary();
  ~UnityDecalLibrary();

  /**
   * Replaces the library with the catalog's decals. Atlases whose images can't be read
   * are logged and their decals have no material.
   */
  void load(
    const UnityCatalog& catalog, AddTextureResource addTextureResource, Logger& logger);
  void clear();

  const std::vector<UnityCatalogDecal>& decals() const;
  const UnityCatalogDecal* decal(const std::string& id) const;
  const mdl::Material* atlasMaterial(const std::string& atlasId) const;
  /**
   * The atlas with the appearance applied: the plain atlas for the default appearance,
   * else a copy made the first time it is asked for.
   */
  const mdl::Material* atlasMaterial(
    const std::string& atlasId, const DecalAppearance& appearance) const;
  /**
   * The atlas image on disk, for thumbnails in the decal browser.
   */
  const std::filesystem::path* atlasImage(const std::string& atlasId) const;
};

} // namespace tb::unity
