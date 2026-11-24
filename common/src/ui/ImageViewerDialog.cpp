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

#include "ImageViewerDialog.h"

#include <QApplication>
#include <QBoxLayout>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QUrl>
#include <QWheelEvent>

#include "PreferenceManager.h"
#include "Preferences.h"
#include "io/DiskIO.h"
#include "io/File.h"
#include "io/PathQt.h"
#include "io/ReadFreeImageTexture.h"
#include "mdl/Texture.h"
#include "mdl/TextureBuffer.h"
#include "ui/ViewConstants.h"

#include "kdl/path_utils.h"
#include "kdl/result.h"
#include "kdl/string_compare.h"

#include <algorithm>
#include <cmath>

namespace tb::ui
{
namespace
{

constexpr auto MinimumZoom = 1.0 / 64.0;
constexpr auto MaximumZoom = 64.0;
constexpr auto ZoomStep = 1.25;

/**
 * Qt reads PNG, JPEG and BMP itself; TGA, TIFF and the rest go through FreeImage, as
 * textures do.
 */
QImage loadImage(const std::filesystem::path& path)
{
  auto image = QImage{io::pathAsQString(path)};
  if (!image.isNull())
  {
    return image.convertToFormat(QImage::Format_ARGB32);
  }

  return io::Disk::openFile(path) | kdl::and_then([](const auto& file) {
           auto reader = file->reader().buffer();
           return io::readFreeImageTexture(reader);
         })
         | kdl::transform([](const auto& texture) {
             const auto& buffers = texture.buffersIfLoaded();
             if (buffers.empty())
             {
               return QImage{};
             }
             // BGRA bytes are ARGB32 on little endian machines
             const auto format = texture.format() == GL_BGRA ? QImage::Format_ARGB32
                                                             : QImage::Format_RGBA8888;
             return QImage{
               buffers.front().data(),
               int(texture.width()),
               int(texture.height()),
               int(texture.width() * 4),
               format}
               .convertToFormat(QImage::Format_ARGB32);
           })
         | kdl::value_or(QImage{});
}

QString formatBytes(const qint64 bytes)
{
  if (bytes >= 1024 * 1024)
  {
    return QString::number(double(bytes) / (1024.0 * 1024.0), 'f', 1) + " MB";
  }
  return QString::number(double(bytes) / 1024.0, 'f', 1) + " KB";
}

} // namespace

bool isSupportedImageFile(const std::filesystem::path& path)
{
  static const auto extensions = std::vector<std::string>{
    ".png", ".tga", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".psd", ".gif", ".exr"};
  const auto extension = path.extension().string();
  return std::ranges::any_of(extensions, [&](const auto& candidate) {
    return kdl::ci::str_is_equal(extension, candidate);
  });
}

void openInExternalImageEditor(const std::filesystem::path& path)
{
  const auto& editor = pref(Preferences::ExternalImageEditor);
  if (editor.empty())
  {
    QDesktopServices::openUrl(QUrl::fromLocalFile(io::pathAsQString(path)));
    return;
  }

  QProcess::startDetached(
    io::pathAsQString(editor), {QDir::toNativeSeparators(io::pathAsQString(path))});
}

void showInFileBrowser(const std::filesystem::path& path)
{
#ifdef _WIN32
  QProcess::startDetached(
    "explorer.exe", {"/select,", QDir::toNativeSeparators(io::pathAsQString(path))});
#else
  QDesktopServices::openUrl(QUrl::fromLocalFile(io::pathAsQString(path.parent_path())));
#endif
}

ImageCanvas::ImageCanvas(QWidget* parent)
  : QWidget{parent}
{
  setMouseTracking(false);
  setMinimumSize(256, 256);
  setFocusPolicy(Qt::StrongFocus);
}

void ImageCanvas::setImage(QImage image)
{
  m_image = std::move(image);
  updateDisplayed();
  if (m_fit)
  {
    updateFit();
  }
  update();
}

void ImageCanvas::setChannels(const ImageChannels channels)
{
  m_channels = channels;
  updateDisplayed();
  update();
}

void ImageCanvas::fit()
{
  m_fit = true;
  updateFit();
  update();
}

void ImageCanvas::actualSize()
{
  m_fit = false;
  m_center = QPointF{m_image.width() / 2.0, m_image.height() / 2.0};
  setZoom(1.0);
}

void ImageCanvas::zoomBy(const double factor)
{
  m_fit = false;
  setZoom(m_zoom * factor);
}

double ImageCanvas::zoom() const
{
  return m_zoom;
}

void ImageCanvas::updateDisplayed()
{
  if (m_image.isNull() || m_channels == ImageChannels::RGBA)
  {
    m_displayed = m_image;
    return;
  }

  m_displayed = QImage{m_image.size(), QImage::Format_ARGB32};
  for (auto y = 0; y < m_image.height(); ++y)
  {
    const auto* source = reinterpret_cast<const QRgb*>(m_image.constScanLine(y));
    auto* target = reinterpret_cast<QRgb*>(m_displayed.scanLine(y));
    for (auto x = 0; x < m_image.width(); ++x)
    {
      const auto c = source[x];
      switch (m_channels)
      {
      case ImageChannels::RGB:
        target[x] = qRgb(qRed(c), qGreen(c), qBlue(c));
        break;
      case ImageChannels::Alpha:
        target[x] = qRgb(qAlpha(c), qAlpha(c), qAlpha(c));
        break;
      case ImageChannels::Red:
        target[x] = qRgb(qRed(c), qRed(c), qRed(c));
        break;
      case ImageChannels::Green:
        target[x] = qRgb(qGreen(c), qGreen(c), qGreen(c));
        break;
      case ImageChannels::Blue:
        target[x] = qRgb(qBlue(c), qBlue(c), qBlue(c));
        break;
      case ImageChannels::RGBA:
        target[x] = c;
        break;
      }
    }
  }
}

void ImageCanvas::updateFit()
{
  if (m_image.isNull())
  {
    return;
  }
  m_center = QPointF{m_image.width() / 2.0, m_image.height() / 2.0};
  const auto zoom = std::min(
    double(width()) / double(m_image.width()),
    double(height()) / double(m_image.height()));
  m_zoom = std::clamp(zoom, MinimumZoom, MaximumZoom);
  emit zoomChanged(m_zoom);
}

void ImageCanvas::setZoom(const double zoom)
{
  m_zoom = std::clamp(zoom, MinimumZoom, MaximumZoom);
  emit zoomChanged(m_zoom);
  update();
}

void ImageCanvas::paintEvent(QPaintEvent*)
{
  auto painter = QPainter{this};

  // checkerboard, so transparent parts show
  constexpr auto checker = 12;
  for (auto y = 0; y < height(); y += checker)
  {
    for (auto x = 0; x < width(); x += checker)
    {
      const auto dark = ((x + y) / checker) % 2 == 0;
      painter.fillRect(
        x, y, checker, checker, dark ? QColor{58, 58, 58} : QColor{82, 82, 82});
    }
  }

  if (m_displayed.isNull())
  {
    painter.setPen(Qt::white);
    painter.drawText(rect(), Qt::AlignCenter, tr("Could not read the image"));
    return;
  }

  painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1.0);
  painter.translate(width() / 2.0, height() / 2.0);
  painter.scale(m_zoom, m_zoom);
  painter.translate(-m_center);
  painter.drawImage(QPointF{0, 0}, m_displayed);
}

void ImageCanvas::resizeEvent(QResizeEvent* event)
{
  if (m_fit)
  {
    updateFit();
  }
  QWidget::resizeEvent(event);
}

void ImageCanvas::wheelEvent(QWheelEvent* event)
{
  const auto steps = event->angleDelta().y() / 120.0;
  if (steps == 0.0 || m_image.isNull())
  {
    return;
  }

  // keep the image point under the cursor in place
  const auto cursor = event->position();
  const auto offset = cursor - QPointF{width() / 2.0, height() / 2.0};
  const auto imagePoint = m_center + offset / m_zoom;

  m_fit = false;
  m_zoom = std::clamp(m_zoom * std::pow(ZoomStep, steps), MinimumZoom, MaximumZoom);
  m_center = imagePoint - offset / m_zoom;
  emit zoomChanged(m_zoom);
  update();
}

void ImageCanvas::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
  {
    m_dragStart = event->pos();
    m_dragCenter = m_center;
    setCursor(Qt::ClosedHandCursor);
  }
}

void ImageCanvas::mouseMoveEvent(QMouseEvent* event)
{
  if (m_dragStart)
  {
    m_fit = false;
    m_center = m_dragCenter - QPointF{event->pos() - *m_dragStart} / m_zoom;
    update();
  }
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent*)
{
  m_dragStart = std::nullopt;
  unsetCursor();
}

void ImageCanvas::mouseDoubleClickEvent(QMouseEvent*)
{
  if (m_fit)
  {
    actualSize();
  }
  else
  {
    fit();
  }
}

void ImageViewerDialog::showImage(const std::filesystem::path& path, QWidget* parent)
{
  auto* dialog = new ImageViewerDialog{parent};
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->load(path);
  dialog->resize(900, 700);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

ImageViewerDialog::ImageViewerDialog(QWidget* parent)
  : QDialog{parent, Qt::Window}
{
  createGui();
  connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() {
    // editors often replace the file, which drops the watch
    reload();
  });
}

void ImageViewerDialog::createGui()
{
  m_canvas = new ImageCanvas{};

  auto* fitButton = new QPushButton{tr("Fit")};
  fitButton->setToolTip(tr("Fit the image to the window (F)"));
  connect(fitButton, &QPushButton::clicked, m_canvas, &ImageCanvas::fit);

  auto* actualButton = new QPushButton{tr("1:1")};
  actualButton->setToolTip(tr("Show the image at its actual size (1)"));
  connect(actualButton, &QPushButton::clicked, m_canvas, &ImageCanvas::actualSize);

  m_zoomLabel = new QLabel{};
  m_zoomLabel->setMinimumWidth(56);
  connect(m_canvas, &ImageCanvas::zoomChanged, this, [this](const double zoom) {
    m_zoomLabel->setText(QString::number(std::round(zoom * 100.0)) + "%");
  });

  m_channelsCombo = new QComboBox{};
  m_channelsCombo->addItem(tr("RGBA"), int(ImageChannels::RGBA));
  m_channelsCombo->addItem(tr("RGB"), int(ImageChannels::RGB));
  m_channelsCombo->addItem(tr("Alpha"), int(ImageChannels::Alpha));
  m_channelsCombo->addItem(tr("Red"), int(ImageChannels::Red));
  m_channelsCombo->addItem(tr("Green"), int(ImageChannels::Green));
  m_channelsCombo->addItem(tr("Blue"), int(ImageChannels::Blue));
  connect(
    m_channelsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
      m_canvas->setChannels(ImageChannels(m_channelsCombo->currentData().toInt()));
    });

  auto* previousButton = new QPushButton{tr("<")};
  previousButton->setToolTip(tr("Previous image in this folder (Left)"));
  connect(previousButton, &QPushButton::clicked, this, [this]() { step(-1); });
  auto* nextButton = new QPushButton{tr(">")};
  nextButton->setToolTip(tr("Next image in this folder (Right)"));
  connect(nextButton, &QPushButton::clicked, this, [this]() { step(1); });

  auto* editButton = new QPushButton{tr("Open in Editor")};
  editButton->setToolTip(tr(
    "Open in the external image editor set in the Unity preferences, or the system's "
    "default application (E)"));
  connect(editButton, &QPushButton::clicked, this, [this]() {
    openInExternalImageEditor(m_path);
  });

  auto* showButton = new QPushButton{tr("Show in Folder")};
  connect(
    showButton, &QPushButton::clicked, this, [this]() { showInFileBrowser(m_path); });

  auto* copyButton = new QPushButton{tr("Copy Path")};
  connect(copyButton, &QPushButton::clicked, this, [this]() {
    QApplication::clipboard()->setText(
      QDir::toNativeSeparators(io::pathAsQString(m_path)));
  });

  auto* toolbar = new QHBoxLayout{};
  toolbar->setContentsMargins(
    LayoutConstants::NarrowHMargin,
    LayoutConstants::NarrowVMargin,
    LayoutConstants::NarrowHMargin,
    LayoutConstants::NarrowVMargin);
  toolbar->addWidget(previousButton);
  toolbar->addWidget(nextButton);
  toolbar->addSpacing(LayoutConstants::WideHMargin);
  toolbar->addWidget(fitButton);
  toolbar->addWidget(actualButton);
  toolbar->addWidget(m_zoomLabel);
  toolbar->addWidget(m_channelsCombo);
  toolbar->addStretch(1);
  toolbar->addWidget(editButton);
  toolbar->addWidget(showButton);
  toolbar->addWidget(copyButton);

  m_infoLabel = new QLabel{};
  m_infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_infoLabel->setContentsMargins(
    LayoutConstants::NarrowHMargin,
    LayoutConstants::NarrowVMargin,
    LayoutConstants::NarrowHMargin,
    LayoutConstants::NarrowVMargin);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addLayout(toolbar);
  layout->addWidget(m_canvas, 1);
  layout->addWidget(m_infoLabel);
  setLayout(layout);
}

void ImageViewerDialog::load(std::filesystem::path path)
{
  if (!m_path.empty())
  {
    m_watcher.removePath(io::pathAsQString(m_path));
  }
  m_path = std::move(path);
  m_canvas->fit();
  reload();
}

void ImageViewerDialog::reload()
{
  const auto pathString = io::pathAsQString(m_path);
  if (!m_watcher.files().contains(pathString) && QFileInfo::exists(pathString))
  {
    m_watcher.addPath(pathString);
  }

  const auto image = loadImage(m_path);
  m_canvas->setImage(image);

  setWindowTitle(QString::fromStdString(m_path.filename().string()) + " - Image Viewer");
  const auto info = QFileInfo{pathString};
  m_infoLabel->setText(
    image.isNull() ? QDir::toNativeSeparators(pathString)
                   : tr("%1 x %2   %3   %4   %5")
                       .arg(image.width())
                       .arg(image.height())
                       .arg(image.hasAlphaChannel() ? tr("with alpha") : tr("opaque"))
                       .arg(formatBytes(info.size()))
                       .arg(QDir::toNativeSeparators(pathString)));
}

std::vector<std::filesystem::path> ImageViewerDialog::siblingImages() const
{
  auto result = std::vector<std::filesystem::path>{};
  auto ec = std::error_code{};
  for (const auto& entry : std::filesystem::directory_iterator{m_path.parent_path(), ec})
  {
    if (entry.is_regular_file(ec) && isSupportedImageFile(entry.path()))
    {
      result.push_back(entry.path());
    }
  }
  std::ranges::sort(result);
  return result;
}

void ImageViewerDialog::step(const int offset)
{
  const auto images = siblingImages();
  if (images.empty())
  {
    return;
  }

  const auto current = std::ranges::find(images, m_path);
  const auto index = current != images.end() ? std::distance(images.begin(), current) : 0;
  const auto count = std::ptrdiff_t(images.size());
  load(images[size_t(((index + offset) % count + count) % count)]);
}

void ImageViewerDialog::keyPressEvent(QKeyEvent* event)
{
  switch (event->key())
  {
  case Qt::Key_Left:
  case Qt::Key_PageUp:
    step(-1);
    return;
  case Qt::Key_Right:
  case Qt::Key_PageDown:
    step(1);
    return;
  case Qt::Key_F:
    m_canvas->fit();
    return;
  case Qt::Key_1:
    m_canvas->actualSize();
    return;
  case Qt::Key_Plus:
  case Qt::Key_Equal:
    m_canvas->zoomBy(ZoomStep);
    return;
  case Qt::Key_Minus:
    m_canvas->zoomBy(1.0 / ZoomStep);
    return;
  case Qt::Key_E:
    openInExternalImageEditor(m_path);
    return;
  default:
    QDialog::keyPressEvent(event);
  }
}

} // namespace tb::ui
