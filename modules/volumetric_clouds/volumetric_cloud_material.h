#pragma once

#include "core/os/mutex.h"
#include "scene/resources/texture.h"

// Dedicated volume material, not an arbitrary Godot Shader graph.
class VolumetricCloudMaterial : public Resource {
	GDCLASS(VolumetricCloudMaterial, Resource);

public:
	enum DensityModel {
		DENSITY_MODEL_LEGACY,
		DENSITY_MODEL_CYCLIC,
	};

	struct Snapshot {
		uint64_t structure_version = 1;
		int density_model = DENSITY_MODEL_LEGACY;
		Ref<Texture3D> cyclic_noise;
		RID cyclic_rid;
		float cyclic_detail_strength = 0.45f;
		float cyclic_height_variation = 0.3f;
		float cyclic_density_contrast = 1.4f;
		Ref<Texture3D> body_noise;
		Ref<Texture3D> detail_noise;
		Ref<Texture2D> weather_map;
		RID body_rid;
		RID detail_rid;
		RID weather_rid;
		float coverage = 0.42f;
		float density = 0.00065f;
		float shape_scale = 4000.0f;
		float detail_scale = 400.0f;
		float weather_scale = 16000.0f;
		float erosion_strength = 0.25f;
		float edge_softness = 0.16f;
		Vector4 height_profile = Vector4(0.02, 0.16, 0.65, 1.0);
		Color albedo = Color(0.95, 0.97, 1.0);
	};

private:
	mutable Mutex mutex;
	Snapshot state;
	void _texture_changed();

protected:
	static void _bind_methods();

public:
	Snapshot get_snapshot() const;
	void set_density_model(int p_model);
	int get_density_model() const;
	void set_cyclic_noise(const Ref<Texture3D> &p_texture);
	Ref<Texture3D> get_cyclic_noise() const;
	void set_body_noise(const Ref<Texture3D> &p_texture);
	Ref<Texture3D> get_body_noise() const;
	void set_detail_noise(const Ref<Texture3D> &p_texture);
	Ref<Texture3D> get_detail_noise() const;
	void set_weather_map(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_weather_map() const;
#define CLOUD_MATERIAL_PROPERTY(m_type, m_name) \
	void set_##m_name(m_type p_value); \
	m_type get_##m_name() const;
	CLOUD_MATERIAL_PROPERTY(float, coverage)
	CLOUD_MATERIAL_PROPERTY(float, cyclic_detail_strength)
	CLOUD_MATERIAL_PROPERTY(float, cyclic_height_variation)
	CLOUD_MATERIAL_PROPERTY(float, cyclic_density_contrast)
	CLOUD_MATERIAL_PROPERTY(float, density)
	CLOUD_MATERIAL_PROPERTY(float, shape_scale)
	CLOUD_MATERIAL_PROPERTY(float, detail_scale)
	CLOUD_MATERIAL_PROPERTY(float, weather_scale)
	CLOUD_MATERIAL_PROPERTY(float, erosion_strength)
	CLOUD_MATERIAL_PROPERTY(float, edge_softness)
	CLOUD_MATERIAL_PROPERTY(Vector4, height_profile)
	CLOUD_MATERIAL_PROPERTY(Color, albedo)
#undef CLOUD_MATERIAL_PROPERTY
	~VolumetricCloudMaterial();
};

VARIANT_ENUM_CAST(VolumetricCloudMaterial::DensityModel);
