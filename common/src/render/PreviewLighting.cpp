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
#include "mdl/AssetUtils.h"
#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushGeometry.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/Entity.h"
#include "mdl/EntityModel.h"
#include "mdl/EntityModelManager.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/PatchNode.h"
#include "mdl/Selection.h"
#include "mdl/TagAttribute.h"
#include "mdl/WorldNode.h"
#include "render/ActiveShader.h"
#include "render/Camera.h"
#include "render/MaterialIndexRangeRenderer.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"
#include "render/RenderService.h"
#include "render/RenderUtils.h"
#include "render/Renderable.h"
#include "render/Shaders.h"

#include "kdl/overload.h"
#include "kdl/string_compare.h"
#include "kdl/string_format.h"
#include "kdl/string_utils.h"

#include "vm/mat_ext.h"
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

struct ShadowPassJob
{
  int slot = -1;
  vm::vec3f position{};
  float range = 0.0f;
};

class NoopMaterialRenderFunc : public MaterialRenderFunc
{
public:
  void before(const mdl::Material*) override {}
  void after(const mdl::Material*) override {}
};

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

class UpdatePreviewLightingPass : public DirectRenderable
{
private:
  mdl::EntityModelManager& m_entityModelManager;
  GLuint& m_lightTextureId;
  GLuint& m_shadowFboId;
  GLuint& m_shadowAtlasTextureId;
  GLuint& m_shadowCasterVboId;
  bool& m_shadowCasterVboDirty;

  std::vector<float> m_lightTexels;
  bool m_shadowsEnabled = false;
  std::vector<ShadowPassJob> m_dirtyShadowJobs;
  const std::vector<vm::vec3f>& m_shadowCasterVertices;
  size_t m_shadowBrushVertexCount = 0;
  size_t m_shadowPatchVertexCount = 0;
  const std::vector<PreviewShadowModelCaster>& m_shadowModelCasters;

public:
  UpdatePreviewLightingPass(
    mdl::EntityModelManager& entityModelManager,
    GLuint& lightTextureId,
    GLuint& shadowFboId,
    GLuint& shadowAtlasTextureId,
    GLuint& shadowCasterVboId,
    bool& shadowCasterVboDirty,
    std::vector<float> lightTexels,
    const bool shadowsEnabled,
    std::vector<ShadowPassJob> dirtyShadowJobs,
    const std::vector<vm::vec3f>& shadowCasterVertices,
    const size_t shadowBrushVertexCount,
    const size_t shadowPatchVertexCount,
    const std::vector<PreviewShadowModelCaster>& shadowModelCasters)
    : m_entityModelManager{entityModelManager}
    , m_lightTextureId{lightTextureId}
    , m_shadowFboId{shadowFboId}
    , m_shadowAtlasTextureId{shadowAtlasTextureId}
    , m_shadowCasterVboId{shadowCasterVboId}
    , m_shadowCasterVboDirty{shadowCasterVboDirty}
    , m_lightTexels{std::move(lightTexels)}
    , m_shadowsEnabled{shadowsEnabled}
    , m_dirtyShadowJobs{std::move(dirtyShadowJobs)}
    , m_shadowCasterVertices{shadowCasterVertices}
    , m_shadowBrushVertexCount{shadowBrushVertexCount}
    , m_shadowPatchVertexCount{shadowPatchVertexCount}
    , m_shadowModelCasters{shadowModelCasters}
  {
  }

private:
  void doPrepareVertices(VboManager& vboManager) override
  {
    if (m_shadowsEnabled && !m_dirtyShadowJobs.empty())
    {
      m_entityModelManager.prepare(vboManager);
    }
  }

  void doRender(RenderContext& renderContext) override
  {
    uploadLightDataTexture();

    if (m_shadowsEnabled)
    {
      ensureShadowResources();
      if (m_shadowFboId != 0 && m_shadowAtlasTextureId != 0)
      {
        if (!m_dirtyShadowJobs.empty())
        {
          uploadShadowCasterVboIfNeeded();
          renderDirtyShadowTiles(renderContext);
        }

        glAssert(glActiveTexture(GL_TEXTURE2));
        glAssert(glBindTexture(GL_TEXTURE_2D, m_shadowAtlasTextureId));
        glAssert(glActiveTexture(GL_TEXTURE0));
      }
    }
  }

  void uploadLightDataTexture()
  {
    glAssert(glActiveTexture(GL_TEXTURE1));
    if (m_lightTextureId == 0)
    {
      glAssert(glGenTextures(1, &m_lightTextureId));
      glAssert(glBindTexture(GL_TEXTURE_2D, m_lightTextureId));
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
        m_lightTexels.data()));
    }
    else
    {
      glAssert(glBindTexture(GL_TEXTURE_2D, m_lightTextureId));
      glAssert(glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        static_cast<GLsizei>(MaxPreviewLights),
        2,
        GL_RGBA,
        GL_FLOAT,
        m_lightTexels.data()));
    }
    glAssert(glActiveTexture(GL_TEXTURE0));
  }

  void ensureShadowResources()
  {
    if (m_shadowFboId != 0 && m_shadowAtlasTextureId != 0)
    {
      return;
    }

    GLint prevFbo = 0;
    glAssert(glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo));
    GLint prevViewport[4] = {};
    glAssert(glGetIntegerv(GL_VIEWPORT, prevViewport));
    const auto prevScissorTest = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepthMask = GL_TRUE;
    glAssert(glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask));

    glAssert(glActiveTexture(GL_TEXTURE2));
    glAssert(glGenTextures(1, &m_shadowAtlasTextureId));
    glAssert(glBindTexture(GL_TEXTURE_2D, m_shadowAtlasTextureId));
    glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE));
    glAssert(glTexImage2D(
      GL_TEXTURE_2D,
      0,
      GL_DEPTH_COMPONENT24,
      PreviewShadowAtlasWidth,
      PreviewShadowAtlasHeight,
      0,
      GL_DEPTH_COMPONENT,
      GL_UNSIGNED_INT,
      nullptr));

    glAssert(glGenFramebuffers(1, &m_shadowFboId));
    glAssert(glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFboId));
    glAssert(glFramebufferTexture2D(
      GL_FRAMEBUFFER,
      GL_DEPTH_ATTACHMENT,
      GL_TEXTURE_2D,
      m_shadowAtlasTextureId,
      0));
    glAssert(glDrawBuffer(GL_NONE));
    glAssert(glReadBuffer(GL_NONE));

    const auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE)
    {
      glAssert(glDisable(GL_SCISSOR_TEST));
      glAssert(glDepthMask(GL_TRUE));
      glAssert(glViewport(0, 0, PreviewShadowAtlasWidth, PreviewShadowAtlasHeight));
      glAssert(glClearDepth(1.0));
      glAssert(glClear(GL_DEPTH_BUFFER_BIT));
    }
    else
    {
      glAssert(glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo)));
      glAssert(glDeleteFramebuffers(1, &m_shadowFboId));
      glAssert(glDeleteTextures(1, &m_shadowAtlasTextureId));
      m_shadowFboId = 0;
      m_shadowAtlasTextureId = 0;
    }

    glAssert(glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo)));
    glAssert(glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]));
    glAssert(glDepthMask(prevDepthMask));
    if (prevScissorTest)
    {
      glAssert(glEnable(GL_SCISSOR_TEST));
    }
    else
    {
      glAssert(glDisable(GL_SCISSOR_TEST));
    }
    glAssert(glActiveTexture(GL_TEXTURE0));
  }

  void uploadShadowCasterVboIfNeeded()
  {
    if (!m_shadowCasterVboDirty)
    {
      return;
    }
    m_shadowCasterVboDirty = false;

    if (m_shadowCasterVboId == 0)
    {
      glAssert(glGenBuffers(1, &m_shadowCasterVboId));
    }

    glAssert(glBindBuffer(GL_ARRAY_BUFFER, m_shadowCasterVboId));
    const auto byteSize =
      static_cast<GLsizeiptr>(m_shadowCasterVertices.size() * sizeof(vm::vec3f));
    glAssert(glBufferData(
      GL_ARRAY_BUFFER,
      byteSize,
      m_shadowCasterVertices.empty() ? nullptr : m_shadowCasterVertices.data(),
      GL_STATIC_DRAW));
    glAssert(glBindBuffer(GL_ARRAY_BUFFER, 0));
  }

  void renderDirtyShadowTiles(RenderContext& renderContext)
  {
    GLint prevFbo = 0;
    glAssert(glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo));
    GLint prevViewport[4] = {};
    glAssert(glGetIntegerv(GL_VIEWPORT, prevViewport));
    GLboolean prevColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    glAssert(glGetBooleanv(GL_COLOR_WRITEMASK, prevColorMask));
    GLboolean prevDepthMask = GL_TRUE;
    glAssert(glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask));
    const auto prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    const auto prevCullFace = glIsEnabled(GL_CULL_FACE);
    const auto prevScissorTest = glIsEnabled(GL_SCISSOR_TEST);
    GLint prevScissorBox[4] = {};
    glAssert(glGetIntegerv(GL_SCISSOR_BOX, prevScissorBox));

    glAssert(glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFboId));
    glAssert(glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE));
    glAssert(glDepthMask(GL_TRUE));
    glAssert(glEnable(GL_DEPTH_TEST));
    glAssert(glDepthFunc(GL_LEQUAL));
    glAssert(glFrontFace(GL_CW));
    glAssert(glCullFace(GL_BACK));
    glAssert(glEnable(GL_SCISSOR_TEST));
    glAssert(glClearDepth(1.0));

    {
      auto shader = ActiveShader{renderContext.shaderManager(), Shaders::ShadowMapShader};
      auto noopFunc = NoopMaterialRenderFunc{};

      for (const auto& job : m_dirtyShadowJobs)
      {
        const auto lightBounds = vm::bbox3f{
          job.position - vm::vec3f::fill(job.range),
          job.position + vm::vec3f::fill(job.range)};

        shader.set("LightPosition", job.position);
        shader.set("LightRange", job.range);

        for (int face = 0; face < 6; ++face)
        {
          const auto rect = previewShadowTileRect(job.slot, face);
          glAssert(glViewport(rect.x, rect.y, rect.width, rect.height));
          glAssert(glScissor(rect.x, rect.y, rect.width, rect.height));
          glAssert(glClear(GL_DEPTH_BUFFER_BIT));

          const auto viewProj =
            previewShadowFaceViewProjection(job.position, job.range, face);
          shader.set("LightViewProjection", viewProj);

          if (m_shadowCasterVboId != 0 && !m_shadowCasterVertices.empty())
          {
            shader.set("ModelMatrix", vm::mat4x4f::identity());
            glAssert(glBindBuffer(GL_ARRAY_BUFFER, m_shadowCasterVboId));
            glAssert(glEnableClientState(GL_VERTEX_ARRAY));
            glAssert(glVertexPointer(3, GL_FLOAT, 0, nullptr));

            if (m_shadowBrushVertexCount > 0)
            {
              glAssert(glEnable(GL_CULL_FACE));
              glAssert(glDrawArrays(
                GL_TRIANGLES, 0, static_cast<GLsizei>(m_shadowBrushVertexCount)));
            }

            if (m_shadowPatchVertexCount > 0)
            {
              glAssert(glDisable(GL_CULL_FACE));
              glAssert(glDrawArrays(
                GL_TRIANGLES,
                static_cast<GLint>(m_shadowBrushVertexCount),
                static_cast<GLsizei>(m_shadowPatchVertexCount)));
            }

            glAssert(glDisableClientState(GL_VERTEX_ARRAY));
            glAssert(glBindBuffer(GL_ARRAY_BUFFER, 0));
          }

          if (!m_shadowModelCasters.empty())
          {
            glAssert(glDisable(GL_CULL_FACE));
            for (const auto& caster : m_shadowModelCasters)
            {
              if (caster.renderer && lightBounds.intersects(caster.worldBounds))
              {
                shader.set("ModelMatrix", caster.modelMatrix);
                caster.renderer->render(noopFunc);
              }
            }
          }
        }
      }
    }

    glAssert(glBindBuffer(GL_ARRAY_BUFFER, 0));
    glAssert(glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo)));
    glAssert(glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]));
    glAssert(glColorMask(
      prevColorMask[0], prevColorMask[1], prevColorMask[2], prevColorMask[3]));
    glAssert(glDepthMask(prevDepthMask));
    if (prevDepthTest)
    {
      glAssert(glEnable(GL_DEPTH_TEST));
    }
    else
    {
      glAssert(glDisable(GL_DEPTH_TEST));
    }
    if (prevCullFace)
    {
      glAssert(glEnable(GL_CULL_FACE));
    }
    else
    {
      glAssert(glDisable(GL_CULL_FACE));
    }
    glAssert(
      glScissor(prevScissorBox[0], prevScissorBox[1], prevScissorBox[2], prevScissorBox[3]));
    if (prevScissorTest)
    {
      glAssert(glEnable(GL_SCISSOR_TEST));
    }
    else
    {
      glAssert(glDisable(GL_SCISSOR_TEST));
    }
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

float encodePreviewShadowMeta(const int shadowSlot, const int shadowMode)
{
  if (shadowSlot < 0 || shadowMode == 0)
  {
    return 0.0f;
  }
  const auto encodedSlot = static_cast<float>(shadowSlot + 1);
  return shadowMode == 1 ? encodedSlot : -encodedSlot;
}

std::vector<int> assignPreviewShadowSlots(
  std::vector<PreviewLight>& activeLights,
  std::array<PreviewShadowSlot, MaxPreviewShadowLights>& cacheSlots,
  const vm::vec3f& cameraPosition,
  const uint64_t geometryGeneration,
  const bool shadowsEnabled)
{
  for (auto& light : activeLights)
  {
    light.shadowSlot = -1;
  }

  if (!shadowsEnabled)
  {
    return {};
  }

  auto candidateIndices = std::vector<size_t>{};
  candidateIndices.reserve(activeLights.size());
  for (size_t i = 0; i < activeLights.size(); ++i)
  {
    const auto& light = activeLights[i];
    if (light.shadows != 0 && light.intensity > 0.0f && light.range > 0.0f)
    {
      candidateIndices.push_back(i);
    }
  }

  const auto sortKey = [&](const size_t idx) {
    const auto& light = activeLights[idx];
    const auto distToCenter = vm::distance(cameraPosition, light.position);
    const auto distToSphere = std::max(0.0f, distToCenter - light.range);
    return std::pair{distToSphere, distToCenter};
  };

  if (candidateIndices.size() > MaxPreviewShadowLights)
  {
    std::nth_element(
      candidateIndices.begin(),
      candidateIndices.begin() + static_cast<std::ptrdiff_t>(MaxPreviewShadowLights),
      candidateIndices.end(),
      [&](const size_t lhs, const size_t rhs) { return sortKey(lhs) < sortKey(rhs); });
    candidateIndices.resize(MaxPreviewShadowLights);
  }

  auto slotUsed = std::array<bool, MaxPreviewShadowLights>{};
  auto dirtySlots = std::vector<int>{};
  auto unassignedCandidates = std::vector<size_t>{};

  // Pass 1: preserve existing cached slots whose light position & range still match.
  for (const auto lightIdx : candidateIndices)
  {
    auto& light = activeLights[lightIdx];
    auto matchedSlot = -1;
    for (size_t s = 0; s < MaxPreviewShadowLights; ++s)
    {
      if (
        !slotUsed[s] && cacheSlots[s].occupied && cacheSlots[s].position == light.position
        && cacheSlots[s].range == light.range)
      {
        matchedSlot = static_cast<int>(s);
        break;
      }
    }

    if (matchedSlot >= 0)
    {
      const auto slotIdx = static_cast<size_t>(matchedSlot);
      slotUsed[slotIdx] = true;
      light.shadowSlot = matchedSlot;
      if (cacheSlots[slotIdx].geometryGeneration != geometryGeneration)
      {
        cacheSlots[slotIdx].geometryGeneration = geometryGeneration;
        dirtySlots.push_back(matchedSlot);
      }
    }
    else
    {
      unassignedCandidates.push_back(lightIdx);
    }
  }

  // Pass 2: allocate unoccupied slots first, then evict unused slots for new/moved lights.
  for (const auto lightIdx : unassignedCandidates)
  {
    auto& light = activeLights[lightIdx];
    auto chosenSlot = -1;

    for (size_t s = 0; s < MaxPreviewShadowLights; ++s)
    {
      if (!slotUsed[s] && !cacheSlots[s].occupied)
      {
        chosenSlot = static_cast<int>(s);
        break;
      }
    }
    if (chosenSlot < 0)
    {
      for (size_t s = 0; s < MaxPreviewShadowLights; ++s)
      {
        if (!slotUsed[s])
        {
          chosenSlot = static_cast<int>(s);
          break;
        }
      }
    }

    if (chosenSlot >= 0)
    {
      const auto slotIdx = static_cast<size_t>(chosenSlot);
      slotUsed[slotIdx] = true;
      light.shadowSlot = chosenSlot;
      cacheSlots[slotIdx] = PreviewShadowSlot{
        true, light.position, light.range, geometryGeneration};
      dirtySlots.push_back(chosenSlot);
    }
  }

  return dirtySlots;
}

vm::mat4x4f previewShadowFaceViewProjection(
  const vm::vec3f& lightPos, const float lightRange, const int face)
{
  static constexpr std::array<vm::vec3f, 6> FaceDirections = {
    vm::vec3f{1.0f, 0.0f, 0.0f},
    vm::vec3f{-1.0f, 0.0f, 0.0f},
    vm::vec3f{0.0f, 1.0f, 0.0f},
    vm::vec3f{0.0f, -1.0f, 0.0f},
    vm::vec3f{0.0f, 0.0f, 1.0f},
    vm::vec3f{0.0f, 0.0f, -1.0f},
  };
  static constexpr std::array<vm::vec3f, 6> FaceUps = {
    vm::vec3f{0.0f, 0.0f, 1.0f},
    vm::vec3f{0.0f, 0.0f, 1.0f},
    vm::vec3f{0.0f, 0.0f, 1.0f},
    vm::vec3f{0.0f, 0.0f, 1.0f},
    vm::vec3f{0.0f, -1.0f, 0.0f},
    vm::vec3f{0.0f, 1.0f, 0.0f},
  };

  const auto clampedFace = static_cast<size_t>(std::clamp(face, 0, 5));
  const auto farPlane = std::max(lightRange * 1.01f, 2.0f);
  const auto nearPlane = std::clamp(lightRange * 0.002f, 0.5f, 4.0f);
  const auto depth = farPlane - nearPlane;

  // Exact 90-degree square perspective projection (vm::perspective_matrix applies a 0.75
  // 4:3 aspect factor, so construct the 1:1 cube-face frustum matrix directly).
  const auto projection = vm::mat4x4f{
    1.0f,
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    -(farPlane + nearPlane) / depth,
    -2.0f * farPlane * nearPlane / depth,
    0.0f,
    0.0f,
    -1.0f,
    0.0f};
  const auto view = vm::view_matrix(FaceDirections[clampedFace], FaceUps[clampedFace])
                    * vm::translation_matrix(-lightPos);
  return projection * view;
}

PreviewShadowFaceUV previewShadowDirectionToFaceUV(const vm::vec3f& d)
{
  const auto ax = std::abs(d.x());
  const auto ay = std::abs(d.y());
  const auto az = std::abs(d.z());

  auto face = 0;
  auto ma = 1.0f;
  auto sc = 0.0f;
  auto tc = 0.0f;

  if (ax >= ay && ax >= az)
  {
    ma = ax;
    if (d.x() >= 0.0f)
    {
      face = 0;
      sc = -d.y();
      tc = d.z();
    }
    else
    {
      face = 1;
      sc = d.y();
      tc = d.z();
    }
  }
  else if (ay >= ax && ay >= az)
  {
    ma = ay;
    if (d.y() >= 0.0f)
    {
      face = 2;
      sc = d.x();
      tc = d.z();
    }
    else
    {
      face = 3;
      sc = -d.x();
      tc = d.z();
    }
  }
  else
  {
    ma = az;
    if (d.z() >= 0.0f)
    {
      face = 4;
      sc = d.x();
      tc = -d.y();
    }
    else
    {
      face = 5;
      sc = d.x();
      tc = d.y();
    }
  }

  const auto invMa = 1.0f / std::max(ma, 0.0001f);
  return PreviewShadowFaceUV{face, sc * invMa * 0.5f + 0.5f, tc * invMa * 0.5f + 0.5f};
}

PreviewShadowTileRect previewShadowTileRect(const int slot, const int face)
{
  const auto row = slot / 2;
  const auto colPair = slot - row * 2;
  const auto tileX = colPair * 6 + face;
  const auto tileY = row;
  return PreviewShadowTileRect{
    tileX * PreviewShadowTileSize,
    tileY * PreviewShadowTileSize,
    PreviewShadowTileSize,
    PreviewShadowTileSize};
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
    texels[row1 + 3] = encodePreviewShadowMeta(light.shadowSlot, light.shadows);
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
  if (m_shadowAtlasTextureId != 0)
  {
    glAssert(glDeleteTextures(1, &m_shadowAtlasTextureId));
    m_shadowAtlasTextureId = 0;
  }
  if (m_shadowFboId != 0)
  {
    glAssert(glDeleteFramebuffers(1, &m_shadowFboId));
    m_shadowFboId = 0;
  }
  if (m_shadowCasterVboId != 0)
  {
    glAssert(glDeleteBuffers(1, &m_shadowCasterVboId));
    m_shadowCasterVboId = 0;
  }
}

void PreviewLightRenderer::invalidateLights()
{
  m_lightsDirty = true;
}

void PreviewLightRenderer::invalidateGeometry()
{
  m_geometryDirty = true;
  m_shadowCasterVboDirty = true;
  ++m_geometryGeneration;
}

void PreviewLightRenderer::invalidateAll()
{
  invalidateLights();
  invalidateGeometry();
}

void PreviewLightRenderer::clear()
{
  m_lights.clear();
  m_shadowCasterVertices.clear();
  m_shadowBrushVertexCount = 0;
  m_shadowPatchVertexCount = 0;
  m_shadowModelCasters.clear();
  m_shadowSlots = {};
  m_lightsDirty = true;
  m_geometryDirty = true;
  m_shadowCasterVboDirty = true;
  ++m_geometryGeneration;
}

void PreviewLightRenderer::prepare(
  RenderContext& renderContext, RenderBatch& renderBatch)
{
  if (!renderContext.previewLighting())
  {
    renderContext.setPreviewLightCount(0);
    return;
  }

  if (m_lightsDirty)
  {
    rebuildLightCache();
  }

  const auto shadowsEnabled = renderContext.previewLightingShadows();
  if (shadowsEnabled && m_geometryDirty)
  {
    rebuildShadowGeometryCache();
  }

  auto activeLights =
    selectActivePreviewLights(m_lights, renderContext.camera().position());

  const auto dirtySlots = assignPreviewShadowSlots(
    activeLights,
    m_shadowSlots,
    renderContext.camera().position(),
    m_geometryGeneration,
    shadowsEnabled);

  auto dirtyJobs = std::vector<ShadowPassJob>{};
  dirtyJobs.reserve(dirtySlots.size());
  for (const auto slot : dirtySlots)
  {
    const auto& cached = m_shadowSlots[static_cast<size_t>(slot)];
    dirtyJobs.push_back(ShadowPassJob{slot, cached.position, cached.range});
  }

  renderContext.setPreviewLightCount(static_cast<int>(activeLights.size()));

  auto texels = packPreviewLightTextureData(activeLights);
  renderBatch.addOneShot(new UpdatePreviewLightingPass{
    m_map.entityModelManager(),
    m_textureId,
    m_shadowFboId,
    m_shadowAtlasTextureId,
    m_shadowCasterVboId,
    m_shadowCasterVboDirty,
    std::move(texels),
    shadowsEnabled,
    std::move(dirtyJobs),
    m_shadowCasterVertices,
    m_shadowBrushVertexCount,
    m_shadowPatchVertexCount,
    m_shadowModelCasters});
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
  m_lightsDirty = false;

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

void PreviewLightRenderer::rebuildShadowGeometryCache()
{
  m_shadowCasterVertices.clear();
  m_shadowBrushVertexCount = 0;
  m_shadowPatchVertexCount = 0;
  m_shadowModelCasters.clear();
  m_geometryDirty = false;
  m_shadowCasterVboDirty = true;

  auto* worldNode = m_map.world();
  if (!worldNode)
  {
    return;
  }

  const auto& editorContext = m_map.editorContext();
  auto& entityModelManager = m_map.entityModelManager();
  auto& logger = m_map.logger();

  auto patchVertices = std::vector<vm::vec3f>{};
  auto facePolygon = std::vector<vm::vec3f>{};

  worldNode->accept(kdl::overload(
    [](auto&& thisLambda, const mdl::WorldNode* world) { world->visitChildren(thisLambda); },
    [](auto&& thisLambda, const mdl::LayerNode* layer) { layer->visitChildren(thisLambda); },
    [](auto&& thisLambda, const mdl::GroupNode* group) { group->visitChildren(thisLambda); },
    [&](auto&& thisLambda, const mdl::EntityNode* entityNode) {
      if (!editorContext.visible(*entityNode))
      {
        return;
      }

      entityNode->visitChildren(thisLambda);

      const auto* model = entityNode->entity().model();
      const auto* modelData = model ? model->data() : nullptr;
      if (!modelData || modelData->orientation() != mdl::Orientation::Oriented)
      {
        return;
      }

      const auto modelSpec =
        mdl::safeGetModelSpecification(logger, entityNode->entity().classname(), [&]() {
          return entityNode->entity().modelSpecification();
        });
      auto* renderer = entityModelManager.renderer(modelSpec);
      if (!renderer)
      {
        return;
      }

      const auto& defaultModelScaleExpression =
        entityNode->entityPropertyConfig().defaultModelScaleExpression;
      const auto modelMatrix =
        vm::mat4x4f{entityNode->entity().modelTransformation(defaultModelScaleExpression)};
      m_shadowModelCasters.push_back(PreviewShadowModelCaster{
        modelMatrix, vm::bbox3f{entityNode->physicalBounds()}, renderer});
    },
    [&](const mdl::BrushNode* brushNode) {
      if (
        !editorContext.visible(*brushNode)
        || brushNode->hasAttribute(mdl::TagAttributes::Transparency))
      {
        return;
      }
      for (const auto& face : brushNode->brush().faces())
      {
        if (
          !editorContext.visible(*brushNode, face)
          || face.hasAttribute(mdl::TagAttributes::Transparency))
        {
          continue;
        }
        const auto* geom = face.geometry();
        if (!geom)
        {
          continue;
        }
        facePolygon.clear();
        const auto& boundary = geom->boundary();
        for (auto it = std::rbegin(boundary), end = std::rend(boundary); it != end; ++it)
        {
          facePolygon.emplace_back((*it)->origin()->position());
        }
        if (facePolygon.size() >= 3)
        {
          for (size_t i = 1; i + 1 < facePolygon.size(); ++i)
          {
            m_shadowCasterVertices.push_back(facePolygon[0]);
            m_shadowCasterVertices.push_back(facePolygon[i]);
            m_shadowCasterVertices.push_back(facePolygon[i + 1]);
          }
        }
      }
    },
    [&](const mdl::PatchNode* patchNode) {
      if (!editorContext.visible(*patchNode))
      {
        return;
      }
      const auto& grid = patchNode->grid();
      const auto pointsPerRow = grid.pointColumnCount;
      for (size_t row = 0u; row < grid.quadRowCount(); ++row)
      {
        for (size_t col = 0u; col < grid.quadColumnCount(); ++col)
        {
          const auto p0 = vm::vec3f{grid.points[row * pointsPerRow + col].position};
          const auto p1 = vm::vec3f{grid.points[row * pointsPerRow + col + 1u].position};
          const auto p2 =
            vm::vec3f{grid.points[(row + 1u) * pointsPerRow + col + 1u].position};
          const auto p3 =
            vm::vec3f{grid.points[(row + 1u) * pointsPerRow + col].position};

          patchVertices.push_back(p0);
          patchVertices.push_back(p1);
          patchVertices.push_back(p2);

          patchVertices.push_back(p2);
          patchVertices.push_back(p3);
          patchVertices.push_back(p0);
        }
      }
    }));

  m_shadowBrushVertexCount = m_shadowCasterVertices.size();
  m_shadowPatchVertexCount = patchVertices.size();
  m_shadowCasterVertices.insert(
    m_shadowCasterVertices.end(), patchVertices.begin(), patchVertices.end());
}

} // namespace tb::render
