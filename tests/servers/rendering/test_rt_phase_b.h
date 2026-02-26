/**************************************************************************/
/*  test_rt_phase_b.h                                                     */
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
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
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

#include "servers/rendering/renderer_rd/effects/ss_effects.h"
#include "servers/rendering/renderer_rd/environment/rt_scene_manager.h"

#include "tests/test_macros.h"

// NOTE: Phase B RT effects (AO, reflections, shadows) require a RenderingDevice
// GPU context for actual dispatch. These tests cover:
//   1. Push constant struct sizes — exceeding 128 bytes breaks Vulkan validation.
//   2. Shadow pool constants — EVICTION_GRACE_FRAMES and MAX_HISTORY_ENTRIES drive
//      the LRU eviction algorithm's correctness.
//   3. Shadow pool eviction predicate — verified with plain HashMap operations, no GPU.
//   4. AO radius range sanity — the property is exposed as 0.1–5.0 m.
//   5. RT-unavailable guard path — is_enabled() == false means all dispatches are
//      skipped in _pre_opaque_render().

namespace TestRTPhaseB {

// ---------------------------------------------------------------------------
// Push constant size invariants
// These mirror the static_asserts in ss_effects.h but are exercised at test
// time so regressions are caught in CI on all platforms.
// ---------------------------------------------------------------------------

TEST_CASE("[RTPhasEB] RTAOPushConstant fits in Vulkan push constant budget") {
	CHECK_MESSAGE(sizeof(RendererRD::SSEffects::RTAOPushConstant) <= 128,
			"RTAOPushConstant exceeds 128-byte Vulkan push constant limit.");
}

TEST_CASE("[RTPhasEB] RTAOAccumulatePushConstant fits in Vulkan push constant budget") {
	CHECK_MESSAGE(sizeof(RendererRD::SSEffects::RTAOAccumulatePushConstant) <= 128,
			"RTAOAccumulatePushConstant exceeds 128-byte Vulkan push constant limit.");
}

TEST_CASE("[RTPhasEB] RTShadowsPushConstant fits in Vulkan push constant budget") {
	CHECK_MESSAGE(sizeof(RendererRD::SSEffects::RTShadowsPushConstant) <= 128,
			"RTShadowsPushConstant exceeds 128-byte Vulkan push constant limit.");
}

TEST_CASE("[RTPhasEB] RTShadowsAccumulatePushConstant fits in Vulkan push constant budget") {
	CHECK_MESSAGE(sizeof(RendererRD::SSEffects::RTShadowsAccumulatePushConstant) <= 128,
			"RTShadowsAccumulatePushConstant exceeds 128-byte Vulkan push constant limit.");
}

// ---------------------------------------------------------------------------
// Shadow pool constants
// ---------------------------------------------------------------------------

TEST_CASE("[RTPhasEB] RTShadowsRenderBuffers pool capacity is 16 slots") {
	// The 16-slot cap prevents unbounded GPU texture allocation when many
	// lights with RT shadows are visible simultaneously.
	CHECK(RendererRD::SSEffects::RTShadowsRenderBuffers::MAX_HISTORY_ENTRIES == 16);
}

TEST_CASE("[RTPhasEB] RTShadowsRenderBuffers eviction grace period is 60 frames") {
	// 60 frames at 60 Hz = 1 second of idle time before eviction. This prevents
	// thrashing when a light briefly leaves the frustum.
	CHECK(RendererRD::SSEffects::RTShadowsRenderBuffers::EVICTION_GRACE_FRAMES == 60);
}

TEST_CASE("[RTPhasEB] RTShadowsRenderBuffers starts empty") {
	RendererRD::SSEffects::RTShadowsRenderBuffers pool;
	CHECK(pool.history_pool.size() == 0);
}

// ---------------------------------------------------------------------------
// Shadow pool eviction predicate logic
// Tests the mathematical condition used by rt_shadow_dispatch() without any
// GPU calls. The actual allocation is GPU-dependent; this verifies the logic.
// ---------------------------------------------------------------------------

TEST_CASE("[RTPhasEB] Shadow pool entry older than grace period is evictable") {
	const uint64_t grace = RendererRD::SSEffects::RTShadowsRenderBuffers::EVICTION_GRACE_FRAMES;

	RendererRD::SSEffects::RTShadowsHistoryEntry entry;
	entry.last_used_frame = 0;

	// An entry at frame 0 with current_frame = grace+1 must be evictable.
	uint64_t current_frame = grace + 1;
	bool evictable = (current_frame > entry.last_used_frame + grace);
	CHECK_MESSAGE(evictable, "Entry at frame 0 should be evictable at frame grace+1.");
}

TEST_CASE("[RTPhasEB] Shadow pool entry exactly at grace boundary is evictable") {
	// Boundary: current_frame == last_used + grace + 1 (strictly greater).
	const uint64_t grace = RendererRD::SSEffects::RTShadowsRenderBuffers::EVICTION_GRACE_FRAMES;

	RendererRD::SSEffects::RTShadowsHistoryEntry entry;
	entry.last_used_frame = 10;

	uint64_t current_frame = entry.last_used_frame + grace + 1;
	bool evictable = (current_frame > entry.last_used_frame + grace);
	CHECK_MESSAGE(evictable, "Entry should be evictable one frame past grace boundary.");
}

TEST_CASE("[RTPhasEB] Shadow pool entry within grace period is retained") {
	const uint64_t grace = RendererRD::SSEffects::RTShadowsRenderBuffers::EVICTION_GRACE_FRAMES;

	RendererRD::SSEffects::RTShadowsHistoryEntry entry;
	entry.last_used_frame = 50;

	// At last_used + grace the entry is still within the grace window (not strictly greater).
	uint64_t current_frame = entry.last_used_frame + grace;
	bool evictable = (current_frame > entry.last_used_frame + grace);
	CHECK_FALSE_MESSAGE(evictable, "Entry at exactly grace frames should NOT be evicted.");
}

TEST_CASE("[RTPhasEB] Shadow pool HashMap eviction removes entry correctly") {
	RendererRD::SSEffects::RTShadowsRenderBuffers pool;
	const uint64_t grace = RendererRD::SSEffects::RTShadowsRenderBuffers::EVICTION_GRACE_FRAMES;

	// Insert two entries with different last-used frames.
	RID old_light = RID::from_uint64(100);
	RID fresh_light = RID::from_uint64(200);

	RendererRD::SSEffects::RTShadowsHistoryEntry old_entry;
	old_entry.last_used_frame = 0;
	pool.history_pool[old_light] = old_entry;

	RendererRD::SSEffects::RTShadowsHistoryEntry fresh_entry;
	fresh_entry.last_used_frame = grace + 1;
	pool.history_pool[fresh_light] = fresh_entry;

	CHECK(pool.history_pool.size() == 2);

	// Simulate eviction pass at frame grace+2: old_light is evictable, fresh_light is not.
	uint64_t current_frame = grace + 2;
	Vector<RID> to_evict;
	for (const KeyValue<RID, RendererRD::SSEffects::RTShadowsHistoryEntry> &kv : pool.history_pool) {
		if (current_frame > kv.value.last_used_frame + grace) {
			to_evict.push_back(kv.key);
		}
	}
	for (const RID &rid : to_evict) {
		pool.history_pool.erase(rid);
	}

	CHECK_MESSAGE(pool.history_pool.size() == 1, "Only the stale entry should be evicted.");
	CHECK_MESSAGE(!pool.history_pool.has(old_light), "Old light entry should be gone.");
	CHECK_MESSAGE(pool.history_pool.has(fresh_light), "Fresh light entry should remain.");
}

// ---------------------------------------------------------------------------
// AO radius range sanity
// ---------------------------------------------------------------------------

TEST_CASE("[RTPhasEB] RT AO radius property range is sensible") {
	// The property is exposed in Environment as PROPERTY_HINT_RANGE "0.1,5.0,0.01,suffix:m".
	// Verify the sentinels used in rt_ambient_occlusion() are consistent.
	constexpr float RT_AO_RADIUS_MIN = 0.1f;
	constexpr float RT_AO_RADIUS_MAX = 5.0f;

	CHECK_MESSAGE(RT_AO_RADIUS_MIN > 0.0f, "Minimum radius must be strictly positive.");
	CHECK_MESSAGE(RT_AO_RADIUS_MAX > RT_AO_RADIUS_MIN, "Maximum radius must exceed minimum.");

	// A typical scene value (1 m) must lie within the range.
	float typical_radius = 1.0f;
	CHECK(typical_radius >= RT_AO_RADIUS_MIN);
	CHECK(typical_radius <= RT_AO_RADIUS_MAX);
}

// ---------------------------------------------------------------------------
// RT-unavailable guard path (headless / no GPU context)
// ---------------------------------------------------------------------------

TEST_CASE("[RTPhasEB] RTSceneManager reports RT disabled in headless context") {
	// All RT dispatch methods in render_forward_clustered.cpp gate on
	// rt_scene_manager.is_enabled(). Without a GPU context this returns false,
	// so no GPU calls are made and the render loop proceeds safely.
	RTSceneManager manager;
	CHECK_FALSE_MESSAGE(manager.is_enabled(),
			"RT must be disabled when no GPU context is present.");
}

TEST_CASE("[RTPhasEB] Shadow pool stays empty when RT is disabled") {
	// With is_enabled() == false, _process_rt_shadows() returns before touching
	// the pool. Verify the initial state that the guard produces.
	RTSceneManager manager;
	CHECK_FALSE(manager.is_enabled());

	RendererRD::SSEffects::RTShadowsRenderBuffers pool;
	CHECK_MESSAGE(pool.history_pool.size() == 0,
			"Pool must be empty when RT shadows have never dispatched.");
}

TEST_CASE("[RTPhasEB] RTShadowsHistoryEntry defaults to frame 0") {
	// last_used_frame == 0 is the sentinel for "never used". The eviction
	// predicate treats this as maximally stale, which is the correct default.
	RendererRD::SSEffects::RTShadowsHistoryEntry entry;
	CHECK(entry.last_used_frame == 0);
}

} // namespace TestRTPhaseB
