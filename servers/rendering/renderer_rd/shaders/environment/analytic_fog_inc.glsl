#ifndef ANALYTIC_ATMOSPHERE_FOG_INCLUDED
#define ANALYTIC_ATMOSPHERE_FOG_INCLUDED

// Shared by Forward+ surfaces, final sky composition, and native clouds.
// Packed rows: enabled/sigmaD/start/maxDistance; sigmaH/base/H/g;
// sun RGB/exposure; sun direction; horizon RGB; zenith RGB.
float analytic_fog_transmittance(vec3 origin, vec3 direction, float distance, vec4 parameters[6]) {
	if (parameters[0].x < 0.5) return 1.0;
	float segment_length = max(min(distance, parameters[0].w) - parameters[0].z, 0.0);
	if (segment_length <= 0.0) return 1.0;
	float tau = segment_length * max(parameters[0].y, 0.0);
	if (parameters[1].x > 0.0) {
		float scale_height = max(parameters[1].z, 0.1);
		float start_y = origin.y + direction.y * parameters[0].z;
		float b = -direction.y * segment_length / scale_height;
		float log_mean;
		if (abs(b) < 0.001) {
			// log(exprel(b)), stable for horizontal and almost-horizontal rays.
			log_mean = b * 0.5 + b * b / 24.0;
		} else if (b > 0.0) {
			log_mean = b + log((1.0 - exp(-b)) / b);
		} else {
			log_mean = log((1.0 - exp(b)) / -b);
		}
		float log_tau = log(parameters[1].x) + log(segment_length)
			- (start_y - parameters[1].y) / scale_height + log_mean;
		// Saturate optical thickness, never the exponent argument by endpoint
		// alone. Long downward paths cannot overflow and upward paths stay finite.
		tau += exp(clamp(log_tau, -80.0, 4.382026634674));
	}
	return exp(-min(tau, 80.0));
}

vec4 analytic_fog_evaluate(vec3 origin, vec3 direction, float distance, vec4 parameters[6]) {
	float transmission = analytic_fog_transmittance(origin, direction, distance, parameters);
	if (transmission == 1.0) return vec4(0.0, 0.0, 0.0, 1.0);
	float g = clamp(parameters[1].w, -0.95, 0.95);
	vec3 sun_direction = parameters[3].xyz / max(length(parameters[3].xyz), 0.00001);
	float cosine = clamp(dot(direction, sun_direction), -1.0, 1.0);
	float denominator = max(1.0 + g * g - 2.0 * g * cosine, 0.0001);
	float phase = (1.0 - g * g) / (12.566370614359 * denominator * sqrt(denominator));
	vec3 sky = mix(parameters[4].rgb, parameters[5].rgb, clamp(direction.y, 0.0, 1.0));
	vec3 source = max(sky + parameters[2].rgb * phase, vec3(0.0)) * parameters[2].w;
	return vec4(source * (1.0 - transmission), transmission);
}

#endif
