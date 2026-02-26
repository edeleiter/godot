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
#include "servers/rendering/rendering_device.h"

// Internal-only class (not a GDCLASS, not exposed to scripting or ClassDB).
// Manages per-frame BLAS/TLAS lifecycle for the RT acceleration structure infrastructure.
class RTSceneManager {
public:
	RTSceneManager();
	~RTSceneManager();

	void cleanup();

	void register_instance(RID p_instance, RID p_mesh, const Transform3D &p_transform);
	void unregister_instance(RID p_instance);
	void update_instance_transform(RID p_instance, const Transform3D &p_transform);

	// Mark an instance as deformable (skinned or blend-shape). Must be called after register_instance.
	// p_mesh_instance is the MeshInstance RID whose current deformed vertex buffer is used each frame.
	// Deformable BLASes are updated each frame via update_deformable_blas_and_rebuild_tlas(),
	// which must be called AFTER GPU skinning (update_mesh_instances) runs.
	void mark_instance_deformable(RID p_instance, RID p_mesh_instance);

	// Call once per frame to rebuild TLAS for non-deformable changes.
	void update_tlas();

	// Call each frame AFTER GPU skinning (update_mesh_instances) to update deformable BLASes
	// and rebuild the TLAS if any deformable geometry changed.
	void update_deformable_blas_and_rebuild_tlas();

	RID get_tlas() const { return tlas; }
	bool is_valid() const { return tlas.is_valid() && !instances.is_empty(); }
	bool is_enabled() const { return rt_available; }

	uint32_t get_instance_count() const { return instances.size(); }
	uint32_t get_tlas_entry_count() const { return instances_buffer_size; }
	const Vector<Color> &get_tlas_entry_colors() const { return tlas_entry_colors; }
	uint32_t get_deformable_instance_count() const { return deformable_instances.size(); }
	bool was_deformable_updated_this_frame() const { return deformable_updated_this_frame; }

private:
	struct InstanceData {
		RID mesh;
		RID mesh_instance; // Only valid for deformable instances; used to access current deformed vertex buffer.
		Transform3D transform;
	};

	HashMap<RID, InstanceData> instances;
	HashSet<RID> deformable_instances; // Subset of instances needing per-frame BLAS update.
	RID tlas;
	RID instances_buffer;
	uint32_t instances_buffer_size = 0;
	Vector<Color> tlas_entry_colors; // Parallel to TLAS entries; one Color per surface.
	bool tlas_dirty = true;
	bool rt_available = false;
	bool is_updating_tlas = false;
	bool deformable_updated_this_frame = false;

};
