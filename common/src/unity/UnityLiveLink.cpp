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

#include "UnityLiveLink.h"

#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

#include "Error.h"
#include "Logger.h"
#include "PreferenceManager.h"
#include "Preferences.h"
#include "mdl/Map.h"
#include "render/Camera.h"
#include "ui/FrameManager.h"
#include "ui/MapDocument.h"
#include "ui/MapFrame.h"
#include "ui/MapView3D.h"
#include "ui/MapViewBase.h"

#include "kdl/overload.h"

#include <variant>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace tb::unity
{
namespace
{
constexpr auto MaxMessageSize = qsizetype{64 * 1024};
// About 30 camera updates per second while the live camera is on.
constexpr auto LiveCameraIntervalMs = 33;

render::Camera* cameraOf(ui::MapFrame& frame)
{
  auto* mapView = frame.findChild<ui::MapView3D*>();
  return mapView ? &static_cast<ui::MapViewBase*>(mapView)->camera() : nullptr;
}

} // namespace

UnityLiveLink::UnityLiveLink(ui::FrameManager& frameManager, QObject* parent)
  : QObject{parent}
  , m_frameManager{frameManager}
  , m_server{new QLocalServer{this}}
  , m_cameraTimer{new QTimer{this}}
{
  m_server->setSocketOptions(QLocalServer::UserAccessOption);
  connect(m_server, &QLocalServer::newConnection, this, &UnityLiveLink::acceptConnection);

  m_cameraTimer->setInterval(LiveCameraIntervalMs);
  connect(m_cameraTimer, &QTimer::timeout, this, &UnityLiveLink::sendLiveCamera);

  auto& prefs = PreferenceManager::instance();
  m_notifierConnection +=
    prefs.preferenceDidChangeNotifier.connect(this, &UnityLiveLink::preferenceDidChange);
  listen(pref(Preferences::UnityLiveLinkSocketName).toStdString());
}

UnityLiveLink::~UnityLiveLink() = default;

void UnityLiveLink::listen(const std::string& socketName)
{
  disconnectClient();
  m_server->close();
  m_listenError.clear();

  if (!socketName.empty() && !m_server->listen(QString::fromStdString(socketName)))
  {
    m_listenError = m_server->errorString();
  }

  emit statusChanged();
}

bool UnityLiveLink::isConnected() const
{
  return m_client != nullptr;
}

QString UnityLiveLink::statusText() const
{
  if (!m_listenError.isEmpty())
  {
    return tr("Not listening: %1").arg(m_listenError);
  }
  if (!m_server->isListening())
  {
    return tr("Off");
  }
  if (!m_client)
  {
    return tr("Waiting for Unity on '%1'").arg(m_server->serverName());
  }
  return tr("Unity connected on '%1'").arg(m_server->serverName());
}

bool UnityLiveLink::liveCamera() const
{
  return m_liveCamera;
}

void UnityLiveLink::setLiveCamera(const bool enabled)
{
  if (enabled == m_liveCamera)
  {
    return;
  }

  m_liveCamera = enabled;
  // Forget the last pose so the current camera is sent right away.
  m_lastPose = std::nullopt;
  write(serializeLiveLinkLiveCamera(enabled));
  updateCameraTimer();
  emit statusChanged();
}

void UnityLiveLink::sendCamera()
{
  if (const auto pose = currentPose())
  {
    m_lastPose = pose;
    write(serializeLiveLinkCamera(*pose, activeMapPath()));
  }
}

void UnityLiveLink::requestCamera()
{
  write(QByteArray{"{\"type\":\"request_camera\"}\n"});
}

void UnityLiveLink::play(const LiveLinkPlayStart& start)
{
#ifdef _WIN32
  // Unity brings itself to the front to play; Windows only lets it while the foreground
  // application allows it.
  AllowSetForegroundWindow(ASFW_ANY);
#endif
  write(serializeLiveLinkPlay(start, activeMapPath()));
}

void UnityLiveLink::stop()
{
  write(serializeLiveLinkStop());
}

bool UnityLiveLink::playing() const
{
  return m_playing;
}

void UnityLiveLink::setPlaying(const bool playing)
{
  if (playing != m_playing)
  {
    m_playing = playing;
    emit statusChanged();
  }
}

void UnityLiveLink::preferenceDidChange(const std::filesystem::path& path)
{
  if (path == Preferences::UnityLiveLinkSocketName.path())
  {
    listen(pref(Preferences::UnityLiveLinkSocketName).toStdString());
  }
}

void UnityLiveLink::acceptConnection()
{
  while (m_server->hasPendingConnections())
  {
    auto* socket = m_server->nextPendingConnection();
    disconnectClient();

    m_client = socket;
    m_client->setReadBufferSize(MaxMessageSize + 1);
    connect(m_client, &QLocalSocket::readyRead, this, &UnityLiveLink::readClient);
    connect(m_client, &QLocalSocket::disconnected, this, [this, socket]() {
      if (m_client == socket)
      {
        m_client = nullptr;
        m_clientBuffer.clear();
        m_playing = false;
        updateCameraTimer();
        emit statusChanged();
      }
      socket->deleteLater();
    });

    m_lastPose = std::nullopt;
    write(serializeLiveLinkHello(activeMapPath(), m_liveCamera));
    updateCameraTimer();
    emit statusChanged();
  }
}

void UnityLiveLink::disconnectClient()
{
  if (!m_client)
  {
    return;
  }

  auto* socket = m_client;
  m_client = nullptr;
  m_clientBuffer.clear();
  m_playing = false;
  socket->disconnect(this);
  socket->disconnectFromServer();
  socket->deleteLater();
  updateCameraTimer();
}

void UnityLiveLink::readClient()
{
  m_clientBuffer += m_client->readAll();

  while (m_client)
  {
    const auto newline = m_clientBuffer.indexOf('\n');
    if (newline < 0)
    {
      break;
    }

    const auto line = m_clientBuffer.left(newline);
    m_clientBuffer.remove(0, newline + 1);
    const auto message = parseLiveLinkMessage(line);
    if (message.is_success())
    {
      handleMessage(message.value());
    }
    else
    {
      warn(std::get<Error>(message.error()).msg);
    }
  }

  if (m_clientBuffer.size() > MaxMessageSize)
  {
    warn("Unity sent a live link message over 64 KiB; disconnecting");
    disconnectClient();
    emit statusChanged();
  }
}

void UnityLiveLink::handleMessage(const LiveLinkMessage& message)
{
  std::visit(
    kdl::overload(
      [&](const LiveLinkHello& hello) {
        if (hello.version != LiveLinkProtocolVersion)
        {
          warn(
            "Unity uses live link protocol version " + std::to_string(hello.version)
            + ", TrenchBroom uses " + std::to_string(LiveLinkProtocolVersion));
        }
      },
      [&](const LiveLinkCamera& camera) { applyCamera(camera.pose); },
      [&](const LiveLinkRequestCamera&) { sendCamera(); },
      [&](const LiveLinkLiveCamera& liveCamera) {
        // Unity already knows; don't send the change back.
        m_liveCamera = liveCamera.enabled;
        m_lastPose = std::nullopt;
        updateCameraTimer();
        emit statusChanged();
      },
      [&](const LiveLinkPlayState& playState) { setPlaying(playState.playing); }),
    message);
}

void UnityLiveLink::applyCamera(const LiveLinkCameraPose& pose)
{
  auto* frame = activeFrame();
  auto* camera = frame ? cameraOf(*frame) : nullptr;
  if (!camera)
  {
    return;
  }

  camera->moveTo(pose.position);
  camera->setDirection(pose.direction, pose.up);
  m_lastPose = currentPose();
}

void UnityLiveLink::sendLiveCamera()
{
  // The active application leads. While Unity is active, it sends and TrenchBroom
  // follows.
  if (QGuiApplication::applicationState() != Qt::ApplicationActive)
  {
    return;
  }

  const auto pose = currentPose();
  if (pose && pose != m_lastPose)
  {
    m_lastPose = pose;
    write(serializeLiveLinkCamera(*pose, activeMapPath()));
  }
}

void UnityLiveLink::updateCameraTimer()
{
  if (m_client && m_liveCamera)
  {
    m_cameraTimer->start();
  }
  else
  {
    m_cameraTimer->stop();
  }
}

void UnityLiveLink::write(const QByteArray& message)
{
  if (m_client)
  {
    m_client->write(message);
  }
}

void UnityLiveLink::warn(const std::string& message) const
{
  if (auto* frame = activeFrame())
  {
    frame->logger().warn() << "Unity live link: " << message;
  }
}

ui::MapFrame* UnityLiveLink::activeFrame() const
{
  return m_frameManager.topFrame();
}

std::optional<LiveLinkCameraPose> UnityLiveLink::currentPose() const
{
  auto* frame = activeFrame();
  auto* camera = frame ? cameraOf(*frame) : nullptr;
  if (!camera)
  {
    return std::nullopt;
  }
  return LiveLinkCameraPose{camera->position(), camera->direction(), camera->up()};
}

std::filesystem::path UnityLiveLink::activeMapPath() const
{
  auto* frame = activeFrame();
  return frame ? frame->document().map().path() : std::filesystem::path{};
}

} // namespace tb::unity
