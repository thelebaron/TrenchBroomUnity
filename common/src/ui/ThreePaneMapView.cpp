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

#include "ThreePaneMapView.h"

#include <QHBoxLayout>

#include "ui/CyclingMapView.h"
#include "ui/MapDocument.h"
#include "ui/MapView2D.h"
#include "ui/QtUtils.h"
#include "ui/Splitter.h"

namespace tb::ui
{

ThreePaneMapView::ThreePaneMapView(
  MapDocument& document,
  MapViewToolBox& toolBox,
  render::MapRenderer& mapRenderer,
  GLContextManager& contextManager,
  const bool include3D,
  QWidget* parent)
  : MultiPaneMapView{parent}
  , m_document{document}
{
  createGui(toolBox, mapRenderer, contextManager, include3D);
}

ThreePaneMapView::~ThreePaneMapView()
{
  saveWindowState(m_hSplitter);
  saveWindowState(m_vSplitter);
}

void ThreePaneMapView::createGui(
  MapViewToolBox& toolBox,
  render::MapRenderer& mapRenderer,
  GLContextManager& contextManager,
  const bool include3D)
{
  m_hSplitter = new Splitter{DrawKnob::No};
  m_hSplitter->setObjectName("ThreePaneMapView_HorizontalSplitter");

  m_vSplitter = new Splitter{Qt::Vertical, DrawKnob::No};
  m_vSplitter->setObjectName("ThreePaneMapView_VerticalSplitter");

  m_primaryView = new CyclingMapView{
    m_document,
    toolBox,
    mapRenderer,
    contextManager,
    include3D ? CyclingMapView::View_3D : CyclingMapView::View_YZ};
  m_mapViewXY = new MapView2D{
    m_document, toolBox, mapRenderer, contextManager, MapView2D::ViewPlane::XY};
  m_mapViewZZ = new CyclingMapView{
    m_document, toolBox, mapRenderer, contextManager, CyclingMapView::View_ZZ};

  m_primaryView->linkCamera(m_linkHelper);
  m_mapViewXY->linkCamera(m_linkHelper);
  m_mapViewZZ->linkCamera(m_linkHelper);

  addMapView(m_primaryView);
  addMapView(m_mapViewXY);
  addMapView(m_mapViewZZ);

  // See comment in CyclingMapView::createGui
  auto* layout = new QHBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  setLayout(layout);
  layout->addWidget(m_hSplitter);

  // Add splitter children
  m_vSplitter->addWidget(m_mapViewXY);
  m_vSplitter->addWidget(m_mapViewZZ);

  m_hSplitter->addWidget(m_primaryView);
  m_hSplitter->addWidget(m_vSplitter);

  // Configure minimum child sizes and initial splitter position at 50%
  m_mapViewXY->setMinimumSize(100, 100);
  m_mapViewZZ->setMinimumSize(100, 100);
  m_primaryView->setMinimumSize(100, 100);

  m_hSplitter->setSizes(QList<int>{1, 1});
  m_vSplitter->setSizes(QList<int>{1, 1});

  restoreWindowState(m_hSplitter);
  restoreWindowState(m_vSplitter);
}

void ThreePaneMapView::maximizeView(MapView* view)
{
  assert(view == m_primaryView || view == m_mapViewXY || view == m_mapViewZZ);
  if (view == m_primaryView)
  {
    m_vSplitter->hide();
  }
  else if (view == m_mapViewXY)
  {
    m_mapViewZZ->hide();
    m_primaryView->hide();
  }
  else if (view == m_mapViewZZ)
  {
    m_mapViewXY->hide();
    m_primaryView->hide();
  }
}

void ThreePaneMapView::restoreViews()
{
  for (int i = 0; i < 2; ++i)
  {
    m_hSplitter->widget(i)->show();
    m_vSplitter->widget(i)->show();
  }
}

} // namespace tb::ui
