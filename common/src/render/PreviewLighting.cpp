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

#include "vm/constants.h"
#include "vm/mat_ext.h"
#include "vm/util.h"
#include "vm/vec.h"
#include "vm/vec_io.h"

#include <algorithm>
#include <cmath>
#include <limits>
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
constexpr float DefaultSpotAngle = 45.0f;
constexpr float SpotInnerAngleRatio = 0.65f;
constexpr int DefaultShadowsSoft = 2;
constexpr size_t LightRangeCircleSegments = 48;

constexpr std::array<vm::vec3f, 6> CubeFaceDirections = {
  vm::vec3f{1.0f, 0.0f, 0.0f},
  vm::vec3f{-1.0f, 0.0f, 0.0f},
  vm::vec3f{0.0f, 1.0f, 0.0f},
  vm::vec3f{0.0f, -1.0f, 0.0f},
  vm::vec3f{0.0f, 0.0f, 1.0f},
  vm::vec3f{0.0f, 0.0f, -1.0f},
};

constexpr std::array<vm::vec3f, 6> CubeFaceUps = {
  vm::vec3f{0.0f, 0.0f, 1.0f},
  vm::vec3f{0.0f, 0.0f, 1.0f},
  vm::vec3f{0.0f, 0.0f, 1.0f},
  vm::vec3f{0.0f, 0.0f, 1.0f},
  vm::vec3f{0.0f, -1.0f, 0.0f},
  vm::vec3f{0.0f, 1.0f, 0.0f},
};

struct ShadowPassJob
{
  int slot = -1;
  PreviewLightType type = PreviewLightType::Point;
  vm::vec3f position{};
  float range = 0.0f;
  vm::vec3f direction{1.0f, 0.0f, 0.0f};
  float spotAngle = DefaultSpotAngle;
};

struct DirectionalShadowJob
{
  bool render = false;
  vm::mat4x4f viewProj = vm::mat4x4f::identity();
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

PreviewLightType parseLightType(const std::string& value)
{
  const auto trimmed = kdl::str_trim(value);
  if (trimmed == "1" || kdl::ci::str_is_equal(trimmed, "spot"))
  {
    return PreviewLightType::Spot;
  }
  if (trimmed == "2" || kdl::ci::str_is_equal(trimmed, "directional"))
  {
    return PreviewLightType::Directional;
  }
  return PreviewLightType::Point;
}

vm::vec3f parseLightDirection(const mdl::Entity& entity)
{
  if (const auto* anglesProp = entity.property("angles"))
  {
    if (const auto parsed = vm::parse<double, 3>(*anglesProp))
    {
      // Matches TrenchBroom's Euler_PositivePitchDown and Scopa's TryGetAngles3D:
      // angles = "pitch yaw roll", positive pitch tilts downward (-Z).
      const auto roll = +vm::to_radians(parsed->z());
      const auto pitch = +vm::to_radians(parsed->x());
      const auto yaw = +vm::to_radians(parsed->y());
      const auto rot = vm::rotation_matrix(roll, pitch, yaw);
      const auto dir = vm::vec3f{rot * vm::vec3d{1.0, 0.0, 0.0}};
      if (!vm::is_zero(dir, 1e-5f))
      {
        return vm::normalize(dir);
      }
    }
  }

  if (const auto* mangleProp = entity.property("mangle"))
  {
    if (const auto parsed = vm::parse<double, 3>(*mangleProp))
    {
      // Quake light mangle: "yaw pitch roll" (positive pitch tilts up, matching TB Mangle).
      const auto roll = +vm::to_radians(parsed->z());
      const auto pitch = -vm::to_radians(parsed->y());
      const auto yaw = +vm::to_radians(parsed->x());
      const auto rot = vm::rotation_matrix(roll, pitch, yaw);
      const auto dir = vm::vec3f{rot * vm::vec3d{1.0, 0.0, 0.0}};
      if (!vm::is_zero(dir, 1e-5f))
      {
        return vm::normalize(dir);
      }
    }
  }

  if (const auto* angleProp = entity.property("angle"))
  {
    if (const auto angle = parseFloat(*angleProp))
    {
      if (*angle == -1.0f)
      {
        return vm::vec3f{0.0f, 0.0f, 1.0f};
      }
      if (*angle == -2.0f)
      {
        return vm::vec3f{0.0f, 0.0f, -1.0f};
      }
      const auto yaw = vm::to_radians(*angle);
      return vm::normalize(vm::vec3f{std::cos(yaw), std::sin(yaw), 0.0f});
    }
  }

  return vm::vec3f{1.0f, 0.0f, 0.0f};
}

std::pair<vm::vec3f, vm::vec3f> buildOrthonormalBasis(const vm::vec3f& forward)
{
  const auto f = vm::normalize(forward);
  const auto refUp =
    std::abs(f.z()) < 0.99f ? vm::vec3f{0.0f, 0.0f, 1.0f} : vm::vec3f{0.0f, 1.0f, 0.0f};
  const auto right = vm::normalize(vm::cross(f, refUp));
  const auto up = vm::normalize(vm::cross(right, f));
  return {right, up};
}

std::pair<float, float> computeSpotAttenuationParams(const float spotAngleDegrees)
{
  const auto outerAngle = std::clamp(spotAngleDegrees, 1.0f, 179.0f);
  const auto innerAngle = outerAngle * SpotInnerAngleRatio;
  const auto cosOuter = std::cos(vm::to_radians(outerAngle * 0.5f));
  const auto cosInner = std::cos(vm::to_radians(innerAngle * 0.5f));
  const auto invRange = 1.0f / std::max(cosInner - cosOuter, 0.001f);
  return {invRange, -cosOuter * invRange};
}

bool spotConeOverlapsCubeFace(
  const vm::vec3f& spotDirection, const float spotAngleDegrees, const int face)
{
  // A 90-degree square frustum's corner sits at atan(sqrt(2)) ~= 54.74 deg from face center.
  const auto halfSpotRad = vm::to_radians(std::clamp(spotAngleDegrees, 1.0f, 179.0f) * 0.5f);
  const auto maxAngle = halfSpotRad + vm::to_radians(56.0f);
  if (maxAngle >= vm::Cf::pi())
  {
    return true;
  }
  const auto& faceDir = CubeFaceDirections[static_cast<size_t>(std::clamp(face, 0, 5))];
  return vm::dot(faceDir, spotDirection) >= std::cos(maxAngle);
}

class UpdatePreviewLightingPass : public DirectRenderable
{
private:
  mdl::EntityModelManager& m_entityModelManager;
  GLuint& m_lightTextureId;
  GLuint& m_shadowFboId;
  GLuint& m_shadowAtlasTextureId;
  GLuint& m_dirShadowFboId;
  GLuint& m_dirShadowTextureId;
  GLuint& m_shadowCasterVboId;
  bool& m_shadowCasterVboDirty;

  std::vector<float> m_lightTexels;
  bool m_shadowsEnabled = false;
  bool m_dirShadowEnabled = false;
  std::vector<ShadowPassJob> m_dirtyShadowJobs;
  DirectionalShadowJob m_dirShadowJob;
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
    GLuint& dirShadowFboId,
    GLuint& dirShadowTextureId,
    GLuint& shadowCasterVboId,
    bool& shadowCasterVboDirty,
    std::vector<float> lightTexels,
    const bool shadowsEnabled,
    const bool dirShadowEnabled,
    std::vector<ShadowPassJob> dirtyShadowJobs,
    DirectionalShadowJob dirShadowJob,
    const std::vector<vm::vec3f>& shadowCasterVertices,
    const size_t shadowBrushVertexCount,
    const size_t shadowPatchVertexCount,
    const std::vector<PreviewShadowModelCaster>& shadowModelCasters)
    : m_entityModelManager{entityModelManager}
    , m_lightTextureId{lightTextureId}
    , m_shadowFboId{shadowFboId}
    , m_shadowAtlasTextureId{shadowAtlasTextureId}
    , m_dirShadowFboId{dirShadowFboId}
    , m_dirShadowTextureId{dirShadowTextureId}
    , m_shadowCasterVboId{shadowCasterVboId}
    , m_shadowCasterVboDirty{shadowCasterVboDirty}
    , m_lightTexels{std::move(lightTexels)}
    , m_shadowsEnabled{shadowsEnabled}
    , m_dirShadowEnabled{dirShadowEnabled}
    , m_dirtyShadowJobs{std::move(dirtyShadowJobs)}
    , m_dirShadowJob{dirShadowJob}
    , m_shadowCasterVertices{shadowCasterVertices}
    , m_shadowBrushVertexCount{shadowBrushVertexCount}
    , m_shadowPatchVertexCount{shadowPatchVertexCount}
    , m_shadowModelCasters{shadowModelCasters}
  {
  }

private:
  void doPrepareVertices(VboManager& vboManager) override
  {
    if (m_shadowsEnabled && (!m_dirtyShadowJobs.empty() || m_dirShadowJob.render))
    {
      m_entityModelManager.prepare(vboManager);
    }
  }

  void doRender(RenderContext& renderContext) override
  {
    uploadLightDataTexture();

    if (m_shadowsEnabled)
    {
      const auto needShadowUpload = !m_dirtyShadowJobs.empty() || m_dirShadowJob.render;
      if (needShadowUpload)
      {
        uploadShadowCasterVboIfNeeded();
      }

      ensurePointShadowResources();
      if (m_shadowFboId != 0 && m_shadowAtlasTextureId != 0)
      {
        if (!m_dirtyShadowJobs.empty())
        {
          renderDirtyShadowTiles(renderContext);
        }

        glAssert(glActiveTexture(GL_TEXTURE2));
        glAssert(glBindTexture(GL_TEXTURE_2D, m_shadowAtlasTextureId));
        glAssert(glActiveTexture(GL_TEXTURE0));
      }

      if (m_dirShadowEnabled)
      {
        ensureDirShadowResources();
        if (m_dirShadowFboId != 0 && m_dirShadowTextureId != 0)
        {
          if (m_dirShadowJob.render)
          {
            renderDirectionalShadowMap(renderContext);
          }

          glAssert(glActiveTexture(GL_TEXTURE3));
          glAssert(glBindTexture(GL_TEXTURE_2D, m_dirShadowTextureId));
          glAssert(glActiveTexture(GL_TEXTURE0));
        }
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
        static_cast<GLsizei>(PreviewLightTextureRows),
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
        static_cast<GLsizei>(PreviewLightTextureRows),
        GL_RGBA,
        GL_FLOAT,
        m_lightTexels.data()));
    }
    glAssert(glActiveTexture(GL_TEXTURE0));
  }

  void createDepthFbo(
    GLuint& fboId,
    GLuint& textureId,
    const GLenum textureUnit,
    const int width,
    const int height)
  {
    if (fboId != 0 && textureId != 0)
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

    glAssert(glActiveTexture(textureUnit));
    glAssert(glGenTextures(1, &textureId));
    glAssert(glBindTexture(GL_TEXTURE_2D, textureId));
    glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    glAssert(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE));
    glAssert(glTexImage2D(
      GL_TEXTURE_2D,
      0,
      GL_DEPTH_COMPONENT24,
      width,
      height,
      0,
      GL_DEPTH_COMPONENT,
      GL_UNSIGNED_INT,
      nullptr));

    glAssert(glGenFramebuffers(1, &fboId));
    glAssert(glBindFramebuffer(GL_FRAMEBUFFER, fboId));
    glAssert(glFramebufferTexture2D(
      GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, textureId, 0));
    glAssert(glDrawBuffer(GL_NONE));
    glAssert(glReadBuffer(GL_NONE));

    const auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE)
    {
      glAssert(glDisable(GL_SCISSOR_TEST));
      glAssert(glDepthMask(GL_TRUE));
      glAssert(glViewport(0, 0, width, height));
      glAssert(glClearDepth(1.0));
      glAssert(glClear(GL_DEPTH_BUFFER_BIT));
    }
    else
    {
      glAssert(glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo)));
      glAssert(glDeleteFramebuffers(1, &fboId));
      glAssert(glDeleteTextures(1, &textureId));
      fboId = 0;
      textureId = 0;
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

  void ensurePointShadowResources()
  {
    createDepthFbo(
      m_shadowFboId,
      m_shadowAtlasTextureId,
      GL_TEXTURE2,
      PreviewShadowAtlasWidth,
      PreviewShadowAtlasHeight);
  }

  void ensureDirShadowResources()
  {
    createDepthFbo(
      m_dirShadowFboId,
      m_dirShadowTextureId,
      GL_TEXTURE3,
      PreviewDirectionalShadowMapSize,
      PreviewDirectionalShadowMapSize);
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

  void drawStaticShadowCasters(ActiveShader& shader)
  {
    if (m_shadowCasterVboId == 0 || m_shadowCasterVertices.empty())
    {
      return;
    }

    shader.set("ModelMatrix", vm::mat4x4f::identity());
    glAssert(glBindBuffer(GL_ARRAY_BUFFER, m_shadowCasterVboId));
    glAssert(glEnableClientState(GL_VERTEX_ARRAY));
    glAssert(glVertexPointer(3, GL_FLOAT, 0, nullptr));

    if (m_shadowBrushVertexCount > 0)
    {
      glAssert(glEnable(GL_CULL_FACE));
      glAssert(
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_shadowBrushVertexCount)));
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

      shader.set("IsDirectionalShadow", false);

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

          if (
            job.type == PreviewLightType::Spot
            && !spotConeOverlapsCubeFace(job.direction, job.spotAngle, face))
          {
            continue;
          }

          const auto viewProj =
            previewShadowFaceViewProjection(job.position, job.range, face);
          shader.set("LightViewProjection", viewProj);

          drawStaticShadowCasters(shader);

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

  void renderDirectionalShadowMap(RenderContext& renderContext)
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

    glAssert(glBindFramebuffer(GL_FRAMEBUFFER, m_dirShadowFboId));
    glAssert(glDisable(GL_SCISSOR_TEST));
    glAssert(glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE));
    glAssert(glDepthMask(GL_TRUE));
    glAssert(glEnable(GL_DEPTH_TEST));
    glAssert(glDepthFunc(GL_LEQUAL));
    glAssert(glFrontFace(GL_CW));
    glAssert(glCullFace(GL_BACK));
    glAssert(
      glViewport(0, 0, PreviewDirectionalShadowMapSize, PreviewDirectionalShadowMapSize));
    glAssert(glClearDepth(1.0));
    glAssert(glClear(GL_DEPTH_BUFFER_BIT));

    {
      auto shader = ActiveShader{renderContext.shaderManager(), Shaders::ShadowMapShader};
      auto noopFunc = NoopMaterialRenderFunc{};

      shader.set("IsDirectionalShadow", true);
      shader.set("LightPosition", vm::vec3f{0.0f, 0.0f, 0.0f});
      shader.set("LightRange", 1.0f);
      shader.set("LightViewProjection", m_dirShadowJob.viewProj);

      drawStaticShadowCasters(shader);

      if (!m_shadowModelCasters.empty())
      {
        glAssert(glDisable(GL_CULL_FACE));
        for (const auto& caster : m_shadowModelCasters)
        {
          if (caster.renderer)
          {
            shader.set("ModelMatrix", caster.modelMatrix);
            caster.renderer->render(noopFunc);
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
  if (kdl::ci::str_is_equal(classname, "light_unity"))
  {
    return true;
  }
  return kdl::ci::str_contains(classname, "light")
         && kdl::ci::str_contains(classname, "realtime");
}

std::optional<PreviewLight> extractPreviewLight(
  const mdl::Entity& entity, const float unityScaleFactor, const bool selected)
{
  const auto& classname = entity.classname();
  if (!isRealtimePreviewLightClassname(classname))
  {
    return std::nullopt;
  }

  const auto isUnityLight = kdl::ci::str_is_equal(classname, "light_unity");
  const auto effectiveScale =
    unityScaleFactor > 0.0f ? unityScaleFactor : DefaultPreviewLightUnitScale;

  auto light = PreviewLight{};
  light.position = vm::vec3f{entity.origin()};
  light.color = vm::vec3f{1.0f, 1.0f, 1.0f};
  light.shadows = DefaultShadowsSoft;
  light.selected = selected;
  light.direction = parseLightDirection(entity);
  light.spotAngle = DefaultSpotAngle;

  if (const auto* typeProp = entity.property("light_type"))
  {
    light.type = parseLightType(*typeProp);
  }

  if (const auto* spotAngleProp = entity.property("spotangle"))
  {
    if (const auto parsed = parseFloat(*spotAngleProp))
    {
      light.spotAngle = std::clamp(*parsed, 1.0f, 179.0f);
    }
  }

  const auto* intensityProp = entity.property("intensity");
  const auto* rangeProp = entity.property("range");

  if (isUnityLight || intensityProp != nullptr || rangeProp != nullptr)
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
  auto directional = std::vector<PreviewLight>{};
  auto punctual = std::vector<PreviewLight>{};
  punctual.reserve(lights.size());

  for (const auto& light : lights)
  {
    if (light.intensity <= 0.0f)
    {
      continue;
    }
    if (light.type == PreviewLightType::Directional)
    {
      directional.push_back(light);
    }
    else if (light.range > 0.0f)
    {
      punctual.push_back(light);
    }
  }

  if (directional.size() >= maxLights)
  {
    directional.resize(maxLights);
    return directional;
  }

  const auto remaining = maxLights - directional.size();
  if (punctual.size() > remaining)
  {
    const auto sortKey = [&](const PreviewLight& light) {
      const auto distToCenter = vm::distance(cameraPosition, light.position);
      const auto distToSphere = std::max(0.0f, distToCenter - light.range);
      return std::pair{distToSphere, distToCenter};
    };

    std::nth_element(
      punctual.begin(),
      punctual.begin() + static_cast<std::ptrdiff_t>(remaining),
      punctual.end(),
      [&](const auto& lhs, const auto& rhs) { return sortKey(lhs) < sortKey(rhs); });
    punctual.resize(remaining);
  }

  directional.insert(directional.end(), punctual.begin(), punctual.end());
  return directional;
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
    if (
      light.type != PreviewLightType::Directional && light.shadows != 0
      && light.intensity > 0.0f && light.range > 0.0f)
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

  // Pass 1: preserve existing cached slots whose light parameters still match.
  for (const auto lightIdx : candidateIndices)
  {
    auto& light = activeLights[lightIdx];
    auto matchedSlot = -1;
    for (size_t s = 0; s < MaxPreviewShadowLights; ++s)
    {
      const auto& cached = cacheSlots[s];
      const auto sameDirection =
        light.type != PreviewLightType::Spot
        || (cached.direction == light.direction && cached.spotAngle == light.spotAngle);
      if (
        !slotUsed[s] && cached.occupied && cached.type == light.type
        && cached.position == light.position && cached.range == light.range
        && sameDirection)
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
        true,
        light.type,
        light.position,
        light.range,
        light.direction,
        light.spotAngle,
        geometryGeneration};
      dirtySlots.push_back(chosenSlot);
    }
  }

  return dirtySlots;
}

vm::mat4x4f previewShadowFaceViewProjection(
  const vm::vec3f& lightPos, const float lightRange, const int face)
{
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
  const auto view =
    vm::view_matrix(CubeFaceDirections[clampedFace], CubeFaceUps[clampedFace])
    * vm::translation_matrix(-lightPos);
  return projection * view;
}

vm::mat4x4f previewDirectionalShadowViewProjection(
  const vm::vec3f& lightDirection, const vm::bbox3f& worldBounds)
{
  const auto dir = vm::is_zero(lightDirection, 1e-5f) ? vm::vec3f{0.0f, 0.0f, -1.0f}
                                                      : vm::normalize(lightDirection);
  const auto [right, up] = buildOrthonormalBasis(dir);
  const auto center = worldBounds.center();
  const auto view = vm::view_matrix(dir, up) * vm::translation_matrix(-center);

  auto minX = std::numeric_limits<float>::max();
  auto maxX = std::numeric_limits<float>::lowest();
  auto minY = std::numeric_limits<float>::max();
  auto maxY = std::numeric_limits<float>::lowest();
  auto minZ = std::numeric_limits<float>::max();
  auto maxZ = std::numeric_limits<float>::lowest();

  for (int cx = 0; cx < 2; ++cx)
  {
    for (int cy = 0; cy < 2; ++cy)
    {
      for (int cz = 0; cz < 2; ++cz)
      {
        const auto corner = vm::vec3f{
          cx == 0 ? worldBounds.min.x() : worldBounds.max.x(),
          cy == 0 ? worldBounds.min.y() : worldBounds.max.y(),
          cz == 0 ? worldBounds.min.z() : worldBounds.max.z()};
        const auto v = (view * vm::vec4f{corner, 1.0f}).xyz();
        minX = std::min(minX, v.x());
        maxX = std::max(maxX, v.x());
        minY = std::min(minY, v.y());
        maxY = std::max(maxY, v.y());
        minZ = std::min(minZ, v.z());
        maxZ = std::max(maxZ, v.z());
      }
    }
  }

  constexpr float xyPadding = 64.0f;
  constexpr float zPadding = 128.0f;

  const auto left = minX - xyPadding;
  const auto rgt = maxX + xyPadding;
  const auto bottom = minY - xyPadding;
  const auto top = maxY + xyPadding;
  // In OpenGL eye space, points further upstream toward the light have larger +Z.
  const auto nearPlane = -maxZ - zPadding;
  const auto farPlane = -minZ + zPadding;

  const auto invW = 1.0f / std::max(rgt - left, 1.0f);
  const auto invH = 1.0f / std::max(top - bottom, 1.0f);
  const auto invD = 1.0f / std::max(farPlane - nearPlane, 1.0f);

  const auto ortho = vm::mat4x4f{
    2.0f * invW,
    0.0f,
    0.0f,
    -(rgt + left) * invW,
    0.0f,
    2.0f * invH,
    0.0f,
    -(top + bottom) * invH,
    0.0f,
    0.0f,
    -2.0f * invD,
    -(farPlane + nearPlane) * invD,
    0.0f,
    0.0f,
    0.0f,
    1.0f};

  return ortho * view;
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
  auto texels = std::vector<float>(MaxPreviewLights * PreviewLightTextureRows * 4, 0.0f);
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

    const auto row2 = (MaxPreviewLights * 2 + i) * 4;
    texels[row2 + 0] = light.direction.x();
    texels[row2 + 1] = light.direction.y();
    texels[row2 + 2] = light.direction.z();
    texels[row2 + 3] = static_cast<float>(static_cast<int>(light.type));

    const auto row3 = (MaxPreviewLights * 3 + i) * 4;
    if (light.type == PreviewLightType::Spot)
    {
      const auto [spotAttenX, spotAttenY] = computeSpotAttenuationParams(light.spotAngle);
      texels[row3 + 0] = spotAttenX;
      texels[row3 + 1] = spotAttenY;
    }
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
  if (m_dirShadowTextureId != 0)
  {
    glAssert(glDeleteTextures(1, &m_dirShadowTextureId));
    m_dirShadowTextureId = 0;
  }
  if (m_dirShadowFboId != 0)
  {
    glAssert(glDeleteFramebuffers(1, &m_dirShadowFboId));
    m_dirShadowFboId = 0;
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
  m_hasShadowWorldBounds = false;
  m_shadowSlots = {};
  m_dirShadowCache = {};
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
    renderContext.setPreviewDirShadowEnabled(false);
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
    dirtyJobs.push_back(ShadowPassJob{
      slot,
      cached.type,
      cached.position,
      cached.range,
      cached.direction,
      cached.spotAngle});
  }

  auto dirShadowEnabled = false;
  auto dirShadowJob = DirectionalShadowJob{};
  if (shadowsEnabled && m_hasShadowWorldBounds)
  {
    // First shadow-casting directional light acts as the main directional shadow caster.
    for (auto& light : activeLights)
    {
      if (light.type == PreviewLightType::Directional && light.shadows != 0)
      {
        light.shadowSlot = 0;
        dirShadowEnabled = true;

        if (
          !m_dirShadowCache.valid || m_dirShadowCache.direction != light.direction
          || m_dirShadowCache.geometryGeneration != m_geometryGeneration)
        {
          m_dirShadowCache.valid = true;
          m_dirShadowCache.direction = light.direction;
          m_dirShadowCache.geometryGeneration = m_geometryGeneration;
          m_dirShadowCache.viewProj =
            previewDirectionalShadowViewProjection(light.direction, m_shadowWorldBounds);
          dirShadowJob.render = true;
        }

        dirShadowJob.viewProj = m_dirShadowCache.viewProj;
        break;
      }
    }
  }

  renderContext.setPreviewLightCount(static_cast<int>(activeLights.size()));
  renderContext.setPreviewDirShadowEnabled(dirShadowEnabled);
  if (dirShadowEnabled)
  {
    renderContext.setPreviewDirLightViewProj(m_dirShadowCache.viewProj);
  }

  auto texels = packPreviewLightTextureData(activeLights);
  renderBatch.addOneShot(new UpdatePreviewLightingPass{
    m_map.entityModelManager(),
    m_textureId,
    m_shadowFboId,
    m_shadowAtlasTextureId,
    m_dirShadowFboId,
    m_dirShadowTextureId,
    m_shadowCasterVboId,
    m_shadowCasterVboDirty,
    std::move(texels),
    shadowsEnabled,
    dirShadowEnabled,
    std::move(dirtyJobs),
    dirShadowJob,
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
    if (!light)
    {
      continue;
    }

    const auto gizmoColor =
      Color{light->color.x(), light->color.y(), light->color.z(), 0.75f};
    renderService.setForegroundColor(gizmoColor);

    if (light->type == PreviewLightType::Directional)
    {
      const auto dir = vm::normalize(light->direction);
      const auto [right, up] = buildOrthonormalBasis(dir);
      constexpr float rayLength = 64.0f;
      constexpr float spread = 14.0f;
      constexpr float headLen = 12.0f;
      constexpr float headWidth = 5.0f;

      const auto offsets = std::array<vm::vec3f, 5>{
        vm::vec3f{0.0f, 0.0f, 0.0f},
        right * spread,
        -right * spread,
        up * spread,
        -up * spread,
      };

      for (const auto& offset : offsets)
      {
        const auto start = light->position + offset;
        const auto tip = start + dir * rayLength;
        renderService.renderLine(start, tip);
        renderService.renderLine(tip, tip - dir * headLen + right * headWidth);
        renderService.renderLine(tip, tip - dir * headLen - right * headWidth);
      }
      continue;
    }

    if (light->range <= 0.0f)
    {
      continue;
    }

    if (light->type == PreviewLightType::Spot)
    {
      const auto dir = vm::normalize(light->direction);
      const auto [right, up] = buildOrthonormalBasis(dir);
      const auto clampedAngle = std::clamp(light->spotAngle, 1.0f, 175.0f);
      const auto halfAngleRad = vm::to_radians(clampedAngle * 0.5f);
      const auto baseCenter = light->position + dir * (light->range * std::cos(halfAngleRad));
      const auto coneRadius = light->range * std::sin(halfAngleRad);

      auto ringPoints = std::vector<vm::vec3f>{};
      ringPoints.reserve(LightRangeCircleSegments + 1);
      for (size_t i = 0; i <= LightRangeCircleSegments; ++i)
      {
        const auto theta =
          vm::Cf::two_pi() * static_cast<float>(i) / static_cast<float>(LightRangeCircleSegments);
        ringPoints.push_back(
          baseCenter + (right * std::cos(theta) + up * std::sin(theta)) * coneRadius);
      }
      renderService.renderLineStrip(ringPoints);

      renderService.renderLine(light->position, baseCenter + right * coneRadius);
      renderService.renderLine(light->position, baseCenter - right * coneRadius);
      renderService.renderLine(light->position, baseCenter + up * coneRadius);
      renderService.renderLine(light->position, baseCenter - up * coneRadius);
      renderService.renderLine(light->position, light->position + dir * light->range);
      continue;
    }

    // Point light range sphere
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
  m_hasShadowWorldBounds = false;
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

  auto boundsBuilder = vm::bbox3f::builder{};
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
      const auto worldBounds = vm::bbox3f{entityNode->physicalBounds()};
      boundsBuilder.add(worldBounds);
      m_shadowModelCasters.push_back(
        PreviewShadowModelCaster{modelMatrix, worldBounds, renderer});
    },
    [&](const mdl::BrushNode* brushNode) {
      if (
        !editorContext.visible(*brushNode)
        || brushNode->hasAttribute(mdl::TagAttributes::Transparency))
      {
        return;
      }
      boundsBuilder.add(vm::bbox3f{brushNode->logicalBounds()});
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
      boundsBuilder.add(vm::bbox3f{patchNode->logicalBounds()});
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

  if (boundsBuilder.initialized())
  {
    m_shadowWorldBounds = boundsBuilder.bounds();
    m_hasShadowWorldBounds = true;
  }
}

} // namespace tb::render
