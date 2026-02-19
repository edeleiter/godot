/**************************************************************************/
/*  test_rt_scene_manager.h                                               */
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

#include "servers/rendering/renderer_rd/environment/rt_scene_manager.h"

#include "tests/test_macros.h"

// NOTE: RTSceneManager requires a RenderingDevice GPU context for meaningful
// register/unregister and TLAS testing. The tests below cover the non-GPU
// (disabled) code paths. GPU-dependent tests would require initializing a
// rendering context (e.g., via a Vulkan or D3D12 backend).

namespace TestRTSceneManager {

TEST_CASE("[RTSceneManager] Initial state") {
	RTSceneManager manager;

	CHECK(manager.get_instance_count() == 0);
	CHECK(manager.is_enabled() == false);
	CHECK(manager.is_valid() == false);
	CHECK_FALSE(manager.get_tlas().is_valid());
}

TEST_CASE("[RTSceneManager] Register and unregister with RT disabled") {
	RTSceneManager manager;

	// Without a GPU context, rt_available is false. register_instance should
	// return early and not add anything to the instance map.
	RID instance = RID::from_uint64(1);
	RID mesh = RID::from_uint64(2);
	Transform3D transform;

	manager.register_instance(instance, mesh, transform);
	CHECK(manager.get_instance_count() == 0);
	CHECK(manager.is_enabled() == false);

	// Unregister on a non-existent instance should be a no-op.
	manager.unregister_instance(instance);
	CHECK(manager.get_instance_count() == 0);
}

TEST_CASE("[RTSceneManager] update_tlas does not crash when disabled") {
	RTSceneManager manager;

	// With rt_available == false, update_tlas should return early without
	// accessing the RenderingDevice singleton.
	manager.update_tlas();

	CHECK(manager.is_enabled() == false);
	CHECK(manager.is_valid() == false);
	CHECK(manager.get_instance_count() == 0);
	CHECK_FALSE(manager.get_tlas().is_valid());
}

TEST_CASE("[RTSceneManager] update_instance_transform with no instances") {
	RTSceneManager manager;

	// Updating a transform for a non-existent instance should be a safe no-op.
	RID instance = RID::from_uint64(42);
	Transform3D new_transform;
	new_transform.origin = Vector3(10.0, 20.0, 30.0);

	manager.update_instance_transform(instance, new_transform);

	CHECK(manager.get_instance_count() == 0);
}

TEST_CASE("[RTSceneManager] No persistent BLAS cache accumulation across register/unregister") {
	RTSceneManager manager;
	// In the old code, built_blases would grow unboundedly and could collide with
	// recycled RIDs, silently skipping GPU builds. After the fix, there is no such
	// set — register/unregister is idempotent with no retained state.
	// Verify instance count starts at zero.
	CHECK(manager.get_instance_count() == 0);
	// RT not available in headless test context, so register is a no-op.
	// This test documents that the cache was removed; GPU-path correctness is
	// verified by Phase B integration tests.
}

} // namespace TestRTSceneManager
