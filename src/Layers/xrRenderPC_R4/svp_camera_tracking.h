#pragma once
LPCSTR svp_camera_domain_name(CSecondVPParams::ECameraDomain domain);

float svp_auto_near(CSecondVPParams& vp, float cap, float& out_min, bool& out_manual, bool& out_fresh);
void svp_epoch_update(const void* lens_visual, float lens_radius, CSecondVPParams& params);
float svp_track_capture_ratio(const CSecondVPParams& params, bool analytic_ok, float ratio_analytic);
bool svp_camera_history(
	const CSecondVPParams& params, u32 camera_session, int entrance_ray_mode, bool referenceEffects);

float svp_clamp_magnification(float scope_magnification, float fov_aim);
