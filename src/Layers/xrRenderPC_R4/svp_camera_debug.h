#pragma once
#include "svp_optics.h"

struct SSvpCameraDiagnostics
{
	float ratio_magnification;
	float ratio_cap;
	float scope_magnification;
	float svp_fov;
	float vFov;
	const Fmatrix& m_W_svpcam;
	const Fvector2& principal_ndc;
	const SSvpEyeSample& eye_sample;
	bool registration_valid;
	int entrance_ray_mode;
	u32 camera_session;
	float fNearPlane;
};

void debug_scope(Fmatrix scope_camera, const Fmatrix& projection);
void svp_camera_diagnostics(const SSvpCameraDiagnostics& frame);
