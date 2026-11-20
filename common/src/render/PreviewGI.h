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

#pragma once

#include "Macros.h"
#include "render/PreviewLighting.h"

#include "vm/bbox.h"
#include "vm/vec.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace tb::render
{

struct GiTriangle
{
  vm::vec3f v0;
  vm::vec3f v1;
  vm::vec3f v2;
  vm::vec3f normal;
  vm::vec3f albedo;
};

struct GiRayHit
{
  float t = 0.0f;
  size_t triangleIndex = 0;
  bool backface = false;
};

struct GiBvhNode
{
  vm::vec3f boundsMin;
  vm::vec3f boundsMax;
  int leftOrFirst = 0;
  int count = 0;
};

class GiBvh
{
private:
  std::vector<GiTriangle> m_triangles;
  std::vector<GiBvhNode> m_nodes;

public:
  void build(std::vector<GiTriangle> triangles);
  bool empty() const;
  const std::vector<GiTriangle>& triangles() const;

  std::optional<GiRayHit> intersectClosest(
    const vm::vec3f& origin,
    const vm::vec3f& direction,
    float tMin,
    float tMax) const;

  bool anyHit(
    const vm::vec3f& origin,
    const vm::vec3f& direction,
    float tMin,
    float tMax) const;

private:
  void buildNode(int nodeIndex, int first, int count);
};

struct GiVolumeData
{
  int dimX = 0;
  int dimY = 0;
  int dimZ = 0;
  vm::vec3f boundsMin{};
  vm::vec3f boundsMax{};
  // Packed 3D RGBA float texels: dimensions (dimX, dimY, dimZ * 6),
  // where face slice f (0..5 = +X, -X, +Y, -Y, +Z, -Z) occupies z in [f*dimZ, (f+1)*dimZ).
  // RGB = bounce irradiance, A = directional ambient occlusion / sky visibility [0, 1].
  std::vector<float> texels;
  uint64_t generation = 0;

  bool valid() const
  {
    return dimX > 0 && dimY > 0 && dimZ > 0
           && texels.size()
                == static_cast<size_t>(dimX) * static_cast<size_t>(dimY)
                     * static_cast<size_t>(dimZ) * 6u * 4u;
  }
};

vm::vec4f sampleGiVolumeCpu(
  const GiVolumeData& volume, const vm::vec3f& worldPos, const vm::vec3f& normal);

GiVolumeData bakePreviewGIVolume(
  const std::vector<GiTriangle>& triangles,
  const std::vector<PreviewLight>& lights,
  uint64_t generation = 0,
  const std::atomic<uint64_t>* cancelToken = nullptr);

class GiAsyncBaker
{
private:
  struct PendingRequest
  {
    uint64_t generation = 0;
    std::vector<GiTriangle> triangles;
    std::vector<PreviewLight> lights;
    std::chrono::steady_clock::time_point readyAt;
  };

  mutable std::mutex m_mutex;
  std::condition_variable m_cv;
  std::thread m_worker;
  bool m_stop = false;
  bool m_working = false;

  std::atomic<uint64_t> m_latestGeneration{0};
  std::optional<PendingRequest> m_pending;
  std::optional<GiVolumeData> m_readyResult;

public:
  GiAsyncBaker();
  ~GiAsyncBaker();

  deleteCopyAndMove(GiAsyncBaker);

  uint64_t schedule(
    std::vector<GiTriangle> triangles,
    std::vector<PreviewLight> lights,
    std::chrono::milliseconds debounce = std::chrono::milliseconds{60});

  void cancelAll();

  bool isBusy() const;
  std::optional<GiVolumeData> takeReadyResult();

private:
  void workerLoop();
};

} // namespace tb::render
