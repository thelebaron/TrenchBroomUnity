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

#include "Result.h"

#include <memory>
#include <string>

namespace tb::ui
{
class CommandLineControlService;

/**
 * Local JSON-lines transport for CommandLineControlService.
 *
 * The server is started only when explicitly requested on the command line. Requests
 * are handled on the GUI thread and operate on the current active document.
 */
class CommandLineControlServer
{
private:
  class Impl;
  std::unique_ptr<Impl> m_impl;

public:
  explicit CommandLineControlServer(CommandLineControlService& service);
  ~CommandLineControlServer();

  CommandLineControlServer(const CommandLineControlServer&) = delete;
  CommandLineControlServer& operator=(const CommandLineControlServer&) = delete;

  Result<void> listen(const std::string& socketName);
};

} // namespace tb::ui
