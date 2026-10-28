#pragma once

struct SSvpEyeSample
{
	Fvector2 raw_mm = {};
	Fvector2 residual_mm = {};
	float eye_relief_mm = 0.f;
	float entrance_scale = 1.f;
	bool valid = false;
};

// Samples the exit pupil and advances the virtual eye once per frame
SSvpEyeSample svp_update_eye_sample(const Fmatrix& eye_view);

// True when the optic image follows the eye through an eyepiece, false on a rigid panel
bool svp_optic_eye_coupled();
bool svp_objective_hud_current();
float svp_eye_relief_mm();

// Bind the optical constants for the current lens draw.
void svp_bind_lens_constants();
bool svp_thermal_overlay_active();
bool svp_electronic_image_active(); // live NV or thermal display, the 3DSS image owns the lens
bool svp_clipon_resolved();
