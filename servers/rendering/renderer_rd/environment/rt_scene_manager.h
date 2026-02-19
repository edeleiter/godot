/**************************************************************************/
/*  rt_scene_manager.h                                                    */
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

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "servers/rendering/rendering_device.h"

class RTSceneManager {
public:
	RTSceneManager();
	~RTSceneManager();

	void cleanup();

	void register_instance(RID p_instance, RID p_mesh, const Transform3D &p_transform);
	void unregister_instance(RID p_instance);
	void update_instance_transform(RID p_instance, const Transform3D &p_transform);

	// Call once per frame to rebuild TLAS if dirty.
	void update_tlas();

	RID get_tlas() const { return tlas; }
	bool is_valid() const { return tlas.is_valid() && !instances.is_empty(); }
	bool is_enabled() const { return rt_available; }

	uint32_t get_instance_count() const { return instances.size(); }

private:
	struct InstanceData {
		RID mesh;
		Transform3D transform;
		bool dirty = true;
	};

	HashMap<RID, InstanceData> instances;
	HashSet<RID> built_blases; // Tracks which BLAS RIDs have been GPU-built.
	RID tlas;
	RID instances_buffer;
	uint32_t instances_buffer_size = 0;
	bool tlas_dirty = true;
	bool rt_available = false;
	bool is_updating_tlas = false;
};
