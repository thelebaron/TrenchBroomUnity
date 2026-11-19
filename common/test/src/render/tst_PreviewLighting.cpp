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
#include "render/PreviewLighting.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace tb::render
{

TEST_CASE("PreviewLighting.isRealtimePreviewLightClassname")
{
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
  const auto lights = std::vector<PreviewLight>{
    PreviewLight{
      vm::vec3f{10.0f, 20.0f, 30.0f}, 320.0f, vm::vec3f{1.0f, 0.5f, 0.25f}, 2.0f, 1, false},
  };

  const auto texels = packPreviewLightTextureData(lights);
  REQUIRE(texels.size() == MaxPreviewLights * 2 * 4);

  // Row 0: position + range
  CHECK(texels[0] == Catch::Approx(10.0f));
  CHECK(texels[1] == Catch::Approx(20.0f));
  CHECK(texels[2] == Catch::Approx(30.0f));
  CHECK(texels[3] == Catch::Approx(320.0f));

  // Row 1: color * intensity + shadows
  const auto row1 = MaxPreviewLights * 4;
  CHECK(texels[row1 + 0] == Catch::Approx(2.0f));
  CHECK(texels[row1 + 1] == Catch::Approx(1.0f));
  CHECK(texels[row1 + 2] == Catch::Approx(0.5f));
  CHECK(texels[row1 + 3] == Catch::Approx(1.0f));
}

} // namespace tb::render
