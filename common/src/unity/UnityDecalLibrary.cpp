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

#include "UnityDecalLibrary.h"

#include "Logger.h"
#include "io/DiskIO.h"
#include "io/File.h"
#include "io/ReadFreeImageTexture.h"
#include "mdl/Material.h"
#include "mdl/Texture.h"
#include "mdl/TextureBuffer.h"

#include "kdl/result.h"

#include <fmt/format.h>

#include <cstring>

namespace tb::unity
{

/**
 * An atlas's material, and a copy of its pixels to make appearance variants from: the
 * material's texture drops its pixels once it is uploaded.
 */
struct UnityDecalLibrary::Atlas
{
  std::unique_ptr<mdl::Material> material;
  size_t width;
  size_t height;
  GLenum format;
  mdl::TextureMask mask;
  std::vector<mdl::TextureBuffer> buffers;
};

namespace
{

std::vector<mdl::TextureBuffer> copyBuffers(
  const std::vector<mdl::TextureBuffer>& buffers)
{
  auto result = std::vector<mdl::TextureBuffer>{};
  for (const auto& buffer : buffers)
  {
    auto copy = mdl::TextureBuffer{buffer.size()};
    std::memcpy(copy.data(), buffer.data(), buffer.size());
    result.push_back(std::move(copy));
  }
  return result;
}

std::unique_ptr<mdl::Material> createMaterial(
  std::string name,
  mdl::Texture texture,
  const UnityDecalLibrary::AddTextureResource& addTextureResource)
{
  auto resource = mdl::createTextureResource(std::move(texture));
  addTextureResource(resource);

  auto material = std::make_unique<mdl::Material>(std::move(name), std::move(resource));
  material->setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  return material;
}

std::string variantKey(const std::string& atlasId, const DecalAppearance& appearance)
{
  return fmt::format(
    "{}|{} {} {}|{}|{}",
    atlasId,
    appearance.tint.x(),
    appearance.tint.y(),
    appearance.tint.z(),
    appearance.brightness,
    appearance.invert);
}

} // namespace

UnityDecalLibrary::UnityDecalLibrary() = default;
UnityDecalLibrary::~UnityDecalLibrary() = default;

void UnityDecalLibrary::load(
  const UnityCatalog& catalog, AddTextureResource addTextureResource, Logger& logger)
{
  clear();
  m_addTextureResource = std::move(addTextureResource);

  for (const auto& atlas : catalog.decalAtlases)
  {
    m_atlasImages.emplace(atlas.id, atlas.texture);

    io::Disk::openFile(atlas.texture) | kdl::and_then([](const auto& file) {
      auto reader = file->reader().buffer();
      return io::readFreeImageTexture(reader);
    }) | kdl::transform([&](auto texture) {
      auto entry = std::make_unique<Atlas>();
      entry->width = texture.width();
      entry->height = texture.height();
      entry->format = texture.format();
      entry->mask = texture.mask();
      entry->buffers = copyBuffers(texture.buffersIfLoaded());
      entry->material = createMaterial(
        "decal atlas " + atlas.id, std::move(texture), m_addTextureResource);
      m_atlases.emplace(atlas.id, std::move(entry));
    }) | kdl::transform_error([&](const auto& e) {
      logger.error() << "Could not load decal atlas '" << atlas.texture.string()
                     << "': " << e.msg;
    });
  }

  m_decals = catalog.decals;
  for (size_t i = 0; i < m_decals.size(); ++i)
  {
    m_decalIndices.emplace(m_decals[i].id, i);
  }
}

void UnityDecalLibrary::clear()
{
  m_decals.clear();
  m_decalIndices.clear();
  m_variants.clear();
  m_atlases.clear();
  m_atlasImages.clear();
}

const std::vector<UnityCatalogDecal>& UnityDecalLibrary::decals() const
{
  return m_decals;
}

const UnityCatalogDecal* UnityDecalLibrary::decal(const std::string& id) const
{
  const auto it = m_decalIndices.find(id);
  return it != m_decalIndices.end() ? &m_decals[it->second] : nullptr;
}

const mdl::Material* UnityDecalLibrary::atlasMaterial(const std::string& atlasId) const
{
  const auto it = m_atlases.find(atlasId);
  return it != m_atlases.end() ? it->second->material.get() : nullptr;
}

const mdl::Material* UnityDecalLibrary::atlasMaterial(
  const std::string& atlasId, const DecalAppearance& appearance) const
{
  const auto it = m_atlases.find(atlasId);
  if (it == m_atlases.end())
  {
    return nullptr;
  }
  if (appearance == DecalAppearance{})
  {
    return it->second->material.get();
  }

  const auto key = variantKey(atlasId, appearance);
  if (const auto variant = m_variants.find(key); variant != m_variants.end())
  {
    return variant->second.get();
  }

  const auto& atlas = *it->second;
  auto buffers = copyBuffers(atlas.buffers);
  const auto bgra = atlas.format == GL_BGRA;
  for (auto& buffer : buffers)
  {
    applyDecalAppearance(buffer.data(), buffer.size(), bgra, appearance);
  }

  auto texture = mdl::Texture{
    atlas.width,
    atlas.height,
    Color{1.0f, 1.0f, 1.0f, 1.0f},
    atlas.format,
    atlas.mask,
    mdl::NoEmbeddedDefaults{},
    std::move(buffers)};
  auto material =
    createMaterial("decal atlas " + key, std::move(texture), m_addTextureResource);
  const auto* result = material.get();
  m_variants.emplace(key, std::move(material));
  return result;
}

const std::filesystem::path* UnityDecalLibrary::atlasImage(
  const std::string& atlasId) const
{
  const auto it = m_atlasImages.find(atlasId);
  return it != m_atlasImages.end() ? &it->second : nullptr;
}

} // namespace tb::unity
