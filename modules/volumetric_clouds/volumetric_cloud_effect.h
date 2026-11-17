#pragma once

#include "core/os/mutex.h"
#include "scene/resources/compositor.h"
#include "volumetric_cloud_material.h"

class VolumetricCloudRenderer;

// Mode 0 uses sparse temporal tracing; complete_tracing opts into a fresh half-resolution grid.
class VolumetricCloudEffect : public CompositorEffect {
	GDCLASS(VolumetricCloudEffect, CompositorEffect);

public:
	struct Parameters {
		float cloud_base_height = 1800.0f;
		float cloud_thickness = 1400.0f;
		float planet_radius = 6360000.0f;
		float coverage = 0.52f;
		float density = 0.00065f;
		float noise_scale = 0.00045f;
		Vector3 wind_velocity = Vector3(8.0, 0.0, 3.0);
		double wind_time = 0.0;
		Vector3 wind_offset;
		Vector3 sun_direction = Vector3(0.0, 1.0, 0.0);
		Color sun_color = Color(1.0, 1.0, 1.0);
		float sun_energy = 1.0f;
		Color ambient_color = Color(0.12, 0.17, 0.24);
		float anisotropy = 0.65f;
		bool complete_tracing = false;
		int view_steps = 48;
		int light_steps = 4;
		float max_distance = 60000.0f;
		Color fog_color = Color(0.5, 0.6, 0.7);
		float fog_transmittance = 1.0f;
		float aerial_strength = 0.0f;
		float aerial_distance = 20000.0f;
		float aerial_start_distance = 0.0f;
		Color aerial_horizon_color = Color(0.5, 0.6, 0.7);
		Color aerial_zenith_color = Color(0.15, 0.3, 0.6);
		uint64_t reset_version = 1;
	};

private:
	mutable Mutex mutex;
	Parameters parameters;
	double wind_accumulator[3] = {};
	Dictionary diagnostics;
	Ref<VolumetricCloudMaterial> cloud_material;
	void _material_changed();
	VolumetricCloudRenderer *renderer = nullptr;

protected:
	static void _bind_methods();
	void _call_render_callback(int p_type, const RenderData *p_data) override;

public:
#define CLOUD_PROPERTY(m_type, m_name) \
	void set_##m_name(m_type p_value); \
	m_type get_##m_name() const;
	CLOUD_PROPERTY(float, cloud_base_height)
	CLOUD_PROPERTY(float, cloud_thickness)
	CLOUD_PROPERTY(float, planet_radius)
	CLOUD_PROPERTY(float, coverage)
	CLOUD_PROPERTY(float, density)
	CLOUD_PROPERTY(float, noise_scale)
	CLOUD_PROPERTY(Vector3, wind_velocity)
	CLOUD_PROPERTY(double, wind_time)
	CLOUD_PROPERTY(Vector3, sun_direction)
	CLOUD_PROPERTY(Color, sun_color)
	CLOUD_PROPERTY(float, sun_energy)
	CLOUD_PROPERTY(Color, ambient_color)
	CLOUD_PROPERTY(float, anisotropy)
	CLOUD_PROPERTY(bool, complete_tracing)
	CLOUD_PROPERTY(int, view_steps)
	CLOUD_PROPERTY(int, light_steps)
	CLOUD_PROPERTY(float, max_distance)
	CLOUD_PROPERTY(Color, fog_color)
	CLOUD_PROPERTY(float, fog_transmittance)
	CLOUD_PROPERTY(float, aerial_strength)
	CLOUD_PROPERTY(float, aerial_distance)
	CLOUD_PROPERTY(float, aerial_start_distance)
	CLOUD_PROPERTY(Color, aerial_horizon_color)
	CLOUD_PROPERTY(Color, aerial_zenith_color)
#undef CLOUD_PROPERTY
	Vector3 get_wind_offset() const;
	void reset_history();
	Dictionary get_diagnostics() const;
	void set_cloud_material(const Ref<VolumetricCloudMaterial> &p_material);
	Ref<VolumetricCloudMaterial> get_cloud_material() const;
	VolumetricCloudEffect();
	~VolumetricCloudEffect();
};
