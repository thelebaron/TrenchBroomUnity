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

#include "mdl/Entity.h"
#include "mdl/EntityProperties.h"
#include "render/PreviewGI.h"
#include "render/PreviewLighting.h"

#include "vm/util.h"

#include <chrono>
#include <cmath>
#include <thread>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace tb::render
{

TEST_CASE("PreviewLighting.isRealtimePreviewLightClassname")
{
  CHECK(isRealtimePreviewLightClassname("light_unity"));
  CHECK(isRealtimePreviewLightClassname("LIGHT_UNITY"));
  CHECK(isRealtimePreviewLightClassname("light_realtime"));
  CHECK(isRealtimePreviewLightClassname("LIGHT_REALTIME"));
  CHECK(isRealtimePreviewLightClassname("custom_realtime_light"));

  CHECK_FALSE(isRealtimePreviewLightClassname("light"));
  CHECK_FALSE(isRealtimePreviewLightClassname("light_baked"));
  CHECK_FALSE(isRealtimePreviewLightClassname("light_mixed"));
  CHECK_FALSE(isRealtimePreviewLightClassname("light_fluoro"));
  CHECK_FALSE(isRealtimePreviewLightClassname("func_realtime"));
  CHECK_FALSE(isRealtimePreviewLightClassname("worldspawn"));
}

TEST_CASE("PreviewLighting.extractUnityLightTypesAndDirection")
{
  SECTION("Bare light_unity uses Unity defaults (not Quake fallback)")
  {
    const auto entity = mdl::Entity{{
      {"classname", "light_unity"},
      {"origin", "16 32 48"},
    }};

    const auto light = extractPreviewLight(entity, 0.03125f);
    REQUIRE(light.has_value());
    CHECK(light->type == PreviewLightType::Point);
    CHECK(light->intensity == Catch::Approx(1.0f));
    CHECK(light->range == Catch::Approx(320.0f));
    CHECK(light->spotAngle == Catch::Approx(45.0f));
    CHECK(light->direction.x() == Catch::Approx(1.0f));
    CHECK(light->direction.y() == Catch::Approx(0.0f).margin(1e-5f));
    CHECK(light->direction.z() == Catch::Approx(0.0f).margin(1e-5f));
  }

  SECTION("Spot light parses light_type, spotangle, and downward pitch")
  {
    const auto entity = mdl::Entity{{
      {"classname", "light_unity"},
      {"light_type", "1"},
      {"spotangle", "60"},
      {"angles", "90 0 0"},
      {"range", "15"},
      {"intensity", "3"},
    }};

    const auto light = extractPreviewLight(entity, 0.03125f);
    REQUIRE(light.has_value());
    CHECK(light->type == PreviewLightType::Spot);
    CHECK(light->spotAngle == Catch::Approx(60.0f));
    CHECK(light->range == Catch::Approx(480.0f));
    CHECK(light->intensity == Catch::Approx(3.0f));
    CHECK(light->direction.x() == Catch::Approx(0.0f).margin(1e-5f));
    CHECK(light->direction.y() == Catch::Approx(0.0f).margin(1e-5f));
    CHECK(light->direction.z() == Catch::Approx(-1.0f).margin(1e-5f));
  }

  SECTION("Directional light parses yaw and downward pitch matching Scopa")
  {
    const auto entity = mdl::Entity{{
      {"classname", "light_unity"},
      {"light_type", "2"},
      {"angles", "45 90 0"},
      {"intensity", "1.5"},
    }};

    const auto light = extractPreviewLight(entity, 0.03125f);
    REQUIRE(light.has_value());
    CHECK(light->type == PreviewLightType::Directional);
    CHECK(light->intensity == Catch::Approx(1.5f));
    CHECK(light->direction.x() == Catch::Approx(0.0f).margin(1e-5f));
    CHECK(light->direction.y() == Catch::Approx(std::cos(vm::to_radians(45.0f))).margin(1e-4f));
    CHECK(light->direction.z() == Catch::Approx(-std::sin(vm::to_radians(45.0f))).margin(1e-4f));
  }
}

TEST_CASE("PreviewLighting.extractPreviewLightUnityProperties")
{
  const auto entity = mdl::Entity{{
    {"classname", "light_realtime"},
    {"origin", "128 -64 32"},
    {"intensity", "2.5"},
    {"range", "12"},
    {"color", "255 128 0"},
    {"shadows", "1"},
  }};

  const auto light = extractPreviewLight(entity, 0.03125f, true);
  REQUIRE(light.has_value());
  CHECK(light->position == vm::vec3f{128.0f, -64.0f, 32.0f});
  CHECK(light->intensity == Catch::Approx(2.5f));
  CHECK(light->range == Catch::Approx(12.0f / 0.03125f));
  CHECK(light->color.x() == Catch::Approx(1.0f));
  CHECK(light->color.y() == Catch::Approx(128.0f / 255.0f));
  CHECK(light->color.z() == Catch::Approx(0.0f));
  CHECK(light->shadows == 1);
  CHECK(light->selected);
}

TEST_CASE("PreviewLighting.extractPreviewLightPartialUnityProperties")
{
  SECTION("Only intensity specified uses Unity default range of 10m")
  {
    const auto entity = mdl::Entity{{
      {"classname", "light_realtime"},
      {"origin", "0 0 0"},
      {"intensity", "5"},
    }};

    const auto light = extractPreviewLight(entity, 0.03125f);
    REQUIRE(light.has_value());
    CHECK(light->intensity == Catch::Approx(5.0f));
    CHECK(light->range == Catch::Approx(320.0f));
    CHECK(light->color == vm::vec3f{1.0f, 1.0f, 1.0f});
    CHECK(light->shadows == 2);
  }

  SECTION("Only range specified uses Unity default intensity of 1")
  {
    const auto entity = mdl::Entity{{
      {"classname", "light_realtime"},
      {"origin", "0 0 0"},
      {"range", "8"},
    }};

    const auto light = extractPreviewLight(entity, 0.03125f);
    REQUIRE(light.has_value());
    CHECK(light->intensity == Catch::Approx(1.0f));
    CHECK(light->range == Catch::Approx(256.0f));
  }
}

TEST_CASE("PreviewLighting.extractPreviewLightQuakeFallback")
{
  SECTION("Bare light_realtime uses Scopa Quake defaults")
  {
    const auto entity = mdl::Entity{{
      {"classname", "light_realtime"},
      {"origin", "10 20 30"},
      {"shadows", "0"},
    }};

    const auto light = extractPreviewLight(entity, 0.03125f);
    REQUIRE(light.has_value());
    CHECK(light->intensity == Catch::Approx(300.0f / 256.0f));
    CHECK(light->range == Catch::Approx(300.0f));
    CHECK(light->shadows == 0);
  }

  SECTION("Custom light and wait")
  {
    const auto entity = mdl::Entity{{
      {"classname", "light_realtime"},
      {"origin", "0 0 0"},
      {"light", "512"},
      {"wait", "2"},
      {"_color", "254 242 173"},
    }};

    const auto light = extractPreviewLight(entity, 0.03125f);
    REQUIRE(light.has_value());
    CHECK(light->intensity == Catch::Approx(2.0f));
    CHECK(light->range == Catch::Approx(256.0f));
    CHECK(light->color.x() == Catch::Approx(254.0f / 255.0f));
    CHECK(light->color.y() == Catch::Approx(242.0f / 255.0f));
    CHECK(light->color.z() == Catch::Approx(173.0f / 255.0f));
  }

  SECTION("Non-positive wait falls back to 1")
  {
    const auto entity = mdl::Entity{{
      {"classname", "light_realtime"},
      {"light", "400"},
      {"wait", "0"},
    }};

    const auto light = extractPreviewLight(entity, 0.03125f);
    REQUIRE(light.has_value());
    CHECK(light->range == Catch::Approx(400.0f));
  }
}

TEST_CASE("PreviewLighting.skipsBakedAndMixedLights")
{
  CHECK_FALSE(extractPreviewLight(mdl::Entity{{{"classname", "light_baked"}}}).has_value());
  CHECK_FALSE(extractPreviewLight(mdl::Entity{{{"classname", "light_mixed"}}}).has_value());
  CHECK_FALSE(extractPreviewLight(mdl::Entity{{{"classname", "light"}}}).has_value());
}

TEST_CASE("PreviewLighting.selectActivePreviewLights")
{
  const auto lights = std::vector<PreviewLight>{
    PreviewLight{vm::vec3f{1000, 0, 0}, 100.0f, vm::vec3f{1, 1, 1}, 1.0f, 2, false},
    PreviewLight{vm::vec3f{50, 0, 0}, 100.0f, vm::vec3f{1, 1, 1}, 1.0f, 2, false},
    PreviewLight{vm::vec3f{10, 0, 0}, 100.0f, vm::vec3f{1, 1, 1}, 0.0f, 2, false},
    PreviewLight{vm::vec3f{300, 0, 0}, 250.0f, vm::vec3f{1, 1, 1}, 1.0f, 2, false},
  };

  const auto active = selectActivePreviewLights(lights, vm::vec3f{0, 0, 0}, 2);
  REQUIRE(active.size() == 2);

  // Zero-intensity light is filtered out; the two closest effective spheres are at x=50
  // (distToSphere=0) and x=300 (distToSphere=50).
  const auto hasX = [&](const float x) {
    return active[0].position.x() == x || active[1].position.x() == x;
  };
  CHECK(hasX(50.0f));
  CHECK(hasX(300.0f));
}

TEST_CASE("PreviewLighting.packPreviewLightTextureData")
{
  auto spotLight = PreviewLight{
    vm::vec3f{-5.0f, 0.0f, 15.0f},
    160.0f,
    vm::vec3f{0.5f, 1.0f, 0.5f},
    1.0f,
    2,
    false,
    3,
    PreviewLightType::Spot,
    vm::vec3f{0.0f, 0.0f, -1.0f},
    60.0f};
  auto dirLight = PreviewLight{
    vm::vec3f{0.0f, 0.0f, 0.0f},
    0.0f,
    vm::vec3f{1.0f, 0.9f, 0.8f},
    1.0f,
    2,
    false,
    0,
    PreviewLightType::Directional,
    vm::normalize(vm::vec3f{1.0f, 0.0f, -1.0f}),
    45.0f};
  const auto lights = std::vector<PreviewLight>{
    PreviewLight{
      vm::vec3f{10.0f, 20.0f, 30.0f},
      320.0f,
      vm::vec3f{1.0f, 0.5f, 0.25f},
      2.0f,
      1,
      false,
      0},
    spotLight,
    dirLight,
  };

  const auto texels = packPreviewLightTextureData(lights);
  REQUIRE(texels.size() == MaxPreviewLights * PreviewLightTextureRows * 4);

  // Row 0: position + range
  CHECK(texels[0] == Catch::Approx(10.0f));
  CHECK(texels[1] == Catch::Approx(20.0f));
  CHECK(texels[2] == Catch::Approx(30.0f));
  CHECK(texels[3] == Catch::Approx(320.0f));

  // Row 1: color * intensity + encoded shadow metadata
  const auto row1 = MaxPreviewLights * 4;
  CHECK(texels[row1 + 0] == Catch::Approx(2.0f));
  CHECK(texels[row1 + 1] == Catch::Approx(1.0f));
  CHECK(texels[row1 + 2] == Catch::Approx(0.5f));
  CHECK(texels[row1 + 3] == Catch::Approx(1.0f));   // Hard shadow in slot 0 -> +1
  CHECK(texels[row1 + 7] == Catch::Approx(-4.0f));  // Soft shadow in slot 3 -> -4
  CHECK(texels[row1 + 11] == Catch::Approx(-1.0f)); // Soft directional shadow -> -1

  // Row 2: direction + type
  const auto row2 = MaxPreviewLights * 2 * 4;
  CHECK(texels[row2 + 3] == Catch::Approx(0.0f));  // Point
  CHECK(texels[row2 + 6] == Catch::Approx(-1.0f)); // Spot dir.z
  CHECK(texels[row2 + 7] == Catch::Approx(1.0f));  // Spot type
  CHECK(texels[row2 + 11] == Catch::Approx(2.0f)); // Directional type

  // Row 3: spot attenuation parameters (non-zero for spot, zero for point/directional)
  const auto row3 = MaxPreviewLights * 3 * 4;
  CHECK(texels[row3 + 0] == Catch::Approx(0.0f));
  CHECK(texels[row3 + 4] > 0.0f);
  CHECK(texels[row3 + 5] < 0.0f);
}

TEST_CASE("PreviewLighting.directionalShadowViewProjectionCoversBoundsAndOrdersDepth")
{
  const auto bounds =
    vm::bbox3f{vm::vec3f{-512.0f, -256.0f, 0.0f}, vm::vec3f{512.0f, 256.0f, 384.0f}};

  const auto directions = std::array<vm::vec3f, 3>{
    vm::normalize(vm::vec3f{1.0f, 1.0f, -1.0f}),
    vm::vec3f{0.0f, 0.0f, -1.0f},
    vm::vec3f{1.0f, 0.0f, 0.0f},
  };

  for (const auto& dir : directions)
  {
    const auto vp = previewDirectionalShadowViewProjection(dir, bounds);

    // Every corner of the world bounds must project strictly inside [-1, 1]^3.
    for (int cx = 0; cx < 2; ++cx)
    {
      for (int cy = 0; cy < 2; ++cy)
      {
        for (int cz = 0; cz < 2; ++cz)
        {
          const auto corner = vm::vec3f{
            cx == 0 ? bounds.min.x() : bounds.max.x(),
            cy == 0 ? bounds.min.y() : bounds.max.y(),
            cz == 0 ? bounds.min.z() : bounds.max.z()};
          const auto clip = vp * vm::vec4f{corner, 1.0f};
          CHECK(clip.x() >= -1.0f);
          CHECK(clip.x() <= 1.0f);
          CHECK(clip.y() >= -1.0f);
          CHECK(clip.y() <= 1.0f);
          CHECK(clip.z() >= -1.0f);
          CHECK(clip.z() <= 1.0f);
        }
      }
    }

    // A point upstream (closer to the sun) must have smaller depth than a downstream point.
    const auto center = bounds.center();
    const auto upstreamClip = vp * vm::vec4f{center - dir * 100.0f, 1.0f};
    const auto downstreamClip = vp * vm::vec4f{center + dir * 100.0f, 1.0f};
    CHECK(upstreamClip.z() < downstreamClip.z());
  }
}

TEST_CASE("PreviewLighting.shadowFaceViewProjectionMatchesDirectionLookup")
{
  const auto lightPos = vm::vec3f{64.0f, -128.0f, 32.0f};
  const auto lightRange = 320.0f;

  // Off-axis sample directions covering all 6 cube faces (+X, -X, +Y, -Y, +Z, -Z)
  const auto sampleOffsets = std::array<vm::vec3f, 6>{
    vm::vec3f{120.0f, -30.0f, 45.0f},  // +X (face 0)
    vm::vec3f{-120.0f, 40.0f, -20.0f}, // -X (face 1)
    vm::vec3f{35.0f, 110.0f, -50.0f},  // +Y (face 2)
    vm::vec3f{-25.0f, -110.0f, 60.0f}, // -Y (face 3)
    vm::vec3f{40.0f, -55.0f, 130.0f},  // +Z (face 4)
    vm::vec3f{-45.0f, 35.0f, -130.0f}, // -Z (face 5)
  };

  for (int expectedFace = 0; expectedFace < 6; ++expectedFace)
  {
    const auto offset = sampleOffsets[static_cast<size_t>(expectedFace)];
    const auto worldPoint = lightPos + offset;

    const auto lookup = previewShadowDirectionToFaceUV(offset);
    CHECK(lookup.face == expectedFace);

    const auto vp = previewShadowFaceViewProjection(lightPos, lightRange, expectedFace);
    const auto clip = vp * vm::vec4f{worldPoint, 1.0f};
    REQUIRE(clip.w() > 0.0f);

    const auto projectedU = (clip.x() / clip.w()) * 0.5f + 0.5f;
    const auto projectedV = (clip.y() / clip.w()) * 0.5f + 0.5f;

    CHECK(lookup.u == Catch::Approx(projectedU).margin(1e-4f));
    CHECK(lookup.v == Catch::Approx(projectedV).margin(1e-4f));
  }
}

TEST_CASE("PreviewLighting.shadowAtlasTileRectsAreUniqueAndInBounds")
{
  auto seen = std::vector<std::pair<int, int>>{};
  for (int slot = 0; slot < static_cast<int>(MaxPreviewShadowLights); ++slot)
  {
    for (int face = 0; face < 6; ++face)
    {
      const auto rect = previewShadowTileRect(slot, face);
      CHECK(rect.x >= 0);
      CHECK(rect.y >= 0);
      CHECK(rect.x + rect.width <= PreviewShadowAtlasWidth);
      CHECK(rect.y + rect.height <= PreviewShadowAtlasHeight);
      CHECK(rect.width == PreviewShadowTileSize);
      CHECK(rect.height == PreviewShadowTileSize);
      const auto key = std::pair{rect.x, rect.y};
      CHECK(std::find(seen.begin(), seen.end(), key) == seen.end());
      seen.push_back(key);
    }
  }
  CHECK(seen.size() == MaxPreviewShadowLights * 6);
}

TEST_CASE("PreviewLighting.assignPreviewShadowSlotsCachingAndInvalidation")
{
  auto cache = std::array<PreviewShadowSlot, MaxPreviewShadowLights>{};
  auto lights = std::vector<PreviewLight>{
    PreviewLight{vm::vec3f{0, 0, 0}, 320.0f, vm::vec3f{1, 1, 1}, 1.0f, 2, false},
    PreviewLight{vm::vec3f{100, 0, 0}, 320.0f, vm::vec3f{1, 1, 1}, 1.0f, 0, false},
    PreviewLight{vm::vec3f{200, 0, 0}, 320.0f, vm::vec3f{1, 1, 1}, 1.0f, 1, false},
  };

  // Frame 1: shadow-casting lights get slots and are marked dirty.
  const auto dirty1 =
    assignPreviewShadowSlots(lights, cache, vm::vec3f{0, 0, 0}, 1, true);
  CHECK(lights[0].shadowSlot == 0);
  CHECK(lights[1].shadowSlot == -1); // shadows == 0 skipped
  CHECK(lights[2].shadowSlot == 1);
  CHECK(dirty1.size() == 2);

  // Frame 2: nothing moved and geometry generation unchanged -> 0 dirty slots.
  const auto dirty2 =
    assignPreviewShadowSlots(lights, cache, vm::vec3f{50, 50, 0}, 1, true);
  CHECK(lights[0].shadowSlot == 0);
  CHECK(lights[2].shadowSlot == 1);
  CHECK(dirty2.empty());

  // Frame 3: move only light 2 -> light 0 stays cached, only light 2 re-renders.
  lights[2].position = vm::vec3f{220, 0, 0};
  const auto dirty3 =
    assignPreviewShadowSlots(lights, cache, vm::vec3f{50, 50, 0}, 1, true);
  CHECK(lights[0].shadowSlot == 0);
  REQUIRE(lights[2].shadowSlot >= 0);
  CHECK(lights[2].shadowSlot != lights[0].shadowSlot);
  REQUIRE(dirty3.size() == 1);
  CHECK(dirty3[0] == lights[2].shadowSlot);

  // Frame 4: geometry changed -> both active shadow slots become dirty.
  const auto dirty4 =
    assignPreviewShadowSlots(lights, cache, vm::vec3f{50, 50, 0}, 2, true);
  CHECK(dirty4.size() == 2);

  // Frame 5: shadows disabled -> no slots assigned, no dirty renders.
  const auto dirty5 =
    assignPreviewShadowSlots(lights, cache, vm::vec3f{50, 50, 0}, 2, false);
  CHECK(lights[0].shadowSlot == -1);
  CHECK(lights[2].shadowSlot == -1);
  CHECK(dirty5.empty());
}

namespace
{
void appendQuad(
  std::vector<GiTriangle>& out,
  const vm::vec3f& p0,
  const vm::vec3f& p1,
  const vm::vec3f& p2,
  const vm::vec3f& p3,
  const vm::vec3f& normal,
  const vm::vec3f& albedo)
{
  out.push_back(GiTriangle{p0, p1, p2, normal, albedo});
  out.push_back(GiTriangle{p0, p2, p3, normal, albedo});
}

void appendBoxInterior(
  std::vector<GiTriangle>& out,
  const vm::vec3f& minB,
  const vm::vec3f& maxB,
  const vm::vec3f& albedoMinX,
  const vm::vec3f& albedoMaxX,
  const vm::vec3f& albedoOther)
{
  // -X wall (normal +X)
  appendQuad(
    out,
    {minB.x(), minB.y(), minB.z()},
    {minB.x(), maxB.y(), minB.z()},
    {minB.x(), maxB.y(), maxB.z()},
    {minB.x(), minB.y(), maxB.z()},
    {1, 0, 0},
    albedoMinX);
  // +X wall (normal -X)
  appendQuad(
    out,
    {maxB.x(), maxB.y(), minB.z()},
    {maxB.x(), minB.y(), minB.z()},
    {maxB.x(), minB.y(), maxB.z()},
    {maxB.x(), maxB.y(), maxB.z()},
    {-1, 0, 0},
    albedoMaxX);
  // -Y wall (normal +Y)
  appendQuad(
    out,
    {maxB.x(), minB.y(), minB.z()},
    {minB.x(), minB.y(), minB.z()},
    {minB.x(), minB.y(), maxB.z()},
    {maxB.x(), minB.y(), maxB.z()},
    {0, 1, 0},
    albedoOther);
  // +Y wall (normal -Y)
  appendQuad(
    out,
    {minB.x(), maxB.y(), minB.z()},
    {maxB.x(), maxB.y(), minB.z()},
    {maxB.x(), maxB.y(), maxB.z()},
    {minB.x(), maxB.y(), maxB.z()},
    {0, -1, 0},
    albedoOther);
  // -Z floor (normal +Z)
  appendQuad(
    out,
    {minB.x(), minB.y(), minB.z()},
    {maxB.x(), minB.y(), minB.z()},
    {maxB.x(), maxB.y(), minB.z()},
    {minB.x(), maxB.y(), minB.z()},
    {0, 0, 1},
    albedoOther);
  // +Z ceiling (normal -Z)
  appendQuad(
    out,
    {minB.x(), maxB.y(), maxB.z()},
    {maxB.x(), maxB.y(), maxB.z()},
    {maxB.x(), minB.y(), maxB.z()},
    {minB.x(), minB.y(), maxB.z()},
    {0, 0, -1},
    albedoOther);
}
} // namespace

TEST_CASE("PreviewLighting.giBvhRayIntersection")
{
  auto tris = std::vector<GiTriangle>{};
  appendQuad(
    tris,
    {100, -50, -50},
    {100, 50, -50},
    {100, 50, 50},
    {100, -50, 50},
    {-1, 0, 0},
    {0.8f, 0.2f, 0.2f});
  appendQuad(
    tris,
    {200, -50, -50},
    {200, 50, -50},
    {200, 50, 50},
    {200, -50, 50},
    {-1, 0, 0},
    {0.2f, 0.8f, 0.2f});

  GiBvh bvh;
  bvh.build(tris);
  REQUIRE_FALSE(bvh.empty());

  SECTION("Closest hit returns nearer plane with frontface flag")
  {
    const auto hit = bvh.intersectClosest({0, 0, 0}, {1, 0, 0}, 0.01f, 500.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->t == Catch::Approx(100.0f).margin(0.01f));
    CHECK_FALSE(hit->backface);
    CHECK(bvh.triangles()[hit->triangleIndex].albedo.x() == Catch::Approx(0.8f));
  }

  SECTION("Hitting from behind marks backface")
  {
    const auto hit = bvh.intersectClosest({150, 0, 0}, {-1, 0, 0}, 0.01f, 500.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->t == Catch::Approx(50.0f).margin(0.01f));
    CHECK(hit->backface);
  }

  SECTION("Misses outside quad and respects tMax")
  {
    CHECK_FALSE(bvh.intersectClosest({0, 0, 0}, {0, 1, 0}, 0.01f, 500.0f).has_value());
    CHECK_FALSE(bvh.anyHit({0, 0, 0}, {1, 0, 0}, 0.01f, 50.0f));
    CHECK(bvh.anyHit({0, 0, 0}, {1, 0, 0}, 0.01f, 150.0f));
  }
}

TEST_CASE("PreviewLighting.giColorBleedAndOcclusion")
{
  // Closed room from (-128, -128, -128) to (128, 128, 128) with a bright red -X wall
  // and neutral walls elsewhere, lit by a white point light near the center.
  auto roomTris = std::vector<GiTriangle>{};
  appendBoxInterior(
    roomTris,
    {-128, -128, -128},
    {128, 128, 128},
    {0.95f, 0.05f, 0.05f},
    {0.75f, 0.75f, 0.75f},
    {0.75f, 0.75f, 0.75f});

  auto light = PreviewLight{};
  light.type = PreviewLightType::Point;
  light.position = {-32, 0, 0};
  light.color = {1.0f, 1.0f, 1.0f};
  light.intensity = 1.5f;
  light.range = 400.0f;
  light.shadows = 1;

  const auto volume = bakePreviewGIVolume(roomTris, {light}, 1);
  REQUIRE(volume.valid());

  // Probe facing the red -X wall (surface normal -X) should pick up strong red bleed.
  const auto bleed = sampleGiVolumeCpu(volume, {64, 0, 0}, {-1, 0, 0});
  CHECK(bleed.x() > 0.05f);
  CHECK(bleed.x() > bleed.y() * 1.2f);
  CHECK(bleed.x() > bleed.z() * 1.2f);

  // Enclosed interior has AO strictly below full open-sky visibility (1.0),
  // and a tight enclosure produces stronger occlusion than a wide room.
  const auto roomAo = sampleGiVolumeCpu(volume, {-112, -112, -112}, {1, 1, 1});
  CHECK(roomAo.w() < 0.99f);
  CHECK(roomAo.w() >= 0.0f);

  auto tightRoom = std::vector<GiTriangle>{};
  appendBoxInterior(
    tightRoom,
    {-48, -48, -48},
    {48, 48, 48},
    {0.5f, 0.5f, 0.5f},
    {0.5f, 0.5f, 0.5f},
    {0.5f, 0.5f, 0.5f});
  const auto tightVolume = bakePreviewGIVolume(tightRoom, {}, 10);
  REQUIRE(tightVolume.valid());
  const auto tightAo = sampleGiVolumeCpu(tightVolume, {0, 0, 0}, {1, 0, 0});
  CHECK(tightAo.w() < 0.5f);
  CHECK(tightAo.w() < roomAo.w());
}

TEST_CASE("PreviewLighting.giWallBlocksSealedRoom")
{
  // Two adjacent sealed rooms separated at x = 0 by solid double-sided walls.
  // Lit room: [-160, -16], Dark room: [16, 160].
  auto tris = std::vector<GiTriangle>{};
  appendBoxInterior(
    tris,
    {-160, -96, -96},
    {-16, 96, 96},
    {0.8f, 0.8f, 0.8f},
    {0.8f, 0.8f, 0.8f},
    {0.8f, 0.8f, 0.8f});
  appendBoxInterior(
    tris,
    {16, -96, -96},
    {160, 96, 96},
    {0.8f, 0.8f, 0.8f},
    {0.8f, 0.8f, 0.8f},
    {0.8f, 0.8f, 0.8f});

  auto light = PreviewLight{};
  light.type = PreviewLightType::Point;
  light.position = {-88, 0, 0};
  light.color = {1.0f, 1.0f, 1.0f};
  light.intensity = 2.0f;
  light.range = 400.0f;
  light.shadows = 1;

  const auto volume = bakePreviewGIVolume(tris, {light}, 2);
  REQUIRE(volume.valid());

  const auto litRoomGi = sampleGiVolumeCpu(volume, {-88, 0, 0}, {1, 0, 0});
  const auto darkRoomGi = sampleGiVolumeCpu(volume, {96, 0, 0}, {-1, 0, 0});

  CHECK(litRoomGi.x() > 0.1f);
  CHECK(darkRoomGi.x() < litRoomGi.x() * 0.15f);
}

TEST_CASE("PreviewLighting.giAsyncBakerProducesVolume")
{
  auto roomTris = std::vector<GiTriangle>{};
  appendBoxInterior(
    roomTris,
    {-96, -96, -96},
    {96, 96, 96},
    {0.2f, 0.9f, 0.2f},
    {0.8f, 0.8f, 0.8f},
    {0.8f, 0.8f, 0.8f});

  auto light = PreviewLight{};
  light.type = PreviewLightType::Point;
  light.position = {0, 0, 0};
  light.color = {1.0f, 1.0f, 1.0f};
  light.intensity = 1.0f;
  light.range = 320.0f;
  light.shadows = 1;

  GiAsyncBaker baker;
  const auto gen = baker.schedule(roomTris, {light}, std::chrono::milliseconds{0});

  auto result = std::optional<GiVolumeData>{};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (std::chrono::steady_clock::now() < deadline)
  {
    if (auto ready = baker.takeReadyResult())
    {
      result = std::move(ready);
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  REQUIRE(result.has_value());
  CHECK(result->valid());
  CHECK(result->generation == gen);
  const auto sample = sampleGiVolumeCpu(*result, {32, 0, 0}, {-1, 0, 0});
  CHECK(sample.y() > sample.x());
}

} // namespace tb::render
