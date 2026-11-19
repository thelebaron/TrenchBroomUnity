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

#include "PreviewLighting.h"

#include "Color.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/PatchNode.h"
#include "mdl/Selection.h"
#include "mdl/WorldNode.h"
#include "render/Camera.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"
#include "render/RenderService.h"
#include "render/Renderable.h"

#include "kdl/overload.h"
#include "kdl/string_compare.h"
#include "kdl/string_format.h"
#include "kdl/string_utils.h"

#include "vm/util.h"
#include "vm/vec.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

namespace tb::render
{
namespace
{

constexpr float DefaultUnityIntensity = 1.0f;
constexpr float DefaultUnityRangeMeters = 10.0f;
constexpr float DefaultQuakeLight = 300.0f;
constexpr float DefaultQuakeWait = 1.0f;
constexpr int DefaultShadowsSoft = 2;
constexpr size_t LightRangeCircleSegments = 48;

std::optional<float> parseFloat(const std::string& value)
{
  auto stream = std::istringstream{value};
  stream.imbue(std::locale::classic());
  auto parsed = 0.0f;
  if (!(stream >> parsed))
  {
    return std::nullopt;
  }
  stream >> std::ws;
  if (!stream.eof() || !std::isfinite(parsed))
  {
    return std::nullopt;
  }
  return parsed;
}

std::optional<vm::vec3f> parseLightColor(const std::string& value)
{
  const auto tokens = kdl::str_split(value, " \t\r\n");
  if (tokens.size() != 3)
  {
    return std::nullopt;
  }

  auto channels = vm::vec3f{};
  for (size_t i = 0; i < 3; ++i)
  {
    const auto parsed = parseFloat(tokens[i]);
    if (!parsed)
    {
      return std::nullopt;
    }
    const auto clamped = std::clamp(std::trunc(*parsed), 0.0f, 255.0f);
    channels[i] = clamped / 255.0f;
  }
  return channels;
}

int parseLightShadows(const std::string& value)
{
  const auto trimmed = kdl::str_trim(value);
  if (trimmed == "0")
  {
    return 0;
  }
  if (trimmed == "1")
  {
    return 1;
  }
  if (trimmed == "2")
  {
    return 2;
  }
  return 0;
}

class UploadPreviewLightTexture : public Renderable
{
private:
  GLuint& m_textureId;
  std::vector<float> m_texels;

public:
  UploadPreviewLightTexture(GLuint& textureId, std::vector<float> texels)
    : m_textureId{textureId}
    , m_texels{std::move(texels)}
  {
  }

private:
  void doRender(RenderContext&) override
  {
    glAssert(glActiveTexture(GL_TEXTURE1));
    if (m_textureId == 0)
    {
      glAssert(glGenTextures(1, &m_textureId));
      glAssert(glBindTexture(GL_TEXTURE_2D, m_textureId));
      glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
      glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
      glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
      glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
      glAssert(glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA32F,
        static_cast<GLsizei>(MaxPreviewLights),
        2,
        0,
        GL_RGBA,
        GL_FLOAT,
        m_texels.data()));
    }
    else
    {
      glAssert(glBindTexture(GL_TEXTURE_2D, m_textureId));
      glAssert(glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        static_cast<GLsizei>(MaxPreviewLights),
        2,
        GL_RGBA,
        GL_FLOAT,
        m_texels.data()));
    }
    glAssert(glActiveTexture(GL_TEXTURE0));
  }
};

} // namespace

bool isRealtimePreviewLightClassname(const std::string_view classname)
{
  return kdl::ci::str_contains(classname, "light")
         && kdl::ci::str_contains(classname, "realtime");
}

std::optional<PreviewLight> extractPreviewLight(
  const mdl::Entity& entity, const float unityScaleFactor, const bool selected)
{
  if (!isRealtimePreviewLightClassname(entity.classname()))
  {
    return std::nullopt;
  }

  const auto effectiveScale =
    unityScaleFactor > 0.0f ? unityScaleFactor : DefaultPreviewLightUnitScale;

  auto light = PreviewLight{};
  light.position = vm::vec3f{entity.origin()};
  light.color = vm::vec3f{1.0f, 1.0f, 1.0f};
  light.shadows = DefaultShadowsSoft;
  light.selected = selected;

  const auto* intensityProp = entity.property("intensity");
  const auto* rangeProp = entity.property("range");

  if (intensityProp != nullptr || rangeProp != nullptr)
  {
    light.intensity = intensityProp
                        ? parseFloat(*intensityProp).value_or(DefaultUnityIntensity)
                        : DefaultUnityIntensity;
    const auto rangeMeters = rangeProp
                               ? parseFloat(*rangeProp).value_or(DefaultUnityRangeMeters)
                               : DefaultUnityRangeMeters;
    light.range = rangeMeters / effectiveScale;
  }
  else
  {
    const auto* quakeLightProp = entity.property("light");
    const auto* quakeWaitProp = entity.property("wait");

    const auto quakeLight = quakeLightProp
                              ? parseFloat(*quakeLightProp).value_or(DefaultQuakeLight)
                              : DefaultQuakeLight;
    auto quakeWait = quakeWaitProp ? parseFloat(*quakeWaitProp).value_or(DefaultQuakeWait)
                                   : DefaultQuakeWait;
    if (quakeWait <= 0.0f)
    {
      quakeWait = DefaultQuakeWait;
    }

    light.intensity = quakeLight / 256.0f;
    // Scopa computes range in Unity meters as (quakeLight / quakeWait) * scalingFactor;
    // converting back to TB units divides by scalingFactor, leaving quakeLight / quakeWait.
    light.range = quakeLight / quakeWait;
  }

  for (const auto& property : entity.properties())
  {
    const auto& key = property.key();
    if (kdl::ci::str_contains(key, "color"))
    {
      if (const auto parsedColor = parseLightColor(property.value()))
      {
        light.color = *parsedColor;
      }
    }
    if (kdl::ci::str_contains(key, "shadows"))
    {
      light.shadows = parseLightShadows(property.value());
    }
  }

  light.intensity = std::max(light.intensity, 0.0f);
  light.range = std::max(light.range, 0.0f);
  return light;
}

std::vector<PreviewLight> selectActivePreviewLights(
  const std::vector<PreviewLight>& lights,
  const vm::vec3f& cameraPosition,
  const size_t maxLights)
{
  auto active = std::vector<PreviewLight>{};
  active.reserve(lights.size());

  for (const auto& light : lights)
  {
    if (light.intensity > 0.0f && light.range > 0.0f)
    {
      active.push_back(light);
    }
  }

  if (active.size() <= maxLights)
  {
    return active;
  }

  const auto sortKey = [&](const PreviewLight& light) {
    const auto distToCenter = vm::distance(cameraPosition, light.position);
    const auto distToSphere = std::max(0.0f, distToCenter - light.range);
    return std::pair{distToSphere, distToCenter};
  };

  std::nth_element(
    active.begin(),
    active.begin() + static_cast<std::ptrdiff_t>(maxLights),
    active.end(),
    [&](const auto& lhs, const auto& rhs) { return sortKey(lhs) < sortKey(rhs); });
  active.resize(maxLights);
  return active;
}

std::vector<float> packPreviewLightTextureData(const std::vector<PreviewLight>& lights)
{
  auto texels = std::vector<float>(MaxPreviewLights * 2 * 4, 0.0f);
  const auto count = std::min(lights.size(), MaxPreviewLights);

  for (size_t i = 0; i < count; ++i)
  {
    const auto& light = lights[i];
    const auto row0 = i * 4;
    texels[row0 + 0] = light.position.x();
    texels[row0 + 1] = light.position.y();
    texels[row0 + 2] = light.position.z();
    texels[row0 + 3] = light.range;

    const auto row1 = (MaxPreviewLights + i) * 4;
    texels[row1 + 0] = light.color.x() * light.intensity;
    texels[row1 + 1] = light.color.y() * light.intensity;
    texels[row1 + 2] = light.color.z() * light.intensity;
    texels[row1 + 3] = static_cast<float>(light.shadows);
  }

  return texels;
}

PreviewLightRenderer::PreviewLightRenderer(mdl::Map& map)
  : m_map{map}
{
}

PreviewLightRenderer::~PreviewLightRenderer()
{
  if (m_textureId != 0)
  {
    glAssert(glDeleteTextures(1, &m_textureId));
    m_textureId = 0;
  }
}

void PreviewLightRenderer::invalidate()
{
  m_dirty = true;
}

void PreviewLightRenderer::clear()
{
  m_lights.clear();
  m_dirty = true;
}

void PreviewLightRenderer::prepare(
  RenderContext& renderContext, RenderBatch& renderBatch)
{
  if (renderContext.previewLighting())
  {
    if (m_dirty)
    {
      rebuildLightCache();
    }

    const auto activeLights =
      selectActivePreviewLights(m_lights, renderContext.camera().position());
    renderContext.setPreviewLightCount(static_cast<int>(activeLights.size()));

    auto texels = packPreviewLightTextureData(activeLights);
    renderBatch.addOneShot(new UploadPreviewLightTexture{m_textureId, std::move(texels)});
  }
  else
  {
    renderContext.setPreviewLightCount(0);
  }
}

void PreviewLightRenderer::renderGizmos(
  RenderContext& renderContext, RenderBatch& renderBatch)
{
  if (renderContext.hideSelection())
  {
    return;
  }

  const auto& selectedEntities = m_map.selection().entities;
  if (selectedEntities.empty())
  {
    return;
  }

  const auto& editorContext = m_map.editorContext();
  const auto unitScale = pref(Preferences::UnityLightUnitScale);

  auto renderService = RenderService{renderContext, renderBatch};
  renderService.setLineWidth(1.5f);
  renderService.setShowOccludedObjectsTransparent();

  for (const auto* entityNode : selectedEntities)
  {
    if (!entityNode || !editorContext.visible(*entityNode))
    {
      continue;
    }

    const auto light = extractPreviewLight(entityNode->entity(), unitScale, true);
    if (!light || light->range <= 0.0f)
    {
      continue;
    }

    const auto gizmoColor =
      Color{light->color.x(), light->color.y(), light->color.z(), 0.75f};
    renderService.setForegroundColor(gizmoColor);

    if (renderContext.render3D())
    {
      renderService.renderCircle(
        light->position, vm::axis::x, LightRangeCircleSegments, light->range);
      renderService.renderCircle(
        light->position, vm::axis::y, LightRangeCircleSegments, light->range);
      renderService.renderCircle(
        light->position, vm::axis::z, LightRangeCircleSegments, light->range);
    }
    else
    {
      const auto viewAxis = vm::find_abs_max_component(renderContext.camera().direction());
      renderService.renderCircle(
        light->position, viewAxis, LightRangeCircleSegments, light->range);
    }
  }
}

void PreviewLightRenderer::rebuildLightCache()
{
  m_lights.clear();
  m_dirty = false;

  const auto& editorContext = m_map.editorContext();
  const auto unitScale = pref(Preferences::UnityLightUnitScale);

  auto* worldNode = m_map.world();
  if (!worldNode)
  {
    return;
  }

  worldNode->accept(kdl::overload(
    [](auto&& thisLambda, const mdl::WorldNode* world) { world->visitChildren(thisLambda); },
    [](auto&& thisLambda, const mdl::LayerNode* layer) { layer->visitChildren(thisLambda); },
    [](auto&& thisLambda, const mdl::GroupNode* group) { group->visitChildren(thisLambda); },
    [&](const mdl::EntityNode* entityNode) {
      if (!editorContext.visible(*entityNode))
      {
        return;
      }
      if (
        auto light =
          extractPreviewLight(entityNode->entity(), unitScale, entityNode->selected()))
      {
        m_lights.push_back(*light);
      }
    },
    [](const mdl::BrushNode*) {},
    [](const mdl::PatchNode*) {}));
}

} // namespace tb::render
