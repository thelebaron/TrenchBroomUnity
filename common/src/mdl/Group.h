/*
 Copyright (C) 2020 Kristian Duske

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

#include "mdl/EntityProperties.h"

#include "kdl/reflection_decl.h"

#include "vm/mat.h"

#include <string>
#include <string_view>
#include <vector>

namespace tb::mdl
{

class Group
{
private:
  std::string m_name;

  vm::mat4x4d m_transformation;

  /**
   * Additional properties that are written to the group's func_group entity. They let
   * tools attach their own data to a group (for example the room creator's recipe).
   */
  std::vector<EntityProperty> m_properties;

  kdl_reflect_decl(Group, m_name, m_transformation, m_properties);

public:
  explicit Group(std::string name);

  const std::string& name() const;
  void setName(std::string name);

  const vm::mat4x4d& transformation() const;
  void setTransformation(const vm::mat4x4d& transformation);
  void transform(const vm::mat4x4d& transformation);

  const std::vector<EntityProperty>& properties() const;
  const std::string* property(std::string_view key) const;
  void setProperty(const std::string& key, const std::string& value);
  void removeProperty(std::string_view key);
};

} // namespace tb::mdl
