/*
 Copyright (C) 2026 TrenchBroom Contributors

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

#include "PreviewGI.h"

#include "vm/constants.h"
#include "vm/util.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace tb::render
{
namespace
{

constexpr int BvhLeafThreshold = 4;
constexpr float TargetCellSize = 64.0f;
constexpr int MinGridDim = 4;
constexpr int MaxGridDim = 36;
constexpr int MaxTotalProbes = 12288;
constexpr size_t RaysPerProbe = 24;
constexpr float AoDistanceRadius = 192.0f;
constexpr float RayEpsilon = 0.5f;
constexpr float SecondBounceWeight = 0.65f;
constexpr float SpotInnerAngleRatio = 0.65f;

constexpr std::array<vm::vec3f, 6> AmbientCubeAxes = {
  vm::vec3f{1.0f, 0.0f, 0.0f},
  vm::vec3f{-1.0f, 0.0f, 0.0f},
  vm::vec3f{0.0f, 1.0f, 0.0f},
  vm::vec3f{0.0f, -1.0f, 0.0f},
  vm::vec3f{0.0f, 0.0f, 1.0f},
  vm::vec3f{0.0f, 0.0f, -1.0f},
};

struct AmbientCube
{
  std::array<vm::vec4f, 6> faces{};
};

std::array<vm::vec3f, RaysPerProbe> buildFibonacciSphereDirections()
{
  auto dirs = std::array<vm::vec3f, RaysPerProbe>{};
  const float goldenAngle = vm::Cf::pi() * (3.0f - std::sqrt(5.0f));
  for (size_t i = 0; i < RaysPerProbe; ++i)
  {
    const float y =
      1.0f - (static_cast<float>(i) + 0.5f) * (2.0f / static_cast<float>(RaysPerProbe));
    const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
    const float theta = goldenAngle * static_cast<float>(i);
    dirs[i] = vm::normalize(vm::vec3f{std::cos(theta) * r, std::sin(theta) * r, y});
  }
  return dirs;
}

bool intersectAabb(
  const vm::vec3f& origin,
  const vm::vec3f& invDir,
  const vm::vec3f& bmin,
  const vm::vec3f& bmax,
  const float tMin,
  const float tMax)
{
  float t0 = tMin;
  float t1 = tMax;

  for (size_t axis = 0; axis < 3; ++axis)
  {
    const float dMin = (bmin[axis] - origin[axis]) * invDir[axis];
    const float dMax = (bmax[axis] - origin[axis]) * invDir[axis];
    const float lo = std::min(dMin, dMax);
    const float hi = std::max(dMin, dMax);
    t0 = std::max(t0, lo);
    t1 = std::min(t1, hi);
    if (t1 < t0)
    {
      return false;
    }
  }
  return true;
}

bool intersectTriangle(
  const vm::vec3f& origin,
  const vm::vec3f& dir,
  const GiTriangle& tri,
  const float tMin,
  const float tMax,
  float& outT)
{
  const auto e1 = tri.v1 - tri.v0;
  const auto e2 = tri.v2 - tri.v0;
  const auto pvec = vm::cross(dir, e2);
  const float det = vm::dot(e1, pvec);
  if (std::abs(det) < 1e-7f)
  {
    return false;
  }

  const float invDet = 1.0f / det;
  const auto tvec = origin - tri.v0;
  const float u = vm::dot(tvec, pvec) * invDet;
  if (u < 0.0f || u > 1.0f)
  {
    return false;
  }

  const auto qvec = vm::cross(tvec, e1);
  const float v = vm::dot(dir, qvec) * invDet;
  if (v < 0.0f || u + v > 1.0f)
  {
    return false;
  }

  const float t = vm::dot(e2, qvec) * invDet;
  if (t < tMin || t > tMax)
  {
    return false;
  }

  outT = t;
  return true;
}

vm::vec3f safeInvDir(const vm::vec3f& d)
{
  const auto inv = [](const float v) {
    return std::abs(v) > 1e-7f ? (1.0f / v) : (v >= 0.0f ? 1e7f : -1e7f);
  };
  return vm::vec3f{inv(d.x()), inv(d.y()), inv(d.z())};
}

vm::vec3f retroBoundLighting(const vm::vec3f& c)
{
  const float maxChan = std::max({c.x(), c.y(), c.z(), 1.0f});
  return c / maxChan;
}

vm::vec3f evaluateDirectSurfaceRadiance(
  const GiBvh& bvh,
  const std::vector<PreviewLight>& lights,
  const vm::vec3f& hitPos,
  const vm::vec3f& hitNormal,
  const vm::vec3f& hitAlbedo)
{
  auto totalLight = vm::vec3f{0.0f, 0.0f, 0.0f};
  const auto shadowOrigin = hitPos + hitNormal * 1.5f;

  for (const auto& light : lights)
  {
    if (light.intensity <= 0.0f)
    {
      continue;
    }

    if (light.type == PreviewLightType::Directional)
    {
      const auto lightDir = vm::normalize(-light.direction);
      if (light.shadows != 0 && bvh.anyHit(shadowOrigin, lightDir, RayEpsilon, 1e5f))
      {
        continue;
      }
      float ndotl = std::clamp(vm::dot(hitNormal, lightDir), 0.0f, 1.0f);
      ndotl = ndotl * 0.5f + 0.5f;
      ndotl = ndotl * ndotl;
      totalLight = totalLight + light.color * (light.intensity * ndotl);
      continue;
    }

    if (light.range <= 0.0f)
    {
      continue;
    }

    const auto toLight = light.position - hitPos;
    const float dist = vm::length(toLight);
    if (dist >= light.range)
    {
      continue;
    }

    const auto lightDir = dist > 1e-4f ? (toLight / dist) : hitNormal;
    float atten = std::clamp(1.0f - dist / light.range, 0.0f, 1.0f);

    if (light.type == PreviewLightType::Spot)
    {
      const float outerAngle = std::clamp(light.spotAngle, 1.0f, 179.0f);
      const float innerAngle = outerAngle * SpotInnerAngleRatio;
      const float cosOuter = std::cos(vm::to_radians(outerAngle * 0.5f));
      const float cosInner = std::cos(vm::to_radians(innerAngle * 0.5f));
      const float invRange = 1.0f / std::max(cosInner - cosOuter, 0.001f);
      const float alignment = vm::dot(-light.direction, lightDir);
      const float cone = std::clamp((alignment - cosOuter) * invRange, 0.0f, 1.0f);
      atten *= cone * cone;
      if (atten <= 0.0f)
      {
        continue;
      }
    }

    if (
      light.shadows != 0 && dist > 2.0f
      && bvh.anyHit(shadowOrigin, lightDir, RayEpsilon, dist - 1.5f))
    {
      continue;
    }

    float ndotl = std::clamp(vm::dot(hitNormal, lightDir), 0.0f, 1.0f);
    ndotl = ndotl * 0.5f + 0.5f;
    ndotl = ndotl * ndotl;

    totalLight = totalLight + light.color * (light.intensity * atten * ndotl);
  }

  return retroBoundLighting(hitAlbedo * totalLight);
}

vm::vec4f evaluateAmbientCube(const AmbientCube& cube, const vm::vec3f& normal)
{
  const auto n = vm::is_zero(normal, 1e-5f) ? vm::vec3f{0, 0, 1} : vm::normalize(normal);
  const auto nSq = n * n;
  const size_t fx = n.x() >= 0.0f ? 0u : 1u;
  const size_t fy = n.y() >= 0.0f ? 2u : 3u;
  const size_t fz = n.z() >= 0.0f ? 4u : 5u;
  return cube.faces[fx] * nSq.x() + cube.faces[fy] * nSq.y() + cube.faces[fz] * nSq.z();
}

void writeCubesToVolume(
  const std::vector<AmbientCube>& cubes, GiVolumeData& volume)
{
  const auto dx = static_cast<size_t>(volume.dimX);
  const auto dy = static_cast<size_t>(volume.dimY);
  const auto dz = static_cast<size_t>(volume.dimZ);
  volume.texels.assign(dx * dy * dz * 6u * 4u, 0.0f);

  for (size_t z = 0; z < dz; ++z)
  {
    for (size_t y = 0; y < dy; ++y)
    {
      for (size_t x = 0; x < dx; ++x)
      {
        const auto& cube = cubes[(z * dy + y) * dx + x];
        for (size_t face = 0; face < 6u; ++face)
        {
          const size_t atlasZ = face * dz + z;
          const size_t texelIndex = ((atlasZ * dy + y) * dx + x) * 4u;
          const auto& val = cube.faces[face];
          volume.texels[texelIndex + 0] = val.x();
          volume.texels[texelIndex + 1] = val.y();
          volume.texels[texelIndex + 2] = val.z();
          volume.texels[texelIndex + 3] = val.w();
        }
      }
    }
  }
}

void dilateInvalidProbes(
  std::vector<AmbientCube>& cubes,
  std::vector<uint8_t>& validFlags,
  const int dimX,
  const int dimY,
  const int dimZ,
  const int iterations)
{
  const auto idx = [&](const int x, const int y, const int z) {
    return static_cast<size_t>((z * dimY + y) * dimX + x);
  };

  static constexpr std::array<std::array<int, 3>, 6> NeighborOffsets = {{
    {{1, 0, 0}},
    {{-1, 0, 0}},
    {{0, 1, 0}},
    {{0, -1, 0}},
    {{0, 0, 1}},
    {{0, 0, -1}},
  }};

  auto nextCubes = cubes;
  auto nextValid = validFlags;

  for (int iter = 0; iter < iterations; ++iter)
  {
    bool anyFilled = false;
    for (int z = 0; z < dimZ; ++z)
    {
      for (int y = 0; y < dimY; ++y)
      {
        for (int x = 0; x < dimX; ++x)
        {
          const size_t i = idx(x, y, z);
          if (validFlags[i])
          {
            continue;
          }

          // Directional dilation: for each Ambient Cube face, prefer the neighbor
          // in that face's own direction so wall-interior probes do not bleed light
          // from the room on the opposite side of a wall.
          std::array<int, 6> validNeighborIndices = {-1, -1, -1, -1, -1, -1};
          int totalValidNeighbors = 0;
          for (size_t d = 0; d < 6u; ++d)
          {
            const int nx = x + NeighborOffsets[d][0];
            const int ny = y + NeighborOffsets[d][1];
            const int nz = z + NeighborOffsets[d][2];
            if (nx < 0 || nx >= dimX || ny < 0 || ny >= dimY || nz < 0 || nz >= dimZ)
            {
              continue;
            }
            const size_t ni = idx(nx, ny, nz);
            if (validFlags[ni])
            {
              validNeighborIndices[d] = static_cast<int>(ni);
              ++totalValidNeighbors;
            }
          }

          if (totalValidNeighbors > 0)
          {
            for (size_t f = 0; f < 6u; ++f)
            {
              // NeighborOffsets order matches AmbientCubeAxes (+X, -X, +Y, -Y, +Z, -Z).
              if (validNeighborIndices[f] >= 0)
              {
                nextCubes[i].faces[f] =
                  cubes[static_cast<size_t>(validNeighborIndices[f])].faces[f];
                continue;
              }

              // Otherwise average non-opposing valid neighbors (skip the opposite face d == f ^ 1).
              const size_t opposite = f ^ 1u;
              auto accum = vm::vec4f{0.0f, 0.0f, 0.0f, 0.0f};
              int count = 0;
              for (size_t d = 0; d < 6u; ++d)
              {
                if (d == opposite || validNeighborIndices[d] < 0)
                {
                  continue;
                }
                accum = accum + cubes[static_cast<size_t>(validNeighborIndices[d])].faces[f];
                ++count;
              }

              if (count > 0)
              {
                nextCubes[i].faces[f] = accum * (1.0f / static_cast<float>(count));
              }
              else if (validNeighborIndices[opposite] >= 0)
              {
                nextCubes[i].faces[f] =
                  cubes[static_cast<size_t>(validNeighborIndices[opposite])].faces[f];
              }
            }
            nextValid[i] = 1;
            anyFilled = true;
          }
        }
      }
    }

    cubes = nextCubes;
    validFlags = nextValid;
    if (!anyFilled)
    {
      break;
    }
  }

  // Any remaining untouched void cell defaults to neutral unoccluded (0 bounce, 1 AO).
  for (size_t i = 0; i < cubes.size(); ++i)
  {
    if (!validFlags[i])
    {
      for (size_t f = 0; f < 6u; ++f)
      {
        cubes[i].faces[f] = vm::vec4f{0.0f, 0.0f, 0.0f, 1.0f};
      }
    }
  }
}

} // namespace

void GiBvh::build(std::vector<GiTriangle> triangles)
{
  m_triangles = std::move(triangles);
  m_nodes.clear();

  if (m_triangles.empty())
  {
    return;
  }

  m_nodes.reserve(m_triangles.size() * 2u);
  m_nodes.push_back(GiBvhNode{});
  buildNode(0, 0, static_cast<int>(m_triangles.size()));
}

bool GiBvh::empty() const
{
  return m_nodes.empty();
}

const std::vector<GiTriangle>& GiBvh::triangles() const
{
  return m_triangles;
}

void GiBvh::buildNode(const int nodeIndex, const int first, const int count)
{
  auto bmin = vm::vec3f::fill(std::numeric_limits<float>::max());
  auto bmax = vm::vec3f::fill(std::numeric_limits<float>::lowest());
  auto cmin = vm::vec3f::fill(std::numeric_limits<float>::max());
  auto cmax = vm::vec3f::fill(std::numeric_limits<float>::lowest());

  for (int i = first; i < first + count; ++i)
  {
    const auto& t = m_triangles[static_cast<size_t>(i)];
    bmin = vm::min(bmin, vm::min(t.v0, vm::min(t.v1, t.v2)));
    bmax = vm::max(bmax, vm::max(t.v0, vm::max(t.v1, t.v2)));
    const auto centroid = (t.v0 + t.v1 + t.v2) * (1.0f / 3.0f);
    cmin = vm::min(cmin, centroid);
    cmax = vm::max(cmax, centroid);
  }

  m_nodes[static_cast<size_t>(nodeIndex)].boundsMin = bmin;
  m_nodes[static_cast<size_t>(nodeIndex)].boundsMax = bmax;

  if (count <= BvhLeafThreshold)
  {
    m_nodes[static_cast<size_t>(nodeIndex)].leftOrFirst = first;
    m_nodes[static_cast<size_t>(nodeIndex)].count = count;
    return;
  }

  const auto extent = cmax - cmin;
  size_t axis = 0;
  if (extent.y() > extent.x())
  {
    axis = 1;
  }
  if (extent.z() > extent[axis])
  {
    axis = 2;
  }

  if (extent[axis] < 1e-4f)
  {
    m_nodes[static_cast<size_t>(nodeIndex)].leftOrFirst = first;
    m_nodes[static_cast<size_t>(nodeIndex)].count = count;
    return;
  }

  const int mid = first + count / 2;
  std::nth_element(
    m_triangles.begin() + first,
    m_triangles.begin() + mid,
    m_triangles.begin() + first + count,
    [axis](const GiTriangle& a, const GiTriangle& b) {
      const float ca = (a.v0[axis] + a.v1[axis] + a.v2[axis]);
      const float cb = (b.v0[axis] + b.v1[axis] + b.v2[axis]);
      return ca < cb;
    });

  const int leftIndex = static_cast<int>(m_nodes.size());
  m_nodes.push_back(GiBvhNode{});
  m_nodes.push_back(GiBvhNode{});

  m_nodes[static_cast<size_t>(nodeIndex)].leftOrFirst = leftIndex;
  m_nodes[static_cast<size_t>(nodeIndex)].count = 0;

  buildNode(leftIndex, first, mid - first);
  buildNode(leftIndex + 1, mid, first + count - mid);
}

std::optional<GiRayHit> GiBvh::intersectClosest(
  const vm::vec3f& origin,
  const vm::vec3f& direction,
  const float tMin,
  const float tMax) const
{
  if (m_nodes.empty())
  {
    return std::nullopt;
  }

  const auto invDir = safeInvDir(direction);
  auto closestT = tMax;
  auto bestHit = std::optional<GiRayHit>{};

  int stack[64];
  int stackPtr = 0;
  stack[stackPtr++] = 0;

  while (stackPtr > 0)
  {
    const auto& node = m_nodes[static_cast<size_t>(stack[--stackPtr])];
    if (!intersectAabb(origin, invDir, node.boundsMin, node.boundsMax, tMin, closestT))
    {
      continue;
    }

    if (node.count > 0)
    {
      for (int i = 0; i < node.count; ++i)
      {
        const auto triIdx = static_cast<size_t>(node.leftOrFirst + i);
        const auto& tri = m_triangles[triIdx];
        float hitT = 0.0f;
        if (intersectTriangle(origin, direction, tri, tMin, closestT, hitT))
        {
          closestT = hitT;
          bestHit = GiRayHit{hitT, triIdx, vm::dot(direction, tri.normal) > 0.0f};
        }
      }
    }
    else if (stackPtr + 2 <= 64)
    {
      stack[stackPtr++] = node.leftOrFirst;
      stack[stackPtr++] = node.leftOrFirst + 1;
    }
  }

  return bestHit;
}

bool GiBvh::anyHit(
  const vm::vec3f& origin,
  const vm::vec3f& direction,
  const float tMin,
  const float tMax) const
{
  if (m_nodes.empty())
  {
    return false;
  }

  const auto invDir = safeInvDir(direction);
  int stack[64];
  int stackPtr = 0;
  stack[stackPtr++] = 0;

  while (stackPtr > 0)
  {
    const auto& node = m_nodes[static_cast<size_t>(stack[--stackPtr])];
    if (!intersectAabb(origin, invDir, node.boundsMin, node.boundsMax, tMin, tMax))
    {
      continue;
    }

    if (node.count > 0)
    {
      for (int i = 0; i < node.count; ++i)
      {
        const auto triIdx = static_cast<size_t>(node.leftOrFirst + i);
        float hitT = 0.0f;
        if (intersectTriangle(
              origin, direction, m_triangles[triIdx], tMin, tMax, hitT))
        {
          return true;
        }
      }
    }
    else if (stackPtr + 2 <= 64)
    {
      stack[stackPtr++] = node.leftOrFirst;
      stack[stackPtr++] = node.leftOrFirst + 1;
    }
  }

  return false;
}

vm::vec4f sampleGiVolumeCpu(
  const GiVolumeData& volume, const vm::vec3f& worldPos, const vm::vec3f& normal)
{
  if (!volume.valid())
  {
    return vm::vec4f{0.0f, 0.0f, 0.0f, 1.0f};
  }

  const auto span = vm::max(volume.boundsMax - volume.boundsMin, vm::vec3f::fill(1.0f));
  const auto dims = vm::vec3f{
    static_cast<float>(volume.dimX),
    static_cast<float>(volume.dimY),
    static_cast<float>(volume.dimZ)};
  const auto cellSize = span / dims;
  const auto n = vm::is_zero(normal, 1e-5f) ? vm::vec3f{0, 0, 1} : vm::normalize(normal);
  const auto samplePos = worldPos + n * (cellSize * 0.35f);

  const auto uvw =
    vm::clamp((samplePos - volume.boundsMin) / span, vm::vec3f::fill(0.0f), vm::vec3f::fill(1.0f));

  const float gx = std::clamp(uvw.x() * dims.x() - 0.5f, 0.0f, dims.x() - 1.0f);
  const float gy = std::clamp(uvw.y() * dims.y() - 0.5f, 0.0f, dims.y() - 1.0f);
  const float gz = std::clamp(uvw.z() * dims.z() - 0.5f, 0.0f, dims.z() - 1.0f);

  const int x0 = static_cast<int>(std::floor(gx));
  const int y0 = static_cast<int>(std::floor(gy));
  const int z0 = static_cast<int>(std::floor(gz));
  const int x1 = std::min(x0 + 1, volume.dimX - 1);
  const int y1 = std::min(y0 + 1, volume.dimY - 1);
  const int z1 = std::min(z0 + 1, volume.dimZ - 1);

  const float tx = gx - static_cast<float>(x0);
  const float ty = gy - static_cast<float>(y0);
  const float tz = gz - static_cast<float>(z0);

  const auto sampleFace = [&](const int face) {
    const auto fetch = [&](const int x, const int y, const int z) {
      const size_t atlasZ = static_cast<size_t>(face * volume.dimZ + z);
      const size_t base =
        ((atlasZ * static_cast<size_t>(volume.dimY) + static_cast<size_t>(y))
           * static_cast<size_t>(volume.dimX)
         + static_cast<size_t>(x))
        * 4u;
      return vm::vec4f{
        volume.texels[base + 0],
        volume.texels[base + 1],
        volume.texels[base + 2],
        volume.texels[base + 3]};
    };

    const auto c000 = fetch(x0, y0, z0);
    const auto c100 = fetch(x1, y0, z0);
    const auto c010 = fetch(x0, y1, z0);
    const auto c110 = fetch(x1, y1, z0);
    const auto c001 = fetch(x0, y0, z1);
    const auto c101 = fetch(x1, y0, z1);
    const auto c011 = fetch(x0, y1, z1);
    const auto c111 = fetch(x1, y1, z1);

    const auto c00 = c000 * (1.0f - tx) + c100 * tx;
    const auto c10 = c010 * (1.0f - tx) + c110 * tx;
    const auto c01 = c001 * (1.0f - tx) + c101 * tx;
    const auto c11 = c011 * (1.0f - tx) + c111 * tx;

    const auto c0 = c00 * (1.0f - ty) + c10 * ty;
    const auto c1 = c01 * (1.0f - ty) + c11 * ty;
    return c0 * (1.0f - tz) + c1 * tz;
  };

  const auto nSq = n * n;
  const int fx = n.x() >= 0.0f ? 0 : 1;
  const int fy = n.y() >= 0.0f ? 2 : 3;
  const int fz = n.z() >= 0.0f ? 4 : 5;
  return sampleFace(fx) * nSq.x() + sampleFace(fy) * nSq.y() + sampleFace(fz) * nSq.z();
}

GiVolumeData bakePreviewGIVolume(
  const std::vector<GiTriangle>& triangles,
  const std::vector<PreviewLight>& lights,
  const uint64_t generation,
  const std::atomic<uint64_t>* cancelToken)
{
  auto result = GiVolumeData{};
  result.generation = generation;

  if (triangles.empty())
  {
    return result;
  }

  auto bmin = vm::vec3f::fill(std::numeric_limits<float>::max());
  auto bmax = vm::vec3f::fill(std::numeric_limits<float>::lowest());
  for (const auto& t : triangles)
  {
    bmin = vm::min(bmin, vm::min(t.v0, vm::min(t.v1, t.v2)));
    bmax = vm::max(bmax, vm::max(t.v0, vm::max(t.v1, t.v2)));
  }

  const auto pad = vm::vec3f::fill(TargetCellSize * 0.5f);
  bmin = bmin - pad;
  bmax = bmax + pad;
  const auto span = vm::max(bmax - bmin, vm::vec3f::fill(TargetCellSize));

  int dimX = std::clamp(static_cast<int>(std::ceil(span.x() / TargetCellSize)), MinGridDim, MaxGridDim);
  int dimY = std::clamp(static_cast<int>(std::ceil(span.y() / TargetCellSize)), MinGridDim, MaxGridDim);
  int dimZ = std::clamp(static_cast<int>(std::ceil(span.z() / TargetCellSize)), MinGridDim, MaxGridDim);

  while (dimX * dimY * dimZ > MaxTotalProbes)
  {
    if (dimX >= dimY && dimX >= dimZ && dimX > MinGridDim)
    {
      dimX = std::max(MinGridDim, dimX - 2);
    }
    else if (dimY >= dimX && dimY >= dimZ && dimY > MinGridDim)
    {
      dimY = std::max(MinGridDim, dimY - 2);
    }
    else if (dimZ > MinGridDim)
    {
      dimZ = std::max(MinGridDim, dimZ - 2);
    }
    else
    {
      break;
    }
  }

  const auto cellSize = vm::vec3f{
    span.x() / static_cast<float>(dimX),
    span.y() / static_cast<float>(dimY),
    span.z() / static_cast<float>(dimZ)};

  // Mark cells near geometry so we skip empty void probes in L-shaped or sparse maps.
  const size_t totalProbes =
    static_cast<size_t>(dimX) * static_cast<size_t>(dimY) * static_cast<size_t>(dimZ);
  auto activeMask = std::vector<uint8_t>(totalProbes, 0);
  const auto margin = cellSize * 1.5f;

  for (const auto& t : triangles)
  {
    const auto tmin = vm::min(t.v0, vm::min(t.v1, t.v2)) - margin;
    const auto tmax = vm::max(t.v0, vm::max(t.v1, t.v2)) + margin;

    const int x0 = std::clamp(static_cast<int>(std::floor((tmin.x() - bmin.x()) / cellSize.x())), 0, dimX - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor((tmin.y() - bmin.y()) / cellSize.y())), 0, dimY - 1);
    const int z0 = std::clamp(static_cast<int>(std::floor((tmin.z() - bmin.z()) / cellSize.z())), 0, dimZ - 1);
    const int x1 = std::clamp(static_cast<int>(std::floor((tmax.x() - bmin.x()) / cellSize.x())), 0, dimX - 1);
    const int y1 = std::clamp(static_cast<int>(std::floor((tmax.y() - bmin.y()) / cellSize.y())), 0, dimY - 1);
    const int z1 = std::clamp(static_cast<int>(std::floor((tmax.z() - bmin.z()) / cellSize.z())), 0, dimZ - 1);

    for (int z = z0; z <= z1; ++z)
    {
      for (int y = y0; y <= y1; ++y)
      {
        const size_t rowBase = static_cast<size_t>((z * dimY + y) * dimX);
        for (int x = x0; x <= x1; ++x)
        {
          activeMask[rowBase + static_cast<size_t>(x)] = 1;
        }
      }
    }
  }

  auto activeIndices = std::vector<size_t>{};
  activeIndices.reserve(totalProbes);
  for (size_t i = 0; i < totalProbes; ++i)
  {
    if (activeMask[i])
    {
      activeIndices.push_back(i);
    }
  }

  auto bvh = GiBvh{};
  bvh.build(triangles);

  if (cancelToken && cancelToken->load(std::memory_order_relaxed) != generation)
  {
    return GiVolumeData{};
  }

  const auto rayDirs = buildFibonacciSphereDirections();
  const auto& bvhTris = bvh.triangles();

  auto cubesBounce1 = std::vector<AmbientCube>(totalProbes);
  auto validFlags = std::vector<uint8_t>(totalProbes, 0);

  // Cache hit points for active probes so the 2nd bounce pass reuses the exact same rays!
  struct CachedHit
  {
    bool hit = false;
    bool backface = false;
    float t = 0.0f;
    vm::vec3f hitPos{};
    vm::vec3f hitNormal{};
    vm::vec3f hitAlbedo{};
    vm::vec3f directRadiance{};
  };
  auto probeHits = std::vector<std::array<CachedHit, RaysPerProbe>>(activeIndices.size());

  const auto numThreads = std::min<size_t>(
    std::max<size_t>(1u, std::thread::hardware_concurrency()),
    std::min<size_t>(8u, std::max<size_t>(1u, activeIndices.size() / 64u)));

  const auto runParallel = [&](auto&& body) {
    if (numThreads <= 1u)
    {
      body(size_t{0}, activeIndices.size());
      return;
    }

    auto workers = std::vector<std::thread>{};
    workers.reserve(numThreads);
    const size_t chunkSize = (activeIndices.size() + numThreads - 1u) / numThreads;

    for (size_t t = 0; t < numThreads; ++t)
    {
      const size_t begin = t * chunkSize;
      const size_t end = std::min(begin + chunkSize, activeIndices.size());
      if (begin >= end)
      {
        break;
      }
      workers.emplace_back([&, begin, end]() { body(begin, end); });
    }
    for (auto& w : workers)
    {
      w.join();
    }
  };

  // Pass 1: Trace hemisphere/sphere rays for 1st-bounce radiance + directional AO.
  runParallel([&](const size_t begin, const size_t end) {
    for (size_t ai = begin; ai < end; ++ai)
    {
      if (
        cancelToken && (ai & 31u) == 0u
        && cancelToken->load(std::memory_order_relaxed) != generation)
      {
        return;
      }

      const size_t probeIdx = activeIndices[ai];
      const int x = static_cast<int>(probeIdx % static_cast<size_t>(dimX));
      const int y =
        static_cast<int>((probeIdx / static_cast<size_t>(dimX)) % static_cast<size_t>(dimY));
      const int z = static_cast<int>(probeIdx / static_cast<size_t>(dimX * dimY));

      const auto probePos = vm::vec3f{
        bmin.x() + (static_cast<float>(x) + 0.5f) * cellSize.x(),
        bmin.y() + (static_cast<float>(y) + 0.5f) * cellSize.y(),
        bmin.z() + (static_cast<float>(z) + 0.5f) * cellSize.z()};

      auto faceAccum = std::array<vm::vec4f, 6>{};
      auto faceWeight = std::array<float, 6>{};
      size_t backfaceCount = 0;

      for (size_t r = 0; r < RaysPerProbe; ++r)
      {
        const auto& dir = rayDirs[r];
        auto& cached = probeHits[ai][r];

        const auto hit = bvh.intersectClosest(probePos, dir, RayEpsilon, 1e5f);
        auto sampleRadiance = vm::vec3f{0.0f, 0.0f, 0.0f};
        float sampleVis = 1.0f;

        if (hit)
        {
          cached.hit = true;
          cached.backface = hit->backface;
          cached.t = hit->t;
          if (hit->backface)
          {
            ++backfaceCount;
            sampleVis = 0.0f;
          }
          else
          {
            const auto& tri = bvhTris[hit->triangleIndex];
            cached.hitPos = probePos + dir * hit->t;
            cached.hitNormal = tri.normal;
            cached.hitAlbedo = tri.albedo;
            cached.directRadiance = evaluateDirectSurfaceRadiance(
              bvh, lights, cached.hitPos, cached.hitNormal, cached.hitAlbedo);
            sampleRadiance = cached.directRadiance;
            sampleVis = std::clamp(hit->t / AoDistanceRadius, 0.0f, 1.0f);
          }
        }

        // Project incoming sample from direction `dir` onto the Ambient Cube faces
        // whose surface normal receives light from `dir` (dot(axis, dir) > 0).
        for (size_t f = 0; f < 6u; ++f)
        {
          const float w = vm::dot(AmbientCubeAxes[f], dir);
          if (w > 0.0f)
          {
            faceAccum[f] = faceAccum[f] + vm::vec4f{sampleRadiance, sampleVis} * w;
            faceWeight[f] += w;
          }
        }
      }

      if (backfaceCount * 3u <= RaysPerProbe)
      {
        auto cube = AmbientCube{};
        for (size_t f = 0; f < 6u; ++f)
        {
          const float invW = faceWeight[f] > 1e-5f ? (1.0f / faceWeight[f]) : 0.0f;
          cube.faces[f] = faceAccum[f] * invW;
        }
        cubesBounce1[probeIdx] = cube;
        validFlags[probeIdx] = 1;
      }
    }
  });

  if (cancelToken && cancelToken->load(std::memory_order_relaxed) != generation)
  {
    return GiVolumeData{};
  }

  dilateInvalidProbes(cubesBounce1, validFlags, dimX, dimY, dimZ, 4);

  // Build intermediate 1st-bounce volume so Pass 2 can sample it for 2nd-bounce GI!
  result.dimX = dimX;
  result.dimY = dimY;
  result.dimZ = dimZ;
  result.boundsMin = bmin;
  result.boundsMax = bmax;
  writeCubesToVolume(cubesBounce1, result);

  // Pass 2: Fast 2nd-bounce gather reusing the cached ray hits (zero extra BVH rays!).
  auto cubesBounce2 = cubesBounce1;
  auto validFlags2 = std::vector<uint8_t>(totalProbes, 0);

  runParallel([&](const size_t begin, const size_t end) {
    for (size_t ai = begin; ai < end; ++ai)
    {
      if (
        cancelToken && (ai & 31u) == 0u
        && cancelToken->load(std::memory_order_relaxed) != generation)
      {
        return;
      }

      const size_t probeIdx = activeIndices[ai];
      auto faceAccum = std::array<vm::vec4f, 6>{};
      auto faceWeight = std::array<float, 6>{};
      size_t backfaceCount = 0;

      for (size_t r = 0; r < RaysPerProbe; ++r)
      {
        const auto& dir = rayDirs[r];
        const auto& cached = probeHits[ai][r];

        auto sampleRadiance = vm::vec3f{0.0f, 0.0f, 0.0f};
        float sampleVis = 1.0f;

        if (cached.hit)
        {
          if (cached.backface)
          {
            ++backfaceCount;
            sampleVis = 0.0f;
          }
          else
          {
            const auto indirectAtHit =
              sampleGiVolumeCpu(result, cached.hitPos, cached.hitNormal).xyz();
            sampleRadiance =
              cached.directRadiance + cached.hitAlbedo * indirectAtHit * SecondBounceWeight;
            sampleVis = std::clamp(cached.t / AoDistanceRadius, 0.0f, 1.0f);
          }
        }

        for (size_t f = 0; f < 6u; ++f)
        {
          const float w = vm::dot(AmbientCubeAxes[f], dir);
          if (w > 0.0f)
          {
            faceAccum[f] = faceAccum[f] + vm::vec4f{sampleRadiance, sampleVis} * w;
            faceWeight[f] += w;
          }
        }
      }

      if (backfaceCount * 3u <= RaysPerProbe)
      {
        auto cube = AmbientCube{};
        for (size_t f = 0; f < 6u; ++f)
        {
          const float invW = faceWeight[f] > 1e-5f ? (1.0f / faceWeight[f]) : 0.0f;
          cube.faces[f] = faceAccum[f] * invW;
        }
        cubesBounce2[probeIdx] = cube;
        validFlags2[probeIdx] = 1;
      }
    }
  });

  if (cancelToken && cancelToken->load(std::memory_order_relaxed) != generation)
  {
    return GiVolumeData{};
  }

  dilateInvalidProbes(cubesBounce2, validFlags2, dimX, dimY, dimZ, 4);
  writeCubesToVolume(cubesBounce2, result);
  return result;
}

GiAsyncBaker::GiAsyncBaker()
  : m_worker{[this]() { workerLoop(); }}
{
}

GiAsyncBaker::~GiAsyncBaker()
{
  {
    const auto lock = std::lock_guard{m_mutex};
    m_stop = true;
    m_pending.reset();
    m_latestGeneration.fetch_add(1u, std::memory_order_relaxed);
  }
  m_cv.notify_all();
  if (m_worker.joinable())
  {
    m_worker.join();
  }
}

uint64_t GiAsyncBaker::schedule(
  std::vector<GiTriangle> triangles,
  std::vector<PreviewLight> lights,
  const std::chrono::milliseconds debounce)
{
  const uint64_t gen = m_latestGeneration.fetch_add(1u, std::memory_order_relaxed) + 1u;
  {
    const auto lock = std::lock_guard{m_mutex};
    m_pending = PendingRequest{
      gen,
      std::move(triangles),
      std::move(lights),
      std::chrono::steady_clock::now() + debounce};
  }
  m_cv.notify_one();
  return gen;
}

void GiAsyncBaker::cancelAll()
{
  m_latestGeneration.fetch_add(1u, std::memory_order_relaxed);
  const auto lock = std::lock_guard{m_mutex};
  m_pending.reset();
  m_readyResult.reset();
}

bool GiAsyncBaker::isBusy() const
{
  const auto lock = std::lock_guard{m_mutex};
  return m_pending.has_value() || m_working || m_readyResult.has_value();
}

std::optional<GiVolumeData> GiAsyncBaker::takeReadyResult()
{
  const auto lock = std::lock_guard{m_mutex};
  if (!m_readyResult.has_value())
  {
    return std::nullopt;
  }
  auto result = std::move(*m_readyResult);
  m_readyResult.reset();
  return result;
}

void GiAsyncBaker::workerLoop()
{
  while (true)
  {
    auto req = PendingRequest{};
    {
      auto lock = std::unique_lock{m_mutex};
      m_cv.wait(lock, [this]() { return m_stop || m_pending.has_value(); });
      if (m_stop)
      {
        return;
      }

      const auto now = std::chrono::steady_clock::now();
      if (m_pending->readyAt > now)
      {
        m_cv.wait_until(lock, m_pending->readyAt, [this]() { return m_stop; });
        if (m_stop)
        {
          return;
        }
        if (!m_pending.has_value() || m_pending->readyAt > std::chrono::steady_clock::now())
        {
          continue;
        }
      }

      req = std::move(*m_pending);
      m_pending.reset();
      m_working = true;
    }

    auto volume =
      bakePreviewGIVolume(req.triangles, req.lights, req.generation, &m_latestGeneration);

    {
      const auto lock = std::lock_guard{m_mutex};
      m_working = false;
      if (
        !m_stop && volume.valid()
        && m_latestGeneration.load(std::memory_order_relaxed) == req.generation)
      {
        m_readyResult = std::move(volume);
      }
    }
  }
}

} // namespace tb::render
