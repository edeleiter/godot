/**************************************************************************/
/*  rt_scene_manager.cpp                                                  */
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

#include "rt_scene_manager.h"

#include "servers/rendering/renderer_rd/storage_rd/mesh_storage.h"

RTSceneManager::RTSceneManager() {
	RD *rd = RD::get_singleton();
	if (rd) {
		rt_available = rd->has_feature(RD::SUPPORTS_RAYTRACING_PIPELINE) || rd->has_feature(RD::SUPPORTS_RAY_QUERY);
	}
}

RTSceneManager::~RTSceneManager() {
	cleanup();
}

void RTSceneManager::cleanup() {
	if (RD::get_singleton()) {
		if (tlas.is_valid()) {
			RD::get_singleton()->free_rid(tlas);
			tlas = RID();
		}
		if (instances_buffer.is_valid()) {
			RD::get_singleton()->free_rid(instances_buffer);
			instances_buffer = RID();
		}
	}
	instances.clear();
	instances_buffer_size = 0;
	tlas_dirty = true;
}

void RTSceneManager::register_instance(RID p_instance, RID p_mesh, const Transform3D &p_transform) {
	if (!rt_available || !p_mesh.is_valid()) {
		return;
	}

	InstanceData data;
	data.mesh = p_mesh;
	data.transform = p_transform;

	instances.insert(p_instance, data);
	tlas_dirty = true;
}

void RTSceneManager::unregister_instance(RID p_instance) {
	if (instances.erase(p_instance)) {
		tlas_dirty = true;
	}
}

void RTSceneManager::update_instance_transform(RID p_instance, const Transform3D &p_transform) {
	InstanceData *data = instances.getptr(p_instance);
	if (data) {
		data->transform = p_transform;
		tlas_dirty = true;
	}
}

void RTSceneManager::update_tlas() {
	if (!rt_available || !tlas_dirty || instances.is_empty()) {
		return;
	}

	// Guard against reentrant calls from ProgressDialog pumping Main::iteration during
	// long editor operations (e.g., filesystem scan). Mesh surfaces may be partially
	// initialized during resource loading, making it unsafe to iterate instances.
	// tlas_dirty stays true so the update completes on the next normal frame.
	if (is_updating_tlas) {
		WARN_PRINT_ONCE("RTSceneManager::update_tlas() skipped due to reentrant call -- will complete next frame.");
		return;
	}

	// RAII guard: ensures is_updating_tlas is always reset on any exit path.
	struct TLASGuard {
		bool &flag;
		TLASGuard(bool &p_flag) : flag(p_flag) { flag = true; }
		~TLASGuard() { flag = false; }
	} tlas_guard(is_updating_tlas);

	RD *rd = RD::get_singleton();
	if (rd == nullptr) {
		ERR_PRINT("RTSceneManager::update_tlas: RenderingDevice singleton is null.");
		return;
	}

	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();
	// Create any deferred BLAS from mesh loading.
	mesh_storage->build_pending_blas_surfaces();

	// TODO(Phase B): Deformable meshes (skinning, blend shapes) need per-frame BLAS rebuilds.
	// Currently, their BLAS is a snapshot from creation time and becomes stale when vertex data
	// changes. Options: (a) exclude skinned meshes from TLAS entirely (safest for Phase B),
	// (b) rebuild BLAS per frame for deformable surfaces (expensive). See RT_INFRASTRUCTURE.md.

	// Collect BLAS from all mesh surfaces across all registered instances.
	// Stale instances (freed mesh RIDs) are collected and unregistered after the loop.
	Vector<RID> blases;
	Vector<Transform3D> transforms;
	Vector<RID> stale_instances;

	for (const KeyValue<RID, InstanceData> &kv : instances) {
		if (!mesh_storage->mesh_is_valid(kv.value.mesh)) {
			// Mesh was freed without calling rt_unregister_instance -- auto-cleanup.
			stale_instances.push_back(kv.key);
			continue;
		}
		int surface_count = mesh_storage->mesh_get_surface_count(kv.value.mesh);
		for (int i = 0; i < surface_count; i++) {
			RID blas = mesh_storage->mesh_surface_get_blas(kv.value.mesh, i);
			if (blas.is_valid()) {
				blases.push_back(blas);
				transforms.push_back(kv.value.transform);
				// Only submit the GPU build command once per BLAS lifetime.
				// Static geometry never needs a rebuild; deformable meshes are a Phase B concern.
				if (!mesh_storage->mesh_surface_is_blas_built(kv.value.mesh, i)) {
					Error blas_err = rd->acceleration_structure_build(blas);
					if (blas_err == OK) {
						mesh_storage->mesh_surface_mark_blas_built(kv.value.mesh, i);
					} else {
						WARN_PRINT("RTSceneManager: BLAS build failed for RID " + itos(blas.get_id()) + " -- will retry next frame.");
					}
				}
			}
		}
	}

	for (const RID &stale : stale_instances) {
		WARN_PRINT("RTSceneManager: auto-unregistering instance with freed mesh RID -- call rt_unregister_instance before freeing the mesh.");
		instances.erase(stale);
	}

	uint32_t count = blases.size();
	if (count == 0) {
		tlas_dirty = false;
		return; // tlas_guard destructor resets is_updating_tlas.
	}

	// Reallocate instances buffer if needed.
	if (count != instances_buffer_size) {
		if (tlas.is_valid()) {
			rd->free_rid(tlas);
			tlas = RID();
		}
		if (instances_buffer.is_valid()) {
			rd->free_rid(instances_buffer);
			instances_buffer = RID();
		}

		instances_buffer = rd->tlas_instances_buffer_create(count);
		instances_buffer_size = count;
	}

	// Fill the instances buffer with BLAS references and transforms.
	rd->tlas_instances_buffer_fill(instances_buffer, blases, transforms);

	// Create or rebuild TLAS.
	if (!tlas.is_valid()) {
		tlas = rd->tlas_create(instances_buffer);
	}

	Error err = rd->acceleration_structure_build(tlas);
	if (err != OK) {
		ERR_PRINT("RTSceneManager: Failed to build TLAS.");
	}

	tlas_dirty = false;
	// tlas_guard destructor resets is_updating_tlas.
}
