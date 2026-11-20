/*
 Copyright (C) 2010 Kristian Duske

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

#include "RenderContext.h"

#include "render/Camera.h"

namespace tb::render
{

RenderContext::RenderContext(
  const RenderMode renderMode,
  const Camera& camera,
  FontManager& fontManager,
  ShaderManager& shaderManager)
  : m_renderMode{renderMode}
  , m_camera{camera}
  , m_transformation{m_camera.projectionMatrix(), m_camera.viewMatrix()}
  , m_fontManager{fontManager}
  , m_shaderManager{shaderManager}
{
}

bool RenderContext::render2D() const
{
  return m_renderMode == RenderMode::Render2D;
}

bool RenderContext::render3D() const
{
  return m_renderMode == RenderMode::Render3D;
}

const Camera& RenderContext::camera() const
{
  return m_camera;
}

Transformation& RenderContext::transformation()
{
  return m_transformation;
}

FontManager& RenderContext::fontManager()
{
  return m_fontManager;
}

ShaderManager& RenderContext::shaderManager()
{
  return m_shaderManager;
}

int RenderContext::minFilterMode() const
{
  return m_textureMinFilter;
}

int RenderContext::magFilterMode() const
{
  return m_textureMagFilter;
}

void RenderContext::setFilterMode(const int minFilter, const int magFilter)
{
  m_textureMinFilter = minFilter;
  m_textureMagFilter = magFilter;
}

bool RenderContext::showMaterials() const
{
  return m_showMaterials;
}

void RenderContext::setShowMaterials(const bool showMaterials)
{
  m_showMaterials = showMaterials;
}

bool RenderContext::showFaces() const
{
  return m_renderMode == RenderMode::Render3D && m_showFaces;
}

void RenderContext::setShowFaces(const bool showFaces)
{
  m_showFaces = showFaces;
}

bool RenderContext::showEdges() const
{
  return m_renderMode == RenderMode::Render2D || m_showEdges;
}

void RenderContext::setShowEdges(const bool showEdges)
{
  m_showEdges = showEdges;
}

bool RenderContext::shadeFaces() const
{
  return m_shadeFaces;
}

void RenderContext::setShadeFaces(const bool shadeFaces)
{
  m_shadeFaces = shadeFaces;
}

bool RenderContext::showPointEntities() const
{
  return m_showPointEntities;
}

void RenderContext::setShowPointEntities(const bool showPointEntities)
{
  m_showPointEntities = showPointEntities;
}

bool RenderContext::showPointEntityModels() const
{
  return m_showPointEntityModels;
}

void RenderContext::setShowPointEntityModels(const bool showPointEntityModels)
{
  m_showPointEntityModels = showPointEntityModels;
}

bool RenderContext::showEntityClassnames() const
{
  return m_showEntityClassnames;
}

void RenderContext::setShowEntityClassnames(const bool showEntityClassnames)
{
  m_showEntityClassnames = showEntityClassnames;
}

bool RenderContext::showGroupBounds() const
{
  return m_showGroupBounds;
}

void RenderContext::setShowGroupBounds(const bool showGroupBounds)
{
  m_showGroupBounds = showGroupBounds;
}

bool RenderContext::showBrushEntityBounds() const
{
  return m_showBrushEntityBounds;
}

void RenderContext::setShowBrushEntityBounds(const bool showBrushEntityBounds)
{
  m_showBrushEntityBounds = showBrushEntityBounds;
}

bool RenderContext::showPointEntityBounds() const
{
  return m_showPointEntityBounds;
}

void RenderContext::setShowPointEntityBounds(const bool showPointEntityBounds)
{
  m_showPointEntityBounds = showPointEntityBounds;
}

bool RenderContext::showFog() const
{
  return m_showFog;
}

void RenderContext::setShowFog(const bool showFog)
{
  m_showFog = showFog;
}

bool RenderContext::previewLighting() const
{
  return m_previewLighting;
}

void RenderContext::setPreviewLighting(const bool previewLighting)
{
  m_previewLighting = previewLighting;
}

bool RenderContext::previewLightingShadows() const
{
  return m_previewLightingShadows;
}

void RenderContext::setPreviewLightingShadows(const bool previewLightingShadows)
{
  m_previewLightingShadows = previewLightingShadows;
}

bool RenderContext::previewLightingGI() const
{
  return m_previewLightingGI;
}

void RenderContext::setPreviewLightingGI(const bool previewLightingGI)
{
  m_previewLightingGI = previewLightingGI;
}

float RenderContext::previewLightingGIBounce() const
{
  return m_previewLightingGIBounce;
}

void RenderContext::setPreviewLightingGIBounce(const float previewLightingGIBounce)
{
  m_previewLightingGIBounce = previewLightingGIBounce;
}

float RenderContext::previewLightingAmbient() const
{
  return m_previewLightingAmbient;
}

void RenderContext::setPreviewLightingAmbient(const float previewLightingAmbient)
{
  m_previewLightingAmbient = previewLightingAmbient;
}

int RenderContext::previewLightCount() const
{
  return m_previewLightCount;
}

void RenderContext::setPreviewLightCount(const int previewLightCount)
{
  m_previewLightCount = previewLightCount;
}

bool RenderContext::previewDirShadowEnabled() const
{
  return m_previewDirShadowEnabled;
}

void RenderContext::setPreviewDirShadowEnabled(const bool previewDirShadowEnabled)
{
  m_previewDirShadowEnabled = previewDirShadowEnabled;
}

const vm::mat4x4f& RenderContext::previewDirLightViewProj() const
{
  return m_previewDirLightViewProj;
}

void RenderContext::setPreviewDirLightViewProj(const vm::mat4x4f& previewDirLightViewProj)
{
  m_previewDirLightViewProj = previewDirLightViewProj;
}

bool RenderContext::previewGIReady() const
{
  return m_previewGIReady;
}

void RenderContext::setPreviewGIReady(const bool previewGIReady)
{
  m_previewGIReady = previewGIReady;
}

bool RenderContext::previewGIPending() const
{
  return m_previewGIPending;
}

void RenderContext::setPreviewGIPending(const bool previewGIPending)
{
  m_previewGIPending = previewGIPending;
}

const vm::vec3f& RenderContext::previewGIBoundsMin() const
{
  return m_previewGIBoundsMin;
}

const vm::vec3f& RenderContext::previewGIBoundsMax() const
{
  return m_previewGIBoundsMax;
}

const vm::vec3f& RenderContext::previewGIDims() const
{
  return m_previewGIDims;
}

void RenderContext::setPreviewGIGrid(
  const vm::vec3f& boundsMin, const vm::vec3f& boundsMax, const vm::vec3f& dims)
{
  m_previewGIBoundsMin = boundsMin;
  m_previewGIBoundsMax = boundsMax;
  m_previewGIDims = dims;
}

bool RenderContext::showGrid() const
{
  return m_showGrid;
}

void RenderContext::setShowGrid(const bool showGrid)
{
  m_showGrid = showGrid;
}

double RenderContext::gridSize() const
{
  return m_gridSize;
}

void RenderContext::setGridSize(const double gridSize)
{
  m_gridSize = gridSize;
}

float RenderContext::dpiScale() const
{
  return m_dpiScale;
}

void RenderContext::setDpiScale(const float dpiScale)
{
  m_dpiScale = dpiScale;
}

const vm::bbox3f& RenderContext::softMapBounds() const
{
  return m_softMapBounds;
}

void RenderContext::setSoftMapBounds(const vm::bbox3f& softMapBounds)
{
  m_softMapBounds = softMapBounds;
}

bool RenderContext::hideSelection() const
{
  return m_hideSelection;
}

void RenderContext::setHideSelection()
{
  m_hideSelection = true;
}

bool RenderContext::tintSelection() const
{
  return m_tintSelection;
}

void RenderContext::clearTintSelection()
{
  m_tintSelection = false;
}

bool RenderContext::showSelectionGuide() const
{
  return m_showSelectionGuide == ShowSelectionGuide::Show
         || m_showSelectionGuide == ShowSelectionGuide::ForceShow;
}

void RenderContext::setShowSelectionGuide()
{
  setShowSelectionGuide(ShowSelectionGuide::Show);
}

void RenderContext::setHideSelectionGuide()
{
  setShowSelectionGuide(ShowSelectionGuide::Hide);
}

void RenderContext::setForceShowSelectionGuide()
{
  setShowSelectionGuide(ShowSelectionGuide::ForceShow);
}

void RenderContext::setForceHideSelectionGuide()
{
  setShowSelectionGuide(ShowSelectionGuide::ForceHide);
}

void RenderContext::setShowSelectionGuide(const ShowSelectionGuide showSelectionGuide)
{
  switch (showSelectionGuide)
  {
  case ShowSelectionGuide::Show:
    if (m_showSelectionGuide == ShowSelectionGuide::Hide)
    {
      m_showSelectionGuide = ShowSelectionGuide::Show;
    }
    break;
  case ShowSelectionGuide::Hide:
    if (m_showSelectionGuide == ShowSelectionGuide::Show)
    {
      m_showSelectionGuide = ShowSelectionGuide::Hide;
    }
    break;
  case ShowSelectionGuide::ForceShow:
    m_showSelectionGuide = ShowSelectionGuide::ForceShow;
    break;
  case ShowSelectionGuide::ForceHide:
    if (m_showSelectionGuide != ShowSelectionGuide::ForceShow)
    {
      m_showSelectionGuide = ShowSelectionGuide::ForceHide;
    }
    break;
  }
}

} // namespace tb::render
