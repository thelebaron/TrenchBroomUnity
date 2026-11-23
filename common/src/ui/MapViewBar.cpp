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

#include "MapViewBar.h"

#include <QActionGroup>
#include <QHBoxLayout>
#include <QMenu>
#include <QStackedLayout>
#include <QToolButton>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "ui/MapDocument.h"
#include "ui/MapViewLayout.h"
#include "ui/ViewConstants.h"
#include "ui/ViewEditor.h"

namespace tb::ui
{

MapViewBar::MapViewBar(MapDocument& document, QWidget* parent)
  : ContainerBar(Sides::BottomSide, parent)
{
  createGui(document);
}

QStackedLayout* MapViewBar::toolBook()
{
  return m_toolBook;
}

void MapViewBar::createGui(MapDocument& document)
{
  setAttribute(Qt::WA_MacSmallSize);

  m_toolBook = new QStackedLayout{};
  m_toolBook->setContentsMargins(0, 0, 0, 0);

  m_viewEditor = new ViewPopupEditor{document};

  auto* layoutButton = new QToolButton{};
  layoutButton->setText(tr("Map View Layout"));
  layoutButton->setPopupMode(QToolButton::InstantPopup);

  auto* layoutMenu = new QMenu{layoutButton};
  layoutButton->setMenu(layoutMenu);

  auto* layoutActions = new QActionGroup{layoutMenu};
  const auto addLayout = [&](const QString& label, const MapViewLayout viewLayout) {
    auto* action = layoutMenu->addAction(label);
    action->setCheckable(true);
    action->setData(static_cast<int>(viewLayout));
    layoutActions->addAction(action);
  };
  addLayout(tr("One Pane"), MapViewLayout::OnePane);
  addLayout(tr("Two Panes"), MapViewLayout::TwoPanes);
  addLayout(tr("Three Panes"), MapViewLayout::ThreePanes);
  addLayout(tr("Four Panes"), MapViewLayout::FourPanes);

  layoutMenu->addSeparator();
  auto* include3D = layoutMenu->addAction(tr("Include 3D View"));
  include3D->setCheckable(true);

  connect(layoutMenu, &QMenu::aboutToShow, this, [layoutActions, include3D]() {
    const auto viewLayout = pref(Preferences::MapViewLayout);
    for (auto* action : layoutActions->actions())
    {
      action->setChecked(action->data().toInt() == viewLayout);
    }
    include3D->setChecked(pref(Preferences::MapViewInclude3D));
  });
  connect(layoutActions, &QActionGroup::triggered, this, [](const QAction* action) {
    setPref(Preferences::MapViewLayout, action->data().toInt());
  });
  connect(include3D, &QAction::triggered, this, [](const bool checked) {
    setPref(Preferences::MapViewInclude3D, checked);
  });

#ifdef __APPLE__
  const auto vMargin = pref(Preferences::Theme) == Preferences::darkTheme()
                         ? LayoutConstants::MediumVMargin
                         : 0;
#else
  const auto vMargin = LayoutConstants::MediumVMargin;
#endif

  auto* layout = new QHBoxLayout{};
  layout->setContentsMargins(
    LayoutConstants::WideHMargin, vMargin, LayoutConstants::WideHMargin, vMargin);
  layout->setSpacing(LayoutConstants::WideHMargin);
  layout->addLayout(m_toolBook, 1);
  layout->addWidget(layoutButton, 0, Qt::AlignVCenter);
  layout->addWidget(m_viewEditor, 0, Qt::AlignVCenter);

  setLayout(layout);
}

} // namespace tb::ui
