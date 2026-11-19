/*
 Copyright (C) 2026 TrenchBroom Contributors

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

#include "Macros.h"
#include "render/GL.h"

#include "vm/bbox.h"
#include "vm/mat.h"
#include "vm/vec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace tb::mdl
{
class Entity;
class Map;
} // namespace tb::mdl

namespace tb::render
{
class MaterialRenderer;
class RenderBatch;
class RenderContext;

inline constexpr size_t MaxPreviewLights = 256;
inline constexpr float DefaultPreviewLightUnitScale = 0.03125f;

inline constexpr size_t MaxPreviewShadowLights = 16;
inline constexpr int PreviewShadowTileSize = 256;
inline constexpr int PreviewShadowAtlasCols = 12;
inline constexpr int PreviewShadowAtlasRows = 8;
inline constexpr int PreviewShadowAtlasWidth =
  PreviewShadowTileSize * PreviewShadowAtlasCols;
inline constexpr int PreviewShadowAtlasHeight =
  PreviewShadowTileSize * PreviewShadowAtlasRows;

struct PreviewLight
{
  vm::vec3f position;
  float range = 0.0f;
  vm::vec3f color = vm::vec3f{1.0f, 1.0f, 1.0f};
  float intensity = 0.0f;
  int shadows = 2;
  bool selected = false;
  int shadowSlot = -1;
};

struct PreviewShadowSlot
{
  bool occupied = false;
  vm::vec3f position{};
  float range = 0.0f;
  uint64_t geometryGeneration = 0;
};

struct PreviewShadowFaceUV
{
  int face = 0;
  float u = 0.0f;
  float v = 0.0f;
};

struct PreviewShadowTileRect
{
  int x = 0;
  int y = 0;
  int width = PreviewShadowTileSize;
  int height = PreviewShadowTileSize;
};

struct PreviewShadowModelCaster
{
  vm::mat4x4f modelMatrix;
  vm::bbox3f worldBounds;
  MaterialRenderer* renderer = nullptr;
};

bool isRealtimePreviewLightClassname(std::string_view classname);

std::optional<PreviewLight> extractPreviewLight(
  const mdl::Entity& entity,
  float unityScaleFactor = DefaultPreviewLightUnitScale,
  bool selected = false);

std::vector<PreviewLight> selectActivePreviewLights(
  const std::vector<PreviewLight>& lights,
  const vm::vec3f& cameraPosition,
  size_t maxLights = MaxPreviewLights);

float encodePreviewShadowMeta(int shadowSlot, int shadowMode);

std::vector<int> assignPreviewShadowSlots(
  std::vector<PreviewLight>& activeLights,
  std::array<PreviewShadowSlot, MaxPreviewShadowLights>& cacheSlots,
  const vm::vec3f& cameraPosition,
  uint64_t geometryGeneration,
  bool shadowsEnabled);

vm::mat4x4f previewShadowFaceViewProjection(
  const vm::vec3f& lightPos, float lightRange, int face);

PreviewShadowFaceUV previewShadowDirectionToFaceUV(const vm::vec3f& direction);

PreviewShadowTileRect previewShadowTileRect(int slot, int face);

std::vector<float> packPreviewLightTextureData(const std::vector<PreviewLight>& lights);

class PreviewLightRenderer
{
private:
  mdl::Map& m_map;

  std::vector<PreviewLight> m_lights;
  bool m_lightsDirty = true;

  std::vector<vm::vec3f> m_shadowCasterVertices;
  size_t m_shadowBrushVertexCount = 0;
  size_t m_shadowPatchVertexCount = 0;
  std::vector<PreviewShadowModelCaster> m_shadowModelCasters;
  bool m_geometryDirty = true;
  bool m_shadowCasterVboDirty = true;
  uint64_t m_geometryGeneration = 1;

  std::array<PreviewShadowSlot, MaxPreviewShadowLights> m_shadowSlots{};

  GLuint m_textureId = 0;
  GLuint m_shadowFboId = 0;
  GLuint m_shadowAtlasTextureId = 0;
  GLuint m_shadowCasterVboId = 0;

public:
  explicit PreviewLightRenderer(mdl::Map& map);
  ~PreviewLightRenderer();

  deleteCopyAndMove(PreviewLightRenderer);

  void invalidateLights();
  void invalidateGeometry();
  void invalidateAll();
  void clear();

  void prepare(RenderContext& renderContext, RenderBatch& renderBatch);
  void renderGizmos(RenderContext& renderContext, RenderBatch& renderBatch);

private:
  void rebuildLightCache();
  void rebuildShadowGeometryCache();
};

} // namespace tb::render
