#[compute]

#version 450

#VERSION_DEFINES

#include "../../servers/rendering/renderer_rd/shaders/environment/analytic_fog_inc.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, std140) uniform CloudParameters {
	mat4 inverse_view_projection;
	mat4 previous_view_projection;
	mat4 previous_inverse_view_projection;
	vec4 camera; // xyz position, max trace distance
	vec4 previous_camera;
	vec4 sun_direction; // w anisotropy
	vec4 sun_radiance;
	vec4 ambient_radiance;
	vec4 layer; // base height, thickness, planet radius, extinction/m
	vec4 noise_wind; // xyz wind displacement, noise frequency
	vec4 wind_delta; // xyz displacement since history
	vec4 fog; // rgb foreground inscattering color, w foreground transmission
	vec4 dimensions; // full xy, half xy
	vec4 trace_dimensions; // quarter xy, frame (phase is low two bits), history valid
	vec4 control; // coverage, view steps, light steps, radiance normalization
	vec4 material_shape; // detail scale, layout scale, erosion, edge softness
	vec4 material_profile; // normalized bottom start/end, top start/end
	vec4 material_albedo; // rgb single-scattering albedo, w material enabled
	vec4 material_flags; // body/detail/layout texture present, half-pixel angular footprint
	vec4 cyclic_shape; // active, detail strength, height variation, density contrast
	vec4 aerial_horizon; // linear directional sky color, w aerial strength
	vec4 aerial_zenith; // linear zenith sky color, w characteristic distance
	vec4 aerial_control; // x sky-contribution fade start distance, y complete half-resolution tracing
	mat4 fog_view_projection; // world to the central froxel camera's clip space
	vec4 fog_view_depth; // world plane yielding -central_view_position.z
	vec4 volume_fog; // enabled, inverse length, inverse detail spread, sky affect
	vec4 volume_fog_options; // legacy blend, integrated fog storage conversion
	vec4 analytic_fog[6];
} p;

layout(set = 0, binding = 1) uniform sampler2D scene_depth;
const float FAR_DEPTH = 1e9;

vec3 ray_direction(vec2 pixel) {
	vec2 uv = (pixel + 0.5) / p.dimensions.xy;
	// A reverse-Z point away from the near plane avoids subtracting nearly
	// identical world positions (e.g. a 0.04 m near plane hundreds of meters
	// from the origin). A positive depth also stays finite with an infinite far plane.
	vec4 world = p.inverse_view_projection * vec4(uv * 2.0 - 1.0, 0.0001, 1.0);
	return normalize(world.xyz / world.w - p.camera.xyz);
}

float scene_distance(ivec2 pixel) {
	pixel = clamp(pixel, ivec2(0), ivec2(p.dimensions.xy) - 1);
	float depth = texelFetch(scene_depth, pixel, 0).r;
	if (depth <= 0.0000001) {
		return FAR_DEPTH;
	}
	vec2 uv = (vec2(pixel) + 0.5) / p.dimensions.xy;
	vec4 world = p.inverse_view_projection * vec4(uv * 2.0 - 1.0, depth, 1.0);
	return length(world.xyz / world.w - p.camera.xyz);
}

ivec2 cloud_phase() {
	const int order[4] = int[4](0, 2, 3, 1);
	int phase = order[int(p.trace_dimensions.z) & 3];
	return ivec2(phase & 1, (phase >> 1) & 1);
}

#ifdef MODE_TRACE
layout(rgba16f, set = 0, binding = 2) uniform writeonly image2D trace_color;
layout(rgba32f, set = 0, binding = 3) uniform writeonly image2D trace_depth;
layout(rgba16f, set = 0, binding = 4) uniform writeonly image2D trace_near_color;
layout(set = 0, binding = 5) uniform sampler3D body_noise_texture;
layout(set = 0, binding = 6) uniform sampler3D detail_noise_texture;
layout(set = 0, binding = 7) uniform sampler2D weather_map_texture;
layout(set = 0, binding = 8) uniform sampler3D cyclic_noise_texture;
layout(rgba32f, set = 0, binding = 9) uniform writeonly image2D trace_limits;

float hash3(vec3 q) {
	q = fract(q * 0.1031);
	q += dot(q, q.yzx + 33.33);
	return fract((q.x + q.y) * q.z);
}

float noise3(vec3 q) {
	vec3 i = floor(q);
	vec3 f = fract(q);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(hash3(i), hash3(i + vec3(1, 0, 0)), f.x),
			mix(hash3(i + vec3(0, 1, 0)), hash3(i + vec3(1, 1, 0)), f.x), f.y),
		mix(mix(hash3(i + vec3(0, 0, 1)), hash3(i + vec3(1, 0, 1)), f.x),
			mix(hash3(i + vec3(0, 1, 1)), hash3(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

vec3 macro_hash(ivec2 cell) {
	uint seed = uint(cell.x) * 1597334677u ^ uint(cell.y) * 3812015801u;
	uvec3 bits = uvec3(seed, seed ^ 0x9e3779b9u, seed ^ 0x85ebca6bu);
	bits ^= bits >> 16;
	bits *= 0x7feb352du;
	bits ^= bits >> 15;
	bits *= 0x846ca68bu;
	bits ^= bits >> 16;
	return vec3(bits & uvec3(0x00ffffffu)) / 16777215.0;
}

vec3 macro_domain(inout vec3 q, out float stretch) {
	// Four hashed world-space corners, not a tiled noise texture or authored region.
	// Cubic interpolation is continuous in value and first derivative at cell edges.
	q.xz = mat2(0.8, 0.6, -0.6, 0.8) * q.xz;
	vec2 grid = q.xz * 0.37;
	ivec2 cell = ivec2(floor(grid));
	vec2 f = fract(grid);
	vec2 u = f * f * (3.0 - 2.0 * f);
	vec2 du = 6.0 * f * (1.0 - f);
	vec3 a = macro_hash(cell);
	vec3 b = macro_hash(cell + ivec2(1, 0));
	vec3 c = macro_hash(cell + ivec2(0, 1));
	vec3 d = macro_hash(cell + ivec2(1, 1));
	vec3 field = mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
	vec2 dx = mix(b.xy - a.xy, d.xy - c.xy, u.y) * du.x * (0.9 * 0.37);
	vec2 dz = mix(c.xy - a.xy, d.xy - b.xy, u.x) * du.y * (0.9 * 0.37);
	mat2 jacobian = mat2(vec2(1.0, 0.0) + dx, vec2(0.0, 1.0) + dz);
	float j00 = dot(jacobian[0], jacobian[0]);
	float j01 = dot(jacobian[0], jacobian[1]);
	float j11 = dot(jacobian[1], jacobian[1]);
	// Largest singular value: account for local expansion in the texture footprint.
	stretch = max(1.0, sqrt(0.5 * (j00 + j11 + sqrt((j00 - j11) * (j00 - j11) + 4.0 * j01 * j01))));
	q.xz += (field.xy - 0.5) * 0.9;
	return field;
}

vec4 stochastic_cyclic(vec3 q, float texel_footprint) {
	// Equilateral world-XZ triangles. Adjacent triangles share the same vertex
	// mappings, so changing triangle never introduces a random hard tile seam.
	vec2 grid = vec2(q.x - q.z * 0.57735026919, q.z * 1.15470053838);
	ivec2 base = ivec2(floor(grid));
	vec2 f = fract(grid);
	ivec2 vertices[3];
	vec3 weights;
	if (f.x + f.y < 1.0) {
		vertices[0] = base;
		vertices[1] = base + ivec2(1, 0);
		vertices[2] = base + ivec2(0, 1);
		weights = vec3(1.0 - f.x - f.y, f.x, f.y);
	} else {
		vertices[0] = base + ivec2(1, 1);
		vertices[1] = base + ivec2(0, 1);
		vertices[2] = base + ivec2(1, 0);
		weights = vec3(f.x + f.y - 1.0, 1.0 - f.x, 1.0 - f.y);
	}
	vec4 value = vec4(0.0);
	for (int i = 0; i < 3; i++) {
		vec3 random = macro_hash(vertices[i] + ivec2(113, 419));
		float angle = random.x * 6.28318530718;
		float c = cos(angle);
		float s = sin(angle);
		float scale = mix(0.8, 1.25, random.y);
		vec2 center = vec2(float(vertices[i].x) + float(vertices[i].y) * 0.5, float(vertices[i].y) * 0.86602540378);
		vec3 sample_q = q;
		sample_q.xz = mat2(c, s, -s, c) * (q.xz - center);
		sample_q = sample_q * scale + random.yzx * vec3(17.0, 23.0, 29.0);
		float lod = max(0.0, log2(max(1.0, texel_footprint * scale)));
		value += textureLod(cyclic_noise_texture, sample_q, lod) * weights[i];
	}
	// Preserve centered variation instead of collapsing three independent fields
	// toward their mean at triangle centers. All RGBA channels share the mapping.
	return clamp(vec4(0.5) + (value - 0.5) / sqrt(max(dot(weights, weights), 0.333333)), 0.0, 1.0);
}

float cloud_density(vec3 position, vec2 footprints, bool shadow_sample) {
	float footprint = footprints.x;
	float altitude = length(position + vec3(0.0, p.layer.z, 0.0)) - p.layer.z;
	float h = (altitude - p.layer.x) / p.layer.y;
	if (h <= 0.0 || h >= 1.0 || p.control.x <= 0.0) {
		return 0.0;
	}
	vec3 q = (position - p.noise_wind.xyz) * p.noise_wind.w;
	if (p.material_albedo.w > 0.5) {
		vec3 local_position = position - p.noise_wind.xyz;
		float coverage = p.control.x;
		if (p.material_flags.z > 0.5) {
			float layout_coverage = textureLod(weather_map_texture, local_position.xz / p.material_shape.y, 0.0).r;
			coverage = clamp(coverage * (0.4 + 1.2 * layout_coverage), 0.0, 1.0);
		}
		if (p.cyclic_shape.x > 0.5) {
			// The texture bakes an independently generated recursive cyclic warp:
			// R is broad structure, G the complete octave sum, B the fine residual,
			// and A an independent broad height field. No runtime octave loop.
			float macro_stretch;
			vec3 macro = macro_domain(q, macro_stretch);
			coverage = clamp(coverage * mix(0.55, 1.45, macro.z), 0.0, 1.0);
			float resolution = float(textureSize(cyclic_noise_texture, 0).x);
			vec3 shape_q = q;
			shape_q.y *= 2.5;
			// The caller independently transforms the longitudinal interval and
			// both screen derivatives, so horizontal rays retain vertical filtering.
			float shape_footprint = footprints.y * p.noise_wind.w * resolution * macro_stretch;
			vec4 cyclic = stochastic_cyclic(shape_q, shape_footprint);
			// Height-profile endpoints are local envelopes, not global horizontal
			// planes. Low-frequency height variation remains after detail is filtered.
			float local_bottom_field = mix(cyclic.a, macro.z, 0.35);
			float local_top_field = mix(cyclic.r, macro.y, 0.35);
			float bottom = clamp(p.material_profile.x + p.cyclic_shape.z * (0.1 + 0.8 * local_bottom_field), 0.0, 0.8);
			float top = min(1.0, max(bottom + 0.12, p.material_profile.w - p.cyclic_shape.z * (0.15 + 0.65 * (1.0 - local_top_field))));
			float local_h = (h - bottom) / max(top - bottom, 0.01);
			if (local_h <= 0.0 || local_h >= 1.0) return 0.0;
			float profile_span = max(p.material_profile.w - p.material_profile.x, 0.01);
			float bottom_fade = max((p.material_profile.y - p.material_profile.x) / profile_span, 0.01);
			float top_fade = min((p.material_profile.z - p.material_profile.x) / profile_span, 0.99);
			float envelope = smoothstep(0.0, bottom_fade, local_h) * (1.0 - smoothstep(top_fade, 1.0, local_h));
			// Retain the low-octave structure as the higher octaves approach their
			// sampling limit. The residual smoothly vanishes instead of turning the
			// complete field into a nearly constant layer near its density threshold.
			float octave_weight = 1.0 - smoothstep(0.12, 0.35, shape_footprint / resolution);
			float field = cyclic.r + (cyclic.g - cyclic.r) * octave_weight;
			float threshold = 0.62 - 0.22 * coverage + (0.5 - macro.z) * 0.25;
			float detail_fade = 1.0 - smoothstep(0.25, 1.0, footprint * 4.0 / p.material_shape.x);
			if (!shadow_sample && p.cyclic_shape.y > 0.0 && detail_fade > 0.0) {
				// Reuse the baked residual from the same three samples, rather than
				// adding a second stochastic texture triplet for boundary detail.
				field += (cyclic.b - 0.5) * p.cyclic_shape.y * 0.5 * detail_fade;
			}
			// A broad continuous ramp keeps density relief within the cloud instead
			// of saturating nearly all surviving samples into a uniform white body.
			float mass = clamp((field - threshold) / (0.18 + p.material_shape.w), 0.0, 1.0);
			return p.layer.w * pow(mass, p.cyclic_shape.w) * envelope * mix(0.55, 1.0, cyclic.r);
		}
		float shape;
		float mass_detail = 1.0;
		if (p.material_flags.x > 0.5) {
			float lod = max(0.0, log2(max(1.0, footprint * p.noise_wind.w * float(textureSize(body_noise_texture, 0).x))));
			// A low-frequency, non-integral warp breaks visible rows of the same
			// tiled cloud at the horizon without blurring the shape itself.
			vec3 warp = textureLod(body_noise_texture, q * 0.173 + vec3(0.37, 0.71, 0.19), max(lod - 2.53, 0.0)).gba;
			q.xz += (warp.xz - 0.5) * 0.45;
			vec4 body_sample = textureLod(body_noise_texture, q, lod);
			// Rounded Worley cells form the lobes; the broad Perlin-Worley field
			// groups them into larger masses instead of becoming thin sheets itself.
			float rounded_cell = pow(max(body_sample.g, 0.0), 1.5);
			shape = rounded_cell * mix(0.35, 1.0, body_sample.r);
			// Preserve broad internal density relief after the edge remap saturates.
			// This is extinction variation, not an extra scattering/energy octave.
			mass_detail = mix(0.55, 1.0, smoothstep(0.15, 0.85, body_sample.g));
		} else {
			shape = max((noise3(q * 4.0) * 0.7 + noise3(q * 8.0 + 17.7) * 0.3 - 0.4) * 2.0, 0.0);
		}
		float height_profile = smoothstep(p.material_profile.x, p.material_profile.y, h)
			* (1.0 - smoothstep(p.material_profile.z, p.material_profile.w, h));
		shape *= height_profile;
		// The packed Perlin-Worley R channel is already eroded and has substantial
		// zero-valued clear space. A conventional 1-coverage threshold would remove
		// most cumulus mass, so map coverage to this signed threshold instead.
		float threshold = (0.65 - coverage) * 0.75;
		if (!shadow_sample && p.material_flags.y > 0.5 && shape > threshold) {
			float texels = footprint / p.material_shape.x * float(textureSize(detail_noise_texture, 0).x);
			float lod = max(0.0, log2(max(1.0, texels)));
			vec3 detail_sample = textureLod(detail_noise_texture, local_position / p.material_shape.x, lod).rgb;
			float detail = dot(detail_sample, vec3(0.625, 0.25, 0.125));
			float detail_fade = 1.0 - smoothstep(0.25, 1.0, footprint * 4.0 / p.material_shape.x);
			float edge_weight = 1.0 - smoothstep(threshold, threshold + 0.35, shape);
			shape -= (1.0 - detail) * p.material_shape.z * detail_fade * edge_weight;
		}
		float body = smoothstep(threshold, threshold + p.material_shape.w, shape);
		return p.layer.w * body * height_profile * mass_detail;
	}
	float shape = noise3(q) * 0.625 + noise3(q * 2.03 + 17.7) * 0.25 + noise3(q * 4.11 + 37.1) * 0.125;
	float threshold = 1.0 - p.control.x;
	float body = clamp((shape - threshold) / max(p.control.x, 0.01) * 3.0, 0.0, 1.0);
	float height_profile = smoothstep(0.0, 0.12, h) * (1.0 - smoothstep(0.60, 1.0, h));
	return p.layer.w * body * height_profile;
}

vec2 sphere(vec3 origin, vec3 direction, float radius) {
	float b = dot(origin, direction);
	float c = dot(origin, origin) - radius * radius;
	float discriminant = b * b - c;
	if (discriminant < 0.0) {
		return vec2(-1.0);
	}
	float root = sqrt(discriminant);
	return vec2(-b - root, -b + root);
}

vec2 cloud_segment(vec3 direction) {
	vec3 origin = p.camera.xyz + vec3(0.0, p.layer.z, 0.0);
	float inner_radius = p.layer.z + p.layer.x;
	vec2 outer_hit = sphere(origin, direction, inner_radius + p.layer.y);
	vec2 segment = vec2(max(outer_hit.x, 0.0), min(outer_hit.y, p.camera.w));
	vec2 inner_hit = sphere(origin, direction, inner_radius);
	if (length(origin) < inner_radius) {
		segment.x = max(segment.x, inner_hit.y);
	} else if (inner_hit.x > segment.x) {
		segment.y = min(segment.y, inner_hit.x);
	}
	vec2 ground_hit = sphere(origin, direction, p.layer.z);
	if (ground_hit.x > 0.0) {
		segment.y = min(segment.y, ground_hit.x);
	}
	return segment;
}

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pixel, ivec2(p.trace_dimensions.xy)))) return;
	ivec2 phase_xy = cloud_phase();
	bool complete_trace = p.aerial_control.y > 0.5;
	int stride = complete_trace ? 2 : 4;
	ivec2 full_pixel = min(pixel * stride + (complete_trace ? ivec2(0) : phase_xy * 2) + 1, ivec2(p.dimensions.xy) - 1);
	float scene_min = FAR_DEPTH;
	float scene_max = 0.0;
	for (int y = 0; y < stride; ++y) {
		for (int x = 0; x < stride; ++x) {
			float d = scene_distance(pixel * stride + ivec2(x, y));
			scene_min = min(scene_min, d);
			scene_max = max(scene_max, d);
		}
	}
	vec3 direction = ray_direction(vec2(full_pixel));
	vec3 cyclic_axis_scale = vec3(1.0, 2.5, 1.0);
	float cyclic_screen_angle = p.material_flags.w;
	float cyclic_ray_scale = 1.0;
	float cyclic_light_scale = 1.0;
	if (p.cyclic_shape.x > 0.5) {
		// One pair of ray differentials per quarter-resolution ray, outside the
		// density march. Half-resolution history samples are two full pixels apart.
		vec3 differential_x = ray_direction(vec2(full_pixel) + vec2(2.0, 0.0)) - direction;
		vec3 differential_y = ray_direction(vec2(full_pixel) + vec2(0.0, 2.0)) - direction;
		cyclic_screen_angle = max(length(differential_x * cyclic_axis_scale), length(differential_y * cyclic_axis_scale));
		cyclic_ray_scale = length(direction * cyclic_axis_scale);
		cyclic_light_scale = length(p.sun_direction.xyz * cyclic_axis_scale);
	}
	vec2 segment = cloud_segment(direction);
	// Trace the farthest depth in the tile; full-resolution bilateral composition rejects
	// foreground pixels separately, preserving thin silhouettes and disoccluded sky.
	segment.y = min(segment.y, scene_max);
	vec3 scattering = vec3(0.0);
	vec3 near_scattering = vec3(0.0);
	float transmission = 1.0;
	float near_transmission = 1.0;
	float front = FAR_DEPTH;
	float near_front = FAR_DEPTH;
	float depth_sum = 0.0;
	float near_depth_sum = 0.0;
	if (segment.y > segment.x && segment.y > 0.0) {
		int count = int(p.control.y);
		float step_length = (segment.y - segment.x) / float(count);
		// Keep one deterministic spatial ray offset per quarter tile across all
		// phases and cycles. Fresh phases are authoritative; without external TAA
		// there is no color accumulation that can resolve changing ray noise.
		float jitter = complete_trace ? 0.5 : fract(52.9829189 * fract(dot(vec2(pixel), vec2(0.06711056, 0.00583715))));
		float g = p.sun_direction.w;
		float cosine = dot(direction, p.sun_direction.xyz);
		float phase_function = (1.0 - g * g) / pow(max(0.03, 1.0 + g * g - 2.0 * g * cosine), 1.5);
		float sunlight = smoothstep(-0.05, 0.02, p.sun_direction.y);
		for (int i = 0; i < count; ++i) {
			float distance = segment.x + (float(i) + jitter) * step_length;
			vec3 position = p.camera.xyz + direction * distance;
			// The view step integrates along a ray, not across an isotropic cube.
			// Retain the existing bounded longitudinal filter independently from
			// the screen cone; stable spatial offsets do not add temporal averaging.
			// Light-march filtering remains conservative and independent below.
			// Complete tracing uses a coherent midpoint grid with distance-aware
			// longitudinal filtering, avoiding screen-locked stochastic grain.
			float longitudinal_filter = p.cyclic_shape.x > 0.5 ?
				(complete_trace ? mix(0.35, 1.0, smoothstep(8000.0, 24000.0, distance)) : 0.35) : 1.0;
			float footprint = max(step_length * longitudinal_filter, distance * p.material_flags.w);
			float cyclic_footprint = max(step_length * longitudinal_filter * cyclic_ray_scale, distance * cyclic_screen_angle);
			float extinction = cloud_density(position, vec2(footprint, cyclic_footprint), false);
			if (extinction > 0.000001) {
				if (front > 1e8) front = segment.x + float(i) * step_length;
				float optical_depth = 0.0;
				float near_shape_occlusion = 0.0;
				vec2 light_shell = sphere(position + vec3(0.0, p.layer.z, 0.0), p.sun_direction.xyz, p.layer.z + p.layer.x + p.layer.y);
				float light_length = min(p.layer.y * 1.5, max(light_shell.y, 0.0));
				float light_count = max(p.control.z, 1.0);
				for (int j = 0; j < int(p.control.z); ++j) {
					// Quadratic spacing retains the fixed sample budget but observes
					// nearby small lobes that uniform steps can entirely jump over.
					// Each midpoint is weighted by its actual interval length.
					float start_fraction = float(j) / light_count;
					float end_fraction = float(j + 1) / light_count;
					float light_start = light_length * start_fraction * start_fraction;
					float light_end = light_length * end_fraction * end_fraction;
					float light_step = light_end - light_start;
					float light_midpoint = (light_start + light_end) * 0.5;
					float light_density = cloud_density(position + p.sun_direction.xyz * light_midpoint, light_step * 0.5 * vec2(1.0, cyclic_light_scale), true);
					optical_depth += light_density * light_step;
					if (j < 2 && p.cyclic_shape.x > 0.5) {
						// Reuse existing samples for bounded artistic shape absorption.
						// This relative density difference is not a physical derivative.
						float relative_rise = max(0.0, (light_density - extinction) / (light_density + extinction + 0.00001));
						near_shape_occlusion += relative_rise * (j == 0 ? 0.65 : 0.35);
					}
				}
				float h = clamp((length(position + vec3(0, p.layer.z, 0)) - p.layer.z - p.layer.x) / p.layer.y, 0.0, 1.0);
				// Suppress the extra shape cue in optically thin wisps to avoid dark
				// outlines. All four physical shadow intervals remain integrated.
				// Use a world-space reference length, not the view-step length, so
				// this gate does not change cloud character with camera distance.
				float substantial_mass = smoothstep(0.02, 0.12, extinction * min(100.0, p.layer.y * 0.1));
				float shape_transmission = 1.0 - 0.5 * smoothstep(0.05, 0.7, near_shape_occlusion) * substantial_mass;
				vec3 source = p.sun_radiance.rgb * exp(-optical_depth) * shape_transmission * phase_function * 0.35 * sunlight
					+ p.ambient_radiance.rgb * mix(0.35, 1.0, h);
				source *= p.material_albedo.rgb;
				float step_transmission = exp(-extinction * step_length);
				// Secondary depth result shares all density and light samples with the far
				// ray. Integrate only the portion before the tile's nearest opaque depth.
				float near_length = clamp(scene_min - (segment.x + float(i) * step_length), 0.0, step_length);
				float near_step_transmission = exp(-extinction * near_length);
				// Statistical depth uses the same T-weighted opacity contribution as
				// radiance. Conservative support is the occupied interval start.
				depth_sum += transmission * (1.0 - step_transmission) * distance;
				near_depth_sum += near_transmission * (1.0 - near_step_transmission) * min(distance, scene_min);
				if (near_length > 0.0 && near_front > 1e8) near_front = segment.x + float(i) * step_length;
				near_scattering += near_transmission * (1.0 - near_step_transmission) * source;
				near_transmission *= near_step_transmission;
				scattering += transmission * (1.0 - step_transmission) * source;
				transmission *= step_transmission;
				if (transmission < 0.01) break;
			}
		}
	}
	imageStore(trace_color, pixel, vec4(scattering, transmission));
	imageStore(trace_near_color, pixel, vec4(near_scattering, near_transmission));
	float representative = 1.0 - transmission > 0.000001 ? depth_sum / (1.0 - transmission) : FAR_DEPTH;
	float representative_near = 1.0 - near_transmission > 0.000001 ? near_depth_sum / (1.0 - near_transmission) : FAR_DEPTH;
	imageStore(trace_depth, pixel, vec4(representative, representative_near, scene_min, scene_max));
	imageStore(trace_limits, pixel, vec4(front, near_front, scene_distance(full_pixel), 0.0));
}
#endif

#if defined(MODE_RECONSTRUCT) || defined(MODE_COMPOSITE)
// Identical point locations supply color, conservative support, and depth.
// The two integration endpoints make four spatial neighbors eight candidates.
float endpoint_error(float target, float endpoint) {
	if (target > 1e8 || endpoint > 1e8) return target > 1e8 && endpoint > 1e8 ? 0.0 : 1e20;
	return abs(target - endpoint) / max(2.0, min(target, endpoint) * 0.02);
}

void spatial_cloud(sampler2D far_texture, sampler2D near_texture, sampler2D depths,
		sampler2D limits, vec2 position, float target, out vec4 result,
		out float representative, out float conservative) {
	ivec2 base = ivec2(floor(position));
	ivec2 size = textureSize(depths, 0);
	vec4 colors[4];
	float depths_cloud[4];
	float fronts[4];
	float weights[4];
	float best = 1e20;
	int selected = -1;
	bool safe = true;
	for (int i = 0; i < 4; i++) {
		ivec2 q = clamp(base + ivec2(i & 1, i >> 1), ivec2(0), size - 1);
		vec4 meta = texelFetch(depths, q, 0);
		vec4 bound = texelFetch(limits, q, 0);
		float near_error = endpoint_error(target, meta.z);
		float far_error = endpoint_error(target, meta.w);
		bool use_near = near_error < far_error;
		float error = min(near_error, far_error);
		vec2 axis_weight = max(vec2(0.0), 1.0 - abs(vec2(q) - position));
		weights[i] = axis_weight.x * axis_weight.y;
		colors[i] = use_near ? texelFetch(near_texture, q, 0) : texelFetch(far_texture, q, 0);
		depths_cloud[i] = use_near ? meta.y : meta.x;
		fronts[i] = use_near ? bound.y : bound.x;
		// A finite surface in front of all occupied intervals sees no cloud,
		// even when a neighboring ray's far endpoint belongs to clear sky.
		if (target < 1e8 && fronts[i] >= target) {
			colors[i] = vec4(0.0, 0.0, 0.0, 1.0);
			depths_cloud[i] = FAR_DEPTH;
			fronts[i] = FAR_DEPTH;
			error = 0.0;
		}
		// Zero-weight corners cannot introduce a geometric discontinuity.
		if (weights[i] > 0.000001) safe = safe && error <= 1.0;
		float score = error + dot(vec2(q) - position, vec2(q) - position) * 0.00001;
		if (error <= 1.0 && weights[i] > 0.000001 && score < best) { best = score; selected = i; }
	}
	result = vec4(0.0, 0.0, 0.0, 1.0);
	representative = FAR_DEPTH;
	conservative = FAR_DEPTH;
	if (selected < 0) return;
	// At discontinuities choose the best endpoint, never normalize tiny weights
	// from unrelated geometry. Cloud opacity variation is not a surface edge.
	if (!safe) {
		result = colors[selected];
		representative = depths_cloud[selected];
		conservative = fronts[selected];
		return;
	}
	result = vec4(0.0);
	float weight_sum = 0.0;
	float depth_sum = 0.0;
	float opacity_sum = 0.0;
	for (int i = 0; i < 4; i++) {
		result += colors[i] * weights[i];
		weight_sum += weights[i];
		if (depths_cloud[i] < 1e8) {
			float opacity = weights[i] * (1.0 - colors[i].a);
			depth_sum += depths_cloud[i] * opacity;
			opacity_sum += opacity;
			if (weights[i] > 0.000001) conservative = min(conservative, fronts[i]);
		}
	}
	result /= max(weight_sum, 0.000001);
	if (opacity_sum > 0.000001) representative = depth_sum / opacity_sum;
}

#endif

#ifdef MODE_RECONSTRUCT
layout(set = 0, binding = 2) uniform sampler2D trace_color;
layout(set = 0, binding = 3) uniform sampler2D trace_depth;
layout(set = 0, binding = 4) uniform sampler2D previous_color;
layout(set = 0, binding = 5) uniform sampler2D previous_depth;
layout(rgba16f, set = 0, binding = 6) uniform writeonly image2D history_color;
layout(rgba32f, set = 0, binding = 7) uniform writeonly image2D history_depth;
layout(set = 0, binding = 8) uniform sampler2D trace_near_color;
layout(set = 0, binding = 9) uniform sampler2D previous_near_color;
layout(rgba16f, set = 0, binding = 10) uniform writeonly image2D history_near_color;
layout(set = 0, binding = 11) uniform sampler2D trace_limits;
layout(set = 0, binding = 12) uniform sampler2D previous_limits;
layout(rgba32f, set = 0, binding = 13) uniform writeonly image2D history_limits;

vec3 history_ray(ivec2 pixel) {
	ivec2 full_pixel = min(pixel * 2 + 1, ivec2(p.dimensions.xy) - 1);
	vec2 uv = (vec2(full_pixel) + 0.5) / p.dimensions.xy;
	vec4 world = p.previous_inverse_view_projection * vec4(uv * 2.0 - 1.0, 0.0001, 1.0);
	return normalize(world.xyz / world.w - p.previous_camera.xyz);
}

float transport_depth(float distance, vec3 source_direction, vec3 displacement) {
	if (distance > 1e8) return FAR_DEPTH;
	vec3 source_position = p.previous_camera.xyz + source_direction * distance + displacement;
	return length(source_position - p.camera.xyz);
}

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pixel, ivec2(p.dimensions.zw)))) return;
	ivec2 full_pixel = min(pixel * 2 + 1, ivec2(p.dimensions.xy) - 1);
	float surface = scene_distance(full_pixel);
	ivec2 phase_xy = cloud_phase();
	bool complete_trace = p.aerial_control.y > 0.5;
	ivec2 tile = complete_trace ? pixel : pixel / 2;
	vec4 tile_meta = texelFetch(trace_depth, tile, 0);
	vec2 output_endpoints = tile_meta.zw;
	float output_surface = surface;
	vec4 current, current_near;
	float representative, representative_near, front, front_near;
	float age = 9.0;
	bool traced = complete_trace || all(equal(pixel & ivec2(1), phase_xy));
	if (traced) {
		// A genuinely traced phase is authoritative. Never blend old color into it.
		current = texelFetch(trace_color, tile, 0);
		current_near = texelFetch(trace_near_color, tile, 0);
		representative = tile_meta.x;
		representative_near = tile_meta.y;
		vec4 bound = texelFetch(trace_limits, tile, 0);
		front = bound.x;
		front_near = bound.y;
		age = 0.0;
	} else {
		vec2 position = (vec2(pixel) - vec2(phase_xy)) * 0.5;
		spatial_cloud(trace_color, trace_near_color, trace_depth, trace_limits,
				position, tile_meta.w, current, representative, front);
		spatial_cloud(trace_color, trace_near_color, trace_depth, trace_limits,
				position, tile_meta.z, current_near, representative_near, front_near);
		if (p.trace_dimensions.w > 0.5) {
			vec3 direction = ray_direction(vec2(full_pixel));
			float seed = representative;
			vec4 retained_meta = texelFetch(previous_depth, pixel, 0);
			vec4 retained_limits = texelFetch(previous_limits, pixel, 0);
			vec3 retained_ray = history_ray(pixel);
			vec2 retained_endpoints = vec2(transport_depth(retained_meta.z, retained_ray, vec3(0.0)),
					transport_depth(retained_meta.w, retained_ray, vec3(0.0)));
			// A retained clear observation is also authoritative until its next
			// trace. Do not replace its FAR seed with a cloudy spatial neighbor.
			if (endpoint_error(tile_meta.z, retained_endpoints.x) <= 1.0
					&& endpoint_error(tile_meta.w, retained_endpoints.y) <= 1.0 && retained_limits.w < 8.0) {
				seed = transport_depth(retained_meta.x, retained_ray, p.wind_delta.xyz);
			}
			float distance = seed < 1e8 ? min(seed, p.camera.w) : min(p.camera.w, max(p.layer.x * 4.0, 8000.0));
			vec3 world = p.camera.xyz + direction * distance - p.wind_delta.xyz;
			vec4 clip = p.previous_view_projection * vec4(world, 1.0);
			vec2 old_position = (clip.xy / max(clip.w, 0.0001) * 0.5 + 0.5) * p.dimensions.xy * 0.5 - 0.75;
			// Static inverse/project round trips can drift a few ULPs from exact
			// half-pixel centers. This 0.002 full-pixel tolerance is not a motion filter.
			vec2 integer_position = round(old_position);
			old_position = mix(old_position, integer_position, lessThan(abs(old_position - integer_position), vec2(0.001)));
			if (clip.w > 0.0 && all(greaterThanEqual(old_position, vec2(0.0))) && all(lessThanEqual(old_position, p.dimensions.zw - 1.0))) {
				ivec2 base = ivec2(floor(old_position));
				float expected = length(world - p.previous_camera.xyz);
				vec4 old_colors[4], old_near_colors[4], old_meta[4], old_limits[4];
				vec3 source_rays[4];
				int best = -1;
				float best_score = 1e20;
				for (int i = 0; i < 4; i++) {
					ivec2 q = clamp(base + ivec2(i & 1, i >> 1), ivec2(0), ivec2(p.dimensions.zw) - 1);
					old_meta[i] = texelFetch(previous_depth, q, 0);
					old_limits[i] = texelFetch(previous_limits, q, 0);
					old_colors[i] = texelFetch(previous_color, q, 0);
					old_near_colors[i] = texelFetch(previous_near_color, q, 0);
					source_rays[i] = history_ray(q);
					// The integration endpoints belong to this source color. Convert
					// their distance coordinate; never relabel them with the new tile.
					// Scene geometry is stationary, unlike the advected cloud field.
					old_meta[i].z = transport_depth(old_meta[i].z, source_rays[i], vec3(0.0));
					old_meta[i].w = transport_depth(old_meta[i].w, source_rays[i], vec3(0.0));
					old_limits[i].z = transport_depth(old_limits[i].z, source_rays[i], vec3(0.0));
					vec2 delta = vec2(q) - old_position;
					vec2 axis_weight = max(vec2(0.0), 1.0 - abs(delta));
					float spatial_weight = axis_weight.x * axis_weight.y;
					bool geometry_valid = endpoint_error(surface, old_limits[i].z) <= 1.0
							&& endpoint_error(tile_meta.z, old_meta[i].z) <= 1.0
							&& endpoint_error(tile_meta.w, old_meta[i].w) <= 1.0;
					// Keep one actual source observation rather than blending color but
					// propagating an unrelated maximum or averaged source age.
					bool valid = geometry_valid && old_limits[i].w < 8.0;
					float score = dot(delta, delta);
					if (valid && spatial_weight >= 0.249999 && score < best_score) { best = i; best_score = score; }
				}
				// Validate cloud depth at the actual reprojection anchor. Cloud depths
				// of adjacent sky rays can differ without any foreground discontinuity.
				float anchor_error = best >= 0 ? (seed > 1e8 && old_meta[best].x > 1e8 ? 0.0
						: abs(old_meta[best].x - expected) / max(80.0, expected * 0.03)) : 1e20;
				if (best >= 0 && anchor_error <= 1.0) {
					// A true source age remains exact through camera/wind reprojection.
					// Spatial reconstruction and final composition still use bilinear
					// filtering in geometry-compatible regions; no color EMA is added.
					current = old_colors[best];
					current_near = old_near_colors[best];
					representative = transport_depth(old_meta[best].x, source_rays[best], p.wind_delta.xyz);
					representative_near = transport_depth(old_meta[best].y, source_rays[best], p.wind_delta.xyz);
					front = transport_depth(old_limits[best].x, source_rays[best], p.wind_delta.xyz);
					front_near = transport_depth(old_limits[best].y, source_rays[best], p.wind_delta.xyz);
					output_endpoints = old_meta[best].zw;
					output_surface = old_limits[best].z;
					age = old_limits[best].w + 1.0;
				}
			}
		}
	}
	imageStore(history_color, pixel, current);
	imageStore(history_near_color, pixel, current_near);
	imageStore(history_depth, pixel, vec4(representative, representative_near, output_endpoints));
	imageStore(history_limits, pixel, vec4(front, front_near, output_surface, age));
}
#endif

#ifdef MODE_COMPOSITE
layout(set = 0, binding = 2) uniform sampler2D history_color;
layout(set = 0, binding = 3) uniform sampler2D history_depth;
layout(rgba16f, set = 0, binding = 4) uniform image2D scene_color;
layout(set = 0, binding = 5) uniform sampler2D history_near_color;
layout(set = 0, binding = 6) uniform sampler3D integrated_fog;
layout(set = 0, binding = 7) uniform sampler2D history_limits;

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pixel, ivec2(p.dimensions.xy)))) return;
	float surface = scene_distance(pixel);
	vec2 half_position = (vec2(pixel) - 1.0) * 0.5;
	vec3 composite_direction = vec3(0.0);
	if (p.analytic_fog[0].x > 0.5 || p.volume_fog.x > 0.5 || (p.aerial_horizon.w > 0.0 && surface < 1e8)) {
		composite_direction = ray_direction(vec2(pixel));
	}
	vec3 aerial_color = vec3(0.0);
	if (p.aerial_horizon.w > 0.0 && surface < 1e8) {
		aerial_color = mix(p.aerial_horizon.rgb, p.aerial_zenith.rgb, clamp(composite_direction.y, 0.0, 1.0));
	}
	vec4 cloud;
	float representative, conservative;
	spatial_cloud(history_color, history_near_color, history_depth, history_limits,
			half_position, surface, cloud, representative, conservative);
	if (p.analytic_fog[0].x > 0.5 && (p.analytic_fog[0].y > 0.0 || p.analytic_fog[1].x > 0.0) && surface >= 1e8 && representative < 1e8) {
		// Retain only the finite trace-boundary fade, not the legacy aerial
		// color/distance approximation. Density and marching remain frozen.
		float range_visibility = 1.0 - smoothstep(p.camera.w * 0.65, p.camera.w * 0.95, representative);
		cloud.rgb *= range_visibility;
		cloud.a = 1.0 - range_visibility * (1.0 - cloud.a);
	} else if (p.analytic_fog[0].x < 0.5 && p.aerial_horizon.w > 0.0 && cloud.a < 0.99999 && representative < 1e8) {
		float aerial_length = min(representative, surface);
		if (surface >= 1e8) {
			float distant_visibility = exp(-max(aerial_length - p.aerial_control.x, 0.0) / p.aerial_zenith.w);
			// Fade before the finite trace boundary instead of exposing a hard
			// cloud cutoff against the already atmospherically shaded sky.
			distant_visibility *= 1.0 - smoothstep(p.camera.w * 0.65, p.camera.w * 0.95, aerial_length);
			float contribution = 1.0 - p.aerial_horizon.w * (1.0 - distant_visibility);
			// Radiance and opacity receive exactly the same factor. After the
			// existing foreground-fog composition this is scene + factor *
			// (unfaded_cloud_result - scene), never a second fog on the scene.
			cloud.rgb *= contribution;
			cloud.a = 1.0 - contribution * (1.0 - cloud.a);
		} else {
			float aerial_t = 1.0 - p.aerial_horizon.w * (1.0 - exp(-max(aerial_length, 0.0) / p.aerial_zenith.w));
			// Geometry and near-prefix samples retain the linear HDR aerial
			// color approximation without changing their cloud transmission.
			cloud.rgb = cloud.rgb * aerial_t + (1.0 - cloud.a) * aerial_color * (1.0 - aerial_t);
		}
	}
	float fog_front_weight = representative < 1e8 ? max(1.0 - cloud.a, 0.0) : 0.0;
	float fog_front_sum = min(representative, surface) * fog_front_weight;
	vec4 scene = imageLoad(scene_color, pixel);
	// The sky has already received foreground fog. Only the replacement cloud
	// contribution receives that fog; do not fog scene_color a second time.
	float fog_t = p.analytic_fog[0].x > 0.5 ? 1.0 : p.fog.a;
	vec3 radiance = cloud.rgb * fog_t * p.control.w;
	vec3 foreground = p.fog.rgb * (1.0 - fog_t) * p.control.w;
	vec3 cloud_contribution = radiance + (1.0 - cloud.a) * foreground;
	float volume_affect = surface >= 1e8 ? p.volume_fog.w : 1.0;
	if (p.analytic_fog[0].x > 0.5 && fog_front_weight > 0.000001) {
		vec4 global_fog = analytic_fog_evaluate(p.camera.xyz, composite_direction,
				fog_front_sum / fog_front_weight, p.analytic_fog);
		cloud_contribution = cloud_contribution * global_fog.a + (1.0 - cloud.a) * global_fog.rgb;
	} else if (p.analytic_fog[0].x < 0.5 && p.volume_fog.x > 0.5 && volume_affect > 0.0 && fog_front_weight > 0.000001) {
		float front_distance = fog_front_sum / fog_front_weight;
		vec4 world_front = vec4(p.camera.xyz + composite_direction * front_distance, 1.0);
		vec4 fog_clip = p.fog_view_projection * world_front;
		float view_depth = max(dot(p.fog_view_depth, world_front), 0.0);
		vec3 fog_uv = vec3(fog_clip.xy / max(fog_clip.w, 0.00001) * 0.5 + 0.5,
			pow(clamp(view_depth * p.volume_fog.y, 0.0, 1.0), p.volume_fog.z));
		// Exactly one sample of Godot's already integrated RGB/transmission field.
		// Use the opacity-weighted representative distance even against sky.
		// This is a single-depth cloud approximation, not its conservative front.
		vec4 volume = textureLod(integrated_fog, fog_uv, 0.0);
		float volume_t = mix(1.0, clamp(volume.a, 0.0, 1.0), volume_affect);
		vec3 volume_light = volume.rgb * volume_affect * p.volume_fog_options.y;
		if (p.volume_fog_options.x > 0.5) volume_light *= 1.0 - clamp(volume.a, 0.0, 1.0);
		cloud_contribution = cloud_contribution * volume_t + (1.0 - cloud.a) * volume_light;
	}
	vec3 result = cloud_contribution + cloud.a * scene.rgb;
	imageStore(scene_color, pixel, vec4(result, scene.a));
}
#endif
