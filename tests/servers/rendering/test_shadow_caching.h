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
#include "servers/rendering/renderer_scene_cull.h"

#include "tests/test_macros.h"

namespace TestShadowCaching {

TEST_CASE("[ShadowCaching] Instance is not static immediately after movement") {
	RendererSceneCull::Instance instance;

	// Set shadow_moved_msec to current time — instance just moved.
	instance.shadow_moved_msec = OS::get_singleton()->get_ticks_msec();

	CHECK_FALSE(instance.is_shadow_static());
}

TEST_CASE("[ShadowCaching] Instance becomes static after threshold time") {
	RendererSceneCull::Instance instance;

	// Set shadow_moved_msec far enough in the past to exceed the threshold.
	uint64_t threshold_msec = (uint64_t)(RendererSceneCull::Instance::SHADOW_STATIC_THRESHOLD_SEC * 1000.0);
	uint64_t now = OS::get_singleton()->get_ticks_msec();
	instance.shadow_moved_msec = now - threshold_msec - 1;

	CHECK(instance.is_shadow_static());
}

TEST_CASE("[ShadowCaching] Static classification resets on transform change") {
	RendererSceneCull::Instance instance;

	uint64_t threshold_msec = (uint64_t)(RendererSceneCull::Instance::SHADOW_STATIC_THRESHOLD_SEC * 1000.0);
	uint64_t now = OS::get_singleton()->get_ticks_msec();

	// Simulate being static (moved long ago).
	instance.shadow_moved_msec = now - threshold_msec - 1;
	CHECK(instance.is_shadow_static());

	// Simulate a transform change (moved just now).
	instance.shadow_moved_msec = OS::get_singleton()->get_ticks_msec();

	// Should no longer be static.
	CHECK_FALSE(instance.is_shadow_static());
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

} // namespace TestShadowCaching
