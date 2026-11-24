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

#include "MapRenderer.h"

#include "PreferenceManager.h"
#include "Preferences.h"
#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/EntityModelManager.h"
#include "mdl/EntityNode.h"
#include "mdl/Game.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MaterialManager.h"
#include "mdl/Node.h"
#include "mdl/PatchNode.h"
#include "mdl/RoomLayout.h"
#include "mdl/SelectionChange.h"
#include "mdl/WorldNode.h"
#include "render/BrushRenderer.h"
#include "render/Camera.h"
#include "render/EntityDecalRenderer.h"
#include "render/EntityLinkRenderer.h"
#include "render/GroupLinkRenderer.h"
#include "render/ObjectRenderer.h"
#include "render/PreviewLighting.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"
#include "render/RenderUtils.h"

#include "kdl/overload.h"
#include "kdl/path_utils.h"

#include <vector>

namespace tb::render
{
namespace
{

using ExcludedBrushSet = std::unordered_set<const mdl::BrushNode*>;

class SelectedBrushRendererFilter : public BrushRenderer::DefaultFilter
{
private:
  const ExcludedBrushSet* m_excludedBrushes;

public:
  explicit SelectedBrushRendererFilter(
    const mdl::EditorContext& context, const ExcludedBrushSet* excludedBrushes = nullptr)
    : DefaultFilter{context}
    , m_excludedBrushes{excludedBrushes}
  {
  }

  RenderSettings markFaces(const mdl::BrushNode& brushNode) const override
  {
    if (m_excludedBrushes && m_excludedBrushes->contains(&brushNode))
    {
      return renderNothing();
    }
    if (!(visible(brushNode) && editable(brushNode)))
    {
      return renderNothing();
    }

    const auto brushSelected = selected(brushNode);
    const auto& brush = brushNode.brush();
    for (const auto& face : brush.faces())
    {
      face.setMarked(brushSelected || selected(brushNode, face));
    }
    return {FaceRenderPolicy::RenderMarked, EdgeRenderPolicy::RenderIfEitherFaceMarked};
  }
};

class LockedBrushRendererFilter : public BrushRenderer::DefaultFilter
{
private:
  const ExcludedBrushSet* m_excludedBrushes;

public:
  explicit LockedBrushRendererFilter(
    const mdl::EditorContext& context, const ExcludedBrushSet* excludedBrushes = nullptr)
    : DefaultFilter{context}
    , m_excludedBrushes{excludedBrushes}
  {
  }

  RenderSettings markFaces(const mdl::BrushNode& brushNode) const override
  {
    if (m_excludedBrushes && m_excludedBrushes->contains(&brushNode))
    {
      return renderNothing();
    }
    if (!visible(brushNode))
    {
      return renderNothing();
    }

    const auto& brush = brushNode.brush();
    bool hasMarkedFaces = false;
    for (const auto& face : brush.faces())
    {
      const bool mark = visible(brushNode, face);
      face.setMarked(mark);
      hasMarkedFaces |= mark;
    }

    if (!hasMarkedFaces)
    {
      return renderNothing();
    }

    return {FaceRenderPolicy::RenderMarked, EdgeRenderPolicy::RenderAll};
  }
};

class UnselectedBrushRendererFilter : public BrushRenderer::DefaultFilter
{
private:
  const ExcludedBrushSet* m_excludedBrushes;

public:
  explicit UnselectedBrushRendererFilter(
    const mdl::EditorContext& context, const ExcludedBrushSet* excludedBrushes = nullptr)
    : DefaultFilter{context}
    , m_excludedBrushes{excludedBrushes}
  {
  }

  RenderSettings markFaces(const mdl::BrushNode& brushNode) const override
  {
    if (m_excludedBrushes && m_excludedBrushes->contains(&brushNode))
    {
      return renderNothing();
    }

    const auto brushVisible = visible(brushNode);
    const auto brushEditable = editable(brushNode);

    const auto renderFaces = (brushVisible && brushEditable);
    auto renderEdges = (brushVisible && !selected(brushNode));

    if (!renderFaces && !renderEdges)
    {
      return renderNothing();
    }

    const auto& brush = brushNode.brush();

    auto anyFaceVisible = false;
    for (const auto& face : brush.faces())
    {
      const bool faceVisible = !selected(brushNode, face) && visible(brushNode, face);
      face.setMarked(faceVisible);
      anyFaceVisible |= faceVisible;
    }

    if (!anyFaceVisible)
    {
      return renderNothing();
    }

    // Render all edges if only one face is visible.
    renderEdges |= anyFaceVisible;

    return {
      renderFaces ? FaceRenderPolicy::RenderMarked : FaceRenderPolicy::RenderNone,
      renderEdges ? EdgeRenderPolicy::RenderAll : EdgeRenderPolicy::RenderNone};
  }
};

std::unique_ptr<ObjectRenderer> createDefaultRenderer(
  mdl::Map& map, const ExcludedBrushSet* excludedBrushes = nullptr)
{
  return std::make_unique<ObjectRenderer>(
    map.logger(),
    map.entityModelManager(),
    map.editorContext(),
    UnselectedBrushRendererFilter{map.editorContext(), excludedBrushes});
}

std::unique_ptr<ObjectRenderer> createSelectionRenderer(
  mdl::Map& map, const ExcludedBrushSet* excludedBrushes = nullptr)
{
  return std::make_unique<ObjectRenderer>(
    map.logger(),
    map.entityModelManager(),
    map.editorContext(),
    SelectedBrushRendererFilter{map.editorContext(), excludedBrushes});
}

std::unique_ptr<ObjectRenderer> createLockRenderer(
  mdl::Map& map, const ExcludedBrushSet* excludedBrushes = nullptr)
{
  return std::make_unique<ObjectRenderer>(
    map.logger(),
    map.entityModelManager(),
    map.editorContext(),
    LockedBrushRendererFilter{map.editorContext(), excludedBrushes});
}

std::unique_ptr<EntityDecalRenderer> createEntityDecalRenderer(mdl::Map& map)
{
  return std::make_unique<EntityDecalRenderer>(map);
}

} // namespace

MapRenderer::MapRenderer(mdl::Map& map)
  : m_map{map}
  , m_defaultRenderer{createDefaultRenderer(m_map, &m_dollhouseHiddenBrushes)}
  , m_selectionRenderer{createSelectionRenderer(m_map, &m_dollhouseHiddenBrushes)}
  , m_lockedRenderer{createLockRenderer(m_map, &m_dollhouseHiddenBrushes)}
  , m_dollhouse2DDefaultRenderer{createDefaultRenderer(m_map)}
  , m_dollhouse2DSelectionRenderer{createSelectionRenderer(m_map)}
  , m_dollhouse2DLockedRenderer{createLockRenderer(m_map)}
  , m_dollhouse3DStubDefaultRenderer{std::make_unique<BrushRenderer>()}
  , m_dollhouse3DStubSelectionRenderer{std::make_unique<BrushRenderer>()}
  , m_dollhouse3DStubLockedRenderer{std::make_unique<BrushRenderer>()}
  , m_entityDecalRenderer{createEntityDecalRenderer(m_map)}
  , m_entityLinkRenderer{std::make_unique<EntityLinkRenderer>(m_map)}
  , m_groupLinkRenderer{std::make_unique<GroupLinkRenderer>(m_map)}
  , m_previewLightRenderer{std::make_unique<PreviewLightRenderer>(m_map)}
{
  connectObservers();
  setupRenderers();
}

MapRenderer::~MapRenderer()
{
  clear();
}

void MapRenderer::overrideSelectionColors(const Color& color, const float mix)
{
  const auto edgeColor = pref(Preferences::SelectedEdgeColor).mixed(color, mix);
  const auto occludedEdgeColor = pref(Preferences::SelectedFaceColor).mixed(color, mix);
  const auto tintColor = pref(Preferences::SelectedFaceColor).mixed(color, mix);

  for (auto* renderer :
       {m_selectionRenderer.get(), m_dollhouse2DSelectionRenderer.get()})
  {
    renderer->setEntityBoundsColor(edgeColor);
    renderer->setBrushEdgeColor(edgeColor);
    renderer->setOccludedEdgeColor(occludedEdgeColor);
    renderer->setTintColor(tintColor);
  }
  m_dollhouse3DStubSelectionRenderer->setEdgeColor(edgeColor);
  m_dollhouse3DStubSelectionRenderer->setOccludedEdgeColor(occludedEdgeColor);
  m_dollhouse3DStubSelectionRenderer->setTintColor(tintColor);
}

void MapRenderer::restoreSelectionColors()
{
  setupSelectionRenderer(*m_selectionRenderer);
  setupSelectionRenderer(*m_dollhouse2DSelectionRenderer);
  setupStubRenderers();
}

bool MapRenderer::dollhouseEnabled() const
{
  return m_dollhouseEnabled;
}

void MapRenderer::setDollhouseEnabled(const bool enabled)
{
  if (m_dollhouseEnabled == enabled)
  {
    return;
  }

  m_dollhouseEnabled = enabled;
  if (!m_dollhouseEnabled)
  {
    m_dollhouseStubsDirty = false;
    m_dollhouseCutaway.clear();
    clearDollhouseStubs();

    if (!m_dollhouseHiddenBrushes.empty())
    {
      const auto oldHidden = std::move(m_dollhouseHiddenBrushes);
      m_dollhouseHiddenBrushes.clear();
      m_dollhouse2DDefaultRenderer->clear();
      m_dollhouse2DSelectionRenderer->clear();
      m_dollhouse2DLockedRenderer->clear();

      for (const auto* brush : oldHidden)
      {
        auto* node = const_cast<mdl::BrushNode*>(brush);
        if (const auto it = m_trackedNodes.find(node); it != m_trackedNodes.end())
        {
          const auto renderers = it->second;
          if (renderers & int(Renderer::Default))
          {
            m_defaultRenderer->invalidateNode(node);
          }
          if (renderers & int(Renderer::Selection))
          {
            m_selectionRenderer->invalidateNode(node);
          }
          if (renderers & int(Renderer::Locked))
          {
            m_lockedRenderer->invalidateNode(node);
          }
        }
      }
    }
  }
  else
  {
    m_dollhouseStubsDirty = true;
  }
}

bool MapRenderer::isDollhouseCutAway(
  const mdl::BrushNode& brushNode, const vm::vec3d& hitPoint) const
{
  if (!m_dollhouseEnabled)
  {
    return false;
  }
  const auto it = m_dollhouseCutaway.find(&brushNode);
  if (it == m_dollhouseCutaway.end())
  {
    return false;
  }
  if (!it->second)
  {
    return true;
  }
  return hitPoint.z() > *it->second + 0.01;
}

void MapRenderer::clearDollhouseStubs()
{
  m_dollhouse3DStubDefaultRenderer->clear();
  m_dollhouse3DStubSelectionRenderer->clear();
  m_dollhouse3DStubLockedRenderer->clear();
  m_dollhouseStubs.clear();
}

void MapRenderer::removeDollhouseStub(const mdl::BrushNode* brushNode)
{
  if (const auto it = m_dollhouseStubs.find(brushNode); it != m_dollhouseStubs.end())
  {
    const auto* stubNode = it->second.node.get();
    const auto renderers = it->second.renderers;
    if (renderers & int(Renderer::Default))
    {
      m_dollhouse3DStubDefaultRenderer->removeBrush(stubNode);
    }
    if (renderers & int(Renderer::Selection))
    {
      m_dollhouse3DStubSelectionRenderer->removeBrush(stubNode);
    }
    if (renderers & int(Renderer::Locked))
    {
      m_dollhouse3DStubLockedRenderer->removeBrush(stubNode);
    }
    m_dollhouseStubs.erase(it);
  }
}

void MapRenderer::rebuildDollhouseStubs()
{
  clearDollhouseStubs();
  m_dollhouseStubsDirty = false;
  if (!m_dollhouseEnabled)
  {
    return;
  }

  const auto& editorContext = m_map.editorContext();
  for (const auto& [brushNode, stubTopZ] : m_dollhouseCutaway)
  {
    if (!stubTopZ || !editorContext.visible(*brushNode))
    {
      continue;
    }

    auto* mutableNode = const_cast<mdl::BrushNode*>(brushNode);
    const auto it = m_trackedNodes.find(mutableNode);
    if (it == m_trackedNodes.end() || it->second == 0)
    {
      continue;
    }

    auto stubBrush = mdl::createDollhouseWallStub(m_map, *brushNode, *stubTopZ);
    if (stubBrush.is_error())
    {
      continue;
    }

    auto stubNode = std::make_unique<mdl::BrushNode>(std::move(stubBrush).value());
    const auto renderers = it->second;
    if (renderers & int(Renderer::Default))
    {
      m_dollhouse3DStubDefaultRenderer->addBrush(stubNode.get());
    }
    if (renderers & int(Renderer::Selection))
    {
      m_dollhouse3DStubSelectionRenderer->addBrush(stubNode.get());
    }
    if (renderers & int(Renderer::Locked))
    {
      m_dollhouse3DStubLockedRenderer->addBrush(stubNode.get());
    }
    m_dollhouseStubs.emplace(
      brushNode, DollhouseStubEntry{std::move(stubNode), renderers});
  }
}

void MapRenderer::setupStubRenderers()
{
  m_dollhouse3DStubDefaultRenderer->setFaceColor(pref(Preferences::FaceColor));
  m_dollhouse3DStubDefaultRenderer->setEdgeColor(pref(Preferences::EdgeColor));
  m_dollhouse3DStubDefaultRenderer->setShowEdges(false);
  m_dollhouse3DStubDefaultRenderer->setTint(false);
  m_dollhouse3DStubDefaultRenderer->setTransparencyAlpha(
    pref(Preferences::TransparentFaceAlpha));

  m_dollhouse3DStubSelectionRenderer->setFaceColor(pref(Preferences::FaceColor));
  m_dollhouse3DStubSelectionRenderer->setEdgeColor(pref(Preferences::SelectedEdgeColor));
  m_dollhouse3DStubSelectionRenderer->setShowEdges(true);
  m_dollhouse3DStubSelectionRenderer->setShowOccludedEdges(true);
  m_dollhouse3DStubSelectionRenderer->setOccludedEdgeColor(Color(
    pref(Preferences::SelectedEdgeColor), pref(Preferences::OccludedSelectedEdgeAlpha)));
  m_dollhouse3DStubSelectionRenderer->setTint(true);
  m_dollhouse3DStubSelectionRenderer->setTintColor(pref(Preferences::SelectedFaceColor));

  m_dollhouse3DStubLockedRenderer->setFaceColor(pref(Preferences::FaceColor));
  m_dollhouse3DStubLockedRenderer->setEdgeColor(pref(Preferences::LockedEdgeColor));
  m_dollhouse3DStubLockedRenderer->setShowOccludedEdges(false);
  m_dollhouse3DStubLockedRenderer->setTint(true);
  m_dollhouse3DStubLockedRenderer->setTintColor(pref(Preferences::LockedFaceColor));
  m_dollhouse3DStubLockedRenderer->setTransparencyAlpha(
    pref(Preferences::TransparentFaceAlpha));
}

void MapRenderer::updateDollhouseHiddenBrushes(const vm::vec3d& cameraPosition)
{
  auto newCutaway = mdl::collectDollhouseCutaway(m_map, cameraPosition);
  if (newCutaway == m_dollhouseCutaway)
  {
    if (m_dollhouseStubsDirty)
    {
      rebuildDollhouseStubs();
    }
    return;
  }

  auto newHidden = std::unordered_set<const mdl::BrushNode*>{};
  newHidden.reserve(newCutaway.size());
  for (const auto& [brush, stubTopZ] : newCutaway)
  {
    newHidden.insert(brush);
  }

  auto entering = std::vector<mdl::BrushNode*>{};
  auto leaving = std::vector<mdl::BrushNode*>{};

  for (const auto* brush : newHidden)
  {
    if (!m_dollhouseHiddenBrushes.contains(brush))
    {
      entering.push_back(const_cast<mdl::BrushNode*>(brush));
    }
  }
  for (const auto* brush : m_dollhouseHiddenBrushes)
  {
    if (!newHidden.contains(brush))
    {
      leaving.push_back(const_cast<mdl::BrushNode*>(brush));
    }
  }

  m_dollhouseCutaway = std::move(newCutaway);
  m_dollhouseHiddenBrushes = std::move(newHidden);

  for (auto* node : entering)
  {
    if (const auto it = m_trackedNodes.find(node); it != m_trackedNodes.end())
    {
      const auto renderers = it->second;
      if (renderers & int(Renderer::Default))
      {
        m_defaultRenderer->invalidateNode(node);
        m_dollhouse2DDefaultRenderer->addNode(node);
      }
      if (renderers & int(Renderer::Selection))
      {
        m_selectionRenderer->invalidateNode(node);
        m_dollhouse2DSelectionRenderer->addNode(node);
      }
      if (renderers & int(Renderer::Locked))
      {
        m_lockedRenderer->invalidateNode(node);
        m_dollhouse2DLockedRenderer->addNode(node);
      }
    }
  }

  for (auto* node : leaving)
  {
    if (const auto it = m_trackedNodes.find(node); it != m_trackedNodes.end())
    {
      const auto renderers = it->second;
      if (renderers & int(Renderer::Default))
      {
        m_dollhouse2DDefaultRenderer->removeNode(node);
        m_defaultRenderer->invalidateNode(node);
      }
      if (renderers & int(Renderer::Selection))
      {
        m_dollhouse2DSelectionRenderer->removeNode(node);
        m_selectionRenderer->invalidateNode(node);
      }
      if (renderers & int(Renderer::Locked))
      {
        m_dollhouse2DLockedRenderer->removeNode(node);
        m_lockedRenderer->invalidateNode(node);
      }
    }
  }

  rebuildDollhouseStubs();
}

void MapRenderer::render(RenderContext& renderContext, RenderBatch& renderBatch)
{
  setupGL(renderBatch);
  if (renderContext.render3D() && m_dollhouseEnabled)
  {
    updateDollhouseHiddenBrushes(vm::vec3d{renderContext.camera().position()});
  }
  m_previewLightRenderer->prepare(renderContext, renderBatch);
  if (renderContext.showEditorOverlays())
  {
    renderEntityLinks(renderContext, renderBatch);
    renderGroupLinks(renderContext, renderBatch);
  }

  renderDefaultOpaque(renderContext, renderBatch);
  renderLockedOpaque(renderContext, renderBatch);
  renderSelectionOpaque(renderContext, renderBatch);

  // after the opaque geometry, so decals with soft alpha blend over the faces they sit on
  renderEntityDecals(renderContext, renderBatch);

  renderDefaultTransparent(renderContext, renderBatch);
  renderLockedTransparent(renderContext, renderBatch);
  renderSelectionTransparent(renderContext, renderBatch);

  if (renderContext.showEditorOverlays())
  {
    m_previewLightRenderer->renderGizmos(renderContext, renderBatch);
  }
}

void MapRenderer::reload()
{
  clear();
  updateAllNodes();
  invalidateEntityLinkRenderer();
  invalidatePreviewLightRenderer();
}

void MapRenderer::clear()
{
  m_dollhouseStubsDirty = false;
  m_dollhouseCutaway.clear();
  m_dollhouseHiddenBrushes.clear();
  clearDollhouseStubs();
  m_defaultRenderer->clear();
  m_selectionRenderer->clear();
  m_lockedRenderer->clear();
  m_dollhouse2DDefaultRenderer->clear();
  m_dollhouse2DSelectionRenderer->clear();
  m_dollhouse2DLockedRenderer->clear();
  m_entityDecalRenderer->clear();
  m_entityLinkRenderer->invalidate();
  m_groupLinkRenderer->invalidate();
  m_previewLightRenderer->clear();
  m_trackedNodes.clear();
}

class SetupGL : public Renderable
{
private:
  void doRender(RenderContext&) override
  {
    glAssert(glFrontFace(GL_CW));
    glAssert(glEnable(GL_CULL_FACE));
    glAssert(glEnable(GL_DEPTH_TEST));
    glAssert(glDepthFunc(GL_LEQUAL));
    glResetEdgeOffset();
  }
};

void MapRenderer::setupGL(RenderBatch& renderBatch)
{
  renderBatch.addOneShot(new SetupGL{});
}

void MapRenderer::renderDefaultOpaque(
  RenderContext& renderContext, RenderBatch& renderBatch)
{
  m_defaultRenderer->setShowOverlays(renderContext.render3D());
  m_defaultRenderer->renderOpaque(renderContext, renderBatch);
  if (renderContext.render3D() && !m_dollhouseStubs.empty())
  {
    m_dollhouse3DStubDefaultRenderer->renderOpaque(renderContext, renderBatch);
  }
  else if (renderContext.render2D() && !m_dollhouseHiddenBrushes.empty())
  {
    m_dollhouse2DDefaultRenderer->setShowOverlays(false);
    m_dollhouse2DDefaultRenderer->renderOpaque(renderContext, renderBatch);
  }
}

void MapRenderer::renderDefaultTransparent(
  RenderContext& renderContext, RenderBatch& renderBatch)
{
  m_defaultRenderer->setShowOverlays(renderContext.render3D());
  m_defaultRenderer->renderTransparent(renderContext, renderBatch);
  if (renderContext.render3D() && !m_dollhouseStubs.empty())
  {
    m_dollhouse3DStubDefaultRenderer->renderTransparent(renderContext, renderBatch);
  }
  else if (renderContext.render2D() && !m_dollhouseHiddenBrushes.empty())
  {
    m_dollhouse2DDefaultRenderer->setShowOverlays(false);
    m_dollhouse2DDefaultRenderer->renderTransparent(renderContext, renderBatch);
  }
}

void MapRenderer::renderSelectionOpaque(
  RenderContext& renderContext, RenderBatch& renderBatch)
{
  if (!renderContext.hideSelection())
  {
    m_selectionRenderer->renderOpaque(renderContext, renderBatch);
    if (renderContext.render3D() && !m_dollhouseStubs.empty())
    {
      m_dollhouse3DStubSelectionRenderer->renderOpaque(renderContext, renderBatch);
    }
    else if (renderContext.render2D() && !m_dollhouseHiddenBrushes.empty())
    {
      m_dollhouse2DSelectionRenderer->renderOpaque(renderContext, renderBatch);
    }
  }
}

void MapRenderer::renderSelectionTransparent(
  RenderContext& renderContext, RenderBatch& renderBatch)
{
  if (!renderContext.hideSelection())
  {
    m_selectionRenderer->renderTransparent(renderContext, renderBatch);
    if (renderContext.render3D() && !m_dollhouseStubs.empty())
    {
      m_dollhouse3DStubSelectionRenderer->renderTransparent(renderContext, renderBatch);
    }
    else if (renderContext.render2D() && !m_dollhouseHiddenBrushes.empty())
    {
      m_dollhouse2DSelectionRenderer->renderTransparent(renderContext, renderBatch);
    }
  }
}

void MapRenderer::renderLockedOpaque(
  RenderContext& renderContext, RenderBatch& renderBatch)
{
  m_lockedRenderer->setShowOverlays(renderContext.render3D());
  m_lockedRenderer->renderOpaque(renderContext, renderBatch);
  if (renderContext.render3D() && !m_dollhouseStubs.empty())
  {
    m_dollhouse3DStubLockedRenderer->renderOpaque(renderContext, renderBatch);
  }
  else if (renderContext.render2D() && !m_dollhouseHiddenBrushes.empty())
  {
    m_dollhouse2DLockedRenderer->setShowOverlays(false);
    m_dollhouse2DLockedRenderer->renderOpaque(renderContext, renderBatch);
  }
}

void MapRenderer::renderLockedTransparent(
  RenderContext& renderContext, RenderBatch& renderBatch)
{
  m_lockedRenderer->setShowOverlays(renderContext.render3D());
  m_lockedRenderer->renderTransparent(renderContext, renderBatch);
  if (renderContext.render3D() && !m_dollhouseStubs.empty())
  {
    m_dollhouse3DStubLockedRenderer->renderTransparent(renderContext, renderBatch);
  }
  else if (renderContext.render2D() && !m_dollhouseHiddenBrushes.empty())
  {
    m_dollhouse2DLockedRenderer->setShowOverlays(false);
    m_dollhouse2DLockedRenderer->renderTransparent(renderContext, renderBatch);
  }
}

void MapRenderer::renderEntityDecals(
  RenderContext& renderContext, RenderBatch& renderBatch)
{
  // only render decals in the 3D view
  if (renderContext.render3D())
  {
    m_entityDecalRenderer->render(renderContext, renderBatch);
  }
  else
  {
    // atlas decals are outlined in 2D views, which don't draw their entity boxes
    m_entityDecalRenderer->render2D(renderContext, renderBatch);
  }
}

void MapRenderer::renderEntityLinks(
  RenderContext& renderContext, RenderBatch& renderBatch)
{
  m_entityLinkRenderer->render(renderContext, renderBatch);
}

void MapRenderer::renderGroupLinks(RenderContext& renderContext, RenderBatch& renderBatch)
{
  m_groupLinkRenderer->render(renderContext, renderBatch);
}

void MapRenderer::setupRenderers()
{
  setupDefaultRenderer(*m_defaultRenderer);
  setupSelectionRenderer(*m_selectionRenderer);
  setupLockedRenderer(*m_lockedRenderer);
  setupDefaultRenderer(*m_dollhouse2DDefaultRenderer);
  setupSelectionRenderer(*m_dollhouse2DSelectionRenderer);
  setupLockedRenderer(*m_dollhouse2DLockedRenderer);
  setupStubRenderers();
}

void MapRenderer::setupDefaultRenderer(ObjectRenderer& renderer)
{
  renderer.setEntityOverlayTextColor(pref(Preferences::InfoOverlayTextColor));
  renderer.setGroupOverlayTextColor(pref(Preferences::GroupInfoOverlayTextColor));
  renderer.setOverlayBackgroundColor(pref(Preferences::InfoOverlayBackgroundColor));
  renderer.setTint(false);
  renderer.setTransparencyAlpha(pref(Preferences::TransparentFaceAlpha));

  renderer.setGroupBoundsColor(pref(Preferences::DefaultGroupColor));
  renderer.setEntityBoundsColor(pref(Preferences::UndefinedEntityColor));

  renderer.setBrushFaceColor(pref(Preferences::FaceColor));
  renderer.setBrushEdgeColor(pref(Preferences::EdgeColor));
}

void MapRenderer::setupSelectionRenderer(ObjectRenderer& renderer)
{
  renderer.setEntityOverlayTextColor(pref(Preferences::SelectedInfoOverlayTextColor));
  renderer.setGroupOverlayTextColor(pref(Preferences::SelectedInfoOverlayTextColor));
  renderer.setOverlayBackgroundColor(
    pref(Preferences::SelectedInfoOverlayBackgroundColor));
  renderer.setShowBrushEdges(true);
  renderer.setShowOccludedObjects(true);
  renderer.setOccludedEdgeColor(Color(
    pref(Preferences::SelectedEdgeColor), pref(Preferences::OccludedSelectedEdgeAlpha)));
  renderer.setTint(true);
  renderer.setTintColor(pref(Preferences::SelectedFaceColor));

  renderer.setOverrideGroupColors(true);
  renderer.setGroupBoundsColor(pref(Preferences::SelectedEdgeColor));

  renderer.setOverrideEntityBoundsColor(true);
  renderer.setEntityBoundsColor(pref(Preferences::SelectedEdgeColor));
  renderer.setShowEntityAngles(true);
  renderer.setEntityAngleColor(pref(Preferences::AngleIndicatorColor));

  renderer.setBrushFaceColor(pref(Preferences::FaceColor));
  renderer.setBrushEdgeColor(pref(Preferences::SelectedEdgeColor));
}

void MapRenderer::setupLockedRenderer(ObjectRenderer& renderer)
{
  renderer.setEntityOverlayTextColor(pref(Preferences::LockedInfoOverlayTextColor));
  renderer.setGroupOverlayTextColor(pref(Preferences::LockedInfoOverlayTextColor));
  renderer.setOverlayBackgroundColor(pref(Preferences::LockedInfoOverlayBackgroundColor));
  renderer.setShowOccludedObjects(false);
  renderer.setTint(true);
  renderer.setTintColor(pref(Preferences::LockedFaceColor));
  renderer.setTransparencyAlpha(pref(Preferences::TransparentFaceAlpha));

  renderer.setOverrideGroupColors(true);
  renderer.setGroupBoundsColor(pref(Preferences::LockedEdgeColor));

  renderer.setOverrideEntityBoundsColor(true);
  renderer.setEntityBoundsColor(pref(Preferences::LockedEdgeColor));
  renderer.setShowEntityAngles(false);

  renderer.setBrushFaceColor(pref(Preferences::FaceColor));
  renderer.setBrushEdgeColor(pref(Preferences::LockedEdgeColor));
}

static bool selected(const mdl::Node* node)
{
  return node->selected() || node->descendantSelected() || node->parentSelected();
}

int MapRenderer::determineDesiredRenderers(mdl::Node* node)
{
  int result = 0;

  node->accept(kdl::overload(
    [](mdl::WorldNode*) {},
    [](mdl::LayerNode*) {},
    [&](mdl::GroupNode* group) {
      if (group->locked())
      {
        result = int(Renderer::Locked);
      }
      else if (selected(group) || group->opened())
      {
        result = int(Renderer::Selection);
      }
      else
      {
        result = int(Renderer::Default);
      }
    },
    [&](mdl::EntityNode* entity) {
      if (entity->locked())
      {
        result = int(Renderer::Locked);
      }
      else if (selected(entity))
      {
        result = int(Renderer::Selection);
      }
      else
      {
        result = int(Renderer::Default);
      }
    },
    [&](mdl::BrushNode* brush) {
      if (brush->locked())
      {
        result = int(Renderer::Locked);
      }
      else if (selected(brush) || brush->hasSelectedFaces())
      {
        result = int(Renderer::Selection);
      }
      if (!brush->selected() && !brush->parentSelected() && !brush->locked())
      {
        result |= int(Renderer::Default);
      }
    },
    [&](mdl::PatchNode* patchNode) {
      if (patchNode->locked())
      {
        result = int(Renderer::Locked);
      }
      else if (selected(patchNode))
      {
        result = int(Renderer::Selection);
      }
      if (!patchNode->selected() && !patchNode->parentSelected() && !patchNode->locked())
      {
        result |= int(Renderer::Default);
      }
    }));
  return result;
}

/**
 * - Determine which renderers the given node should be in
 * - Remove from any renderers the node shouldn't be in
 * - Add to desired renderers, if not already present
 * - Invalidate, for any renderers it was already present in
 */
void MapRenderer::updateAndInvalidateNode(mdl::Node* node)
{
  const auto desiredRenderers = determineDesiredRenderers(node);
  int currentRenderers = 0;

  if (auto it = m_trackedNodes.find(node); it != m_trackedNodes.end())
  {
    currentRenderers = it->second;
  }

  auto updateForRenderer = [&](const Renderer r, ObjectRenderer* o) {
    const auto isRDesired = (desiredRenderers & int(r)) != 0;
    const auto isRCurrent = (currentRenderers & int(r)) != 0;

    if (isRCurrent && !isRDesired)
    {
      o->removeNode(node);
    }
    else if (!isRCurrent && isRDesired)
    {
      o->addNode(node);
    }
    else if (isRCurrent && isRDesired)
    {
      o->invalidateNode(node);
    }
  };

  updateForRenderer(Renderer::Default, m_defaultRenderer.get());
  updateForRenderer(Renderer::Selection, m_selectionRenderer.get());
  updateForRenderer(Renderer::Locked, m_lockedRenderer.get());

  if (const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(node);
      brushNode && m_dollhouseHiddenBrushes.contains(brushNode))
  {
    updateForRenderer(Renderer::Default, m_dollhouse2DDefaultRenderer.get());
    updateForRenderer(Renderer::Selection, m_dollhouse2DSelectionRenderer.get());
    updateForRenderer(Renderer::Locked, m_dollhouse2DLockedRenderer.get());
    m_dollhouseStubsDirty = true;
  }

  // Update the metadata to reflect the changes that we made above
  m_trackedNodes[node] = desiredRenderers;

  m_entityDecalRenderer->updateNode(node);
}

void MapRenderer::updateAndInvalidateNodeRecursive(mdl::Node* node)
{
  node->accept(kdl::overload(
    [](auto&& thisLambda, mdl::WorldNode* world) { world->visitChildren(thisLambda); },
    [](auto&& thisLambda, mdl::LayerNode* layer) { layer->visitChildren(thisLambda); },
    [&](auto&& thisLambda, mdl::GroupNode* group) {
      updateAndInvalidateNode(group);
      group->visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, mdl::EntityNode* entity) {
      updateAndInvalidateNode(entity);
      entity->visitChildren(thisLambda);
    },
    [&](mdl::BrushNode* brush) { updateAndInvalidateNode(brush); },
    [&](mdl::PatchNode* patchNode) { updateAndInvalidateNode(patchNode); }));

  // Due to the definition of `selected()` above, we also need to update the parent.
  // (not recursively, though, so this has little performance impact.)
  // This handles clicking on a brush in a brush entity -> the entity label needs to
  // render as selected.
  if (node->parent())
  {
    updateAndInvalidateNode(node->parent());
  }
}

void MapRenderer::removeNode(mdl::Node* node)
{
  if (auto it = m_trackedNodes.find(node); it != m_trackedNodes.end())
  {
    const auto renderers = it->second;

    if (renderers & int(Renderer::Default))
    {
      m_defaultRenderer->removeNode(node);
    }
    if (renderers & int(Renderer::Selection))
    {
      m_selectionRenderer->removeNode(node);
    }
    if (renderers & int(Renderer::Locked))
    {
      m_lockedRenderer->removeNode(node);
    }

    if (const auto* brushNode = dynamic_cast<const mdl::BrushNode*>(node);
        brushNode && m_dollhouseHiddenBrushes.erase(brushNode) > 0u)
    {
      m_dollhouseCutaway.erase(brushNode);
      removeDollhouseStub(brushNode);
      if (renderers & int(Renderer::Default))
      {
        m_dollhouse2DDefaultRenderer->removeNode(node);
      }
      if (renderers & int(Renderer::Selection))
      {
        m_dollhouse2DSelectionRenderer->removeNode(node);
      }
      if (renderers & int(Renderer::Locked))
      {
        m_dollhouse2DLockedRenderer->removeNode(node);
      }
    }

    m_trackedNodes.erase(it);

    // At this point, none of the default/selection/locked renderers,
    // or their underlying node-type specific renderers, have a reference
    // to `node` anymore, and they won't render it.

    m_entityDecalRenderer->removeNode(node);
  }
}

void MapRenderer::removeNodeRecursive(mdl::Node* node)
{
  node->accept(kdl::overload(
    [](auto&& thisLambda, mdl::WorldNode* world) { world->visitChildren(thisLambda); },
    [](auto&& thisLambda, mdl::LayerNode* layer) { layer->visitChildren(thisLambda); },
    [&](auto&& thisLambda, mdl::GroupNode* group) {
      removeNode(group);
      group->visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, mdl::EntityNode* entity) {
      removeNode(entity);
      entity->visitChildren(thisLambda);
    },
    [&](mdl::BrushNode* brush) { removeNode(brush); },
    [&](mdl::PatchNode* patchNode) { removeNode(patchNode); }));
}

/**
 * Calls updateAndInvalidateNode() on every node in the world.
 */
void MapRenderer::updateAllNodes()
{
  updateAndInvalidateNodeRecursive(m_map.world());
}

/**
 * Marks the nodes that are already tracked in the given renderers as invalid, i.e.
 * needing to be re-rendered.
 */
void MapRenderer::invalidateRenderers(const Renderer renderers)
{
  if (int(renderers) & int(Renderer::Default))
  {
    m_defaultRenderer->invalidate();
    m_dollhouse2DDefaultRenderer->invalidate();
    m_dollhouse3DStubDefaultRenderer->invalidate();
  }
  if (int(renderers) & int(Renderer::Selection))
  {
    m_selectionRenderer->invalidate();
    m_dollhouse2DSelectionRenderer->invalidate();
    m_dollhouse3DStubSelectionRenderer->invalidate();
  }
  if (int(renderers) & int(Renderer::Locked))
  {
    m_lockedRenderer->invalidate();
    m_dollhouse2DLockedRenderer->invalidate();
    m_dollhouse3DStubLockedRenderer->invalidate();
  }
}

void MapRenderer::invalidateEntityDecalRenderer()
{
  m_entityDecalRenderer->invalidate();
}

void MapRenderer::invalidateEntityLinkRenderer()
{
  m_entityLinkRenderer->invalidate();
}

void MapRenderer::invalidateGroupLinkRenderer()
{
  m_groupLinkRenderer->invalidate();
}

void MapRenderer::invalidatePreviewLightRenderer()
{
  m_previewLightRenderer->invalidateAll();
}

void MapRenderer::invalidatePreviewLightRendererForNodes(
  const std::vector<mdl::Node*>& nodes)
{
  auto lightsChanged = false;
  auto geometryChanged = false;

  for (const auto* node : nodes)
  {
    node->accept(kdl::overload(
      [](const mdl::WorldNode*) {},
      [](const mdl::LayerNode*) {},
      [](const mdl::GroupNode*) {},
      [&](const mdl::EntityNode* entityNode) {
        lightsChanged = true;
        if (entityNode->hasChildren() || entityNode->entity().model() != nullptr)
        {
          geometryChanged = true;
        }
      },
      [&](const mdl::BrushNode*) { geometryChanged = true; },
      [&](const mdl::PatchNode*) { geometryChanged = true; }));
  }

  if (lightsChanged)
  {
    m_previewLightRenderer->invalidateLights();
  }
  if (geometryChanged)
  {
    m_previewLightRenderer->invalidateGeometry();
  }
}

void MapRenderer::reloadEntityModels()
{
  m_defaultRenderer->reloadModels();
  m_selectionRenderer->reloadModels();
  m_lockedRenderer->reloadModels();
}

void MapRenderer::connectObservers()
{
  m_notifierConnection +=
    m_map.mapWasCreatedNotifier.connect(this, &MapRenderer::mapWasCreated);
  m_notifierConnection +=
    m_map.mapWasLoadedNotifier.connect(this, &MapRenderer::mapWasLoaded);
  m_notifierConnection +=
    m_map.mapWasClearedNotifier.connect(this, &MapRenderer::mapWasCleared);
  m_notifierConnection +=
    m_map.nodesWereAddedNotifier.connect(this, &MapRenderer::nodesWereAdded);
  m_notifierConnection +=
    m_map.nodesWereRemovedNotifier.connect(this, &MapRenderer::nodesWereRemoved);
  m_notifierConnection +=
    m_map.nodesDidChangeNotifier.connect(this, &MapRenderer::nodesDidChange);
  m_notifierConnection += m_map.nodeVisibilityDidChangeNotifier.connect(
    this, &MapRenderer::nodeVisibilityDidChange);
  m_notifierConnection +=
    m_map.nodeLockingDidChangeNotifier.connect(this, &MapRenderer::nodeLockingDidChange);
  m_notifierConnection +=
    m_map.groupWasOpenedNotifier.connect(this, &MapRenderer::groupWasOpened);
  m_notifierConnection +=
    m_map.groupWasClosedNotifier.connect(this, &MapRenderer::groupWasClosed);
  m_notifierConnection +=
    m_map.brushFacesDidChangeNotifier.connect(this, &MapRenderer::brushFacesDidChange);
  m_notifierConnection +=
    m_map.selectionDidChangeNotifier.connect(this, &MapRenderer::selectionDidChange);
  m_notifierConnection += m_map.resourcesWereProcessedNotifier.connect(
    this, &MapRenderer::resourcesWereProcessed);
  m_notifierConnection += m_map.materialCollectionsWillChangeNotifier.connect(
    this, &MapRenderer::materialCollectionsWillChange);
  m_notifierConnection += m_map.entityDefinitionsDidChangeNotifier.connect(
    this, &MapRenderer::entityDefinitionsDidChange);
  m_notifierConnection += m_map.decalLibraryDidChangeNotifier.connect(
    this, &MapRenderer::invalidateEntityDecalRenderer);
  m_notifierConnection +=
    m_map.modsDidChangeNotifier.connect(this, &MapRenderer::modsDidChange);
  m_notifierConnection += m_map.editorContextDidChangeNotifier.connect(
    this, &MapRenderer::editorContextDidChange);

  auto& prefs = PreferenceManager::instance();
  m_notifierConnection +=
    prefs.preferenceDidChangeNotifier.connect(this, &MapRenderer::preferenceDidChange);
}

void MapRenderer::mapWasCreated(mdl::Map&)
{
  reload();
}

void MapRenderer::mapWasLoaded(mdl::Map&)
{
  reload();
}

void MapRenderer::mapWasCleared(mdl::Map&)
{
  clear();
}

void MapRenderer::nodesWereAdded(const std::vector<mdl::Node*>& nodes)
{
  for (auto* node : nodes)
  {
    // The nodes passed in don't include recursive children, so we need to visit them
    // ourselves.
    updateAndInvalidateNodeRecursive(node);
  }
  invalidateGroupLinkRenderer();
  invalidateEntityLinkRenderer();
  invalidatePreviewLightRenderer();
}

void MapRenderer::nodesWereRemoved(const std::vector<mdl::Node*>& nodes)
{
  for (auto* node : nodes)
  {
    // The nodes passed in don't include recursive children, so we need to visit them
    // ourselves. Otherwise deleting a group doesn't delete the brushes within.
    removeNodeRecursive(node);
  }
  invalidateGroupLinkRenderer();
  invalidateEntityLinkRenderer();
  invalidatePreviewLightRenderer();
}

void MapRenderer::nodesDidChange(const std::vector<mdl::Node*>& nodes)
{
  for (auto* node : nodes)
  {
    // nodesDidChange() will report ancestors changing, e.g. the world and layer are
    // reported as changing when a brush is dragged. So, don't update recursively here as
    // it would cause the entire map to be invalidated on every change.
    updateAndInvalidateNode(node);
  }
  invalidateEntityLinkRenderer();
  invalidateGroupLinkRenderer();
  invalidatePreviewLightRendererForNodes(nodes);
}

void MapRenderer::nodeVisibilityDidChange(const std::vector<mdl::Node*>& nodes)
{
  for (auto* node : nodes)
  {
    updateAndInvalidateNodeRecursive(node);
  }
  invalidateEntityLinkRenderer();
  invalidatePreviewLightRenderer();
}

void MapRenderer::nodeLockingDidChange(const std::vector<mdl::Node*>& nodes)
{
  for (auto* node : nodes)
  {
    updateAndInvalidateNodeRecursive(node);
  }
  invalidateEntityLinkRenderer();
}

void MapRenderer::groupWasOpened(mdl::GroupNode&)
{
  invalidateGroupLinkRenderer();
  invalidateEntityLinkRenderer();
}

void MapRenderer::groupWasClosed(mdl::GroupNode&)
{
  invalidateGroupLinkRenderer();
  invalidateEntityLinkRenderer();
}

void MapRenderer::brushFacesDidChange(const std::vector<mdl::BrushFaceHandle>& faces)
{
  for (const auto& face : faces)
  {
    updateAndInvalidateNode(face.node());
  }
  m_previewLightRenderer->invalidateGeometry();
}

void MapRenderer::selectionDidChange(const mdl::SelectionChange& selectionChange)
{
  for (const auto& face : selectionChange.deselectedBrushFaces)
  {
    updateAndInvalidateNode(face.node());
  }
  for (const auto& face : selectionChange.selectedBrushFaces)
  {
    updateAndInvalidateNode(face.node());
  }
  // These need to be recursive otherwise selecting a Group doesn't render the contents
  // selected
  for (auto* node : selectionChange.deselectedNodes)
  {
    updateAndInvalidateNodeRecursive(node);
  }
  for (auto* node : selectionChange.selectedNodes)
  {
    updateAndInvalidateNodeRecursive(node);
  }

  invalidateEntityLinkRenderer();
  invalidateGroupLinkRenderer();
}

void MapRenderer::resourcesWereProcessed(const std::vector<mdl::ResourceId>& resourceIds)
{
  const auto& materialManager = m_map.materialManager();
  const auto materials = materialManager.findMaterialsByTextureResourceId(resourceIds);

  m_defaultRenderer->invalidateMaterials(materials);
  m_selectionRenderer->invalidateMaterials(materials);
  m_lockedRenderer->invalidateMaterials(materials);
  m_dollhouse2DDefaultRenderer->invalidateMaterials(materials);
  m_dollhouse2DSelectionRenderer->invalidateMaterials(materials);
  m_dollhouse2DLockedRenderer->invalidateMaterials(materials);
  m_dollhouse3DStubDefaultRenderer->invalidateMaterials(materials);
  m_dollhouse3DStubSelectionRenderer->invalidateMaterials(materials);
  m_dollhouse3DStubLockedRenderer->invalidateMaterials(materials);

  const auto& entityModelManager = m_map.entityModelManager();
  const auto entityModels =
    entityModelManager.findEntityModelsByTextureResourceId(resourceIds);
  m_defaultRenderer->invalidateEntityModels(entityModels);
  m_selectionRenderer->invalidateEntityModels(entityModels);
  m_lockedRenderer->invalidateEntityModels(entityModels);
  if (!entityModels.empty() || !materials.empty())
  {
    m_previewLightRenderer->invalidateGeometry();
  }
}

void MapRenderer::materialCollectionsWillChange()
{
  clearDollhouseStubs();
  m_dollhouseStubsDirty = true;
  invalidateRenderers(Renderer::All);
  m_previewLightRenderer->invalidateGeometry();
}

void MapRenderer::entityDefinitionsDidChange()
{
  reloadEntityModels();
  invalidateRenderers(Renderer::All);
  invalidateEntityLinkRenderer();
  invalidatePreviewLightRenderer();
}

void MapRenderer::modsDidChange()
{
  reloadEntityModels();
  invalidateRenderers(Renderer::All);
  invalidateEntityLinkRenderer();
  invalidatePreviewLightRenderer();
}

void MapRenderer::editorContextDidChange()
{
  m_dollhouseStubsDirty = true;
  invalidateRenderers(Renderer::All);
  invalidateEntityLinkRenderer();
  invalidateGroupLinkRenderer();
  invalidatePreviewLightRenderer();
}

void MapRenderer::preferenceDidChange(const std::filesystem::path& path)
{
  const auto isRenderOnlyMapViewPref =
    path == Preferences::PreviewLighting.path()
    || path == Preferences::PreviewLightingShadows.path()
    || path == Preferences::PreviewLightingGI.path()
    || path == Preferences::PreviewLightingGIBounce.path()
    || path == Preferences::PreviewLightingAmbient.path()
    || path == Preferences::ShowEdges.path()
    || path == Preferences::ShowEditorOverlays.path()
    || path == Preferences::ShadeFaces.path() || path == Preferences::ShowFog.path();

  if (isRenderOnlyMapViewPref)
  {
    // Pure render-time uniforms/flags: do not tear down brush/entity VBOs or
    // throw away cached shadow maps and baked GI volumes on toggle.
    return;
  }

  if (path == Preferences::UnityLightUnitScale.path())
  {
    m_previewLightRenderer->invalidateLights();
    return;
  }

  setupRenderers();

  if (m_map.game()->isGamePathPreference(path))
  {
    reloadEntityModels();
    invalidateRenderers(Renderer::All);
    invalidateEntityLinkRenderer();
    invalidateGroupLinkRenderer();
    invalidatePreviewLightRenderer();
  }

  if (kdl::path_has_prefix(path, "Map view"))
  {
    invalidateRenderers(Renderer::All);
    invalidateEntityLinkRenderer();
    invalidateGroupLinkRenderer();
    invalidatePreviewLightRenderer();
  }
}

} // namespace tb::render
