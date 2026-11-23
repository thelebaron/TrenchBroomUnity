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

#include "Group.h"

#include "kdl/reflection_impl.h"

#include "vm/mat_io.h" // IWYU pragma: keep

#include <algorithm>

namespace tb::mdl
{

kdl_reflect_impl(Group);

Group::Group(std::string name)
  : m_name{std::move(name)}
{
}

const std::string& Group::name() const
{
  return m_name;
}

void Group::setName(std::string name)
{
  m_name = std::move(name);
}

const vm::mat4x4d& Group::transformation() const
{
  return m_transformation;
}

void Group::setTransformation(const vm::mat4x4d& transformation)
{
  m_transformation = transformation;
}

void Group::transform(const vm::mat4x4d& transformation)
{
  m_transformation = transformation * m_transformation;
}

const std::vector<EntityProperty>& Group::properties() const
{
  return m_properties;
}

const std::string* Group::property(const std::string_view key) const
{
  const auto it = std::ranges::find_if(
    m_properties, [&](const auto& property) { return property.key() == key; });
  return it != m_properties.end() ? &it->value() : nullptr;
}

void Group::setProperty(const std::string& key, const std::string& value)
{
  const auto it = std::ranges::find_if(
    m_properties, [&](const auto& property) { return property.key() == key; });
  if (it != m_properties.end())
  {
    *it = EntityProperty{key, value};
  }
  else
  {
    m_properties.emplace_back(key, value);
  }
}

void Group::removeProperty(const std::string_view key)
{
  std::erase_if(
    m_properties, [&](const auto& property) { return property.key() == key; });
}

} // namespace tb::mdl
