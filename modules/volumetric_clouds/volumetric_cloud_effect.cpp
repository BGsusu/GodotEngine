#include "volumetric_cloud_effect.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "servers/rendering/renderer_rd/environment/fog.h"
#include "servers/rendering/renderer_rd/renderer_scene_render_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_data_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/rendering_server_globals.h"
#include "volumetric_cloud.glsl.gen.h"

class VolumetricCloudRenderer {
	struct UniformData {
		float inverse_view_projection[16];
		float previous_view_projection[16];
		float previous_inverse_view_projection[16];
		float camera[4];
		float previous_camera[4];
		float sun_direction[4];
		float sun_radiance[4];
		float ambient_radiance[4];
		float layer[4];
		float noise_wind[4];
		float wind_delta[4];
		float fog[4];
		float dimensions[4];
		float trace_dimensions[4];
		float control[4];
		float material_shape[4];
		float material_profile[4];
		float material_albedo[4];
		float material_flags[4];
		float cyclic_shape[4];
		float aerial_horizon[4];
		float aerial_zenith[4];
		float aerial_control[4];
		float fog_view_projection[16];
		float fog_view_depth[4];
		float volume_fog[4];
		float volume_fog_options[4];
		float analytic_fog[24];
	};
	struct ViewState {
		RID uniform_buffer;
		Projection previous_view_projection;
		Projection previous_projection;
		Transform3D previous_transform;
		Vector3 previous_sun;
		Vector3 previous_wind_offset;
		float previous_coverage = 0.0f;
		float previous_density = 0.0f;
		uint64_t material_version = 0;
		float previous_energy = 0.0;
		uint64_t reset_version = 0;
		uint64_t frame = 0;
		uint64_t last_frame = 0;
		uint64_t resets = 0;
	};
	struct ViewportState {
		Size2i size;
		Size2i trace_size;
		Vector<ViewState> views;
	};
	HashMap<ObjectID, ViewportState> viewports;
	StringName context;
	VolumetricCloudShaderRD shader;
	RID version;
	RID pipelines[3];
	RID noise_sampler;
	bool initialized = false;

	void free_viewport(ViewportState &p_viewport) {
		for (ViewState &view : p_viewport.views) {
			if (view.uniform_buffer.is_valid()) {
				RD::get_singleton()->free_rid(view.uniform_buffer);
			}
		}
		p_viewport.views.clear();
	}

	void initialize() {
		Vector<String> variants;
		variants.push_back("\n#define MODE_TRACE\n");
		variants.push_back("\n#define MODE_RECONSTRUCT\n");
		variants.push_back("\n#define MODE_COMPOSITE\n");
		shader.initialize(variants);
		version = shader.version_create();
		for (int i = 0; i < 3; i++) {
			pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(version, i));
		}
		RD::SamplerState sampler;
		sampler.mag_filter = RD::SAMPLER_FILTER_LINEAR;
		sampler.min_filter = RD::SAMPLER_FILTER_LINEAR;
		sampler.mip_filter = RD::SAMPLER_FILTER_LINEAR;
		sampler.repeat_u = RD::SAMPLER_REPEAT_MODE_REPEAT;
		sampler.repeat_v = RD::SAMPLER_REPEAT_MODE_REPEAT;
		sampler.repeat_w = RD::SAMPLER_REPEAT_MODE_REPEAT;
		sampler.max_lod = 16.0;
		noise_sampler = RD::get_singleton()->sampler_create(sampler);
		initialized = true;
	}

public:
	explicit VolumetricCloudRenderer(ObjectID p_owner) {
		context = StringName("volumetric_clouds_" + itos(uint64_t(p_owner)));
	}

	~VolumetricCloudRenderer() {
		if (!RD::get_singleton()) {
			return;
		}
		for (KeyValue<ObjectID, ViewportState> &entry : viewports) {
			free_viewport(entry.value);
			RenderSceneBuffersRD *buffers = Object::cast_to<RenderSceneBuffersRD>(ObjectDB::get_instance(entry.key));
			if (buffers) {
				buffers->clear_context(context);
			}
		}
		if (initialized) {
			shader.version_free(version);
			RD::get_singleton()->free_rid(noise_sampler);
		}
	}

	Dictionary render(const RenderData *p_data, const VolumetricCloudEffect::Parameters &p_params, const VolumetricCloudMaterial::Snapshot *p_material) {
		Dictionary result;
		RD *rd = RD::get_singleton();
		Ref<RenderSceneBuffersRD> buffers = p_data->get_render_scene_buffers();
		RenderSceneDataRD *scene = Object::cast_to<RenderSceneDataRD>(p_data->get_render_scene_data());
		if (!rd || buffers.is_null() || !scene || !buffers->get_can_be_storage() || scene->cam_orthogonal || buffers->get_base_data_format() != RD::DATA_FORMAT_R16G16B16A16_SFLOAT) {
			result["supported"] = false;
			return result;
		}
		Size2i full = buffers->get_internal_size();
		if (full.x < 1 || full.y < 1) {
			return result;
		}
		if (!initialized) {
			initialize();
		}
		for (int i = 0; i < 3; i++) {
			ERR_FAIL_COND_V(pipelines[i].is_null(), result);
		}
		// CPU state never retains a strong viewport reference. Release UBOs of closed views.
		Vector<ObjectID> retired;
		for (const KeyValue<ObjectID, ViewportState> &entry : viewports) {
			if (!ObjectDB::get_instance(entry.key)) {
				retired.push_back(entry.key);
			}
		}
		for (ObjectID id : retired) {
			free_viewport(viewports[id]);
			viewports.erase(id);
		}
		ViewportState &state = viewports[buffers->get_instance_id()];
		Size2i half((full.x + 1) / 2, (full.y + 1) / 2);
		Size2i quarter((full.x + 3) / 4, (full.y + 3) / 4);
		Size2i trace_size = p_params.complete_tracing ? half : quarter;
		uint32_t views = buffers->get_view_count();
		if (state.size != full || state.trace_size != trace_size || state.views.size() != int(views) || !buffers->has_texture(context, SNAME("trace_color"))) {
			free_viewport(state);
			buffers->clear_context(context);
			state.size = full;
			state.trace_size = trace_size;
			state.views.resize(views);
			uint32_t usage = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
			buffers->create_texture(context, SNAME("trace_color"), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, trace_size);
			buffers->create_texture(context, SNAME("trace_near_color"), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, trace_size);
			buffers->create_texture(context, SNAME("trace_depth"), RD::DATA_FORMAT_R32G32B32A32_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, trace_size);
			buffers->create_texture(context, SNAME("trace_limits"), RD::DATA_FORMAT_R32G32B32A32_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, trace_size);
			for (const char *name : { "color_0", "color_1", "near_color_0", "near_color_1" }) {
				buffers->create_texture(context, StringName(name), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, half);
			}
			for (const char *name : { "depth_0", "depth_1", "limits_0", "limits_1" }) {
				buffers->create_texture(context, StringName(name), RD::DATA_FORMAT_R32G32B32A32_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, half);
			}
			for (ViewState &view : state.views) {
				view.uniform_buffer = rd->uniform_buffer_create(sizeof(UniformData));
			}
		}
		RendererRD::MaterialStorage *materials = RendererRD::MaterialStorage::get_singleton();
		UniformSetCacheRD *cache = UniformSetCacheRD::get_singleton();
		RID nearest = materials->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		RID linear = materials->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		RendererRD::TextureStorage *textures = RendererRD::TextureStorage::get_singleton();
		RID body_texture = p_material && p_material->body_rid.is_valid() ? textures->texture_get_rd_texture(p_material->body_rid) : RID();
		RID detail_texture = p_material && p_material->detail_rid.is_valid() ? textures->texture_get_rd_texture(p_material->detail_rid) : RID();
		RID weather_texture = p_material && p_material->weather_rid.is_valid() ? textures->texture_get_rd_texture(p_material->weather_rid) : RID();
		RID cyclic_texture = p_material && p_material->cyclic_rid.is_valid() ? textures->texture_get_rd_texture(p_material->cyclic_rid) : RID();
		bool has_body = body_texture.is_valid();
		bool has_detail = detail_texture.is_valid();
		bool has_weather = weather_texture.is_valid();
		bool has_cyclic = cyclic_texture.is_valid();
		bool cyclic_active = p_material && p_material->density_model == VolumetricCloudMaterial::DENSITY_MODEL_CYCLIC && has_cyclic;
		if (!has_body) body_texture = textures->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_3D_WHITE);
		if (!has_detail) detail_texture = textures->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_3D_WHITE);
		if (!has_weather) weather_texture = textures->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_WHITE);
		if (!has_cyclic) cyclic_texture = textures->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_3D_WHITE);
		RendererSceneRenderRD *scene_renderer = RendererSceneRenderRD::get_singleton();
		Ref<RendererRD::Fog::VolumetricFog> volume_fog;
		RID environment = p_data->get_environment();
		if (environment.is_valid() && scene_renderer->environment_get_volumetric_fog_enabled(environment) && buffers->has_custom_data(RB_SCOPE_FOG)) {
			volume_fog = buffers->get_custom_data(RB_SCOPE_FOG);
		}
		bool has_volume_fog = volume_fog.is_valid() && volume_fog->fog_map.is_valid();
		RID volume_fog_texture = has_volume_fog ? volume_fog->fog_map : textures->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_3D_WHITE);
		PackedFloat32Array analytic_parameters = environment.is_valid() ? scene_renderer->environment_get_analytic_fog(environment) : PackedFloat32Array();
		bool analytic_enabled = analytic_parameters.size() == 24 && analytic_parameters[0] > 0.5f;
		float volume_sky_affect = has_volume_fog ? scene_renderer->environment_get_volumetric_fog_sky_affect(environment) : 0.0f;
		float normalization = buffers->get_luminance_multiplier();
		if (p_data->get_camera_attributes().is_valid()) {
			normalization *= RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(p_data->get_camera_attributes());
		} else if (scene->emissive_exposure_normalization > 0.0) {
			normalization *= scene->emissive_exposure_normalization;
		}
		uint64_t engine_frame = Engine::get_singleton()->get_frames_drawn();
		String timestamp_begin = String(context) + "_" + itos(uint64_t(buffers->get_instance_id())) + " Begin";
		String timestamp_end = String(context) + "_" + itos(uint64_t(buffers->get_instance_id())) + " End";
		uint64_t gpu_begin = 0;
		bool has_gpu_begin = false;
		for (uint32_t stamp = 0; stamp < rd->get_captured_timestamps_count(); stamp++) {
			String name = rd->get_captured_timestamp_name(stamp);
			if (name == timestamp_begin) {
				gpu_begin = rd->get_captured_timestamp_gpu_time(stamp);
				has_gpu_begin = true;
			} else if (name == timestamp_end && has_gpu_begin) {
				uint64_t gpu_end = rd->get_captured_timestamp_gpu_time(stamp);
				if (gpu_end >= gpu_begin) {
					result["gpu_time_ms"] = double(gpu_end - gpu_begin) / 1000000.0;
					result["gpu_timing_frame"] = rd->get_captured_timestamps_frame();
				}
			}
		}
		rd->draw_command_begin_label("Volumetric Clouds Mode 0");
		rd->capture_timestamp(timestamp_begin);
		for (uint32_t index = 0; index < views; index++) {
			ViewState &view = state.views.write[index];
			Transform3D camera = scene->get_cam_transform();
			camera.origin += camera.basis.xform(scene->get_view_eye_offset(index));
			Projection projection = views > 1 ? scene->get_view_projection(index) : scene->get_cam_projection();
			Projection view_projection = projection * Projection(camera.affine_inverse());
			uint64_t material_version = p_material ? p_material->structure_version : 0;
			bool valid = view.frame > 0 && view.reset_version == p_params.reset_version
					&& material_version == view.material_version
					&& Math::abs(p_params.coverage - view.previous_coverage) <= 0.1f
					&& Math::abs(p_params.density - view.previous_density) <= MAX(0.00001f, MAX(p_params.density, view.previous_density) * 0.25f)
					&& p_params.wind_offset.distance_to(view.previous_wind_offset) < 100.0
					&& engine_frame <= view.last_frame + 1
					&& camera.origin.distance_to(view.previous_transform.origin) < 100.0
					&& camera.basis.get_column(2).dot(view.previous_transform.basis.get_column(2)) > 0.8
					&& p_params.sun_direction.dot(view.previous_sun) > 0.95
					&& Math::abs(projection[0][0] - view.previous_projection[0][0]) < 0.05
					&& Math::abs(projection[1][1] - view.previous_projection[1][1]) < 0.05
					&& Math::abs(p_params.sun_energy - view.previous_energy) < 0.5;
			if (!valid) view.resets++;
			UniformData data = {};
			RendererRD::MaterialStorage::store_camera(view_projection.inverse(), data.inverse_view_projection);
			RendererRD::MaterialStorage::store_camera(valid ? view.previous_view_projection : view_projection, data.previous_view_projection);
			RendererRD::MaterialStorage::store_camera((valid ? view.previous_view_projection : view_projection).inverse(), data.previous_inverse_view_projection);
			for (int axis = 0; axis < 3; axis++) {
				data.camera[axis] = camera.origin[axis];
				data.previous_camera[axis] = view.previous_transform.origin[axis];
				data.sun_direction[axis] = p_params.sun_direction[axis];
				data.sun_radiance[axis] = p_params.sun_color[axis] * p_params.sun_energy;
				data.ambient_radiance[axis] = p_params.ambient_color[axis];
				data.noise_wind[axis] = p_params.wind_offset[axis];
				data.wind_delta[axis] = valid ? (p_params.wind_offset[axis] - view.previous_wind_offset[axis]) : 0.0;
				data.fog[axis] = p_params.fog_color[axis];
			}
			data.camera[3] = p_params.max_distance;
			data.sun_direction[3] = p_params.anisotropy;
			data.layer[0] = p_params.cloud_base_height;
			data.layer[1] = p_params.cloud_thickness;
			data.layer[2] = p_params.planet_radius;
			data.layer[3] = p_params.density;
			data.noise_wind[3] = p_params.noise_scale;
			data.fog[3] = p_params.fog_transmittance;
			data.dimensions[0] = full.x;
			data.dimensions[1] = full.y;
			data.dimensions[2] = half.x;
			data.dimensions[3] = half.y;
			data.trace_dimensions[0] = trace_size.x;
			data.trace_dimensions[1] = trace_size.y;
			// Keep the phase and low-discrepancy cycle exactly representable in the
			// shader float, including long-running editor sessions.
			data.trace_dimensions[2] = view.frame % 4096;
			data.trace_dimensions[3] = valid ? 1.0 : 0.0;
			data.control[0] = p_params.coverage;
			data.control[1] = p_params.view_steps;
			data.control[2] = p_params.light_steps;
			data.control[3] = normalization;
			data.material_shape[0] = p_material ? p_material->detail_scale : 400.0;
			data.material_shape[1] = p_material ? p_material->weather_scale : 16000.0;
			data.material_shape[2] = p_material ? p_material->erosion_strength : 0.0;
			data.material_shape[3] = p_material ? p_material->edge_softness : 0.16;
			Vector4 height_profile = p_material ? p_material->height_profile : Vector4(0.0, 0.12, 0.6, 1.0);
			for (int channel = 0; channel < 4; channel++) {
				data.material_profile[channel] = height_profile[channel];
				data.material_albedo[channel] = p_material ? p_material->albedo[channel] : 1.0;
			}
			data.material_albedo[3] = p_material ? 1.0 : 0.0;
			data.material_flags[0] = has_body ? 1.0 : 0.0;
			data.material_flags[1] = has_detail ? 1.0 : 0.0;
			data.material_flags[2] = has_weather ? 1.0 : 0.0;
			data.material_flags[3] = 4.0 / (MAX(Math::abs(projection[1][1]), 0.001) * full.y);
			data.cyclic_shape[0] = cyclic_active ? 1.0 : 0.0;
			data.cyclic_shape[1] = p_material ? p_material->cyclic_detail_strength : 0.45;
			data.cyclic_shape[2] = p_material ? p_material->cyclic_height_variation : 0.3;
			data.cyclic_shape[3] = p_material ? p_material->cyclic_density_contrast : 1.4;
			for (int channel = 0; channel < 3; channel++) {
				data.aerial_horizon[channel] = p_params.aerial_horizon_color[channel];
				data.aerial_zenith[channel] = p_params.aerial_zenith_color[channel];
			}
			data.aerial_horizon[3] = p_params.aerial_strength;
			data.aerial_zenith[3] = p_params.aerial_distance;
			data.aerial_control[0] = p_params.aerial_start_distance;
			data.aerial_control[1] = p_params.complete_tracing ? 1.0f : 0.0f;
			Transform3D central_view = scene->get_cam_transform().affine_inverse();
			RendererRD::MaterialStorage::store_camera(scene->get_cam_projection() * Projection(central_view), data.fog_view_projection);
			for (int axis = 0; axis < 3; axis++) {
				data.fog_view_depth[axis] = -central_view.basis[2][axis];
			}
			data.fog_view_depth[3] = -central_view.origin.z;
			data.volume_fog[0] = has_volume_fog ? 1.0f : 0.0f;
			data.volume_fog[1] = has_volume_fog && volume_fog->length > 0.0f ? 1.0f / volume_fog->length : 1.0f;
			data.volume_fog[2] = has_volume_fog && volume_fog->spread > 0.0f ? 1.0f / volume_fog->spread : 1.0f;
			data.volume_fog[3] = volume_sky_affect;
			data.volume_fog_options[0] = scene_renderer->fog_use_legacy_blending_get() ? 1.0f : 0.0f;
			// The integrated fog map already uses exposed lighting. Match the sky's
			// final storage conversion, without applying cloud exposure a second time.
			data.volume_fog_options[1] = 1.0f / MAX(buffers->get_luminance_multiplier(), 0.00001f);
			if (analytic_parameters.size() == 24) {
				memcpy(data.analytic_fog, analytic_parameters.ptr(), sizeof(data.analytic_fog));
				float buffer_luminance = MAX(buffers->get_luminance_multiplier(), 0.00001f);
				data.analytic_fog[11] = normalization / (buffer_luminance * buffer_luminance);
			}
			rd->buffer_update(view.uniform_buffer, 0, sizeof(UniformData), &data);
			RID trace_color = buffers->get_texture_slice(context, SNAME("trace_color"), index, 0);
			RID trace_near_color = buffers->get_texture_slice(context, SNAME("trace_near_color"), index, 0);
			RID trace_depth = buffers->get_texture_slice(context, SNAME("trace_depth"), index, 0);
			RID trace_limits = buffers->get_texture_slice(context, SNAME("trace_limits"), index, 0);
			bool odd = (view.frame & 1) != 0;
			RID history_limits = buffers->get_texture_slice(context, odd ? SNAME("limits_1") : SNAME("limits_0"), index, 0);
			RID previous_limits = buffers->get_texture_slice(context, odd ? SNAME("limits_0") : SNAME("limits_1"), index, 0);
			RID history_color = buffers->get_texture_slice(context, odd ? SNAME("color_1") : SNAME("color_0"), index, 0);
			RID history_depth = buffers->get_texture_slice(context, odd ? SNAME("depth_1") : SNAME("depth_0"), index, 0);
			RID previous_color = buffers->get_texture_slice(context, odd ? SNAME("color_0") : SNAME("color_1"), index, 0);
			RID history_near_color = buffers->get_texture_slice(context, odd ? SNAME("near_color_1") : SNAME("near_color_0"), index, 0);
			RID previous_near_color = buffers->get_texture_slice(context, odd ? SNAME("near_color_0") : SNAME("near_color_1"), index, 0);
			RID previous_depth = buffers->get_texture_slice(context, odd ? SNAME("depth_0") : SNAME("depth_1"), index, 0);
			RD::Uniform u_params(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, { view.uniform_buffer });
			RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, { nearest, buffers->get_depth_texture(index) });
			RID trace_shader = shader.version_get_shader(version, 0);
			RID trace_set = cache->get_cache(trace_shader, 0, u_params, u_depth,
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 2, { trace_color }), RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 3, { trace_depth }),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 4, { trace_near_color }),
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 5, { noise_sampler, body_texture }),
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 6, { noise_sampler, detail_texture }),
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 7, { noise_sampler, weather_texture }),
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 8, { noise_sampler, cyclic_texture }),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 9, { trace_limits }));
			RD::ComputeListID list = rd->compute_list_begin();
			rd->compute_list_bind_compute_pipeline(list, pipelines[0]);
			rd->compute_list_bind_uniform_set(list, trace_set, 0);
			rd->compute_list_dispatch_threads(list, trace_size.x, trace_size.y, 1);
			rd->compute_list_add_barrier(list);
			RID reconstruct_shader = shader.version_get_shader(version, 1);
			RID reconstruct_set = cache->get_cache(reconstruct_shader, 0, u_params, u_depth,
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, { nearest, trace_color }),
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, { nearest, trace_depth }),
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 4, { linear, previous_color }),
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 5, { nearest, previous_depth }),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 6, { history_color }),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 7, { history_depth }),
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 8, { nearest, trace_near_color }),
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 9, { linear, previous_near_color }),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 10, { history_near_color }),
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 11, { nearest, trace_limits }),
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 12, { nearest, previous_limits }),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 13, { history_limits }));
			rd->compute_list_bind_compute_pipeline(list, pipelines[1]);
			rd->compute_list_bind_uniform_set(list, reconstruct_set, 0);
			rd->compute_list_dispatch_threads(list, half.x, half.y, 1);
			rd->compute_list_add_barrier(list);
			RID composite_shader = shader.version_get_shader(version, 2);
			RID composite_set = cache->get_cache(composite_shader, 0, u_params, u_depth,
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, { nearest, history_color }),
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, { nearest, history_depth }),
					RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 4, { buffers->get_internal_texture(index) }),
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 5, { nearest, history_near_color }),
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 6, { linear, volume_fog_texture }),
					RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 7, { nearest, history_limits }));
			rd->compute_list_bind_compute_pipeline(list, pipelines[2]);
			rd->compute_list_bind_uniform_set(list, composite_set, 0);
			rd->compute_list_dispatch_threads(list, full.x, full.y, 1);
			rd->compute_list_end();
			view.previous_view_projection = view_projection;
			view.previous_projection = projection;
			view.previous_transform = camera;
			view.previous_sun = p_params.sun_direction;
			view.previous_energy = p_params.sun_energy;
			view.previous_wind_offset = p_params.wind_offset;
			view.previous_coverage = p_params.coverage;
			view.previous_density = p_params.density;
			view.material_version = material_version;
			view.reset_version = p_params.reset_version;
			view.last_frame = engine_frame;
			view.frame++;
			if (index == 0) {
				result["frame"] = view.frame;
				result["history_valid"] = valid;
				result["reset_count"] = view.resets;
			}
		}
		rd->capture_timestamp(timestamp_end);
		rd->draw_command_end_label();
		result["wind_offset"] = p_params.wind_offset;
		result["temporal_version"] = 2;
		result["history_max_age"] = 8;
		result["temporal_ray_jitter"] = false;
		result["supported"] = true;
		result["mode"] = p_params.complete_tracing ? 1 : 0;
		result["complete_tracing"] = p_params.complete_tracing;
		result["trace_size"] = trace_size;
		result["history_size"] = half;
		result["output_size"] = full;
		result["view_count"] = views;
		result["material_enabled"] = p_material != nullptr;
		result["body_texture_enabled"] = has_body;
		result["detail_texture_enabled"] = has_detail;
		result["density_model"] = p_material ? p_material->density_model : 0;
		result["cyclic_texture_enabled"] = has_cyclic;
		result["cyclic_active"] = cyclic_active;
		result["aerial_enabled"] = p_params.aerial_strength > 0.0;
		result["aerial_strength"] = p_params.aerial_strength;
		result["aerial_distance"] = p_params.aerial_distance;
		result["aerial_start_distance"] = p_params.aerial_start_distance;
		result["analytic_fog_enabled"] = analytic_enabled;
		result["volumetric_fog_bound"] = has_volume_fog;
		result["volumetric_fog_sky_affect"] = volume_sky_affect;
		return result;
	}
};

static void free_cloud_renderer(uint64_t p_renderer) {
	memdelete(reinterpret_cast<VolumetricCloudRenderer *>(p_renderer));
}

void VolumetricCloudEffect::_call_render_callback(int p_type, const RenderData *p_data) {
	if (p_type != EFFECT_CALLBACK_TYPE_POST_SKY || !p_data || !RD::get_singleton()) {
		return;
	}
	// ShaderRD queries the active device in its constructor. Headless instances
	// may load this Resource but must never construct the renderer.
	if (!renderer) {
		renderer = memnew(VolumetricCloudRenderer(get_instance_id()));
	}
	Parameters snapshot;
	Ref<VolumetricCloudMaterial> material_ref;
	{
		MutexLock lock(mutex);
		snapshot = parameters;
		material_ref = cloud_material;
	}
	VolumetricCloudMaterial::Snapshot material_snapshot;
	if (material_ref.is_valid()) {
		material_snapshot = material_ref->get_snapshot();
		snapshot.coverage = material_snapshot.coverage;
		snapshot.density = material_snapshot.density;
		snapshot.noise_scale = 1.0 / material_snapshot.shape_scale;
	}
	Dictionary rendered = renderer->render(p_data, snapshot, material_ref.is_valid() ? &material_snapshot : nullptr);
	{
		MutexLock lock(mutex);
		diagnostics = rendered;
	}
}

#define CLOUD_FLOAT(m_name, m_min, m_max, m_reset) \
	void VolumetricCloudEffect::set_##m_name(float p_value) { \
		ERR_FAIL_COND(!Math::is_finite(p_value)); \
		MutexLock lock(mutex); \
		p_value = CLAMP(p_value, float(m_min), float(m_max)); \
		if (parameters.m_name == p_value) return; \
		parameters.m_name = p_value; \
		if (m_reset) parameters.reset_version++; \
	} \
	float VolumetricCloudEffect::get_##m_name() const { \
		MutexLock lock(mutex); \
		return parameters.m_name; \
	}
CLOUD_FLOAT(cloud_base_height, 10, 50000, true)
CLOUD_FLOAT(cloud_thickness, 10, 20000, true)
CLOUD_FLOAT(planet_radius, 100000, 100000000, true)
CLOUD_FLOAT(coverage, 0, 1, false)
CLOUD_FLOAT(density, 0, 0.1, false)
CLOUD_FLOAT(noise_scale, 0.000001, 0.1, true)
CLOUD_FLOAT(sun_energy, 0, 100000, false)
CLOUD_FLOAT(anisotropy, -0.95, 0.95, true)
CLOUD_FLOAT(max_distance, 100, 200000, true)
CLOUD_FLOAT(fog_transmittance, 0, 1, false)
CLOUD_FLOAT(aerial_strength, 0, 1, true)
CLOUD_FLOAT(aerial_distance, 100, 200000, true)
CLOUD_FLOAT(aerial_start_distance, 0, 200000, true)
#undef CLOUD_FLOAT

#define CLOUD_INTEGER(m_name, m_min, m_max) \
	void VolumetricCloudEffect::set_##m_name(int p_value) { \
		MutexLock lock(mutex); \
		parameters.m_name = CLAMP(p_value, m_min, m_max); \
		parameters.reset_version++; \
	} \
	int VolumetricCloudEffect::get_##m_name() const { \
		MutexLock lock(mutex); \
		return parameters.m_name; \
	}
CLOUD_INTEGER(view_steps, 8, 128)
CLOUD_INTEGER(light_steps, 1, 12)
#undef CLOUD_INTEGER

void VolumetricCloudEffect::set_wind_velocity(Vector3 p_value) {
	ERR_FAIL_COND(!p_value.is_finite());
	MutexLock lock(mutex);
	if (parameters.wind_velocity == p_value) return;
	parameters.wind_velocity = p_value;
}

Vector3 VolumetricCloudEffect::get_wind_velocity() const {
	MutexLock lock(mutex);
	return parameters.wind_velocity;
}

void VolumetricCloudEffect::set_wind_time(double p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	MutexLock lock(mutex);

	double delta = p_value - parameters.wind_time;
	if (delta < 0.0 || delta > 1.0) {
		// An explicit seek has a deterministic new origin. Ordinary velocity edits
		// preserve the accumulated position and affect only future intervals.
		for (int axis = 0; axis < 3; axis++) wind_accumulator[axis] = double(parameters.wind_velocity[axis]) * p_value;
		parameters.reset_version++;
	} else {
		for (int axis = 0; axis < 3; axis++) wind_accumulator[axis] += double(parameters.wind_velocity[axis]) * delta;
	}
	for (int axis = 0; axis < 3; axis++) parameters.wind_offset[axis] = wind_accumulator[axis];
	parameters.wind_time = p_value;
}

double VolumetricCloudEffect::get_wind_time() const {
	MutexLock lock(mutex);
	return parameters.wind_time;
}

Vector3 VolumetricCloudEffect::get_wind_offset() const {
	MutexLock lock(mutex);
	return parameters.wind_offset;
}

void VolumetricCloudEffect::set_sun_direction(Vector3 p_value) {
	ERR_FAIL_COND(!p_value.is_finite() || p_value.length_squared() < 0.000001);
	MutexLock lock(mutex);
	parameters.sun_direction = p_value.normalized();
}

Vector3 VolumetricCloudEffect::get_sun_direction() const {
	MutexLock lock(mutex);
	return parameters.sun_direction;
}

#define CLOUD_COLOR(m_name) \
	void VolumetricCloudEffect::set_##m_name(Color p_value) { \
		ERR_FAIL_COND(!Math::is_finite(p_value.r) || !Math::is_finite(p_value.g) || !Math::is_finite(p_value.b)); \
		MutexLock lock(mutex); \
		parameters.m_name = p_value.clamp(Color(0, 0, 0, 0), Color(100000, 100000, 100000, 1)); \
	} \
	Color VolumetricCloudEffect::get_##m_name() const { \
		MutexLock lock(mutex); \
		return parameters.m_name; \
	}
CLOUD_COLOR(sun_color)
CLOUD_COLOR(ambient_color)
CLOUD_COLOR(fog_color)
CLOUD_COLOR(aerial_horizon_color)
CLOUD_COLOR(aerial_zenith_color)
#undef CLOUD_COLOR

void VolumetricCloudEffect::set_complete_tracing(bool p_value) {
	MutexLock lock(mutex);
	if (parameters.complete_tracing == p_value) return;
	parameters.complete_tracing = p_value;
	parameters.reset_version++;
}

bool VolumetricCloudEffect::get_complete_tracing() const {
	MutexLock lock(mutex);
	return parameters.complete_tracing;
}

void VolumetricCloudEffect::reset_history() {
	MutexLock lock(mutex);
	parameters.reset_version++;
}

Dictionary VolumetricCloudEffect::get_diagnostics() const {
	MutexLock lock(mutex);
	return diagnostics.duplicate();
}

void VolumetricCloudEffect::_material_changed() {
	// The render snapshot distinguishes structural edits from continuous weather.
	emit_changed();
}

void VolumetricCloudEffect::set_cloud_material(const Ref<VolumetricCloudMaterial> &p_material) {
	Ref<VolumetricCloudMaterial> old_material;
	{
		MutexLock lock(mutex);
		if (cloud_material == p_material) return;
		old_material = cloud_material;
		cloud_material = p_material;
		parameters.reset_version++;
	}
	Callable changed = callable_mp(this, &VolumetricCloudEffect::_material_changed);
	if (old_material.is_valid()) old_material->disconnect_changed(changed);
	if (p_material.is_valid()) p_material->connect_changed(changed);
	emit_changed();
}

Ref<VolumetricCloudMaterial> VolumetricCloudEffect::get_cloud_material() const {
	MutexLock lock(mutex);
	return cloud_material;
}

void VolumetricCloudEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_cloud_material", "material"), &VolumetricCloudEffect::set_cloud_material);
	ClassDB::bind_method(D_METHOD("get_cloud_material"), &VolumetricCloudEffect::get_cloud_material);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "cloud_material", PROPERTY_HINT_RESOURCE_TYPE, "VolumetricCloudMaterial"), "set_cloud_material", "get_cloud_material");
#define CLOUD_BIND(m_type, m_name, m_hint) \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &VolumetricCloudEffect::set_##m_name); \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &VolumetricCloudEffect::get_##m_name); \
	ADD_PROPERTY(PropertyInfo(Variant::m_type, #m_name, PROPERTY_HINT_NONE, m_hint), "set_" #m_name, "get_" #m_name);
	ADD_GROUP("Cloud Layer", "");
	CLOUD_BIND(FLOAT, cloud_base_height, "suffix:m")
	CLOUD_BIND(FLOAT, cloud_thickness, "suffix:m")
	CLOUD_BIND(FLOAT, planet_radius, "suffix:m")
	CLOUD_BIND(FLOAT, coverage, "")
	CLOUD_BIND(FLOAT, density, "")
	CLOUD_BIND(FLOAT, noise_scale, "")
	CLOUD_BIND(FLOAT, max_distance, "suffix:m")
	ADD_GROUP("Wind", "wind_");
	CLOUD_BIND(VECTOR3, wind_velocity, "")
	CLOUD_BIND(FLOAT, wind_time, "suffix:s")
	ADD_GROUP("Lighting", "");
	CLOUD_BIND(VECTOR3, sun_direction, "")
	CLOUD_BIND(COLOR, sun_color, "")
	CLOUD_BIND(FLOAT, sun_energy, "")
	CLOUD_BIND(COLOR, ambient_color, "")
	CLOUD_BIND(FLOAT, anisotropy, "")
	ADD_GROUP("Quality", "");
	CLOUD_BIND(BOOL, complete_tracing, "")
	CLOUD_BIND(INT, view_steps, "")
	CLOUD_BIND(INT, light_steps, "")
	ADD_GROUP("Foreground Fog", "fog_");
	CLOUD_BIND(COLOR, fog_color, "")
	CLOUD_BIND(FLOAT, fog_transmittance, "")
	ADD_GROUP("Cloud Aerial Perspective", "aerial_");
	CLOUD_BIND(FLOAT, aerial_strength, "")
	CLOUD_BIND(FLOAT, aerial_distance, "suffix:m")
	CLOUD_BIND(FLOAT, aerial_start_distance, "suffix:m")
	CLOUD_BIND(COLOR, aerial_horizon_color, "")
	CLOUD_BIND(COLOR, aerial_zenith_color, "")
#undef CLOUD_BIND
	ClassDB::bind_method(D_METHOD("get_wind_offset"), &VolumetricCloudEffect::get_wind_offset);
	ClassDB::bind_method(D_METHOD("reset_history"), &VolumetricCloudEffect::reset_history);
	ClassDB::bind_method(D_METHOD("get_diagnostics"), &VolumetricCloudEffect::get_diagnostics);
}

VolumetricCloudEffect::VolumetricCloudEffect() {
	set_effect_callback_type(EFFECT_CALLBACK_TYPE_POST_SKY);
	set_access_resolved_color(true);
	set_access_resolved_depth(true);
	// Forces resolved HDR color back into the MSAA target before transparent water.
	set_needs_separate_specular(true);
}

VolumetricCloudEffect::~VolumetricCloudEffect() {
	if (cloud_material.is_valid()) {
		cloud_material->disconnect_changed(callable_mp(this, &VolumetricCloudEffect::_material_changed));
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!renderer) {
		return;
	}
	if (rs) {
		// Unregister the callback before queuing GPU state destruction.
		rs->compositor_effect_set_callback(get_rid(), RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_SKY, Callable());
		rs->call_on_render_thread(callable_mp_static(&free_cloud_renderer).bind(reinterpret_cast<uint64_t>(renderer)));
	} else {
		memdelete(renderer);
	}
}
