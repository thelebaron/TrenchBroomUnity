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

#include "TextRenderer.h"

#include "AttrString.h"
#include "render/ActiveShader.h"
#include "render/Camera.h"
#include "render/FontManager.h"
#include "render/PrimType.h"
#include "render/RenderContext.h"
#include "render/RenderUtils.h"
#include "render/Shaders.h"
#include "render/TextAnchor.h"
#include "render/TextureFont.h"

#include "vm/mat_ext.h"
#include "vm/vec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace tb::render
{

namespace
{

bool overlaps(const LabelRect& lhs, const LabelRect& rhs)
{
  return lhs.min.x() < rhs.min.x() + rhs.size.x()
         && rhs.min.x() < lhs.min.x() + lhs.size.x()
         && lhs.min.y() < rhs.min.y() + rhs.size.y()
         && rhs.min.y() < lhs.min.y() + lhs.size.y();
}

/**
 * Placed label rects, binned by the grid cells they touch.
 */
class LabelGrid
{
private:
  float m_cellSize;
  std::vector<LabelRect> m_rects;
  std::unordered_map<std::int64_t, std::vector<size_t>> m_cells;

public:
  explicit LabelGrid(const float cellSize)
    : m_cellSize{cellSize}
  {
  }

  bool fits(const LabelRect& rect) const
  {
    auto result = true;
    visitCells(rect, [&](const auto key) {
      if (const auto it = m_cells.find(key); result && it != m_cells.end())
      {
        result = std::none_of(it->second.begin(), it->second.end(), [&](const auto i) {
          return overlaps(m_rects[i], rect);
        });
      }
    });
    return result;
  }

  void add(const LabelRect& rect)
  {
    const auto index = m_rects.size();
    m_rects.push_back(rect);
    visitCells(rect, [&](const auto key) { m_cells[key].push_back(index); });
  }

private:
  std::int64_t cell(const float coord) const
  {
    return static_cast<std::int64_t>(std::floor(coord / m_cellSize));
  }

  template <typename F>
  void visitCells(const LabelRect& rect, const F& f) const
  {
    const auto x0 = cell(rect.min.x());
    const auto y0 = cell(rect.min.y());
    const auto x1 = cell(rect.min.x() + rect.size.x());
    const auto y1 = cell(rect.min.y() + rect.size.y());
    for (auto y = y0; y <= y1; ++y)
    {
      for (auto x = x0; x <= x1; ++x)
      {
        f((y << 32) ^ (x & 0xffffffff));
      }
    }
  }
};

} // namespace

std::vector<bool> declutterLabels(
  const std::vector<LabelRect>& labels, const float cellSize)
{
  auto grid = LabelGrid{cellSize};
  auto result = std::vector<bool>(labels.size(), false);
  for (size_t i = 0; i < labels.size(); ++i)
  {
    if (grid.fits(labels[i]))
    {
      grid.add(labels[i]);
      result[i] = true;
    }
  }
  return result;
}

const float TextRenderer::DefaultMaxViewDistance = 768.0f;
const float TextRenderer::DefaultMinZoomFactor = 0.5f;
const vm::vec2f TextRenderer::DefaultInset = vm::vec2f(4.0f, 4.0f);
const size_t TextRenderer::RectCornerSegments = 3;
const float TextRenderer::RectCornerRadius = 3.0f;

TextRenderer::TextRenderer(
  FontDescriptor fontDescriptor,
  const float maxViewDistance,
  const float minZoomFactor,
  const vm::vec2f& inset)
  : m_fontDescriptor{std::move(fontDescriptor)}
  , m_maxViewDistance{maxViewDistance}
  , m_minZoomFactor{minZoomFactor}
  , m_inset{inset}
{
}

void TextRenderer::setDeclutter(const bool declutter)
{
  m_declutter = declutter;
}

void TextRenderer::renderString(
  RenderContext& renderContext,
  const Color& textColor,
  const Color& backgroundColor,
  const AttrString& string,
  const TextAnchor& position)
{
  renderString(renderContext, textColor, backgroundColor, string, position, false);
}

void TextRenderer::renderStringOnTop(
  RenderContext& renderContext,
  const Color& textColor,
  const Color& backgroundColor,
  const AttrString& string,
  const TextAnchor& position)
{
  renderString(renderContext, textColor, backgroundColor, string, position, true);
}

void TextRenderer::renderString(
  RenderContext& renderContext,
  const Color& textColor,
  const Color& backgroundColor,
  const AttrString& string,
  const TextAnchor& position,
  const bool onTop)
{

  const auto& camera = renderContext.camera();
  const auto distance = camera.perpendicularDistanceTo(position.position(camera));
  if (distance <= 0.0f || !isVisible(renderContext, string, position, distance, onTop))
  {
    return;
  }

  auto& fontManager = renderContext.fontManager();
  auto& font = fontManager.font(m_fontDescriptor);

  auto vertices = font.quads(string, true);
  const auto alphaFactor = computeAlphaFactor(renderContext, distance, onTop);
  const auto size = font.measure(string);
  const auto offset = position.offset(camera, size);

  if (onTop)
  {
    addEntry(
      m_entriesOnTop,
      Entry{
        std::move(vertices),
        size,
        offset,
        Color{textColor, alphaFactor * textColor.a()},
        Color{backgroundColor, alphaFactor * backgroundColor.a()},
      });
  }
  else
  {
    addEntry(
      m_entries,
      Entry{
        std::move(vertices),
        size,
        offset,
        Color{textColor, alphaFactor * textColor.a()},
        Color{backgroundColor, alphaFactor * backgroundColor.a()}});
  }
}

bool TextRenderer::isVisible(
  RenderContext& renderContext,
  const AttrString& string,
  const TextAnchor& position,
  const float distance,
  const bool onTop) const
{
  if (!onTop)
  {
    if (renderContext.render3D() && distance > m_maxViewDistance)
    {
      return false;
    }
    if (renderContext.render2D() && renderContext.camera().zoom() < m_minZoomFactor)
    {
      return false;
    }
  }

  const auto& camera = renderContext.camera();
  const auto& viewport = camera.viewport();

  const auto size = stringSize(renderContext, string);
  const auto offset = vm::vec2f{position.offset(camera, size)} - m_inset;
  const auto actualSize = size + 2.0f * m_inset;

  return viewport.contains(offset.x(), offset.y(), actualSize.x(), actualSize.y());
}

float TextRenderer::computeAlphaFactor(
  const RenderContext& renderContext, const float distance, const bool onTop) const
{
  if (onTop)
  {
    return 1.0f;
  }

  if (renderContext.render3D())
  {
    const auto a = m_maxViewDistance - distance;
    return std::min(a / 128.0f, 1.0f);
  }

  const auto z = renderContext.camera().zoom();
  const auto d = z - m_minZoomFactor;
  return std::min(d / 0.3f, 1.0f);
}

void TextRenderer::addEntry(EntryCollection& collection, const Entry& entry)
{
  collection.entries.push_back(entry);
  collection.textVertexCount += entry.vertices.size();
  collection.rectVertexCount += roundedRect2DVertexCount(RectCornerSegments);
}

vm::vec2f TextRenderer::stringSize(
  RenderContext& renderContext, const AttrString& string) const
{
  auto& fontManager = renderContext.fontManager();
  auto& font = fontManager.font(m_fontDescriptor);
  return vm::round(font.measure(string));
}

void TextRenderer::doPrepareVertices(VboManager& vboManager)
{
  if (m_declutter)
  {
    declutter();
  }
  prepare(m_entries, false, vboManager);
  prepare(m_entriesOnTop, true, vboManager);
}

LabelRect TextRenderer::labelRect(const Entry& entry) const
{
  // the background rect, with a pixel to spare on each side
  const auto gap = vm::vec2f{1.0f, 1.0f};
  return {entry.offset.xy() - m_inset - gap, entry.size + 2.0f * (m_inset + gap)};
}

void TextRenderer::declutter()
{
  // labels drawn on top win over the others, and nearer labels over farther ones
  auto collections = {&m_entriesOnTop.entries, &m_entries.entries};

  auto labels = std::vector<LabelRect>{};
  for (auto* entries : collections)
  {
    std::stable_sort(
      entries->begin(), entries->end(), [](const auto& lhs, const auto& rhs) {
        return lhs.offset.z() < rhs.offset.z();
      });
    for (const auto& entry : *entries)
    {
      labels.push_back(labelRect(entry));
    }
  }

  const auto keep = declutterLabels(labels);
  auto index = size_t(0);
  for (auto* entries : collections)
  {
    auto kept = std::vector<Entry>{};
    for (auto& entry : *entries)
    {
      if (keep[index++])
      {
        kept.push_back(std::move(entry));
      }
    }
    *entries = std::move(kept);
  }
}

void TextRenderer::prepare(
  EntryCollection& collection, const bool onTop, VboManager& vboManager)
{
  auto textVertices = std::vector<TextVertex>{};
  textVertices.reserve(collection.textVertexCount);

  auto rectVertices = std::vector<RectVertex>{};
  rectVertices.reserve(collection.rectVertexCount);

  for (const auto& entry : collection.entries)
  {
    addEntry(entry, onTop, textVertices, rectVertices);
  }

  collection.textArray = VertexArray::move(std::move(textVertices));
  collection.rectArray = VertexArray::move(std::move(rectVertices));

  collection.textArray.prepare(vboManager);
  collection.rectArray.prepare(vboManager);
}

void TextRenderer::addEntry(
  const Entry& entry,
  const bool /* onTop */,
  std::vector<TextVertex>& textVertices,
  std::vector<RectVertex>& rectVertices)
{
  const auto& stringVertices = entry.vertices;
  const auto& stringSize = entry.size;

  const auto& offset = entry.offset;

  const auto& textColor = entry.textColor;
  const auto& rectColor = entry.backgroundColor;

  for (size_t i = 0; i < stringVertices.size() / 2; ++i)
  {
    const auto& position2 = stringVertices[2 * i];
    const auto& uvCoords = stringVertices[2 * i + 1];
    textVertices.emplace_back(
      vm::vec3f{position2 + offset.xy(), -offset.z()}, uvCoords, textColor);
  }

  const auto rect =
    roundedRect2D(stringSize + 2.0f * m_inset, RectCornerRadius, RectCornerSegments);
  for (size_t i = 0; i < rect.size(); ++i)
  {
    const auto& vertex = rect[i];
    rectVertices.emplace_back(
      vm::vec3f{vertex + offset.xy() + stringSize / 2.0f, -offset.z()}, rectColor);
  }
}

void TextRenderer::doRender(RenderContext& renderContext)
{
  const auto& viewport = renderContext.camera().viewport();
  const auto projection = vm::ortho_matrix(
    0.0f,
    1.0f,
    static_cast<float>(viewport.x),
    static_cast<float>(viewport.height),
    static_cast<float>(viewport.width),
    static_cast<float>(viewport.y));
  const auto view = vm::view_matrix(vm::vec3f{0, 0, -1}, vm::vec3f{0, 1, 0});
  auto ortho = ReplaceTransformation{renderContext.transformation(), projection, view};

  render(m_entries, renderContext);

  glAssert(glDisable(GL_DEPTH_TEST));
  render(m_entriesOnTop, renderContext);
  glAssert(glEnable(GL_DEPTH_TEST));
}

void TextRenderer::render(EntryCollection& collection, RenderContext& renderContext)
{
  auto& fontManager = renderContext.fontManager();
  auto& font = fontManager.font(m_fontDescriptor);

  glAssert(glDisable(GL_TEXTURE_2D));

  auto backgroundShader =
    ActiveShader{renderContext.shaderManager(), Shaders::TextBackgroundShader};
  collection.rectArray.render(PrimType::Triangles);

  glAssert(glEnable(GL_TEXTURE_2D));

  auto textShader =
    ActiveShader{renderContext.shaderManager(), Shaders::ColoredTextShader};
  textShader.set("Texture", 0);
  font.activate();
  collection.textArray.render(PrimType::Quads);
  font.deactivate();
}

} // namespace tb::render
