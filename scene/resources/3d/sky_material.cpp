/**************************************************************************/
/*  sky_material.cpp                                                      */
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

#include "sky_material.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "core/version.h"
#include "scene/resources/texture.h"
#include "scene/resources/image_texture.h"
#include "servers/rendering/rendering_server.h"

Mutex ProceduralSkyMaterial::shader_mutex;
RID ProceduralSkyMaterial::shader_cache[4];

void ProceduralSkyMaterial::set_sky_top_color(const Color &p_sky_top) {
	sky_top_color = p_sky_top;
	RS::get_singleton()->material_set_param(_get_material(), "sky_top_color", sky_top_color * sky_energy_multiplier);
}

Color ProceduralSkyMaterial::get_sky_top_color() const {
	return sky_top_color;
}

void ProceduralSkyMaterial::set_sky_horizon_color(const Color &p_sky_horizon) {
	sky_horizon_color = p_sky_horizon;
	RS::get_singleton()->material_set_param(_get_material(), "sky_horizon_color", sky_horizon_color * sky_energy_multiplier);
}

Color ProceduralSkyMaterial::get_sky_horizon_color() const {
	return sky_horizon_color;
}

void ProceduralSkyMaterial::set_sky_curve(float p_curve) {
	sky_curve = p_curve;
	// Actual curve passed to shader includes an ad hoc adjustment because the curve used to be
	// in calculated in angles and now uses cosines.
	RS::get_singleton()->material_set_param(_get_material(), "inv_sky_curve", 0.6 / sky_curve);
}

float ProceduralSkyMaterial::get_sky_curve() const {
	return sky_curve;
}

void ProceduralSkyMaterial::set_sky_energy_multiplier(float p_multiplier) {
	sky_energy_multiplier = p_multiplier;
	RS::get_singleton()->material_set_param(_get_material(), "sky_top_color", sky_top_color * sky_energy_multiplier);
	RS::get_singleton()->material_set_param(_get_material(), "sky_horizon_color", sky_horizon_color * sky_energy_multiplier);
	RS::get_singleton()->material_set_param(_get_material(), "sky_cover_modulate", Color(sky_cover_modulate.r, sky_cover_modulate.g, sky_cover_modulate.b, sky_cover_modulate.a * sky_energy_multiplier));
}

float ProceduralSkyMaterial::get_sky_energy_multiplier() const {
	return sky_energy_multiplier;
}

void ProceduralSkyMaterial::set_sky_cover(const Ref<Texture2D> &p_sky_cover) {
	sky_cover = p_sky_cover;

	if (p_sky_cover.is_valid()) {
		RS::get_singleton()->material_set_param(_get_material(), "sky_cover", p_sky_cover->get_rid());
	} else {
		RS::get_singleton()->material_set_param(_get_material(), "sky_cover", Variant());
	}

	_update_shader(use_debanding, sky_cover.is_valid());

	if (shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), get_shader_cache());
	}
}

Ref<Texture2D> ProceduralSkyMaterial::get_sky_cover() const {
	return sky_cover;
}

void ProceduralSkyMaterial::set_sky_cover_modulate(const Color &p_sky_cover_modulate) {
	sky_cover_modulate = p_sky_cover_modulate;
	RS::get_singleton()->material_set_param(_get_material(), "sky_cover_modulate", Color(sky_cover_modulate.r, sky_cover_modulate.g, sky_cover_modulate.b, sky_cover_modulate.a * sky_energy_multiplier));
}

Color ProceduralSkyMaterial::get_sky_cover_modulate() const {
	return sky_cover_modulate;
}

void ProceduralSkyMaterial::set_ground_bottom_color(const Color &p_ground_bottom) {
	ground_bottom_color = p_ground_bottom;
	RS::get_singleton()->material_set_param(_get_material(), "ground_bottom_color", ground_bottom_color * ground_energy_multiplier);
}

Color ProceduralSkyMaterial::get_ground_bottom_color() const {
	return ground_bottom_color;
}

void ProceduralSkyMaterial::set_ground_horizon_color(const Color &p_ground_horizon) {
	ground_horizon_color = p_ground_horizon;
	RS::get_singleton()->material_set_param(_get_material(), "ground_horizon_color", ground_horizon_color * ground_energy_multiplier);
}

Color ProceduralSkyMaterial::get_ground_horizon_color() const {
	return ground_horizon_color;
}

void ProceduralSkyMaterial::set_ground_curve(float p_curve) {
	ground_curve = p_curve;
	// Actual curve passed to shader includes an ad hoc adjustment because the curve used to be
	// in calculated in angles and now uses cosines.
	RS::get_singleton()->material_set_param(_get_material(), "inv_ground_curve", 0.6 / ground_curve);
}

float ProceduralSkyMaterial::get_ground_curve() const {
	return ground_curve;
}

void ProceduralSkyMaterial::set_ground_energy_multiplier(float p_multiplier) {
	ground_energy_multiplier = p_multiplier;
	RS::get_singleton()->material_set_param(_get_material(), "ground_bottom_color", ground_bottom_color * ground_energy_multiplier);
	RS::get_singleton()->material_set_param(_get_material(), "ground_horizon_color", ground_horizon_color * ground_energy_multiplier);
}

float ProceduralSkyMaterial::get_ground_energy_multiplier() const {
	return ground_energy_multiplier;
}

void ProceduralSkyMaterial::set_sun_angle_max(float p_angle) {
	sun_angle_max = p_angle;
	RS::get_singleton()->material_set_param(_get_material(), "sun_angle_max", Math::cos(Math::deg_to_rad(sun_angle_max)));
}

float ProceduralSkyMaterial::get_sun_angle_max() const {
	return sun_angle_max;
}

void ProceduralSkyMaterial::set_sun_curve(float p_curve) {
	sun_curve = p_curve;
	// Actual curve passed to shader includes an ad hoc adjustment because the curve used to be
	// in calculated in angles and now uses cosines.
	RS::get_singleton()->material_set_param(_get_material(), "inv_sun_curve", 1.6f / Math::pow(sun_curve, 1.4f));
}

float ProceduralSkyMaterial::get_sun_curve() const {
	return sun_curve;
}

void ProceduralSkyMaterial::set_use_debanding(bool p_use_debanding) {
	use_debanding = p_use_debanding;
	_update_shader(use_debanding, sky_cover.is_valid());
	// Only set if shader already compiled
	if (shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), get_shader_cache());
	}
}

bool ProceduralSkyMaterial::get_use_debanding() const {
	return use_debanding;
}

void ProceduralSkyMaterial::set_energy_multiplier(float p_multiplier) {
	global_energy_multiplier = p_multiplier;
	RS::get_singleton()->material_set_param(_get_material(), "exposure", global_energy_multiplier);
}

float ProceduralSkyMaterial::get_energy_multiplier() const {
	return global_energy_multiplier;
}

Shader::Mode ProceduralSkyMaterial::get_shader_mode() const {
	return Shader::MODE_SKY;
}

// Internal function to grab the current shader RID.
// Must only be called if the shader is initialized.
RID ProceduralSkyMaterial::get_shader_cache() const {
	return shader_cache[int(use_debanding) + (sky_cover.is_valid() ? 2 : 0)];
}

RID ProceduralSkyMaterial::get_rid() const {
	_update_shader(use_debanding, sky_cover.is_valid());
	if (!shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), get_shader_cache());
		shader_set = true;
	}
	return _get_material();
}

RID ProceduralSkyMaterial::get_shader_rid() const {
	_update_shader(use_debanding, sky_cover.is_valid());
	return get_shader_cache();
}

void ProceduralSkyMaterial::_validate_property(PropertyInfo &p_property) const {
	if (!Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if ((p_property.name == "sky_luminance" || p_property.name == "ground_luminance") && !GLOBAL_GET_CACHED(bool, "rendering/lights_and_shadows/use_physical_light_units")) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

void ProceduralSkyMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_sky_top_color", "color"), &ProceduralSkyMaterial::set_sky_top_color);
	ClassDB::bind_method(D_METHOD("get_sky_top_color"), &ProceduralSkyMaterial::get_sky_top_color);

	ClassDB::bind_method(D_METHOD("set_sky_horizon_color", "color"), &ProceduralSkyMaterial::set_sky_horizon_color);
	ClassDB::bind_method(D_METHOD("get_sky_horizon_color"), &ProceduralSkyMaterial::get_sky_horizon_color);

	ClassDB::bind_method(D_METHOD("set_sky_curve", "curve"), &ProceduralSkyMaterial::set_sky_curve);
	ClassDB::bind_method(D_METHOD("get_sky_curve"), &ProceduralSkyMaterial::get_sky_curve);

	ClassDB::bind_method(D_METHOD("set_sky_energy_multiplier", "multiplier"), &ProceduralSkyMaterial::set_sky_energy_multiplier);
	ClassDB::bind_method(D_METHOD("get_sky_energy_multiplier"), &ProceduralSkyMaterial::get_sky_energy_multiplier);

	ClassDB::bind_method(D_METHOD("set_sky_cover", "sky_cover"), &ProceduralSkyMaterial::set_sky_cover);
	ClassDB::bind_method(D_METHOD("get_sky_cover"), &ProceduralSkyMaterial::get_sky_cover);

	ClassDB::bind_method(D_METHOD("set_sky_cover_modulate", "color"), &ProceduralSkyMaterial::set_sky_cover_modulate);
	ClassDB::bind_method(D_METHOD("get_sky_cover_modulate"), &ProceduralSkyMaterial::get_sky_cover_modulate);

	ClassDB::bind_method(D_METHOD("set_ground_bottom_color", "color"), &ProceduralSkyMaterial::set_ground_bottom_color);
	ClassDB::bind_method(D_METHOD("get_ground_bottom_color"), &ProceduralSkyMaterial::get_ground_bottom_color);

	ClassDB::bind_method(D_METHOD("set_ground_horizon_color", "color"), &ProceduralSkyMaterial::set_ground_horizon_color);
	ClassDB::bind_method(D_METHOD("get_ground_horizon_color"), &ProceduralSkyMaterial::get_ground_horizon_color);

	ClassDB::bind_method(D_METHOD("set_ground_curve", "curve"), &ProceduralSkyMaterial::set_ground_curve);
	ClassDB::bind_method(D_METHOD("get_ground_curve"), &ProceduralSkyMaterial::get_ground_curve);

	ClassDB::bind_method(D_METHOD("set_ground_energy_multiplier", "energy"), &ProceduralSkyMaterial::set_ground_energy_multiplier);
	ClassDB::bind_method(D_METHOD("get_ground_energy_multiplier"), &ProceduralSkyMaterial::get_ground_energy_multiplier);

	ClassDB::bind_method(D_METHOD("set_sun_angle_max", "degrees"), &ProceduralSkyMaterial::set_sun_angle_max);
	ClassDB::bind_method(D_METHOD("get_sun_angle_max"), &ProceduralSkyMaterial::get_sun_angle_max);

	ClassDB::bind_method(D_METHOD("set_sun_curve", "curve"), &ProceduralSkyMaterial::set_sun_curve);
	ClassDB::bind_method(D_METHOD("get_sun_curve"), &ProceduralSkyMaterial::get_sun_curve);

	ClassDB::bind_method(D_METHOD("set_use_debanding", "use_debanding"), &ProceduralSkyMaterial::set_use_debanding);
	ClassDB::bind_method(D_METHOD("get_use_debanding"), &ProceduralSkyMaterial::get_use_debanding);

	ClassDB::bind_method(D_METHOD("set_energy_multiplier", "multiplier"), &ProceduralSkyMaterial::set_energy_multiplier);
	ClassDB::bind_method(D_METHOD("get_energy_multiplier"), &ProceduralSkyMaterial::get_energy_multiplier);

	ADD_GROUP("Sky", "sky_");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "sky_top_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_sky_top_color", "get_sky_top_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "sky_horizon_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_sky_horizon_color", "get_sky_horizon_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sky_curve", PROPERTY_HINT_EXP_EASING), "set_sky_curve", "get_sky_curve");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sky_energy_multiplier", PROPERTY_HINT_RANGE, "0,64,0.01"), "set_sky_energy_multiplier", "get_sky_energy_multiplier");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sky_cover", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_sky_cover", "get_sky_cover");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "sky_cover_modulate"), "set_sky_cover_modulate", "get_sky_cover_modulate");

	ADD_GROUP("Ground", "ground_");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "ground_bottom_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_ground_bottom_color", "get_ground_bottom_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "ground_horizon_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_ground_horizon_color", "get_ground_horizon_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ground_curve", PROPERTY_HINT_EXP_EASING), "set_ground_curve", "get_ground_curve");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ground_energy_multiplier", PROPERTY_HINT_RANGE, "0,64,0.01"), "set_ground_energy_multiplier", "get_ground_energy_multiplier");

	ADD_GROUP("Sun", "sun_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sun_angle_max", PROPERTY_HINT_RANGE, "0,360,0.01,degrees"), "set_sun_angle_max", "get_sun_angle_max");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sun_curve", PROPERTY_HINT_EXP_EASING), "set_sun_curve", "get_sun_curve");

	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_debanding"), "set_use_debanding", "get_use_debanding");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "energy_multiplier", PROPERTY_HINT_RANGE, "0,128,0.01"), "set_energy_multiplier", "get_energy_multiplier");
}

void ProceduralSkyMaterial::cleanup_shader() {
	for (int i = 0; i < 4; i++) {
		if (shader_cache[i].is_valid()) {
			RS::get_singleton()->free_rid(shader_cache[i]);
		}
	}
}

void ProceduralSkyMaterial::_update_shader(bool p_use_debanding, bool p_use_sky_cover) {
	MutexLock shader_lock(shader_mutex);
	int index = int(p_use_debanding) + int(p_use_sky_cover) * 2;
	if (shader_cache[index].is_null()) {
		shader_cache[index] = RS::get_singleton()->shader_create();

		// Add a comment to describe the shader origin (useful when converting to ShaderMaterial).
		RS::get_singleton()->shader_set_code(shader_cache[index], vformat(R"(
// NOTE: Shader automatically converted from )" GODOT_VERSION_NAME " " GODOT_VERSION_FULL_CONFIG R"('s ProceduralSkyMaterial.

shader_type sky;
%s

uniform vec4 sky_top_color : source_color = vec4(0.385, 0.454, 0.55, 1.0);
uniform vec4 sky_horizon_color : source_color = vec4(0.646, 0.656, 0.67, 1.0);
uniform float inv_sky_curve : hint_range(1, 100) = 4.0;
uniform vec4 ground_bottom_color : source_color = vec4(0.2, 0.169, 0.133, 1.0);
uniform vec4 ground_horizon_color : source_color = vec4(0.646, 0.656, 0.67, 1.0);
uniform float inv_ground_curve : hint_range(1, 100) = 30.0;
uniform float sun_angle_max = 0.877;
uniform float inv_sun_curve : hint_range(1, 100) = 22.78;
uniform float exposure : hint_range(0, 128) = 1.0;

uniform sampler2D sky_cover : filter_linear, source_color, hint_default_black;
uniform vec4 sky_cover_modulate : source_color = vec4(1.0, 1.0, 1.0, 1.0);

void sky() {
	float v_angle = clamp(EYEDIR.y, -1.0, 1.0);
	vec3 sky = mix(sky_top_color.rgb, sky_horizon_color.rgb, clamp(pow(1.0 - v_angle, inv_sky_curve), 0.0, 1.0));

	if (LIGHT0_ENABLED) {
		float sun_angle = dot(LIGHT0_DIRECTION, EYEDIR);
		float sun_size = cos(LIGHT0_SIZE);
		if (sun_angle > sun_size) {
			sky = LIGHT0_COLOR * LIGHT0_ENERGY;
		} else if (sun_angle > sun_angle_max) {
			float c2 = (sun_size - sun_angle) / (sun_size - sun_angle_max);
			sky = mix(sky, LIGHT0_COLOR * LIGHT0_ENERGY, clamp(pow(1.0 - c2, inv_sun_curve), 0.0, 1.0));
		}
	}

	if (LIGHT1_ENABLED) {
		float sun_angle = dot(LIGHT1_DIRECTION, EYEDIR);
		float sun_size = cos(LIGHT1_SIZE);
		if (sun_angle > sun_size) {
			sky = LIGHT1_COLOR * LIGHT1_ENERGY;
		} else if (sun_angle > sun_angle_max) {
			float c2 = (sun_size - sun_angle) / (sun_size - sun_angle_max);
			sky = mix(sky, LIGHT1_COLOR * LIGHT1_ENERGY, clamp(pow(1.0 - c2, inv_sun_curve), 0.0, 1.0));
		}
	}

	if (LIGHT2_ENABLED) {
		float sun_angle = dot(LIGHT2_DIRECTION, EYEDIR);
		float sun_size = cos(LIGHT2_SIZE);
		if (sun_angle > sun_size) {
			sky = LIGHT2_COLOR * LIGHT2_ENERGY;
		} else if (sun_angle > sun_angle_max) {
			float c2 = (sun_size - sun_angle) / (sun_size - sun_angle_max);
			sky = mix(sky, LIGHT2_COLOR * LIGHT2_ENERGY, clamp(pow(1.0 - c2, inv_sun_curve), 0.0, 1.0));
		}
	}

	if (LIGHT3_ENABLED) {
		float sun_angle = dot(LIGHT3_DIRECTION, EYEDIR);
		float sun_size = cos(LIGHT3_SIZE);
		if (sun_angle > sun_size) {
			sky = LIGHT3_COLOR * LIGHT3_ENERGY;
		} else if (sun_angle > sun_angle_max) {
			float c2 = (sun_size - sun_angle) / (sun_size - sun_angle_max);
			sky = mix(sky, LIGHT3_COLOR * LIGHT3_ENERGY, clamp(pow(1.0 - c2, inv_sun_curve), 0.0, 1.0));
		}
	}

	%s
	%s
	vec3 ground = mix(ground_bottom_color.rgb, ground_horizon_color.rgb, clamp(pow(1.0 + v_angle, inv_ground_curve), 0.0, 1.0));

	COLOR = mix(ground, sky, step(0.0, EYEDIR.y)) * exposure;
}
)",
																		  p_use_debanding ? "render_mode use_debanding;" : "", p_use_sky_cover ? "vec4 sky_cover_texture = texture(sky_cover, SKY_COORDS);" : "", p_use_sky_cover ? "sky += (sky_cover_texture.rgb * sky_cover_modulate.rgb) * sky_cover_texture.a * sky_cover_modulate.a;" : ""));
	}
}

ProceduralSkyMaterial::ProceduralSkyMaterial() {
	_set_material(RS::get_singleton()->material_create());
	set_sky_top_color(Color(0.385, 0.454, 0.55));
	set_sky_horizon_color(Color(0.6463, 0.6558, 0.6708));
	set_sky_curve(0.15);
	set_sky_energy_multiplier(1.0);
	set_sky_cover_modulate(Color(1, 1, 1));

	set_ground_bottom_color(Color(0.2, 0.169, 0.133));
	set_ground_horizon_color(Color(0.6463, 0.6558, 0.6708));
	set_ground_curve(0.02);
	set_ground_energy_multiplier(1.0);

	set_sun_angle_max(30.0);
	set_sun_curve(0.15);
	set_use_debanding(true);
	set_energy_multiplier(1.0);
}

ProceduralSkyMaterial::~ProceduralSkyMaterial() {
}

/////////////////////////////////////////
/* PanoramaSkyMaterial */

void PanoramaSkyMaterial::set_panorama(const Ref<Texture2D> &p_panorama) {
	panorama = p_panorama;
	if (p_panorama.is_valid()) {
		RS::get_singleton()->material_set_param(_get_material(), "source_panorama", p_panorama->get_rid());
	} else {
		RS::get_singleton()->material_set_param(_get_material(), "source_panorama", Variant());
	}
}

Ref<Texture2D> PanoramaSkyMaterial::get_panorama() const {
	return panorama;
}

void PanoramaSkyMaterial::set_filtering_enabled(bool p_enabled) {
	filter = p_enabled;
	notify_property_list_changed();
	_update_shader(filter);
	// Only set if shader already compiled
	if (shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), shader_cache[int(filter)]);
	}
}

bool PanoramaSkyMaterial::is_filtering_enabled() const {
	return filter;
}

void PanoramaSkyMaterial::set_energy_multiplier(float p_multiplier) {
	energy_multiplier = p_multiplier;
	RS::get_singleton()->material_set_param(_get_material(), "exposure", energy_multiplier);
}

float PanoramaSkyMaterial::get_energy_multiplier() const {
	return energy_multiplier;
}

Shader::Mode PanoramaSkyMaterial::get_shader_mode() const {
	return Shader::MODE_SKY;
}

RID PanoramaSkyMaterial::get_rid() const {
	_update_shader(filter);
	if (!shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), shader_cache[int(filter)]);
		shader_set = true;
	}
	return _get_material();
}

RID PanoramaSkyMaterial::get_shader_rid() const {
	_update_shader(filter);
	return shader_cache[int(filter)];
}

void PanoramaSkyMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_panorama", "texture"), &PanoramaSkyMaterial::set_panorama);
	ClassDB::bind_method(D_METHOD("get_panorama"), &PanoramaSkyMaterial::get_panorama);

	ClassDB::bind_method(D_METHOD("set_filtering_enabled", "enabled"), &PanoramaSkyMaterial::set_filtering_enabled);
	ClassDB::bind_method(D_METHOD("is_filtering_enabled"), &PanoramaSkyMaterial::is_filtering_enabled);

	ClassDB::bind_method(D_METHOD("set_energy_multiplier", "multiplier"), &PanoramaSkyMaterial::set_energy_multiplier);
	ClassDB::bind_method(D_METHOD("get_energy_multiplier"), &PanoramaSkyMaterial::get_energy_multiplier);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "panorama", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_panorama", "get_panorama");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "filter"), "set_filtering_enabled", "is_filtering_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "energy_multiplier", PROPERTY_HINT_RANGE, "0,128,0.01"), "set_energy_multiplier", "get_energy_multiplier");
}

Mutex PanoramaSkyMaterial::shader_mutex;
RID PanoramaSkyMaterial::shader_cache[2];

void PanoramaSkyMaterial::cleanup_shader() {
	for (int i = 0; i < 2; i++) {
		if (shader_cache[i].is_valid()) {
			RS::get_singleton()->free_rid(shader_cache[i]);
		}
	}
}

void PanoramaSkyMaterial::_update_shader(bool p_filter) {
	MutexLock shader_lock(shader_mutex);
	int index = int(p_filter);
	if (shader_cache[index].is_null()) {
		shader_cache[index] = RS::get_singleton()->shader_create();

		// Add a comment to describe the shader origin (useful when converting to ShaderMaterial).
		RS::get_singleton()->shader_set_code(shader_cache[index], vformat(R"(
// NOTE: Shader automatically converted from )" GODOT_VERSION_NAME " " GODOT_VERSION_FULL_CONFIG R"('s PanoramaSkyMaterial.

shader_type sky;

uniform sampler2D source_panorama : %s, source_color, hint_default_black;
uniform float exposure : hint_range(0, 128) = 1.0;

void sky() {
	COLOR = texture(source_panorama, SKY_COORDS).rgb * exposure;
}
)",
																		  p_filter ? "filter_linear" : "filter_nearest"));
	}
}

PanoramaSkyMaterial::PanoramaSkyMaterial() {
	_set_material(RS::get_singleton()->material_create());
	set_energy_multiplier(1.0);
}

PanoramaSkyMaterial::~PanoramaSkyMaterial() {
}

//////////////////////////////////
/* PhysicalSkyMaterial */


void PhysicalSkyMaterial::_get_atmosphere_coefficients(Color &r_rayleigh, Color &r_mie_scattering, Color &r_mie_extinction) const {
	// SI scattering coefficients. Legacy artistic colors tint the terrestrial reference.
	const Color reference_r = Color(0.3, 0.405, 0.6).srgb_to_linear();
	const Color reference_m = Color(0.69, 0.729, 0.812).srgb_to_linear();
	const Color tint_r = rayleigh_color.srgb_to_linear();
	const Color tint_m = mie_color.srgb_to_linear();
	r_rayleigh = Color(5.802e-6, 13.558e-6, 33.1e-6) * (MAX(rayleigh, 0.0f) * 0.5f);
	r_mie_scattering = Color(3.996e-6, 3.996e-6, 3.996e-6) * (MAX(mie, 0.0f) / 0.005f) * (MAX(turbidity, 0.0f) / 10.0f);
	for (int channel = 0; channel < 3; channel++) {
		r_rayleigh[channel] *= MAX(tint_r[channel], 0.0f) / reference_r[channel];
		r_mie_scattering[channel] *= MAX(tint_m[channel], 0.0f) / reference_m[channel];
	}
	r_mie_extinction = r_mie_scattering / mie_single_scattering_albedo;
}

Color PhysicalSkyMaterial::_sample_transmittance(double p_mu, double p_altitude) const {
	const double height = CLAMP(p_altitude, 0.0, double(atmosphere_height));
	const double radius = double(atmosphere_planet_radius) + height;
	const double top = double(atmosphere_planet_radius) + atmosphere_height;
	const double mu = CLAMP(p_mu, -1.0, 1.0);
	// Test the planet analytically, independently of quadrature and LUT filtering.
	if (mu < 0.0 && radius * radius * (1.0 - mu * mu) <= double(atmosphere_planet_radius) * atmosphere_planet_radius) {
		return Color(0, 0, 0);
	}
	const double distance = MAX(0.0, -radius * mu + Math::sqrt(MAX(0.0, radius * radius * (mu * mu - 1.0) + top * top)));
	double optical_r = 0.0;
	double optical_m = 0.0;
	double optical_o = 0.0;
	// Squared intervals resolve the dense first kilometers, including grazing rays.
	for (int i = 0; i < 64; i++) {
		const double a = double(i) / 64.0;
		const double b = double(i + 1) / 64.0;
		const double t0 = distance * a * a;
		const double t1 = distance * b * b;
		const double t = (t0 + t1) * 0.5;
		const double h = MAX(0.0, Math::sqrt(radius * radius + t * t + 2.0 * radius * mu * t) - atmosphere_planet_radius);
		optical_r += Math::exp(-h / rayleigh_scale_height) * (t1 - t0);
		optical_m += Math::exp(-h / mie_scale_height) * (t1 - t0);
		optical_o += MAX(0.0, 1.0 - Math::abs(h - 25000.0) / 15000.0) * (t1 - t0);
	}
	Color beta_r, beta_m, extinction_m;
	_get_atmosphere_coefficients(beta_r, beta_m, extinction_m);
	const Color ozone_beta = Color(0.650e-6, 1.881e-6, 0.085e-6) * atmosphere_ozone_strength;
	Color result(1, 1, 1);
	for (int channel = 0; channel < 3; channel++) {
		result[channel] = Math::exp(-beta_r[channel] * optical_r - extinction_m[channel] * optical_m - ozone_beta[channel] * optical_o);
	}
	return result;
}

Color PhysicalSkyMaterial::sample_sun_transmittance(Vector3 p_sun_direction, float p_altitude) const {
	ERR_FAIL_COND_V(!p_sun_direction.is_finite() || !Math::is_finite(p_altitude), Color(0, 0, 0));
	ERR_FAIL_COND_V(p_sun_direction.is_zero_approx(), Color(0, 0, 0));
	return _sample_transmittance(p_sun_direction.normalized().y, p_altitude);
}

Color PhysicalSkyMaterial::sample_sky_radiance(Vector3 p_view_direction, Vector3 p_sun_direction, float p_altitude) const {
	ERR_FAIL_COND_V(!p_view_direction.is_finite() || !p_sun_direction.is_finite() || !Math::is_finite(p_altitude), Color(0, 0, 0));
	ERR_FAIL_COND_V(p_view_direction.is_zero_approx() || p_sun_direction.is_zero_approx(), Color(0, 0, 0));
	const Vector3 view = p_view_direction.normalized();
	const Vector3 sun = p_sun_direction.normalized();
	const double height = CLAMP(double(p_altitude), 0.0, double(atmosphere_height) - 1.0);
	const double radius = double(atmosphere_planet_radius) + MAX(height, 1.0);
	const double top = double(atmosphere_planet_radius) + atmosphere_height;
	double distance = -radius * view.y + Math::sqrt(MAX(0.0, radius * radius * (view.y * view.y - 1.0) + top * top));
	const double ground_discriminant = radius * radius * (view.y * view.y - 1.0) + double(atmosphere_planet_radius) * atmosphere_planet_radius;
	bool ground_hit = view.y < 0.0 && ground_discriminant >= 0.0;
	if (ground_hit) {
		distance = MIN(distance, -radius * view.y - Math::sqrt(ground_discriminant));
	}
	Color beta_r, beta_m, extinction_m;
	_get_atmosphere_coefficients(beta_r, beta_m, extinction_m);
	const double cosine = view.dot(sun);
	const double g = CLAMP(double(mie_eccentricity), -0.99, 0.99);
	const double phase_r = 3.0 * (1.0 + cosine * cosine) / (16.0 * Math::PI);
	const double phase_m = (1.0 - g * g) / (4.0 * Math::PI * Math::pow(MAX(1.0 + g * g - 2.0 * g * cosine, 0.0001), 1.5));
	const Color ozone_beta = Color(0.650e-6, 1.881e-6, 0.085e-6) * atmosphere_ozone_strength;
	Color throughput(1, 1, 1);
	Color radiance(0, 0, 0);
	for (int i = 0; i < 16; i++) {
		const double a = double(i) / 16.0;
		const double b = double(i + 1) / 16.0;
		const double ds = distance * (b * b - a * a);
		const double t = distance * (a * a + b * b) * 0.5;
		const double sample_radius = Math::sqrt(radius * radius + t * t + 2.0 * radius * view.y * t);
		const double h = MAX(0.0, sample_radius - atmosphere_planet_radius);
		const double density_r = Math::exp(-h / rayleigh_scale_height);
		const double density_m = Math::exp(-h / mie_scale_height);
		const double density_o = MAX(0.0, 1.0 - Math::abs(h - 25000.0) / 15000.0);
		const Color sun_t = _sample_transmittance((sun.y * radius + t * cosine) / sample_radius, h);
		for (int channel = 0; channel < 3; channel++) {
			const double extinction = beta_r[channel] * density_r + extinction_m[channel] * density_m + ozone_beta[channel] * density_o;
			const double segment_t = Math::exp(-extinction * ds);
			const double integral = extinction > 1e-12 ? (1.0 - segment_t) / extinction : ds;
			radiance[channel] += throughput[channel] * sun_t[channel] * (beta_r[channel] * density_r * phase_r + beta_m[channel] * density_m * phase_m) * integral;
			throughput[channel] *= segment_t;
		}
	}
	radiance *= atmosphere_sun_color.srgb_to_linear() * atmosphere_sun_energy;
	if (ground_hit) {
		radiance += ground_color.srgb_to_linear() * atmosphere_sun_color.srgb_to_linear() * sample_sun_transmittance(sun, 0.0f) * (MAX(sun.y, 0.0f) * atmosphere_sun_energy / Math::PI) * throughput;
	}
	radiance += atmosphere_night_color * throughput;
	radiance *= energy_multiplier;
	radiance.a = 1.0f;
	return radiance;
}

void PhysicalSkyMaterial::_mark_atmosphere_dirty() {
	atmosphere_lut_dirty = true;
	if (atmosphere_enabled && !atmosphere_lut_queued) {
		atmosphere_lut_queued = true;
		call_deferred("_update_atmosphere_lut");
	}
}

void PhysicalSkyMaterial::_update_atmosphere_lut() const {
	atmosphere_lut_queued = false;
	if (!atmosphere_enabled || !atmosphere_lut_dirty) {
		return;
	}
	atmosphere_lut_dirty = false;
	Ref<Image> image = Image::create_empty(256, 64, false, Image::FORMAT_RGBAF);
	for (int y = 0; y < 64; y++) {
		const double v = double(y) / 63.0;
		for (int x = 0; x < 256; x++) {
			const double u = double(x) / 255.0 * 2.0 - 1.0;
			const double mu = u * Math::abs(u);
			image->set_pixel(x, y, _sample_transmittance(mu, v * v * atmosphere_height));
		}
	}
	atmosphere_lut = ImageTexture::create_from_image(image);
	RS::get_singleton()->material_set_param(_get_material(), "atmosphere_transmittance", atmosphere_lut->get_rid());
	Color beta_r, beta_m, extinction_m;
	_get_atmosphere_coefficients(beta_r, beta_m, extinction_m);
	// Shader operates in kilometers for stable spherical intersections.
	RS::get_singleton()->material_set_param(_get_material(), "atmosphere_beta_r", Vector3(beta_r.r, beta_r.g, beta_r.b) * 1000.0f);
	RS::get_singleton()->material_set_param(_get_material(), "atmosphere_beta_m", Vector3(beta_m.r, beta_m.g, beta_m.b) * 1000.0f);
	RS::get_singleton()->material_set_param(_get_material(), "atmosphere_extinction_m", Vector3(extinction_m.r, extinction_m.g, extinction_m.b) * 1000.0f);
}

void PhysicalSkyMaterial::set_atmosphere_enabled(bool p_value) {
	if (atmosphere_enabled == p_value) {
		return;
	}
	atmosphere_enabled = p_value;
	_update_shader(use_debanding, night_sky.is_valid(), atmosphere_enabled);
	_update_atmosphere_lut();
	if (shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), get_shader_cache());
	}
}

bool PhysicalSkyMaterial::get_atmosphere_enabled() const {
	return atmosphere_enabled;
}

void PhysicalSkyMaterial::set_atmosphere_altitude(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	p_value = CLAMP(p_value, 0.0f, 1000000.0f);
	if (atmosphere_altitude == p_value) {
		return;
	}
	atmosphere_altitude = p_value;
	RS::get_singleton()->material_set_param(_get_material(), "atmosphere_altitude", atmosphere_altitude);
}

float PhysicalSkyMaterial::get_atmosphere_altitude() const {
	return atmosphere_altitude;
}

void PhysicalSkyMaterial::set_atmosphere_planet_radius(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	p_value = CLAMP(p_value, 100000.0f, 100000000.0f);
	if (atmosphere_planet_radius == p_value) {
		return;
	}
	atmosphere_planet_radius = p_value;
	RS::get_singleton()->material_set_param(_get_material(), "atmosphere_planet_radius", atmosphere_planet_radius);
	_mark_atmosphere_dirty();
}

float PhysicalSkyMaterial::get_atmosphere_planet_radius() const {
	return atmosphere_planet_radius;
}


void PhysicalSkyMaterial::set_atmosphere_ozone_strength(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	p_value = CLAMP(p_value, 0.0f, 10.0f);
	if (atmosphere_ozone_strength == p_value) {
		return;
	}
	atmosphere_ozone_strength = p_value;
	RS::get_singleton()->material_set_param(_get_material(), "atmosphere_ozone_strength", atmosphere_ozone_strength);
	_mark_atmosphere_dirty();
}

float PhysicalSkyMaterial::get_atmosphere_ozone_strength() const {
	return atmosphere_ozone_strength;
}

void PhysicalSkyMaterial::set_atmosphere_height(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	p_value = CLAMP(p_value, 1000.0f, 200000.0f);
	if (atmosphere_height == p_value) {
		return;
	}
	atmosphere_height = p_value;
	RS::get_singleton()->material_set_param(_get_material(), "atmosphere_height", atmosphere_height);
	_mark_atmosphere_dirty();
}

float PhysicalSkyMaterial::get_atmosphere_height() const {
	return atmosphere_height;
}

void PhysicalSkyMaterial::set_rayleigh_scale_height(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	p_value = CLAMP(p_value, 100.0f, 50000.0f);
	if (rayleigh_scale_height == p_value) {
		return;
	}
	rayleigh_scale_height = p_value;
	RS::get_singleton()->material_set_param(_get_material(), "rayleigh_scale_height", rayleigh_scale_height);
	_mark_atmosphere_dirty();
}

float PhysicalSkyMaterial::get_rayleigh_scale_height() const {
	return rayleigh_scale_height;
}

void PhysicalSkyMaterial::set_mie_scale_height(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	p_value = CLAMP(p_value, 100.0f, 20000.0f);
	if (mie_scale_height == p_value) {
		return;
	}
	mie_scale_height = p_value;
	RS::get_singleton()->material_set_param(_get_material(), "mie_scale_height", mie_scale_height);
	_mark_atmosphere_dirty();
}

float PhysicalSkyMaterial::get_mie_scale_height() const {
	return mie_scale_height;
}

void PhysicalSkyMaterial::set_mie_single_scattering_albedo(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	p_value = CLAMP(p_value, 0.01f, 1.0f);
	if (mie_single_scattering_albedo == p_value) {
		return;
	}
	mie_single_scattering_albedo = p_value;
	RS::get_singleton()->material_set_param(_get_material(), "mie_single_scattering_albedo", mie_single_scattering_albedo);
	_mark_atmosphere_dirty();
}

float PhysicalSkyMaterial::get_mie_single_scattering_albedo() const {
	return mie_single_scattering_albedo;
}

void PhysicalSkyMaterial::set_atmosphere_sun_direction(Vector3 p_value) {
	ERR_FAIL_COND(!p_value.is_finite() || p_value.is_zero_approx());
	p_value.normalize();
	if (atmosphere_sun_direction == p_value) {
		return;
	}
	atmosphere_sun_direction = p_value;
	RS::get_singleton()->material_set_param(_get_material(), "atmosphere_sun_direction", atmosphere_sun_direction);
}

Vector3 PhysicalSkyMaterial::get_atmosphere_sun_direction() const {
	return atmosphere_sun_direction;
}

void PhysicalSkyMaterial::set_atmosphere_sun_color(Color p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value.r) || !Math::is_finite(p_value.g) || !Math::is_finite(p_value.b));
	p_value = Color(MAX(p_value.r, 0.0f), MAX(p_value.g, 0.0f), MAX(p_value.b, 0.0f));
	if (atmosphere_sun_color == p_value) {
		return;
	}
	atmosphere_sun_color = p_value;
	RS::get_singleton()->material_set_param(_get_material(), "atmosphere_sun_color", atmosphere_sun_color);
}

Color PhysicalSkyMaterial::get_atmosphere_sun_color() const {
	return atmosphere_sun_color;
}

void PhysicalSkyMaterial::set_atmosphere_sun_energy(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	p_value = CLAMP(p_value, 0.0f, 100000.0f);
	if (atmosphere_sun_energy == p_value) {
		return;
	}
	atmosphere_sun_energy = p_value;
	RS::get_singleton()->material_set_param(_get_material(), "atmosphere_sun_energy", atmosphere_sun_energy);
}

float PhysicalSkyMaterial::get_atmosphere_sun_energy() const {
	return atmosphere_sun_energy;
}

void PhysicalSkyMaterial::set_atmosphere_night_color(Color p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value.r) || !Math::is_finite(p_value.g) || !Math::is_finite(p_value.b));
	p_value = Color(MAX(p_value.r, 0.0f), MAX(p_value.g, 0.0f), MAX(p_value.b, 0.0f));
	if (atmosphere_night_color == p_value) {
		return;
	}
	atmosphere_night_color = p_value;
	RS::get_singleton()->material_set_param(_get_material(), "atmosphere_night_color", atmosphere_night_color);
}

Color PhysicalSkyMaterial::get_atmosphere_night_color() const {
	return atmosphere_night_color;
}

void PhysicalSkyMaterial::set_rayleigh_coefficient(float p_rayleigh) {
	rayleigh = p_rayleigh;
	RS::get_singleton()->material_set_param(_get_material(), "rayleigh", rayleigh);
	_mark_atmosphere_dirty();
}

float PhysicalSkyMaterial::get_rayleigh_coefficient() const {
	return rayleigh;
}

void PhysicalSkyMaterial::set_rayleigh_color(Color p_rayleigh_color) {
	rayleigh_color = p_rayleigh_color;
	RS::get_singleton()->material_set_param(_get_material(), "rayleigh_color", rayleigh_color);
	_mark_atmosphere_dirty();
}

Color PhysicalSkyMaterial::get_rayleigh_color() const {
	return rayleigh_color;
}

void PhysicalSkyMaterial::set_mie_coefficient(float p_mie) {
	mie = p_mie;
	RS::get_singleton()->material_set_param(_get_material(), "mie", mie);
	_mark_atmosphere_dirty();
}

float PhysicalSkyMaterial::get_mie_coefficient() const {
	return mie;
}

void PhysicalSkyMaterial::set_mie_eccentricity(float p_eccentricity) {
	mie_eccentricity = p_eccentricity;
	RS::get_singleton()->material_set_param(_get_material(), "mie_eccentricity", mie_eccentricity);
}

float PhysicalSkyMaterial::get_mie_eccentricity() const {
	return mie_eccentricity;
}

void PhysicalSkyMaterial::set_mie_color(Color p_mie_color) {
	mie_color = p_mie_color;
	RS::get_singleton()->material_set_param(_get_material(), "mie_color", mie_color);
	_mark_atmosphere_dirty();
}

Color PhysicalSkyMaterial::get_mie_color() const {
	return mie_color;
}

void PhysicalSkyMaterial::set_turbidity(float p_turbidity) {
	turbidity = p_turbidity;
	RS::get_singleton()->material_set_param(_get_material(), "turbidity", turbidity);
	_mark_atmosphere_dirty();
}

float PhysicalSkyMaterial::get_turbidity() const {
	return turbidity;
}

void PhysicalSkyMaterial::set_sun_disk_scale(float p_sun_disk_scale) {
	sun_disk_scale = p_sun_disk_scale;
	RS::get_singleton()->material_set_param(_get_material(), "sun_disk_scale", sun_disk_scale);
}

float PhysicalSkyMaterial::get_sun_disk_scale() const {
	return sun_disk_scale;
}

void PhysicalSkyMaterial::set_ground_color(Color p_ground_color) {
	ground_color = p_ground_color;
	RS::get_singleton()->material_set_param(_get_material(), "ground_color", ground_color);
}

Color PhysicalSkyMaterial::get_ground_color() const {
	return ground_color;
}

void PhysicalSkyMaterial::set_energy_multiplier(float p_multiplier) {
	energy_multiplier = p_multiplier;
	RS::get_singleton()->material_set_param(_get_material(), "exposure", energy_multiplier);
}

float PhysicalSkyMaterial::get_energy_multiplier() const {
	return energy_multiplier;
}

void PhysicalSkyMaterial::set_use_debanding(bool p_use_debanding) {
	use_debanding = p_use_debanding;
	_update_shader(use_debanding, night_sky.is_valid(), atmosphere_enabled);
	// Only set if shader already compiled
	if (shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), get_shader_cache());
	}
}

bool PhysicalSkyMaterial::get_use_debanding() const {
	return use_debanding;
}

void PhysicalSkyMaterial::set_night_sky(const Ref<Texture2D> &p_night_sky) {
	night_sky = p_night_sky;
	if (p_night_sky.is_valid()) {
		RS::get_singleton()->material_set_param(_get_material(), "night_sky", p_night_sky->get_rid());
	} else {
		RS::get_singleton()->material_set_param(_get_material(), "night_sky", Variant());
	}

	_update_shader(use_debanding, night_sky.is_valid(), atmosphere_enabled);

	if (shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), get_shader_cache());
	}
}

Ref<Texture2D> PhysicalSkyMaterial::get_night_sky() const {
	return night_sky;
}

Shader::Mode PhysicalSkyMaterial::get_shader_mode() const {
	return Shader::MODE_SKY;
}

// Internal function to grab the current shader RID.
// Must only be called if the shader is initialized.
RID PhysicalSkyMaterial::get_shader_cache() const {
	return shader_cache[int(use_debanding) + (night_sky.is_valid() ? 2 : 0) + (atmosphere_enabled ? 4 : 0)];
}

RID PhysicalSkyMaterial::get_rid() const {
	_update_atmosphere_lut();
	_update_shader(use_debanding, night_sky.is_valid(), atmosphere_enabled);
	if (!shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), get_shader_cache());
		shader_set = true;
	}
	return _get_material();
}

RID PhysicalSkyMaterial::get_shader_rid() const {
	_update_atmosphere_lut();
	_update_shader(use_debanding, night_sky.is_valid(), atmosphere_enabled);
	return get_shader_cache();
}

void PhysicalSkyMaterial::_validate_property(PropertyInfo &p_property) const {
	if (!Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if (p_property.name == "exposure_value" && !GLOBAL_GET_CACHED(bool, "rendering/lights_and_shadows/use_physical_light_units")) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

Mutex PhysicalSkyMaterial::shader_mutex;
RID PhysicalSkyMaterial::shader_cache[8];

void PhysicalSkyMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_atmosphere_ozone_strength", "value"), &PhysicalSkyMaterial::set_atmosphere_ozone_strength);
	ClassDB::bind_method(D_METHOD("get_atmosphere_ozone_strength"), &PhysicalSkyMaterial::get_atmosphere_ozone_strength);
	ClassDB::bind_method(D_METHOD("set_atmosphere_enabled", "value"), &PhysicalSkyMaterial::set_atmosphere_enabled);
	ClassDB::bind_method(D_METHOD("get_atmosphere_enabled"), &PhysicalSkyMaterial::get_atmosphere_enabled);
	ClassDB::bind_method(D_METHOD("set_atmosphere_altitude", "value"), &PhysicalSkyMaterial::set_atmosphere_altitude);
	ClassDB::bind_method(D_METHOD("get_atmosphere_altitude"), &PhysicalSkyMaterial::get_atmosphere_altitude);
	ClassDB::bind_method(D_METHOD("set_atmosphere_planet_radius", "value"), &PhysicalSkyMaterial::set_atmosphere_planet_radius);
	ClassDB::bind_method(D_METHOD("get_atmosphere_planet_radius"), &PhysicalSkyMaterial::get_atmosphere_planet_radius);
	ClassDB::bind_method(D_METHOD("set_atmosphere_height", "value"), &PhysicalSkyMaterial::set_atmosphere_height);
	ClassDB::bind_method(D_METHOD("get_atmosphere_height"), &PhysicalSkyMaterial::get_atmosphere_height);
	ClassDB::bind_method(D_METHOD("set_rayleigh_scale_height", "value"), &PhysicalSkyMaterial::set_rayleigh_scale_height);
	ClassDB::bind_method(D_METHOD("get_rayleigh_scale_height"), &PhysicalSkyMaterial::get_rayleigh_scale_height);
	ClassDB::bind_method(D_METHOD("set_mie_scale_height", "value"), &PhysicalSkyMaterial::set_mie_scale_height);
	ClassDB::bind_method(D_METHOD("get_mie_scale_height"), &PhysicalSkyMaterial::get_mie_scale_height);
	ClassDB::bind_method(D_METHOD("set_mie_single_scattering_albedo", "value"), &PhysicalSkyMaterial::set_mie_single_scattering_albedo);
	ClassDB::bind_method(D_METHOD("get_mie_single_scattering_albedo"), &PhysicalSkyMaterial::get_mie_single_scattering_albedo);
	ClassDB::bind_method(D_METHOD("set_atmosphere_sun_direction", "value"), &PhysicalSkyMaterial::set_atmosphere_sun_direction);
	ClassDB::bind_method(D_METHOD("get_atmosphere_sun_direction"), &PhysicalSkyMaterial::get_atmosphere_sun_direction);
	ClassDB::bind_method(D_METHOD("set_atmosphere_sun_color", "value"), &PhysicalSkyMaterial::set_atmosphere_sun_color);
	ClassDB::bind_method(D_METHOD("get_atmosphere_sun_color"), &PhysicalSkyMaterial::get_atmosphere_sun_color);
	ClassDB::bind_method(D_METHOD("set_atmosphere_sun_energy", "value"), &PhysicalSkyMaterial::set_atmosphere_sun_energy);
	ClassDB::bind_method(D_METHOD("get_atmosphere_sun_energy"), &PhysicalSkyMaterial::get_atmosphere_sun_energy);
	ClassDB::bind_method(D_METHOD("set_atmosphere_night_color", "value"), &PhysicalSkyMaterial::set_atmosphere_night_color);
	ClassDB::bind_method(D_METHOD("get_atmosphere_night_color"), &PhysicalSkyMaterial::get_atmosphere_night_color);
	ClassDB::bind_method(D_METHOD("sample_sun_transmittance", "sun_direction", "altitude"), &PhysicalSkyMaterial::sample_sun_transmittance, DEFVAL(0.0f));
	ClassDB::bind_method(D_METHOD("sample_sky_radiance", "view_direction", "sun_direction", "altitude"), &PhysicalSkyMaterial::sample_sky_radiance, DEFVAL(0.0f));
	ClassDB::bind_method(D_METHOD("_update_atmosphere_lut"), &PhysicalSkyMaterial::_update_atmosphere_lut);
	ADD_GROUP("Integrated Atmosphere", "atmosphere_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "atmosphere_ozone_strength", PROPERTY_HINT_RANGE, "0,10,0.01"), "set_atmosphere_ozone_strength", "get_atmosphere_ozone_strength");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "atmosphere_enabled"), "set_atmosphere_enabled", "get_atmosphere_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "atmosphere_altitude", PROPERTY_HINT_RANGE, "0,100000,1,or_greater,suffix:m"), "set_atmosphere_altitude", "get_atmosphere_altitude");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "atmosphere_planet_radius", PROPERTY_HINT_RANGE, "100000,10000000,1,or_greater,suffix:m"), "set_atmosphere_planet_radius", "get_atmosphere_planet_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "atmosphere_height", PROPERTY_HINT_RANGE, "1000,200000,1,suffix:m"), "set_atmosphere_height", "get_atmosphere_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rayleigh_scale_height", PROPERTY_HINT_RANGE, "100,50000,1,suffix:m"), "set_rayleigh_scale_height", "get_rayleigh_scale_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mie_scale_height", PROPERTY_HINT_RANGE, "100,20000,1,suffix:m"), "set_mie_scale_height", "get_mie_scale_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mie_single_scattering_albedo", PROPERTY_HINT_RANGE, "0.01,1,0.01"), "set_mie_single_scattering_albedo", "get_mie_single_scattering_albedo");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "atmosphere_sun_direction"), "set_atmosphere_sun_direction", "get_atmosphere_sun_direction");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "atmosphere_sun_color"), "set_atmosphere_sun_color", "get_atmosphere_sun_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "atmosphere_sun_energy", PROPERTY_HINT_RANGE, "0,128,0.01,or_greater"), "set_atmosphere_sun_energy", "get_atmosphere_sun_energy");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "atmosphere_night_color"), "set_atmosphere_night_color", "get_atmosphere_night_color");
	ADD_GROUP("", "");

	ClassDB::bind_method(D_METHOD("set_rayleigh_coefficient", "rayleigh"), &PhysicalSkyMaterial::set_rayleigh_coefficient);
	ClassDB::bind_method(D_METHOD("get_rayleigh_coefficient"), &PhysicalSkyMaterial::get_rayleigh_coefficient);

	ClassDB::bind_method(D_METHOD("set_rayleigh_color", "color"), &PhysicalSkyMaterial::set_rayleigh_color);
	ClassDB::bind_method(D_METHOD("get_rayleigh_color"), &PhysicalSkyMaterial::get_rayleigh_color);

	ClassDB::bind_method(D_METHOD("set_mie_coefficient", "mie"), &PhysicalSkyMaterial::set_mie_coefficient);
	ClassDB::bind_method(D_METHOD("get_mie_coefficient"), &PhysicalSkyMaterial::get_mie_coefficient);

	ClassDB::bind_method(D_METHOD("set_mie_eccentricity", "eccentricity"), &PhysicalSkyMaterial::set_mie_eccentricity);
	ClassDB::bind_method(D_METHOD("get_mie_eccentricity"), &PhysicalSkyMaterial::get_mie_eccentricity);

	ClassDB::bind_method(D_METHOD("set_mie_color", "color"), &PhysicalSkyMaterial::set_mie_color);
	ClassDB::bind_method(D_METHOD("get_mie_color"), &PhysicalSkyMaterial::get_mie_color);

	ClassDB::bind_method(D_METHOD("set_turbidity", "turbidity"), &PhysicalSkyMaterial::set_turbidity);
	ClassDB::bind_method(D_METHOD("get_turbidity"), &PhysicalSkyMaterial::get_turbidity);

	ClassDB::bind_method(D_METHOD("set_sun_disk_scale", "scale"), &PhysicalSkyMaterial::set_sun_disk_scale);
	ClassDB::bind_method(D_METHOD("get_sun_disk_scale"), &PhysicalSkyMaterial::get_sun_disk_scale);

	ClassDB::bind_method(D_METHOD("set_ground_color", "color"), &PhysicalSkyMaterial::set_ground_color);
	ClassDB::bind_method(D_METHOD("get_ground_color"), &PhysicalSkyMaterial::get_ground_color);

	ClassDB::bind_method(D_METHOD("set_energy_multiplier", "multiplier"), &PhysicalSkyMaterial::set_energy_multiplier);
	ClassDB::bind_method(D_METHOD("get_energy_multiplier"), &PhysicalSkyMaterial::get_energy_multiplier);

	ClassDB::bind_method(D_METHOD("set_use_debanding", "use_debanding"), &PhysicalSkyMaterial::set_use_debanding);
	ClassDB::bind_method(D_METHOD("get_use_debanding"), &PhysicalSkyMaterial::get_use_debanding);

	ClassDB::bind_method(D_METHOD("set_night_sky", "night_sky"), &PhysicalSkyMaterial::set_night_sky);
	ClassDB::bind_method(D_METHOD("get_night_sky"), &PhysicalSkyMaterial::get_night_sky);

	ADD_GROUP("Rayleigh", "rayleigh_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rayleigh_coefficient", PROPERTY_HINT_RANGE, "0,64,0.01"), "set_rayleigh_coefficient", "get_rayleigh_coefficient");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "rayleigh_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_rayleigh_color", "get_rayleigh_color");

	ADD_GROUP("Mie", "mie_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mie_coefficient", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_mie_coefficient", "get_mie_coefficient");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mie_eccentricity", PROPERTY_HINT_RANGE, "-1,1,0.01"), "set_mie_eccentricity", "get_mie_eccentricity");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "mie_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_mie_color", "get_mie_color");

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "turbidity", PROPERTY_HINT_RANGE, "0,1000,0.01"), "set_turbidity", "get_turbidity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sun_disk_scale", PROPERTY_HINT_RANGE, "0,360,0.01"), "set_sun_disk_scale", "get_sun_disk_scale");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "ground_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_ground_color", "get_ground_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "energy_multiplier", PROPERTY_HINT_RANGE, "0,128,0.01"), "set_energy_multiplier", "get_energy_multiplier");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_debanding"), "set_use_debanding", "get_use_debanding");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "night_sky", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_night_sky", "get_night_sky");
}

void PhysicalSkyMaterial::cleanup_shader() {
	for (int i = 0; i < 8; i++) {
		if (shader_cache[i].is_valid()) {
			RS::get_singleton()->free_rid(shader_cache[i]);
		}
	}
}

void PhysicalSkyMaterial::_update_shader(bool p_use_debanding, bool p_use_night_sky, bool p_atmosphere_enabled) {
	MutexLock shader_lock(shader_mutex);
	int index = int(p_use_debanding) + int(p_use_night_sky) * 2 + int(p_atmosphere_enabled) * 4;
	if (shader_cache[index].is_null()) {
		shader_cache[index] = RS::get_singleton()->shader_create();
		if (p_atmosphere_enabled) {
			RS::get_singleton()->shader_set_code(shader_cache[index], vformat(R"(
// Opt-in spherical single-scattering atmosphere. All integration distances are km.
// Transmittance is media-dependent only; light direction does not rebuild its LUT.
shader_type sky;
%s
uniform sampler2D atmosphere_transmittance : filter_linear, repeat_disable;
uniform vec3 atmosphere_beta_r = vec3(0.005802, 0.013558, 0.0331);
uniform vec3 atmosphere_beta_m = vec3(0.003996);
uniform vec3 atmosphere_extinction_m = vec3(0.00444);
uniform float atmosphere_planet_radius = 6360000.0;
uniform float atmosphere_height = 100000.0;
uniform float atmosphere_ozone_strength = 1.0;
uniform float atmosphere_altitude = 0.0;
uniform float rayleigh_scale_height = 8000.0;
uniform float mie_scale_height = 1200.0;
uniform float mie_eccentricity = 0.8;
uniform vec3 atmosphere_sun_direction = vec3(0.0, 1.0, 0.0);
uniform vec4 atmosphere_sun_color : source_color = vec4(1.0);
uniform float atmosphere_sun_energy = 20.0;
uniform vec4 atmosphere_night_color = vec4(0.002, 0.004, 0.008, 1.0);
uniform vec4 ground_color : source_color = vec4(0.1, 0.07, 0.034, 1.0);
uniform float sun_disk_scale = 1.0;
uniform float exposure = 1.0;
uniform sampler2D night_sky : filter_linear, source_color, hint_default_black;

vec3 sun_transmittance(float mu, float h) {
	float r = atmosphere_planet_radius * 0.001 + max(h, 0.0);
	float planet = atmosphere_planet_radius * 0.001;
	if (mu < 0.0 && r * r * (1.0 - mu * mu) <= planet * planet) {
		return vec3(0.0);
	}
	float u = sign(mu) * sqrt(abs(clamp(mu, -1.0, 1.0))) * 0.5 + 0.5;
	float v = sqrt(clamp(h / (atmosphere_height * 0.001), 0.0, 1.0));
	return texture(atmosphere_transmittance, (vec2(u, v) * vec2(255.0, 63.0) + 0.5) / vec2(256.0, 64.0)).rgb;
}

void sky() {
	vec3 view = normalize(EYEDIR);
	vec3 sun = normalize(atmosphere_sun_direction);
	float planet = atmosphere_planet_radius * 0.001;
	float top = planet + atmosphere_height * 0.001;
	float radius = planet + clamp(atmosphere_altitude * 0.001, 0.001, atmosphere_height * 0.001 - 0.001);
	float distance = -radius * view.y + sqrt(max(0.0, radius * radius * (view.y * view.y - 1.0) + top * top));
	float ground_d = radius * radius * (view.y * view.y - 1.0) + planet * planet;
	bool ground_hit = view.y < 0.0 && ground_d >= 0.0;
	if (ground_hit) {
		distance = min(distance, max(0.0, -radius * view.y - sqrt(ground_d)));
	}
	float cosine = dot(view, sun);
	float g = clamp(mie_eccentricity, -0.99, 0.99);
	float phase_r = 3.0 * (1.0 + cosine * cosine) / (16.0 * PI);
	float phase_m = (1.0 - g * g) / (4.0 * PI * pow(max(1.0 + g * g - 2.0 * g * cosine, 0.0001), 1.5));
	vec3 throughput = vec3(1.0);
	vec3 radiance = vec3(0.0);
	for (int i = 0; i < 16; i++) {
		float a = float(i) / 16.0;
		float b = float(i + 1) / 16.0;
		float ds = distance * (b * b - a * a);
		float t = distance * (a * a + b * b) * 0.5;
		vec3 point = vec3(0.0, radius, 0.0) + t * view;
		float sample_radius = length(point);
		float h = max(0.0, sample_radius - planet);
		float density_r = exp(-h / (rayleigh_scale_height * 0.001));
		float density_m = exp(-h / (mie_scale_height * 0.001));
		float density_o = max(0.0, 1.0 - abs(h - 25.0) / 15.0);
		vec3 extinction = atmosphere_beta_r * density_r + atmosphere_extinction_m * density_m + vec3(0.000650, 0.001881, 0.000085) * atmosphere_ozone_strength * density_o;
		vec3 segment_t = exp(-extinction * ds);
		vec3 integral = (vec3(1.0) - segment_t) / max(extinction, vec3(1e-8));
		vec3 sunlight = sun_transmittance(dot(point / sample_radius, sun), h);
		radiance += throughput * sunlight * (atmosphere_beta_r * density_r * phase_r + atmosphere_beta_m * density_m * phase_m) * integral;
		throughput *= segment_t;
	}
	vec3 source = atmosphere_sun_color.rgb * atmosphere_sun_energy;
	radiance *= source;
	if (ground_hit) {
		radiance += ground_color.rgb * sun_transmittance(sun.y, 0.0) * max(sun.y, 0.0) * source / PI * throughput;
	} else {
		float disk = 0.0;
		if (sun_disk_scale > 0.0) {
			float angular = max(0.00001, 0.00465 * sun_disk_scale);
			// Angle-space edges remain distinct even when cos(tiny_angle) rounds to one.
			disk = 1.0 - smoothstep(angular * 0.9, angular, acos(clamp(cosine, -1.0, 1.0)));
		}
		radiance += disk * source * sun_transmittance(sun.y, atmosphere_altitude * 0.001);
	}
	radiance += atmosphere_night_color.rgb * throughput;
	radiance += texture(night_sky, SKY_COORDS).rgb * throughput;
	COLOR = max(radiance * exposure, vec3(0.0));
}
)", p_use_debanding ? "render_mode use_debanding;" : ""));
			return;
		}


		// Add a comment to describe the shader origin (useful when converting to ShaderMaterial).
		RS::get_singleton()->shader_set_code(shader_cache[index], vformat(R"(
// NOTE: Shader automatically converted from )" GODOT_VERSION_NAME " " GODOT_VERSION_FULL_CONFIG R"('s PhysicalSkyMaterial.

shader_type sky;
%s

uniform float rayleigh : hint_range(0, 64) = 2.0;
uniform vec4 rayleigh_color : source_color = vec4(0.3, 0.405, 0.6, 1.0);
uniform float mie : hint_range(0, 1) = 0.005;
uniform float mie_eccentricity : hint_range(-1, 1) = 0.8;
uniform vec4 mie_color : source_color = vec4(0.69, 0.729, 0.812, 1.0);

uniform float turbidity : hint_range(0, 1000) = 10.0;
uniform float sun_disk_scale : hint_range(0, 360) = 1.0;
uniform vec4 ground_color : source_color = vec4(0.1, 0.07, 0.034, 1.0);
uniform float exposure : hint_range(0, 128) = 1.0;

uniform sampler2D night_sky : filter_linear, source_color, hint_default_black;

const vec3 UP = vec3( 0.0, 1.0, 0.0 );

// Optical length at zenith for molecules.
const float rayleigh_zenith_size = 8.4e3;
const float mie_zenith_size = 1.25e3;

float henyey_greenstein(float cos_theta, float g) {
	const float k = 0.0795774715459;
	return k * (1.0 - g * g) / (pow(1.0 + g * g - 2.0 * g * cos_theta, 1.5));
}

void sky() {
	if (LIGHT0_ENABLED) {
		float zenith_angle = clamp( dot(UP, normalize(LIGHT0_DIRECTION)), -1.0, 1.0 );
		float sun_energy = max(0.0, 0.757 * zenith_angle) * LIGHT0_ENERGY;
		float sun_fade = 1.0 - clamp(1.0 - exp(LIGHT0_DIRECTION.y), 0.0, 1.0);

		// Rayleigh coefficients.
		float rayleigh_coefficient = rayleigh - ( 1.0 * ( 1.0 - sun_fade ) );
		vec3 rayleigh_beta = rayleigh_coefficient * rayleigh_color.rgb * 0.0001;
		// mie coefficients from Preetham
		vec3 mie_beta = turbidity * mie * mie_color.rgb * 0.000434;

		// Optical length.
		float zenith = max(0.0, dot(UP, EYEDIR));
		float optical_mass = 1.0 / (zenith + 0.15 * pow(3.885 + 54.5 * zenith, -1.253));
		float rayleigh_scatter = rayleigh_zenith_size * optical_mass;
		float mie_scatter = mie_zenith_size * optical_mass;

		// Light extinction based on thickness of atmosphere.
		vec3 extinction = exp(-(rayleigh_beta * rayleigh_scatter + mie_beta * mie_scatter));

		// In scattering.
		float cos_theta = dot(EYEDIR, normalize(LIGHT0_DIRECTION));

		float rayleigh_phase = (3.0 / (16.0 * PI)) * (1.0 + pow(cos_theta * 0.5 + 0.5, 2.0));
		vec3 betaRTheta = rayleigh_beta * rayleigh_phase;

		float mie_phase = henyey_greenstein(cos_theta, mie_eccentricity);
		vec3 betaMTheta = mie_beta * mie_phase;

		vec3 Lin = pow(sun_energy * ((betaRTheta + betaMTheta) / (rayleigh_beta + mie_beta)) * (1.0 - extinction), vec3(1.5));
		// Hack from https://github.com/mrdoob/three.js/blob/master/examples/jsm/objects/Sky.js
		Lin *= mix(vec3(1.0), pow(sun_energy * ((betaRTheta + betaMTheta) / (rayleigh_beta + mie_beta)) * extinction, vec3(0.5)), clamp(pow(1.0 - zenith_angle, 5.0), 0.0, 1.0));

		// Hack in the ground color.
		Lin  *= mix(ground_color.rgb, vec3(1.0), smoothstep(-0.1, 0.1, dot(UP, EYEDIR)));

		// Solar disk and out-scattering.
		float sunAngularDiameterCos = cos(LIGHT0_SIZE * sun_disk_scale);
		float sunAngularDiameterCos2 = cos(LIGHT0_SIZE * sun_disk_scale * 0.5);
		float sundisk = smoothstep(sunAngularDiameterCos, sunAngularDiameterCos2, cos_theta);
		vec3 L0 = (sun_energy * extinction) * sundisk * LIGHT0_COLOR;
		%s

		vec3 color = Lin + L0;
		COLOR = pow(color, vec3(1.0 / (1.2 + (1.2 * sun_fade))));
		COLOR *= exposure;
	} else {
		// There is no sun, so display night_sky and nothing else.
		%s
		COLOR *= exposure;
	}
}
)",
																		  p_use_debanding ? "render_mode use_debanding;" : "", p_use_night_sky ? "L0 += texture(night_sky, SKY_COORDS).xyz * extinction;" : "", p_use_night_sky ? "COLOR = texture(night_sky, SKY_COORDS).xyz;" : ""));
	}
}

PhysicalSkyMaterial::PhysicalSkyMaterial() {
	_set_material(RS::get_singleton()->material_create());
	set_rayleigh_coefficient(2.0);
	set_rayleigh_color(Color(0.3, 0.405, 0.6));
	set_mie_coefficient(0.005);
	set_mie_eccentricity(0.8);
	set_mie_color(Color(0.69, 0.729, 0.812));
	set_turbidity(10.0);
	set_sun_disk_scale(1.0);
	set_ground_color(Color(0.1, 0.07, 0.034));
	set_energy_multiplier(1.0);
	set_use_debanding(true);
}

PhysicalSkyMaterial::~PhysicalSkyMaterial() {
}
