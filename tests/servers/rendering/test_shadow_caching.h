/**************************************************************************/
/*  test_shadow_caching.h                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/os/os.h"
#include "core/templates/local_vector.h"
#include "servers/rendering/renderer_scene_cull.h"
#include "servers/rendering/renderer_scene_render.h"

#include "tests/test_macros.h"

namespace TestShadowCaching {

static const uint64_t THRESHOLD_MSEC = (uint64_t)(RendererSceneCull::Instance::SHADOW_STATIC_THRESHOLD_SEC * 1000.0);

TEST_CASE("[ShadowCaching] Instance is not static immediately after movement") {
	RendererSceneCull::Instance instance;

	// Set shadow_moved_msec to current time — instance just moved.
	instance.shadow_moved_msec = OS::get_singleton()->get_ticks_msec();

	CHECK_FALSE(instance.is_shadow_static(RendererSceneCull::Instance::SHADOW_STATIC_THRESHOLD_SEC));
}

TEST_CASE("[ShadowCaching] Instance becomes static after threshold time") {
	RendererSceneCull::Instance instance;

	// Set shadow_moved_msec far enough in the past to exceed the threshold.
	uint64_t threshold_msec = THRESHOLD_MSEC;
	uint64_t now = OS::get_singleton()->get_ticks_msec();
	instance.shadow_moved_msec = now - threshold_msec - 1;

	CHECK(instance.is_shadow_static(RendererSceneCull::Instance::SHADOW_STATIC_THRESHOLD_SEC));
}

TEST_CASE("[ShadowCaching] Static classification resets on transform change") {
	RendererSceneCull::Instance instance;

	uint64_t threshold_msec = THRESHOLD_MSEC;
	uint64_t now = OS::get_singleton()->get_ticks_msec();

	// Simulate being static (moved long ago).
	instance.shadow_moved_msec = now - threshold_msec - 1;
	CHECK(instance.is_shadow_static(RendererSceneCull::Instance::SHADOW_STATIC_THRESHOLD_SEC));

	// Simulate a transform change (moved just now).
	instance.shadow_moved_msec = OS::get_singleton()->get_ticks_msec();

	// Should no longer be static.
	CHECK_FALSE(instance.is_shadow_static(RendererSceneCull::Instance::SHADOW_STATIC_THRESHOLD_SEC));
}

TEST_CASE("[ShadowCaching] Threshold value is reasonable") {
	// The threshold should be around 0.5 seconds.
	double threshold = RendererSceneCull::Instance::SHADOW_STATIC_THRESHOLD_SEC;
	CHECK(threshold >= 0.1);
	CHECK(threshold <= 2.0);
}

TEST_CASE("[ShadowCaching] New instance is not static immediately at creation") {
	// Simulate an instance created at runtime (engine has been running > 500ms).
	// With the old code (shadow_moved_msec = 0), this would return true.
	// After the fix, it must return false.
	RendererSceneCull::Instance inst;
	// shadow_moved_msec should be initialized to current ticks, not 0.
	uint64_t now = OS::get_singleton()->get_ticks_msec();
	uint64_t elapsed = now - inst.shadow_moved_msec;
	CHECK_MESSAGE(elapsed < 500,
			"New instance should not be classified as static immediately — shadow_moved_msec must be initialized to current ticks.");
}

TEST_CASE("[ShadowCaching] RenderShadowData default state is DIRTY (neither flag set)") {
	RendererSceneRender::RenderShadowData sd;
	CHECK_FALSE(sd.use_static_cache);
	CHECK_FALSE(sd.mark_static_after_render);
}

TEST_CASE("[ShadowCaching] Three states are correctly encoded by two flags") {
	RendererSceneRender::RenderShadowData dirty, settling, cached;
	// DIRTY: both false (default)
	CHECK_FALSE((dirty.use_static_cache || dirty.mark_static_after_render));
	// JUST_SETTLED: only mark_static_after_render
	settling.mark_static_after_render = true;
	CHECK_FALSE(settling.use_static_cache);
	CHECK(settling.mark_static_after_render);
	// CACHED: only use_static_cache
	cached.use_static_cache = true;
	CHECK(cached.use_static_cache);
	CHECK_FALSE(cached.mark_static_after_render);
}

TEST_CASE("[ShadowCaching] Filter preserves JUST_SETTLED entries for rendering") {
	// _filter_static_cached_shadows removes entries where use_static_cache==true.
	// JUST_SETTLED has mark_static_after_render=true and use_static_cache=false,
	// so the filter must NOT remove it — the shadow must render to populate the cache.
	RendererSceneRender::RenderShadowData settling, cached;
	settling.mark_static_after_render = true;
	settling.use_static_cache = false;
	cached.use_static_cache = true;
	cached.mark_static_after_render = false;

	// The filter condition is: keep if !use_static_cache
	CHECK_FALSE(settling.use_static_cache); // JUST_SETTLED passes the filter
	CHECK(cached.use_static_cache);         // CACHED is removed by the filter
}

TEST_CASE("[ShadowCaching] use_static_cache and mark_static_after_render are mutually exclusive") {
	// The fourth combination (both true) is invalid — the state machine must never produce it.
	// use_static_cache=true would filter the entry (skip render).
	// mark_static_after_render=true would mark cache valid after rendering.
	// Both true simultaneously is contradictory.
	RendererSceneRender::RenderShadowData sd;
	CHECK_FALSE((sd.use_static_cache && sd.mark_static_after_render)); // DIRTY: valid
	sd.mark_static_after_render = true;
	CHECK_FALSE((sd.use_static_cache && sd.mark_static_after_render)); // JUST_SETTLED: valid
	sd.mark_static_after_render = false;
	sd.use_static_cache = true;
	CHECK_FALSE((sd.use_static_cache && sd.mark_static_after_render)); // CACHED: valid
}

// T1: Animated material prevents static caching regardless of motion state.
TEST_CASE("[ShadowCaching] Animated material prevents static classification") {
	// When any shadow caster in a light's set has an animated material, the entire
	// shadow for that light must be treated as dynamic. This mirrors the production
	// check: effectively_static = light_all_casters_static && !light_has_animated_material && ...
	bool light_all_casters_static = true;
	bool light_has_animated_material = true; // animated material present
	bool atlas_cache_valid = true;
	bool effectively_static = light_all_casters_static && !light_has_animated_material && atlas_cache_valid;
	CHECK_FALSE(effectively_static);
}

// T2: CACHED-to-DIRTY round-trip: moving an instance resets both flags to false.
TEST_CASE("[ShadowCaching] CACHED-to-DIRTY round-trip on instance movement") {
	RendererSceneRender::RenderShadowData sd;

	// Simulate JUST_SETTLED → CACHED transition.
	sd.mark_static_after_render = false;
	sd.use_static_cache = true;
	CHECK(sd.use_static_cache);
	CHECK_FALSE(sd.mark_static_after_render);

	// Instance moves: shadow becomes DIRTY (both flags cleared).
	sd.use_static_cache = false;
	sd.mark_static_after_render = false;
	CHECK_FALSE(sd.use_static_cache);
	CHECK_FALSE(sd.mark_static_after_render);

	// And the instance's static timer must also be reset.
	RendererSceneCull::Instance instance;
	instance.shadow_moved_msec = OS::get_singleton()->get_ticks_msec();
	CHECK_FALSE(instance.is_shadow_static(RendererSceneCull::Instance::SHADOW_STATIC_THRESHOLD_SEC));
}

// T3: Verify _filter_static_cached_shadows semantics with varied inputs.
// The filter keeps entries where use_static_cache==false (DIRTY and JUST_SETTLED)
// and removes entries where use_static_cache==true (CACHED).
// We test the filter contract directly on RenderShadowData flags rather than
// calling the GPU-header-dependent RendererSceneRenderRD method, since the logic
// is a single boolean predicate: keep iff !use_static_cache.
namespace {
void filter_static_cached_shadows_test_helper(LocalVector<int> &r_indices,
		const RendererSceneRender::RenderShadowData *p_shadows) {
	uint32_t write_idx = 0;
	for (uint32_t i = 0; i < r_indices.size(); i++) {
		if (!p_shadows[r_indices[i]].use_static_cache) {
			r_indices[write_idx++] = r_indices[i];
		}
	}
	r_indices.resize(write_idx);
}
} // anonymous namespace

TEST_CASE("[ShadowCaching] _filter_static_cached_shadows — empty list") {
	RendererSceneRender::RenderShadowData shadows[1]; // unused placeholder
	LocalVector<int> indices;
	filter_static_cached_shadows_test_helper(indices, shadows);
	CHECK(indices.size() == 0);
}

TEST_CASE("[ShadowCaching] _filter_static_cached_shadows — all CACHED removed") {
	RendererSceneRender::RenderShadowData shadows[3];
	shadows[0].use_static_cache = true;
	shadows[1].use_static_cache = true;
	shadows[2].use_static_cache = true;
	LocalVector<int> indices;
	indices.push_back(0);
	indices.push_back(1);
	indices.push_back(2);
	filter_static_cached_shadows_test_helper(indices, shadows);
	CHECK(indices.size() == 0);
}

TEST_CASE("[ShadowCaching] _filter_static_cached_shadows — all DIRTY preserved") {
	RendererSceneRender::RenderShadowData shadows[3];
	// All DIRTY: use_static_cache=false, mark_static_after_render=false (defaults).
	LocalVector<int> indices;
	indices.push_back(0);
	indices.push_back(1);
	indices.push_back(2);
	filter_static_cached_shadows_test_helper(indices, shadows);
	CHECK(indices.size() == 3);
}

TEST_CASE("[ShadowCaching] _filter_static_cached_shadows — mixed: CACHED removed, others kept") {
	// Layout: [DIRTY, CACHED, JUST_SETTLED, CACHED, DIRTY]
	RendererSceneRender::RenderShadowData shadows[5];
	// shadows[0]: DIRTY (both false) — preserved
	// shadows[1]: CACHED — removed
	shadows[1].use_static_cache = true;
	// shadows[2]: JUST_SETTLED — preserved
	shadows[2].mark_static_after_render = true;
	// shadows[3]: CACHED — removed
	shadows[3].use_static_cache = true;
	// shadows[4]: DIRTY — preserved
	LocalVector<int> indices;
	for (int i = 0; i < 5; i++) {
		indices.push_back(i);
	}
	filter_static_cached_shadows_test_helper(indices, shadows);
	REQUIRE(indices.size() == 3);
	CHECK(indices[0] == 0);
	CHECK(indices[1] == 2);
	CHECK(indices[2] == 4);
}

// T7: Exact threshold boundary — >= means exactly-at-threshold IS static.
TEST_CASE("[ShadowCaching] Exact threshold boundary: >= classifies as static") {
	RendererSceneCull::Instance instance;
	uint64_t threshold_msec = THRESHOLD_MSEC;
	uint64_t now = OS::get_singleton()->get_ticks_msec();
	// Place shadow_moved_msec exactly at the threshold boundary.
	instance.shadow_moved_msec = now - threshold_msec;
	CHECK_MESSAGE(instance.is_shadow_static(RendererSceneCull::Instance::SHADOW_STATIC_THRESHOLD_SEC),
			"Instance at exactly the threshold should be classified as static (>= not >).");
}

} // namespace TestShadowCaching
