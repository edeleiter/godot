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
	built_blases.clear();
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
	data.dirty = true;

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
		data->dirty = true;
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
		WARN_PRINT_ONCE("RTSceneManager::update_tlas() skipped due to reentrant call — will complete next frame.");
		return;
	}
	is_updating_tlas = true;

	RD *rd = RD::get_singleton();
	if (rd == nullptr) {
		is_updating_tlas = false;
		ERR_PRINT("RTSceneManager::update_tlas: RenderingDevice singleton is null.");
		return;
	}

	// Create any deferred BLAS from mesh loading.
	RendererRD::MeshStorage::get_singleton()->build_pending_blas_surfaces();

	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();

	// Collect BLAS from all mesh surfaces across all registered instances.
	Vector<RID> blases;
	Vector<Transform3D> transforms;

	for (const KeyValue<RID, InstanceData> &kv : instances) {
		int surface_count = mesh_storage->mesh_get_surface_count(kv.value.mesh);
		for (int i = 0; i < surface_count; i++) {
			RID blas = mesh_storage->mesh_surface_get_blas(kv.value.mesh, i);
			if (blas.is_valid()) {
				blases.push_back(blas);
				transforms.push_back(kv.value.transform);
				// Only GPU-build BLASes that haven't been built yet.
				if (!built_blases.has(blas)) {
					Error blas_err = rd->acceleration_structure_build(blas);
					if (blas_err == OK) {
						built_blases.insert(blas);
					} else {
						WARN_PRINT("RTSceneManager: BLAS build failed for RID " + itos(blas.get_id()) + " — will retry next frame.");
					}
				}
			}
		}
	}

	uint32_t count = blases.size();
	if (count == 0) {
		tlas_dirty = false;
		is_updating_tlas = false;
		return;
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

	// Clear dirty flags.
	for (KeyValue<RID, InstanceData> &kv : instances) {
		kv.value.dirty = false;
	}
	tlas_dirty = false;
	is_updating_tlas = false;
}
