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

#include "vm/vec.h"

#include <cstddef>
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
class RenderBatch;
class RenderContext;

inline constexpr size_t MaxPreviewLights = 256;
inline constexpr float DefaultPreviewLightUnitScale = 0.03125f;

struct PreviewLight
{
  vm::vec3f position;
  float range = 0.0f;
  vm::vec3f color = vm::vec3f{1.0f, 1.0f, 1.0f};
  float intensity = 0.0f;
  int shadows = 2;
  bool selected = false;
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

std::vector<float> packPreviewLightTextureData(const std::vector<PreviewLight>& lights);

class PreviewLightRenderer
{
private:
  mdl::Map& m_map;
  std::vector<PreviewLight> m_lights;
  bool m_dirty = true;
  GLuint m_textureId = 0;

public:
  explicit PreviewLightRenderer(mdl::Map& map);
  ~PreviewLightRenderer();

  deleteCopyAndMove(PreviewLightRenderer);

  void invalidate();
  void clear();

  void prepare(RenderContext& renderContext, RenderBatch& renderBatch);
  void renderGizmos(RenderContext& renderContext, RenderBatch& renderBatch);

private:
  void rebuildLightCache();
};

} // namespace tb::render
