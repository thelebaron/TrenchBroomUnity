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

#include <QByteArray>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLocalSocket>

#include <iostream>
#include <optional>
#include <string>

namespace
{
constexpr auto MaxRequestSize = qsizetype{1024 * 1024};

std::optional<QByteArray> readResponseLine(
  QLocalSocket& socket,
  QByteArray& buffer,
  const int timeoutMs,
  std::string& error)
{
  while (true)
  {
    const auto newline = buffer.indexOf('\n');
    if (newline >= 0)
    {
      const auto line = buffer.left(newline);
      buffer.remove(0, newline + 1);
      return line;
    }

    if (!socket.waitForReadyRead(timeoutMs))
    {
      error = socket.state() == QLocalSocket::UnconnectedState
                ? "The TrenchBroom connection closed before a response arrived"
                : "Timed out waiting for a TrenchBroom response: "
                    + socket.errorString().toStdString();
      return std::nullopt;
    }
    buffer += socket.readAll();
  }
}

bool writeRequest(
  QLocalSocket& socket,
  const std::string& line,
  const int timeoutMs,
  std::string& error)
{
  auto request = QByteArray::fromStdString(line);
  request.append('\n');

  auto offset = qsizetype{0};
  while (offset < request.size())
  {
    const auto written = socket.write(request.constData() + offset, request.size() - offset);
    if (written <= 0)
    {
      error = "Could not write to the TrenchBroom connection: "
              + socket.errorString().toStdString();
      return false;
    }
    offset += written;
  }

  while (socket.bytesToWrite() > 0)
  {
    if (!socket.waitForBytesWritten(timeoutMs))
    {
      error = "Could not send request to TrenchBroom: "
              + socket.errorString().toStdString();
      return false;
    }
  }
  return true;
}

} // namespace

int main(int argc, char* argv[])
{
  auto app = QCoreApplication{argc, argv};
  app.setApplicationName("TrenchBroomCommandLineClient");

  auto parser = QCommandLineParser{};
  parser.setApplicationDescription(
    "Streams JSON-lines requests to a running TrenchBroom command-line-control socket.");
  parser.addHelpOption();
  const auto socketOption = QCommandLineOption{
    "socket", "Local socket name used by TrenchBroom.", "name"};
  const auto timeoutOption = QCommandLineOption{
    "timeout-ms", "Timeout for connection, writes, and responses.", "milliseconds", "120000"};
  parser.addOption(socketOption);
  parser.addOption(timeoutOption);

  const auto arguments = app.arguments();
  if (arguments.contains("--help-all"))
  {
    std::cout << parser.helpText().toStdString();
    return 0;
  }
  if (!parser.parse(arguments))
  {
    std::cerr << parser.errorText().toStdString() << '\n';
    return 2;
  }
  if (parser.isSet("help"))
  {
    std::cout << parser.helpText().toStdString();
    return 0;
  }

  if (!parser.isSet(socketOption))
  {
    std::cerr << parser.helpText().toStdString();
    return 2;
  }

  const auto socketName = parser.value(socketOption);
  if (socketName.isEmpty())
  {
    std::cerr << "--socket must not be empty\n";
    return 2;
  }

  auto timeoutOk = false;
  const auto timeoutMs = parser.value(timeoutOption).toInt(&timeoutOk);
  if (!timeoutOk || timeoutMs <= 0)
  {
    std::cerr << "--timeout-ms must be a positive integer\n";
    return 2;
  }

  auto socket = QLocalSocket{};
  socket.connectToServer(socketName);
  if (!socket.waitForConnected(timeoutMs))
  {
    std::cerr << "Could not connect to TrenchBroom socket '" << socketName.toStdString()
              << "': " << socket.errorString().toStdString() << '\n';
    return 1;
  }

  auto responseBuffer = QByteArray{};
  auto line = std::string{};
  auto error = std::string{};
  while (std::getline(std::cin, line))
  {
    if (line.size() > static_cast<size_t>(MaxRequestSize))
    {
      std::cerr << "Request exceeds the 1 MiB limit\n";
      return 2;
    }
    if (!writeRequest(socket, line, timeoutMs, error))
    {
      std::cerr << error << '\n';
      return 1;
    }

    const auto response = readResponseLine(socket, responseBuffer, timeoutMs, error);
    if (!response)
    {
      std::cerr << error << '\n';
      return 1;
    }
    std::cout.write(response->constData(), response->size());
    std::cout << '\n';
    std::cout.flush();
  }

  socket.disconnectFromServer();
  return 0;
}
