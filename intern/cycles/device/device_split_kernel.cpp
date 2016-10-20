/*
 * Copyright 2011-2016 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "device_split_kernel.h"

#include "kernel_types.h"
#include "kernel_split_data.h"

CCL_NAMESPACE_BEGIN

DeviceSplitKernel::DeviceSplitKernel(Device *device) : device(device)
{
	path_iteration_times = PATH_ITER_INC_FACTOR;
	current_max_closure = -1;
	first_tile = true;
}

DeviceSplitKernel::~DeviceSplitKernel()
{
	device->mem_free(kgbuffer);
	device->mem_free(split_data);
	device->mem_free(ray_state);
	device->mem_free(use_queues_flag);
	device->mem_free(queue_index);
	device->mem_free(work_pool_wgs);
}

bool DeviceSplitKernel::load_kernels(const DeviceRequestedFeatures& requested_features)
{
#define LOAD_KERNEL(name) \
		kernel_##name = device->get_split_kernel_function(#name, requested_features); \
		if(!kernel_##name) { \
			return false; \
		}

	LOAD_KERNEL(scene_intersect);
	LOAD_KERNEL(lamp_emission);
	LOAD_KERNEL(queue_enqueue);
	LOAD_KERNEL(background_buffer_update);
	LOAD_KERNEL(shader_eval);
	LOAD_KERNEL(holdout_emission_blurring_pathtermination_ao);
	LOAD_KERNEL(direct_lighting);
	LOAD_KERNEL(shadow_blocked);
	LOAD_KERNEL(next_iteration_setup);
	LOAD_KERNEL(sum_all_radiance);

#undef LOAD_KERNEL

	current_max_closure = requested_features.max_closure;

	return true;
}

bool DeviceSplitKernel::path_trace(DeviceTask *task,
                                   RenderTile& rtile,
                                   int2 max_render_feasible_tile_size,
                                   size_t per_thread_output_buffer_size,
                                   device_memory& kernel_data)
{
	device_memory& d_data = kernel_data;

	/* Make sure that set render feasible tile size is a multiple of local
	 * work size dimensions.
	 */
	assert(max_render_feasible_tile_size.x % SPLIT_KERNEL_LOCAL_SIZE_X == 0);
	assert(max_render_feasible_tile_size.y % SPLIT_KERNEL_LOCAL_SIZE_Y == 0);

	size_t global_size[2];
	size_t local_size[2] = {SPLIT_KERNEL_LOCAL_SIZE_X,
	                        SPLIT_KERNEL_LOCAL_SIZE_Y};

	int d_w = rtile.w;
	int d_h = rtile.h;

#ifdef __WORK_STEALING__
	global_size[0] = (((d_w - 1) / local_size[0]) + 1) * local_size[0];
	global_size[1] = (((d_h - 1) / local_size[1]) + 1) * local_size[1];
	unsigned int num_parallel_samples = 1;
#else
	global_size[1] = (((d_h - 1) / local_size[1]) + 1) * local_size[1];
	unsigned int num_threads = max_render_feasible_tile_size.x *
	                           max_render_feasible_tile_size.y;
	unsigned int num_tile_columns_possible = num_threads / global_size[1];
	/* Estimate number of parallel samples that can be
	 * processed in parallel.
	 */
	unsigned int num_parallel_samples = min(num_tile_columns_possible / d_w,
	                                        rtile.num_samples);
	/* Wavefront size in AMD is 64.
	 * TODO(sergey): What about other platforms?
	 */
	if(num_parallel_samples >= 64) {
		/* TODO(sergey): Could use generic round-up here. */
		num_parallel_samples = (num_parallel_samples / 64) * 64;
	}
	assert(num_parallel_samples != 0);

	global_size[0] = d_w * num_parallel_samples;
#endif  /* __WORK_STEALING__ */

	assert(global_size[0] * global_size[1] <=
	       max_render_feasible_tile_size.x * max_render_feasible_tile_size.y);

	int num_global_elements = max_render_feasible_tile_size.x *
	                          max_render_feasible_tile_size.y;

	/* Allocate all required global memory once. */
	if(first_tile) {
#ifdef __WORK_STEALING__
		/* Calculate max groups */
		size_t max_global_size[2];
		size_t tile_x = max_render_feasible_tile_size.x;
		size_t tile_y = max_render_feasible_tile_size.y;
		max_global_size[0] = (((tile_x - 1) / local_size[0]) + 1) * local_size[0];
		max_global_size[1] = (((tile_y - 1) / local_size[1]) + 1) * local_size[1];

		/* Denotes the maximum work groups possible w.r.t. current tile size. */
		unsigned int max_work_groups = (max_global_size[0] * max_global_size[1]) /
		                  (local_size[0] * local_size[1]);

		/* Allocate work_pool_wgs memory. */
		device->mem_alloc(work_pool_wgs, max_work_groups * sizeof(unsigned int));
#endif  /* __WORK_STEALING__ */

		device->mem_alloc(queue_index, NUM_QUEUES * sizeof(int));
		device->mem_alloc(use_queues_flag, sizeof(char));
		device->mem_alloc(kgbuffer, device->sizeof_KernelGlobals());

		ray_state.resize(num_global_elements);
		device->mem_alloc(ray_state, MEM_READ_WRITE);

		device->mem_alloc(split_data, split_data_buffer_size(num_global_elements,
		                                                     current_max_closure,
		                                                     per_thread_output_buffer_size));
	}

	if(!device->enqueue_split_kernel_data_init(KernelDimensions(global_size, local_size),
	                                           rtile,
	                                           num_global_elements,
	                                           num_parallel_samples,
	                                           kgbuffer,
	                                           d_data,
	                                           split_data,
	                                           ray_state,
	                                           queue_index,
	                                           use_queues_flag,
	                                           work_pool_wgs
	                                           ))
	{
		return false;
	}

#define ENQUEUE_SPLIT_KERNEL(name, global_size, local_size) \
		if(!kernel_##name->enqueue(KernelDimensions(global_size, local_size), kgbuffer, d_data)) { \
			return false; \
		}

	/* Record number of time host intervention has been made */
	unsigned int numHostIntervention = 0;
	unsigned int numNextPathIterTimes = path_iteration_times;
	bool canceled = false;

	bool activeRaysAvailable = true;
	while(activeRaysAvailable) {
		/* Twice the global work size of other kernels for
		 * ckPathTraceKernel_shadow_blocked_direct_lighting. */
		size_t global_size_shadow_blocked[2];
		global_size_shadow_blocked[0] = global_size[0] * 2;
		global_size_shadow_blocked[1] = global_size[1];

		/* Do path-iteration in host [Enqueue Path-iteration kernels. */
		for(int PathIter = 0; PathIter < path_iteration_times; PathIter++) {
			ENQUEUE_SPLIT_KERNEL(scene_intersect, global_size, local_size);
			ENQUEUE_SPLIT_KERNEL(lamp_emission, global_size, local_size);
			ENQUEUE_SPLIT_KERNEL(queue_enqueue, global_size, local_size);
			ENQUEUE_SPLIT_KERNEL(background_buffer_update, global_size, local_size);
			ENQUEUE_SPLIT_KERNEL(shader_eval, global_size, local_size);
			ENQUEUE_SPLIT_KERNEL(holdout_emission_blurring_pathtermination_ao, global_size, local_size);
			ENQUEUE_SPLIT_KERNEL(direct_lighting, global_size, local_size);
			ENQUEUE_SPLIT_KERNEL(shadow_blocked, global_size_shadow_blocked, local_size);
			ENQUEUE_SPLIT_KERNEL(next_iteration_setup, global_size, local_size);

			if(task->get_cancel()) {
				canceled = true;
				break;
			}
		}

		/* Decide if we should exit path-iteration in host. */
		device->mem_copy_from(ray_state, 0, global_size[0] * global_size[1] * sizeof(char), 1, 1);

		activeRaysAvailable = false;

		for(int rayStateIter = 0;
		    rayStateIter < global_size[0] * global_size[1];
		    ++rayStateIter)
		{
			if(int8_t(ray_state.get_data()[rayStateIter]) != RAY_INACTIVE) {
				/* Not all rays are RAY_INACTIVE. */
				activeRaysAvailable = true;
				break;
			}
		}

		if(activeRaysAvailable) {
			numHostIntervention++;
			path_iteration_times = PATH_ITER_INC_FACTOR;
			/* Host intervention done before all rays become RAY_INACTIVE;
			 * Set do more initial iterations for the next tile.
			 */
			numNextPathIterTimes += PATH_ITER_INC_FACTOR;
		}

		if(task->get_cancel()) {
			canceled = true;
			break;
		}
	}

	/* Execute SumALLRadiance kernel to accumulate radiance calculated in
	 * per_sample_output_buffers into RenderTile's output buffer.
	 */
	if(!canceled) {
		size_t sum_all_radiance_local_size[2] = {16, 16};
		size_t sum_all_radiance_global_size[2];
		sum_all_radiance_global_size[0] =
			(((d_w - 1) / sum_all_radiance_local_size[0]) + 1) *
			sum_all_radiance_local_size[0];
		sum_all_radiance_global_size[1] =
			(((d_h - 1) / sum_all_radiance_local_size[1]) + 1) *
			sum_all_radiance_local_size[1];

		ENQUEUE_SPLIT_KERNEL(sum_all_radiance,
		                     sum_all_radiance_global_size,
		                     sum_all_radiance_local_size);
	}

#undef ENQUEUE_SPLIT_KERNEL

	if(numHostIntervention == 0) {
		/* This means that we are executing kernel more than required
		 * Must avoid this for the next sample/tile.
		 */
		path_iteration_times = ((numNextPathIterTimes - PATH_ITER_INC_FACTOR) <= 0) ?
		PATH_ITER_INC_FACTOR : numNextPathIterTimes - PATH_ITER_INC_FACTOR;
	}
	else {
		/* Number of path-iterations done for this tile is set as
		 * Initial path-iteration times for the next tile
		 */
		path_iteration_times = numNextPathIterTimes;
	}

	first_tile = false;

	return true;
}

CCL_NAMESPACE_END


