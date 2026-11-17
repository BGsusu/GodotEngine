#include "volumetric_cloud_material.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

VolumetricCloudMaterial::Snapshot VolumetricCloudMaterial::get_snapshot() const {
	MutexLock lock(mutex);
	return state;
}

void VolumetricCloudMaterial::_texture_changed() {
	{
		MutexLock lock(mutex);
		state.structure_version++;
		state.body_rid = state.body_noise.is_valid() ? state.body_noise->get_rid() : RID();
		state.detail_rid = state.detail_noise.is_valid() ? state.detail_noise->get_rid() : RID();
		state.weather_rid = state.weather_map.is_valid() ? state.weather_map->get_rid() : RID();
		state.cyclic_rid = state.cyclic_noise.is_valid() ? state.cyclic_noise->get_rid() : RID();
	}
	emit_changed();
}

#define CLOUD_TEXTURE(m_type, m_name, m_rid) \
	void VolumetricCloudMaterial::set_##m_name(const Ref<m_type> &p_texture) { \
		Ref<m_type> old_texture; \
		RID rid = p_texture.is_valid() ? p_texture->get_rid() : RID(); \
		{ \
			MutexLock lock(mutex); \
			if (state.m_name == p_texture) return; \
			old_texture = state.m_name; \
			state.m_name = p_texture; \
			state.m_rid = rid; \
			state.structure_version++; \
		} \
		Callable changed = callable_mp(this, &VolumetricCloudMaterial::_texture_changed); \
		if (old_texture.is_valid()) old_texture->disconnect_changed(changed); \
		if (p_texture.is_valid()) p_texture->connect_changed(changed, CONNECT_REFERENCE_COUNTED); \
		emit_changed(); \
	} \
	Ref<m_type> VolumetricCloudMaterial::get_##m_name() const { \
		MutexLock lock(mutex); \
		return state.m_name; \
	}
CLOUD_TEXTURE(Texture3D, body_noise, body_rid)
CLOUD_TEXTURE(Texture3D, detail_noise, detail_rid)
CLOUD_TEXTURE(Texture2D, weather_map, weather_rid)
CLOUD_TEXTURE(Texture3D, cyclic_noise, cyclic_rid)
#undef CLOUD_TEXTURE

#define CLOUD_MATERIAL_FLOAT(m_name, m_min, m_max, m_structural) \
	void VolumetricCloudMaterial::set_##m_name(float p_value) { \
		ERR_FAIL_COND(!Math::is_finite(p_value)); \
		p_value = CLAMP(p_value, float(m_min), float(m_max)); \
		{ \
			MutexLock lock(mutex); \
			if (state.m_name == p_value) return; \
			state.m_name = p_value; \
			if (m_structural) state.structure_version++; \
		} \
		emit_changed(); \
	} \
	float VolumetricCloudMaterial::get_##m_name() const { \
		MutexLock lock(mutex); \
		return state.m_name; \
	}
CLOUD_MATERIAL_FLOAT(coverage, 0, 1, false)
CLOUD_MATERIAL_FLOAT(cyclic_detail_strength, 0, 1, true)
CLOUD_MATERIAL_FLOAT(cyclic_height_variation, 0, 0.6, true)
CLOUD_MATERIAL_FLOAT(cyclic_density_contrast, 0.1, 4, true)
CLOUD_MATERIAL_FLOAT(density, 0, 0.1, false)
CLOUD_MATERIAL_FLOAT(shape_scale, 10, 100000, true)
CLOUD_MATERIAL_FLOAT(detail_scale, 1, 20000, true)
CLOUD_MATERIAL_FLOAT(weather_scale, 10, 500000, true)
CLOUD_MATERIAL_FLOAT(erosion_strength, 0, 1, true)
CLOUD_MATERIAL_FLOAT(edge_softness, 0.005, 1, true)
#undef CLOUD_MATERIAL_FLOAT

void VolumetricCloudMaterial::set_density_model(int p_model) {
	ERR_FAIL_COND(p_model < DENSITY_MODEL_LEGACY || p_model > DENSITY_MODEL_CYCLIC);
	{
		MutexLock lock(mutex);
		if (state.density_model == p_model) return;
		state.density_model = p_model;
		state.structure_version++;
	}
	emit_changed();
}

int VolumetricCloudMaterial::get_density_model() const {
	MutexLock lock(mutex);
	return state.density_model;
}

void VolumetricCloudMaterial::set_height_profile(Vector4 p_value) {
	ERR_FAIL_COND(!p_value.is_finite());
	ERR_FAIL_COND(p_value.x < 0.0 || p_value.w > 1.0 || p_value.y <= p_value.x || p_value.z < p_value.y || p_value.w <= p_value.z);
	{
		MutexLock lock(mutex);
		if (state.height_profile == p_value) return;
		state.height_profile = p_value;
		state.structure_version++;
	}
	emit_changed();
}

Vector4 VolumetricCloudMaterial::get_height_profile() const {
	MutexLock lock(mutex);
	return state.height_profile;
}

void VolumetricCloudMaterial::set_albedo(Color p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value.r) || !Math::is_finite(p_value.g) || !Math::is_finite(p_value.b));
	p_value = p_value.clamp();
	{
		MutexLock lock(mutex);
		if (state.albedo == p_value) return;
		state.albedo = p_value;
		state.structure_version++;
	}
	emit_changed();
}

Color VolumetricCloudMaterial::get_albedo() const {
	MutexLock lock(mutex);
	return state.albedo;
}

void VolumetricCloudMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_density_model", "model"), &VolumetricCloudMaterial::set_density_model);
	ClassDB::bind_method(D_METHOD("get_density_model"), &VolumetricCloudMaterial::get_density_model);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "density_model", PROPERTY_HINT_ENUM, "Legacy,Cyclic"), "set_density_model", "get_density_model");
	BIND_ENUM_CONSTANT(DENSITY_MODEL_LEGACY);
	BIND_ENUM_CONSTANT(DENSITY_MODEL_CYCLIC);
#define CLOUD_MATERIAL_TEXTURE_BIND(m_name, m_class) \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "texture"), &VolumetricCloudMaterial::set_##m_name); \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &VolumetricCloudMaterial::get_##m_name); \
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, #m_name, PROPERTY_HINT_RESOURCE_TYPE, #m_class), "set_" #m_name, "get_" #m_name);
	ADD_GROUP("Noise Textures", "");
	CLOUD_MATERIAL_TEXTURE_BIND(body_noise, Texture3D)
	CLOUD_MATERIAL_TEXTURE_BIND(detail_noise, Texture3D)
	CLOUD_MATERIAL_TEXTURE_BIND(weather_map, Texture2D)
	CLOUD_MATERIAL_TEXTURE_BIND(cyclic_noise, Texture3D)
#undef CLOUD_MATERIAL_TEXTURE_BIND
#define CLOUD_MATERIAL_BIND(m_type, m_name, m_hint) \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &VolumetricCloudMaterial::set_##m_name); \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &VolumetricCloudMaterial::get_##m_name); \
	ADD_PROPERTY(PropertyInfo(Variant::m_type, #m_name, PROPERTY_HINT_RANGE, m_hint), "set_" #m_name, "get_" #m_name);
	ADD_GROUP("Density", "");
	CLOUD_MATERIAL_BIND(FLOAT, coverage, "0,1,0.01")
	CLOUD_MATERIAL_BIND(FLOAT, density, "0,0.02,0.00001,or_greater")
	CLOUD_MATERIAL_BIND(FLOAT, shape_scale, "10,100000,1,suffix:m")
	CLOUD_MATERIAL_BIND(FLOAT, detail_scale, "1,20000,1,suffix:m")
	CLOUD_MATERIAL_BIND(FLOAT, weather_scale, "10,500000,1,suffix:m")
	CLOUD_MATERIAL_BIND(FLOAT, erosion_strength, "0,1,0.01")
	CLOUD_MATERIAL_BIND(FLOAT, edge_softness, "0.005,1,0.005")
	ADD_GROUP("Cyclic Shape", "cyclic_");
	CLOUD_MATERIAL_BIND(FLOAT, cyclic_detail_strength, "0,1,0.01")
	CLOUD_MATERIAL_BIND(FLOAT, cyclic_height_variation, "0,0.6,0.01")
	CLOUD_MATERIAL_BIND(FLOAT, cyclic_density_contrast, "0.1,4,0.05")
#undef CLOUD_MATERIAL_BIND
	ClassDB::bind_method(D_METHOD("set_height_profile", "value"), &VolumetricCloudMaterial::set_height_profile);
	ClassDB::bind_method(D_METHOD("get_height_profile"), &VolumetricCloudMaterial::get_height_profile);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR4, "height_profile"), "set_height_profile", "get_height_profile");
	ClassDB::bind_method(D_METHOD("set_albedo", "value"), &VolumetricCloudMaterial::set_albedo);
	ClassDB::bind_method(D_METHOD("get_albedo"), &VolumetricCloudMaterial::get_albedo);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "albedo"), "set_albedo", "get_albedo");
}

VolumetricCloudMaterial::~VolumetricCloudMaterial() {
	Callable changed = callable_mp(this, &VolumetricCloudMaterial::_texture_changed);
	if (state.body_noise.is_valid()) state.body_noise->disconnect_changed(changed);
	if (state.detail_noise.is_valid()) state.detail_noise->disconnect_changed(changed);
	if (state.weather_map.is_valid()) state.weather_map->disconnect_changed(changed);
	if (state.cyclic_noise.is_valid()) state.cyclic_noise->disconnect_changed(changed);
}
