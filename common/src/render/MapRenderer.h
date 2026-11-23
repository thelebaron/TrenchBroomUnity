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

#pragma once

#include "Macros.h"
#include "NotifierConnection.h"
#include "mdl/RoomLayout.h"

#include "vm/vec.h"

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tb
{
class Color;
}

namespace tb::mdl
{
class BrushNode;
class BrushFaceHandle;
class GroupNode;
class LayerNode;
class Map;
class Node;
class ResourceId;
struct SelectionChange;
} // namespace tb::mdl

namespace tb::render
{
class BrushRenderer;
class EntityDecalRenderer;
class EntityLinkRenderer;
class GroupLinkRenderer;
class ObjectRenderer;
class PreviewLightRenderer;
class RenderBatch;
class RenderContext;

class MapRenderer
{
private:
  mdl::Map& m_map;

  bool m_dollhouseEnabled = false;
  bool m_dollhouseStubsDirty = false;
  mdl::DollhouseCutawayMap m_dollhouseCutaway;
  std::unordered_set<const mdl::BrushNode*> m_dollhouseHiddenBrushes;

  struct DollhouseStubEntry
  {
    std::unique_ptr<mdl::BrushNode> node;
    int renderers = 0;
  };
  std::unordered_map<const mdl::BrushNode*, DollhouseStubEntry> m_dollhouseStubs;

  std::unique_ptr<ObjectRenderer> m_defaultRenderer;
  std::unique_ptr<ObjectRenderer> m_selectionRenderer;
  std::unique_ptr<ObjectRenderer> m_lockedRenderer;
  std::unique_ptr<ObjectRenderer> m_dollhouse2DDefaultRenderer;
  std::unique_ptr<ObjectRenderer> m_dollhouse2DSelectionRenderer;
  std::unique_ptr<ObjectRenderer> m_dollhouse2DLockedRenderer;
  std::unique_ptr<BrushRenderer> m_dollhouse3DStubDefaultRenderer;
  std::unique_ptr<BrushRenderer> m_dollhouse3DStubSelectionRenderer;
  std::unique_ptr<BrushRenderer> m_dollhouse3DStubLockedRenderer;
  std::unique_ptr<EntityDecalRenderer> m_entityDecalRenderer;
  std::unique_ptr<EntityLinkRenderer> m_entityLinkRenderer;
  std::unique_ptr<GroupLinkRenderer> m_groupLinkRenderer;
  std::unique_ptr<PreviewLightRenderer> m_previewLightRenderer;

  enum class Renderer
  {
    Default = 1,
    Selection = 2,
    Locked = 4,
    All = Default | Selection | Locked
  };

  std::unordered_map<mdl::Node*, int> m_trackedNodes;

  NotifierConnection m_notifierConnection;

public:
  explicit MapRenderer(mdl::Map& map);
  ~MapRenderer();

  deleteCopyAndMove(MapRenderer);

public: // color config
  void overrideSelectionColors(const Color& color, float mix);
  void restoreSelectionColors();

public: // dollhouse cutaway (3D view only)
  bool dollhouseEnabled() const;
  void setDollhouseEnabled(bool enabled);
  bool isDollhouseCutAway(
    const mdl::BrushNode& brushNode, const vm::vec3d& hitPoint) const;

public: // rendering
  void render(RenderContext& renderContext, RenderBatch& renderBatch);

private:
  void updateDollhouseHiddenBrushes(const vm::vec3d& cameraPosition);
  void clearDollhouseStubs();
  void removeDollhouseStub(const mdl::BrushNode* brushNode);
  void rebuildDollhouseStubs();
  void setupStubRenderers();
  void reload();
  void clear();

  void setupGL(RenderBatch& renderBatch);
  void renderDefaultOpaque(RenderContext& renderContext, RenderBatch& renderBatch);
  void renderDefaultTransparent(RenderContext& renderContext, RenderBatch& renderBatch);
  void renderSelectionOpaque(RenderContext& renderContext, RenderBatch& renderBatch);
  void renderSelectionTransparent(RenderContext& renderContext, RenderBatch& renderBatch);
  void renderLockedOpaque(RenderContext& renderContext, RenderBatch& renderBatch);
  void renderLockedTransparent(RenderContext& renderContext, RenderBatch& renderBatch);
  void renderEntityDecals(RenderContext& renderContext, RenderBatch& renderBatch);
  void renderEntityLinks(RenderContext& renderContext, RenderBatch& renderBatch);
  void renderGroupLinks(RenderContext& renderContext, RenderBatch& renderBatch);

  void setupRenderers();
  void setupDefaultRenderer(ObjectRenderer& renderer);
  void setupSelectionRenderer(ObjectRenderer& renderer);
  void setupLockedRenderer(ObjectRenderer& renderer);

  static int determineDesiredRenderers(mdl::Node* node);
  void updateAndInvalidateNode(mdl::Node* node);
  void updateAndInvalidateNodeRecursive(mdl::Node* node);
  void removeNode(mdl::Node* node);
  void removeNodeRecursive(mdl::Node* node);
  void updateAllNodes();

  void invalidateRenderers(Renderer renderers);
  void invalidateEntityDecalRenderer();
  void invalidateEntityLinkRenderer();
  void invalidateGroupLinkRenderer();
  void invalidatePreviewLightRenderer();
  void invalidatePreviewLightRendererForNodes(const std::vector<mdl::Node*>& nodes);
  void reloadEntityModels();

private: // notification
  void connectObservers();

  void mapWasCreated(mdl::Map& map);
  void mapWasLoaded(mdl::Map& map);
  void mapWasCleared(mdl::Map& map);

  void nodesWereAdded(const std::vector<mdl::Node*>& nodes);
  void nodesWereRemoved(const std::vector<mdl::Node*>& nodes);
  void nodesDidChange(const std::vector<mdl::Node*>& nodes);

  void nodeVisibilityDidChange(const std::vector<mdl::Node*>& nodes);
  void nodeLockingDidChange(const std::vector<mdl::Node*>& nodes);

  void groupWasOpened(mdl::GroupNode& group);
  void groupWasClosed(mdl::GroupNode& group);

  void brushFacesDidChange(const std::vector<mdl::BrushFaceHandle>& faces);

  void selectionDidChange(const mdl::SelectionChange& selectionChange);

  void resourcesWereProcessed(const std::vector<mdl::ResourceId>& resourceIds);

  void materialCollectionsWillChange();
  void entityDefinitionsDidChange();
  void modsDidChange();

  void editorContextDidChange();

  void preferenceDidChange(const std::filesystem::path& path);
};

} // namespace tb::render
