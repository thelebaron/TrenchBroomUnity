/*
 Copyright (C) 2026 Kristian Duske

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

#include <QDialog>
#include <QToolButton>

#include <string>

class QPushButton;

namespace tb::mdl
{
class Material;
}

namespace tb::ui
{
class GLContextManager;
class MapDocument;
class MaterialBrowser;

/**
 * A modal dialog that lets the user pick a material from a full material browser, with
 * the usual filtering, sorting and collection grouping.
 */
class MaterialPickerDialog : public QDialog
{
  Q_OBJECT
private:
  MaterialBrowser* m_browser = nullptr;
  QPushButton* m_okButton = nullptr;

public:
  MaterialPickerDialog(
    MapDocument& document,
    GLContextManager& contextManager,
    const std::string& initialMaterialName,
    const QString& title,
    QWidget* parent = nullptr);

  std::string selectedMaterialName() const;

private:
  void materialSelected(const mdl::Material* material);
};

/**
 * A button that shows a material swatch and name and opens a MaterialPickerDialog when
 * clicked. The swatch uses the material's average color because the texture's pixels
 * are not kept in memory once they are uploaded to the GPU.
 */
class MaterialSwatchButton : public QToolButton
{
  Q_OBJECT
private:
  MapDocument& m_document;
  GLContextManager& m_contextManager;
  QString m_pickerTitle;
  std::string m_materialName;

public:
  MaterialSwatchButton(
    MapDocument& document,
    GLContextManager& contextManager,
    QString pickerTitle,
    QWidget* parent = nullptr);

  const std::string& materialName() const;
  void setMaterialName(const std::string& materialName);

  /** Redraws the swatch, e.g. after the map's materials have been (re)loaded. */
  void refresh();

signals:
  void materialChanged(const QString& materialName);

private:
  void pickMaterial();
};

} // namespace tb::ui
