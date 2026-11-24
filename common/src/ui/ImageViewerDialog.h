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
#include <QFileSystemWatcher>
#include <QImage>
#include <QPointF>
#include <QWidget>

#include <filesystem>
#include <optional>
#include <vector>

class QComboBox;
class QLabel;

namespace tb::ui
{

enum class ImageChannels
{
  RGBA,
  RGB,
  Alpha,
  Red,
  Green,
  Blue,
};

/**
 * Shows an image with zoom (wheel, around the cursor) and panning (left or middle drag),
 * on a checkerboard so transparency shows. Small zoom levels are smoothed; large ones
 * show pixels as blocks.
 */
class ImageCanvas : public QWidget
{
  Q_OBJECT
private:
  QImage m_image;
  QImage m_displayed;
  ImageChannels m_channels = ImageChannels::RGBA;
  double m_zoom = 1.0;
  QPointF m_center;
  bool m_fit = true;
  std::optional<QPoint> m_dragStart;
  QPointF m_dragCenter;

public:
  explicit ImageCanvas(QWidget* parent = nullptr);

  void setImage(QImage image);
  void setChannels(ImageChannels channels);
  void fit();
  void actualSize();
  void zoomBy(double factor);
  double zoom() const;

signals:
  void zoomChanged(double zoom);

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
  void updateDisplayed();
  void updateFit();
  void setZoom(double zoom);
};

/**
 * A built-in image viewer: the image, its size and format, and buttons to open it in the
 * external image editor (Unity preferences) or the file browser. Left and Right step
 * through the images in the same folder. The image reloads when it changes on disk, so
 * edits made elsewhere show up.
 */
class ImageViewerDialog : public QDialog
{
  Q_OBJECT
private:
  std::filesystem::path m_path;
  ImageCanvas* m_canvas = nullptr;
  QLabel* m_infoLabel = nullptr;
  QLabel* m_zoomLabel = nullptr;
  QComboBox* m_channelsCombo = nullptr;
  QFileSystemWatcher m_watcher;

public:
  /**
   * Opens a viewer for the image. Viewers close themselves and are not modal.
   */
  static void showImage(const std::filesystem::path& path, QWidget* parent);

  explicit ImageViewerDialog(QWidget* parent = nullptr);

  void load(std::filesystem::path path);

protected:
  void keyPressEvent(QKeyEvent* event) override;

private:
  void createGui();
  void reload();
  void step(int offset);
  std::vector<std::filesystem::path> siblingImages() const;
};

/**
 * Opens the image in the external image editor from the preferences, or in the system's
 * default application for it when none is set.
 */
void openInExternalImageEditor(const std::filesystem::path& path);

/**
 * Opens the file browser at the file, selecting it where the platform allows.
 */
void showInFileBrowser(const std::filesystem::path& path);

/**
 * Whether the file is an image the viewer and the texture import handle.
 */
bool isSupportedImageFile(const std::filesystem::path& path);

} // namespace tb::ui
