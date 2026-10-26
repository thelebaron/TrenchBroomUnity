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

#include <QObject>
#include <QString>

#include "NotifierConnection.h"
#include "unity/UnityLiveLinkProtocol.h"

#include <optional>
#include <string>

class QLocalServer;
class QLocalSocket;
class QTimer;

namespace tb::ui
{
class FrameManager;
class MapFrame;
} // namespace tb::ui

namespace tb::unity
{

/**
 * A persistent connection to the Unity editor, used to keep the TrenchBroom 3D camera and
 * Unity's scene view camera in sync.
 *
 * TrenchBroom listens on a local socket (a named pipe on Windows) and the Unity editor
 * connects to it. Only one Unity editor is connected at a time; a new connection replaces
 * the previous one. Messages are JSON lines in both directions, see
 * UnityLiveLinkProtocol.md.
 *
 * With the live camera on, whichever application is active sends its camera and the
 * other one follows, so the two never fight over the camera.
 */
class UnityLiveLink : public QObject
{
  Q_OBJECT
private:
  ui::FrameManager& m_frameManager;
  QLocalServer* m_server = nullptr;
  QLocalSocket* m_client = nullptr;
  QByteArray m_clientBuffer;
  QTimer* m_cameraTimer = nullptr;
  QString m_listenError;
  bool m_liveCamera = false;
  // The camera last sent to or received from Unity. The live camera only sends when
  // the camera differs from this, so a camera received from Unity is never echoed back.
  std::optional<LiveLinkCameraPose> m_lastPose;
  NotifierConnection m_notifierConnection;

public:
  explicit UnityLiveLink(ui::FrameManager& frameManager, QObject* parent = nullptr);
  ~UnityLiveLink() override;

  /**
   * Stops listening and starts again on the given socket name. An empty name turns the
   * live link off. The live link listens on the socket name preference when it is
   * created and whenever that preference changes. A failure shows in statusText().
   */
  void listen(const std::string& socketName);

  bool isConnected() const;
  QString statusText() const;

  bool liveCamera() const;
  void setLiveCamera(bool enabled);

  void sendCamera();
  void requestCamera();

signals:
  void statusChanged();

private:
  void preferenceDidChange(const std::filesystem::path& path);
  void acceptConnection();
  void disconnectClient();
  void readClient();
  void handleMessage(const LiveLinkMessage& message);
  void applyCamera(const LiveLinkCameraPose& pose);
  void sendLiveCamera();
  void updateCameraTimer();
  void write(const QByteArray& message);
  void warn(const std::string& message) const;

  ui::MapFrame* activeFrame() const;
  std::optional<LiveLinkCameraPose> currentPose() const;
  std::filesystem::path activeMapPath() const;
};

} // namespace tb::unity
