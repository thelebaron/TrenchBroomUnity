/*
 Copyright (C) 2024 Kristian Duske

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

#include "LayerHighlightRenderer.h"

#include "PreferenceManager.h"
#include "Preferences.h"
#include "mdl/BrushNode.h"
#include "mdl/EditorContext.h"
#include "mdl/EntityNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Selection.h"
#include "render/RenderService.h"
#include "render/TextAnchor.h"
#include "vm/bbox.h"
#include "vm/vec.h"

#include <QObject>
#include <QString>
#include <string>

namespace tb::render
{
namespace
{
std::string layerLabel(const mdl::LayerNode& layer)
{
  return QObject::tr("Layer \"%1\"")
    .arg(QString::fromStdString(layer.layer().name()))
    .toStdString();
}

class EntityLayerAnchor : public TextAnchor3D
{
private:
  const mdl::EntityNode* m_entity;

public:
  explicit EntityLayerAnchor(const mdl::EntityNode* entity)
    : m_entity{entity}
  {
  }

private:
  vm::vec3f basePosition() const override
  {
    const auto bounds = m_entity->logicalBounds();
    return vm::vec3f{bounds.center().xy(), bounds.max.z() + 2.0f};
  }

  TextAlignment::Type alignment() const override
  {
    return TextAlignment::Top;
  }

  vm::vec2f extraOffsets(TextAlignment::Type alignment) const override
  {
    auto result = TextAnchor3D::extraOffsets(alignment);
    if (alignment & TextAlignment::Top)
    {
      result[1] += 6.0f;
    }
    return result;
  }
};

class BrushLayerAnchor : public TextAnchor3D
{
private:
  vm::bbox3d m_bounds;

public:
  explicit BrushLayerAnchor(const vm::bbox3d& bounds)
    : m_bounds(bounds)
  {
  }

private:
  vm::vec3f basePosition() const override
  {
    return vm::vec3f{m_bounds.center().xy(), m_bounds.max.z() + 2.0f};
  }

  TextAlignment::Type alignment() const override
  {
    return TextAlignment::Top;
  }

  vm::vec2f extraOffsets(TextAlignment::Type alignment) const override
  {
    auto result = TextAnchor3D::extraOffsets(alignment);
    if (alignment & TextAlignment::Top)
    {
      result[1] += 6.0f;
    }
    return result;
  }
};

} // namespace

void LayerHighlightRenderer::render(
  RenderContext& renderContext,
  RenderBatch& renderBatch,
  const mdl::Selection& selection,
  const mdl::EditorContext& editorContext)
{
  if (!selection.hasAny())
  {
    return;
  }

  auto renderService = RenderService{renderContext, renderBatch};
  renderService.setForegroundColor(pref(Preferences::InfoOverlayTextColor));
  renderService.setBackgroundColor(Color{
    pref(Preferences::InfoOverlayBackgroundColor),
    pref(Preferences::WeakInfoOverlayBackgroundAlpha),
  });
  renderService.setShowOccludedObjects();

  for (const auto* entity : selection.entities)
  {
    if (!editorContext.visible(*entity))
    {
      continue;
    }

    if (const auto* layer = entity->containingLayer())
    {
      renderService.renderString(
        layerLabel(*layer), EntityLayerAnchor{entity});
    }
  }

  for (const auto* brush : selection.brushes)
  {
    if (!editorContext.visible(*brush))
    {
      continue;
    }

    if (auto* layer = brush->containingLayer())
    {
      renderService.renderString(
        layerLabel(*layer), BrushLayerAnchor{brush->logicalBounds()});
    }
  }
}

} // namespace tb::render
