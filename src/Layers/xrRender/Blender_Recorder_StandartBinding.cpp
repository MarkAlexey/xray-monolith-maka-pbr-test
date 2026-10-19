#include "stdafx.h"
#pragma hdrstop

#pragma warning(push)
#pragma warning(disable:4995)
#include <d3dx9.h>
#pragma warning(pop)

#include "ResourceManager.h"
#include "RenderDimensions.h"
#include "blenders\Blender_Recorder.h"
#include "blenders\Blender.h"

#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "../../xrEngine/shader_bus.h"

#include "dxRenderDeviceRender.h"

class NativeConstantSetup : public R_constant_setup
{
public:
    bool parallel_safe() const override { return true; }
};

// matrices
#define	BIND_DECLARE(xf)	\
class cl_xform_##xf	: public NativeConstantSetup {	virtual void setup (R_constant* C) { RCache.xforms.set_c_##xf (C); } }; \
	static cl_xform_##xf	binder_##xf
BIND_DECLARE(w);
BIND_DECLARE(invw);
BIND_DECLARE(v);
BIND_DECLARE(p);
BIND_DECLARE(wv);
BIND_DECLARE(vp);
BIND_DECLARE(wvp);
#if defined(USE_DX11) && RENDER == R_R4
bool IsNativeParticleMatrix(R_constant* constant, bool vp)
{
    return constant && constant->handler == (vp ? static_cast<R_constant_setup*>(&binder_vp) : &binder_wvp);
}

bool IsNativeShadowMatrix(R_constant* constant)
{
    return constant && constant->handler == &binder_wvp && constant->name == "m_WVP";
}
#endif

BIND_DECLARE(v_prev);
BIND_DECLARE(p_prev);
BIND_DECLARE(wv_prev);
BIND_DECLARE(vp_prev);
BIND_DECLARE(wvp_prev);

#define DECLARE_TREE_BIND(c)	\
	class cl_tree_##c: public NativeConstantSetup	{virtual void setup(R_constant* C) {RCache.tree.set_c_##c(C);} };	\
	static cl_tree_##c	tree_binder_##c

DECLARE_TREE_BIND(m_xform_v);
DECLARE_TREE_BIND(m_xform);
DECLARE_TREE_BIND(consts);
DECLARE_TREE_BIND(wave);
DECLARE_TREE_BIND(wind);
DECLARE_TREE_BIND(c_scale);
DECLARE_TREE_BIND(c_bias);
DECLARE_TREE_BIND(c_sun);
#if defined(USE_DX11) && RENDER == R_R4
bool NativeShadowHandlers(const R_constant_table* table)
{
    if (!table) return true;
    R_constant_setup* pure[] = {&binder_w, &binder_invw, &binder_v, &binder_p, &binder_wv, &binder_vp, &binder_wvp,
        &binder_v_prev, &binder_p_prev, &binder_wv_prev, &binder_vp_prev, &binder_wvp_prev,
        &tree_binder_m_xform_v, &tree_binder_m_xform, &tree_binder_consts, &tree_binder_wave, &tree_binder_wind,
        &tree_binder_c_scale, &tree_binder_c_bias, &tree_binder_c_sun};
    for (const auto& constant : table->table)
        if (constant->handler && std::find(std::begin(pure), std::end(pure), constant->handler) == std::end(pure)) return false;
    return true;
}
#endif

class cl_hemi_cube_pos_faces : public NativeConstantSetup
{
	virtual void setup(R_constant* C) { RCache.hemi.set_c_pos_faces(C); }
};

static cl_hemi_cube_pos_faces binder_hemi_cube_pos_faces;

class cl_hemi_cube_neg_faces : public NativeConstantSetup
{
	virtual void setup(R_constant* C) { RCache.hemi.set_c_neg_faces(C); }
};

static cl_hemi_cube_neg_faces binder_hemi_cube_neg_faces;

class cl_material : public NativeConstantSetup
{
	virtual void setup(R_constant* C) { RCache.hemi.set_c_material(C); }
};

static cl_material binder_material;

class cl_texgen : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		Fmatrix mTexgen;

#if defined(USE_DX10) || defined(USE_DX11)
		Fmatrix mTexelAdjust =
		{
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f, 0.5f, 0.0f, 1.0f
		};
#else	//	USE_DX10
		float _w = float(RDEVICE.dwWidth);
		float _h = float(RDEVICE.dwHeight);
		float o_w = (.5f / _w);
		float o_h = (.5f / _h);
		Fmatrix mTexelAdjust =
		{
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f + o_w, 0.5f + o_h, 0.0f, 1.0f
		};
#endif	//	USE_DX10

#if defined(USE_DX11)
		if (dlss::Active())
		{
			const auto jitter = dlss::RasterJitter();
			mTexelAdjust._41 += jitter.x / RenderScreenWidth();
			mTexelAdjust._42 += jitter.y / RenderScreenHeight();
		}
#endif
		mTexgen.mul(mTexelAdjust, RCache.xforms.m_wvp);

		RCache.set_c(C, mTexgen);
	}
};

static cl_texgen binder_texgen;

class cl_VPtexgen : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		Fmatrix mTexgen;

#if defined(USE_DX10) || defined(USE_DX11)
		Fmatrix mTexelAdjust =
		{
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f, 0.5f, 0.0f, 1.0f
		};
#else	//	USE_DX10
		float _w = float(RDEVICE.dwWidth);
		float _h = float(RDEVICE.dwHeight);
		float o_w = (.5f / _w);
		float o_h = (.5f / _h);
		Fmatrix mTexelAdjust =
		{
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f + o_w, 0.5f + o_h, 0.0f, 1.0f
		};
#endif	//	USE_DX10

#if defined(USE_DX11)
		if (dlss::Active())
		{
			const auto jitter = dlss::RasterJitter();
			mTexelAdjust._41 += jitter.x / RenderScreenWidth();
			mTexelAdjust._42 += jitter.y / RenderScreenHeight();
		}
#endif
		mTexgen.mul(mTexelAdjust, RCache.xforms.m_vp);

		RCache.set_c(C, mTexgen);
	}
};

static cl_VPtexgen binder_VPtexgen;

// fog
#ifndef _EDITOR
class cl_fog_plane : public NativeConstantSetup
{

	virtual void setup(R_constant* C)
	{
        Fvector4 result;
		{
			// Plane
			Fvector4 plane;
#if defined(USE_DX11) && RENDER == R_R4
            const Fmatrix& M = RenderCameraFullTransform();
#else
            const Fmatrix& M = Device.mFullTransform;
#endif
			plane.x = -(M._14 + M._13);
			plane.y = -(M._24 + M._23);
			plane.z = -(M._34 + M._33);
			plane.w = -(M._44 + M._43);
			float denom = -1.0f / _sqrt(_sqr(plane.x) + _sqr(plane.y) + _sqr(plane.z));
			plane.mul(denom);

			// Near/Far
			float A = g_pGamePersistent->Environment().CurrentEnv->fog_near;
			float B = 1 / (g_pGamePersistent->Environment().CurrentEnv->fog_far - A);
			result.set(-plane.x * B, -plane.y * B, -plane.z * B, 1 - (plane.w - A) * B); // view-plane
		}
		RCache.set_c(C, result);
	}
};

static cl_fog_plane binder_fog_plane;

// fog-params
class cl_fog_params : public NativeConstantSetup
{

	virtual void setup(R_constant* C)
	{
        Fvector4 result;
		{
			// Near/Far
			float n = g_pGamePersistent->Environment().CurrentEnv->fog_near;
			float f = g_pGamePersistent->Environment().CurrentEnv->fog_far;
			float r = 1 / (f - n);
			result.set(-n * r, n, f, r);
		}
		RCache.set_c(C, result);
	}
};

static cl_fog_params binder_fog_params;

// fog-color
class cl_fog_color : public NativeConstantSetup
{

	virtual void setup(R_constant* C)
	{
        Fvector4 result;
		{
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(desc.fog_color.x, desc.fog_color.y, desc.fog_color.z, desc.fog_density);
		}
		RCache.set_c(C, result);
	}
};

static cl_fog_color binder_fog_color;

static class cl_wind_params : public NativeConstantSetup
{

	virtual void setup(R_constant* C)
	{
        Fvector4 result;
		{
			CEnvDescriptor& E = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(E.wind_direction, E.wind_velocity, 0.0f, 0.0f);
		}
		RCache.set_c(C, result);
	}
} binder_wind_params;

#endif


// times
class cl_times : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		float t = RDEVICE.fTimeGlobal;
		RCache.set_c(C, t, t * 10, t / 10, _sin(t));
	}
};

static cl_times binder_times;

// game time
class cl_game_times : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		float t = g_pGamePersistent->Environment().GetGameTime();
		RCache.set_c(C, t, t / DAY_LENGTH, t / (DAY_LENGTH / 24), floor(t / (DAY_LENGTH / 24)));
	}
};

static cl_game_times binder_game_times;

// eye-params
class cl_eye_P : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		Fvector& V = RDEVICE.vCameraPosition;
		RCache.set_c(C, V.x, V.y, V.z, 1);
	}
};
static cl_eye_P binder_eye_P;

// interpolated eye position (crookr scope parallax)
// We can improve this by clamping the magnitude of the travel here instead of in-shader. 
// it would fix the issue with the fog "sticking" when moving too far off center
extern float scope_fog_interp;
extern float scope_fog_travel;
class cl_eye_PL : public R_constant_setup
{
	Fvector tV;
	virtual void setup(R_constant* C)
	{
		Fvector& V = RDEVICE.vCameraPosition;
		tV = tV.lerp(tV, V, scope_fog_interp);

		RCache.set_c(C, tV.x, tV.y, tV.z, 1);
	}
};
static cl_eye_PL binder_eye_PL;

// eye-params
class cl_eye_D : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		Fvector& V = RDEVICE.vCameraDirection;
		RCache.set_c(C, V.x, V.y, V.z, 0);
	}
};
static cl_eye_D binder_eye_D;

// interpolated eye direction (crookr scope parallax)
class cl_eye_DL : public R_constant_setup
{
	Fvector tV;
	virtual void setup(R_constant* C)
	{
		Fvector& V = RDEVICE.vCameraDirection;
		tV = tV.lerp(tV, V, scope_fog_interp);

		RCache.set_c(C, tV.x, tV.y, tV.z, 0);
	}
};
static cl_eye_DL binder_eye_DL;

// eye-params
class cl_eye_N : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		Fvector& V = RDEVICE.vCameraTop;
		RCache.set_c(C, V.x, V.y, V.z, 0);
	}
};

static cl_eye_N binder_eye_N;


// fake scope params (crookr)
extern float scope_outerblur;
extern float scope_innerblur;
extern float scope_scrollpower;
extern float scope_brightness;
class cl_fakescope_params : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, scope_scrollpower, scope_innerblur, scope_outerblur, scope_brightness);
	}
};
static cl_fakescope_params binder_fakescope_params;

extern float scope_ca;
extern float scope_fog_attack;
extern float scope_fog_mattack;
//extern float scope_fog_travel;
class cl_fakescope_ca : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, scope_ca, scope_fog_attack, scope_fog_mattack, scope_fog_travel);
	}
};
static cl_fakescope_ca binder_fakescope_ca;

extern float scope_radius;
extern float scope_fog_radius;
extern float scope_fog_sharp;
//extern float scope_drift_amount;
class cl_fakescope_params3 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, scope_radius, scope_fog_radius, scope_fog_sharp, 0.0f);
	}
};
static cl_fakescope_params3 binder_fakescope_params3;
// Mark Switch
extern int ps_markswitch_current;
extern int ps_markswitch_count;
extern Fvector4 ps_markswitch_color;

static class markswitch_current : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_markswitch_current, 0, 0, 0);
	}
}    markswitch_current;

static class markswitch_count : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_markswitch_count, 0, 0, 0);
	}
}    markswitch_count;

static class markswitch_color : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_markswitch_color.x, ps_markswitch_color.y, ps_markswitch_color.z, ps_markswitch_color.w);
	}
}    markswitch_color;
// Shader 3D Scopes
extern Fvector4 ps_s3ds_param_1;
extern Fvector4 ps_s3ds_param_2;
extern Fvector4 ps_s3ds_param_3;
extern Fvector4 ps_s3ds_param_4;

static class s3ds_param_1 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_s3ds_param_1.x, ps_s3ds_param_1.y, ps_s3ds_param_1.z, ps_s3ds_param_1.w);
	}
}    s3ds_param_1;

static class s3ds_param_2 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_s3ds_param_2.x, ps_s3ds_param_2.y, ps_s3ds_param_2.z, ps_s3ds_param_2.w);
	}
}    s3ds_param_2;

static class s3ds_param_3 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_s3ds_param_3.x, ps_s3ds_param_3.y, ps_s3ds_param_3.z, ps_s3ds_param_3.w);
	}
}    s3ds_param_3;

static class s3ds_param_4 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_s3ds_param_4.x, ps_s3ds_param_4.y, ps_s3ds_param_4.z, ps_s3ds_param_4.w);
	}
}    s3ds_param_4;

//--DSR-- SilencerOverheat_start
static class cl_silencer_glowing : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.hemi.set_c_glowing(C);
	}
} binder_silencer_glowing;
//--DSR-- SilencerOverheat_end

//--DSR-- HeatVision_start
extern float heat_vision_mode;
extern Fvector4 heat_vision_steps;
extern Fvector4 heat_vision_blurring;
extern Fvector4 heat_vision_args_1;
extern Fvector4 heat_vision_args_2;

static class cl_heatvision_hotness : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.hemi.set_c_hotness(C);
	}
} binder_heatvision_hotness;

static class cl_heatvision_steps : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_r2_heatvision, heat_vision_steps.x, heat_vision_steps.y, heat_vision_steps.z);
	}
} binder_heatvision_params1;

static class cl_heatvision_blurring : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, heat_vision_blurring.x, heat_vision_blurring.y, heat_vision_blurring.z, heat_vision_mode);
	}
} binder_heatvision_params2;

static class cl_heatvision_args1 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, heat_vision_args_1.x, heat_vision_args_1.y, heat_vision_args_1.z, heat_vision_args_1.w);
	}
} binder_heatvision_args1;

static class cl_heatvision_args2 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, heat_vision_args_2.x, heat_vision_args_2.y, heat_vision_args_2.z, heat_vision_args_2.w);
	}
} binder_heatvision_args2;

//--DSR-- HeatVision_end


#ifndef _EDITOR
// D-Light0
class cl_sun0_color : public NativeConstantSetup
{

	virtual void setup(R_constant* C)
	{
        Fvector4 result;
		{
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			#if RENDER==R_R1 //Lumscale control for R1
				result.set(desc.sun_color.x * ps_r2_sun_lumscale * ps_r2_sun_lumscale_color.x, desc.sun_color.y * ps_r2_sun_lumscale * ps_r2_sun_lumscale_color.y, desc.sun_color.z * ps_r2_sun_lumscale * ps_r2_sun_lumscale_color.z, 0);
			#else
				result.set(desc.sun_color.x, desc.sun_color.y, desc.sun_color.z, 0);
			#endif
		}
		RCache.set_c(C, result);
	}
};

static cl_sun0_color binder_sun0_color;

class cl_sun0_dir_w : public NativeConstantSetup
{

	virtual void setup(R_constant* C)
	{
        Fvector4 result;
		{
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(desc.sun_dir.x, desc.sun_dir.y, desc.sun_dir.z, 0);
		}
		RCache.set_c(C, result);
	}
};

static cl_sun0_dir_w binder_sun0_dir_w;

class cl_sun0_dir_e : public NativeConstantSetup
{

	virtual void setup(R_constant* C)
	{
        Fvector4 result;
		{
			Fvector D;
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
#if defined(USE_DX11) && RENDER == R_R4
            RenderCameraView().transform_dir(D, desc.sun_dir);
#else
            Device.mView.transform_dir(D, desc.sun_dir);
#endif
			D.normalize();
			result.set(D.x, D.y, D.z, 0);
		}
		RCache.set_c(C, result);
	}
};

static cl_sun0_dir_e binder_sun0_dir_e;

//
class cl_amb_color : public NativeConstantSetup
{

	virtual void setup(R_constant* C)
	{
        Fvector4 result;
		{
			CEnvDescriptorMixer& desc = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(desc.ambient.x, desc.ambient.y, desc.ambient.z, desc.weight);
		}
		RCache.set_c(C, result);
	}
};

static cl_amb_color binder_amb_color;

class cl_hemi_color : public NativeConstantSetup
{

	virtual void setup(R_constant* C)
	{
        Fvector4 result;
		{
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z, desc.hemi_color.w);
		}
		RCache.set_c(C, result);
	}
};

static cl_hemi_color binder_hemi_color;

class cl_sky_color : public NativeConstantSetup
{

	virtual void setup(R_constant* C)
	{
        Fvector4 result;
		{
			CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
			result.set(desc.sky_color.x, desc.sky_color.y, desc.sky_color.z, desc.sky_rotation);
		}
		RCache.set_c(C, result);
	}
};
static cl_sky_color binder_sky_color;

// IX-Ray PBR: sky colour of the current weather, used to tint sky reflections (w = blend factor).
// SSS binds "sky_color" separately (w = sky rotation).
class cl_pbr_sky_color : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		CEnvDescriptorMixer& desc = *g_pGamePersistent->Environment().CurrentEnv;
		RCache.set_c(C, desc.sky_color.x, desc.sky_color.y, desc.sky_color.z, desc.weight);
	}
};

static cl_pbr_sky_color binder_pbr_sky_color;

// IX-Ray PBR: rotation of the skybox around Y (x = cos, y = sin, z = angle), so sky reflections
// line up with the sky that is drawn
class cl_sky_rotation : public R_constant_setup
{
	virtual void setup(R_constant* C)
	{
		const float angle = g_pGamePersistent->Environment().CurrentEnv->sky_rotation;
		RCache.set_c(C, _cos(angle), _sin(angle), angle, 0.f);
	}
};

static cl_sky_rotation binder_sky_rotation;
#endif

static class cl_screen_res : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, (float)RenderScreenWidth(), (float)RenderScreenHeight(), 1.0f / (float)RenderScreenWidth(),
		             1.0f / (float)RenderScreenHeight());
	}
} binder_screen_res;

static class cl_screen_params : public NativeConstantSetup
{

	virtual void setup(R_constant* C)
	{
        Fvector4 result;
		float fov = float(Device.fFOV);
		float aspect = float(Device.fASPECT);
		result.set(fov, aspect, tan(deg2rad(fov) / 2), g_pGamePersistent->Environment().CurrentEnv->far_plane * 0.75f);
		RCache.set_c(C, result);
	}
};

static cl_screen_params binder_screen_params;

static class cl_hud_params : public NativeConstantSetup //--#SM+#--
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, g_pGamePersistent->m_pGShaderConstants->hud_params);
	}
}	binder_hud_params;

static class cl_hud_fov_params : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, g_pGamePersistent->m_pGShaderConstants->hud_fov_params);
	}
}	binder_hud_fov_params;

static class cl_script_params : public NativeConstantSetup //--#SM+#--
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, g_pGamePersistent->m_pGShaderConstants->m_script_params);
	}
}	binder_script_params;

static class cl_blend_mode : public NativeConstantSetup //--#SM+#--
{
	virtual void setup(R_constant* C)
	{
        const auto& source = g_pGamePersistent->m_pGShaderConstants->m_blender_mode;
        Fvector4 mode;
#if RENDER == R_R4
        if (RenderContextId()) mode.set(source.x, source.y, source.z, RCache.recording_grass ? 1.f : 0.f);
        else
#endif
            mode = source;
        RCache.set_c(C, mode);
	}
}	binder_blend_mode;

static class cl_rain_params : public NativeConstantSetup
{

	virtual void setup(R_constant* C)
	{
		float rainDensity = g_pGamePersistent->Environment().CurrentEnv->rain_density;
		float rainWetness = g_pGamePersistent->Environment().wetness_factor;

		RCache.set_c(C, rainDensity, rainWetness, 0.0f, 0.0f);
	}
} binder_rain_params;

static class cl_actor_params : public NativeConstantSetup
{

	virtual void setup(R_constant* C)
	{
		float actorHealth = g_pGamePersistent->actor_data.health;
		float actorStamina = g_pGamePersistent->actor_data.stamina;
		float actorBleeding = g_pGamePersistent->actor_data.bleeding;
		int actorHelmet = g_pGamePersistent->actor_data.helmet;

		RCache.set_c(C, actorHealth, actorStamina, actorBleeding, actorHelmet);
	}
} binder_actor_data;

static class cl_inv_v : public NativeConstantSetup
{
	u32	marker;

	virtual void setup(R_constant* C)
	{
#if defined(USE_DX11) && RENDER == R_R4
        RCache.set_c(C, RenderCameraInverseView());
#else
        RCache.set_c(C, Device.mInvView);
#endif
	}
} binder_inv_v;

static class pp_image_corrections : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_r2_img_exposure, ps_r2_img_gamma, ps_r2_img_saturation, 1);
	}
} pp_image_corrections;

static class pp_color_grading : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_r2_img_cg.x, ps_r2_img_cg.y, ps_r2_img_cg.z, 1);
	}
} pp_color_grading;

static class cl_pda_params : public NativeConstantSetup
{

	virtual void setup(R_constant* C)
	{
		float pda_factor = g_pGamePersistent->pda_shader_data.pda_display_factor;
		float pda_psy_factor = g_pGamePersistent->pda_shader_data.pda_psy_influence;
		float pda_display_brightness = g_pGamePersistent->pda_shader_data.pda_displaybrightness;
		RCache.set_c(C, pda_factor, pda_psy_factor, pda_display_brightness, 0.0f);
	}

} binder_pda_params;

static class cl_near_far_plane : public NativeConstantSetup
{

	virtual void setup(R_constant* C)
	{
        Fvector4 result;
		float nearPlane = float(VIEWPORT_NEAR);
		float farPlane = g_pGamePersistent->Environment().CurrentEnv->far_plane;
		result.set(nearPlane, farPlane, 0, 0);
		RCache.set_c(C, result);
	}
} binder_near_far_plane;

// Screen Space Shaders Stuff
extern Fvector4 ps_ssfx_floravariation;
extern Fvector4 ps_ssfx_fog;
extern Fvector4 ps_ssfx_motionblur;

extern Fvector4 ps_ssfx_pom;
extern Fvector4 ps_ssfx_terrain_pom;

extern Fvector4 ps_ssfx_bloom_1;
extern Fvector4 ps_ssfx_bloom_2;
extern Fvector4 ps_ssfx_il_setup1;

extern float ps_ssfx_hud_hemi;
extern Fvector4 ps_ssfx_il;
extern Fvector4 ps_ssfx_il_setup1;
extern Fvector4 ps_ssfx_ao;
extern Fvector4 ps_ssfx_ao_setup1;
extern Fvector4 ps_ssfx_water;
extern Fvector4 ps_ssfx_water_setup1;
extern Fvector4 ps_ssfx_water_setup2;

extern Fvector4 ps_ssfx_volumetric;
extern Fvector4 ps_ssfx_ssr_2;
extern Fvector4 ps_ssfx_terrain_offset;

extern Fvector3 ps_ssfx_shadow_bias;
extern Fvector4 ps_ssfx_lut;
extern Fvector4 ps_ssfx_wind_grass;
extern Fvector4 ps_ssfx_wind_trees;

extern Fvector4 ps_ssfx_florafixes_1;
extern Fvector4 ps_ssfx_florafixes_2;

extern float ps_ssfx_gloss_factor;
extern Fvector3 ps_ssfx_gloss_minmax;

extern Fvector4 ps_ssfx_wetsurfaces_1;
extern Fvector4 ps_ssfx_wetsurfaces_2;

extern int ps_ssfx_is_underground;
extern Fvector4 ps_ssfx_lightsetup_1;
extern Fvector4 ps_ssfx_hud_drops_1;
extern Fvector4 ps_ssfx_hud_drops_2;
extern Fvector4 ps_ssfx_blood_decals;
extern Fvector4 ps_ssfx_wpn_dof_1;
extern float ps_ssfx_wpn_dof_2;

//Sneaky debug stuff
extern Fvector4 ps_dev_param_1;
extern Fvector4 ps_dev_param_2;
extern Fvector4 ps_dev_param_3;
extern Fvector4 ps_dev_param_4;
extern Fvector4 ps_dev_param_5;
extern Fvector4 ps_dev_param_6;
extern Fvector4 ps_dev_param_7;
extern Fvector4 ps_dev_param_8;

static class dev_param_1 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_1.x, ps_dev_param_1.y, ps_dev_param_1.z, ps_dev_param_1.w);
	}
}    dev_param_1;

static class dev_param_2 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_2.x, ps_dev_param_2.y, ps_dev_param_2.z, ps_dev_param_2.w);
	}
}    dev_param_2;

static class dev_param_3 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_3.x, ps_dev_param_3.y, ps_dev_param_3.z, ps_dev_param_3.w);
	}
}    dev_param_3;

static class dev_param_4 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_4.x, ps_dev_param_4.y, ps_dev_param_4.z, ps_dev_param_4.w);
	}
}    dev_param_4;

static class dev_param_5 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_5.x, ps_dev_param_5.y, ps_dev_param_5.z, ps_dev_param_5.w);
	}
}    dev_param_5;

static class dev_param_6 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_6.x, ps_dev_param_6.y, ps_dev_param_6.z, ps_dev_param_6.w);
	}
}    dev_param_6;

static class dev_param_7 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_7.x, ps_dev_param_7.y, ps_dev_param_7.z, ps_dev_param_7.w);
	}
}    dev_param_7;

static class dev_param_8 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_dev_param_8.x, ps_dev_param_8.y, ps_dev_param_8.z, ps_dev_param_8.w);
	}
}    dev_param_8;

static class ssfx_wpn_dof_1 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_wpn_dof_1.x, ps_ssfx_wpn_dof_1.y, ps_ssfx_wpn_dof_1.z, ps_ssfx_wpn_dof_1.w);
	}
}    ssfx_wpn_dof_1;

static class ssfx_wpn_dof_2 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_wpn_dof_2, 0, 0, 0);
	}
}    ssfx_wpn_dof_2;

static class ssfx_blood_decals : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_blood_decals);
	}
}    ssfx_blood_decals;

static class ssfx_hud_drops_1 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_hud_drops_1);
	}
}    ssfx_hud_drops_1;

static class ssfx_hud_drops_2 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_hud_drops_2);
	}
}    ssfx_hud_drops_2;

static class ssfx_lightsetup_1 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_lightsetup_1);
	}
}    ssfx_lightsetup_1;

static class ssfx_is_underground : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_is_underground, 0, 0, 0);
	}
}    ssfx_is_underground;

static class ssfx_wetsurfaces_1 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_wetsurfaces_1);
	}
}    ssfx_wetsurfaces_1;

static class ssfx_wetsurfaces_2 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_wetsurfaces_2);
	}
}    ssfx_wetsurfaces_2;

static class ssfx_gloss : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_gloss_minmax.x, ps_ssfx_gloss_minmax.y, ps_ssfx_gloss_factor, 0);
	}
}    ssfx_gloss;

static class ssfx_florafixes_1 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_florafixes_1);
	}
}    ssfx_florafixes_1;

static class ssfx_florafixes_2 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_florafixes_2);
	}
}    ssfx_florafixes_2;

static class ssfx_wind_grass : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_wind_grass);
	}
}    ssfx_wind_grass;

static class ssfx_wind_trees : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_wind_trees);
	}
}    ssfx_wind_trees;

static class ssfx_wind_anim : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, g_pGamePersistent->Environment().wind_anim);
	}
}    ssfx_wind_anim;

static class ssfx_wind_anim_prev : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, Device.wind_anim_prev);
	}
}    ssfx_wind_anim_prev;

static class ssfx_lut : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_lut);
	}
}    ssfx_lut;

static class ssfx_shadow_bias : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_shadow_bias.x, ps_ssfx_shadow_bias.y, 0, 0);
	}
}    ssfx_shadow_bias;

static class ssfx_terrain_offset : public NativeConstantSetup
{
	virtual void setup(R_constant * C)
	{
		RCache.set_c(C, ps_ssfx_terrain_offset);
	}
}    ssfx_terrain_offset;

static class ssfx_ssr_2 : public NativeConstantSetup
{
	virtual void setup(R_constant * C)
	{
		RCache.set_c(C, ps_ssfx_ssr_2);
	}
}    ssfx_ssr_2;

static class ssfx_volumetric : public NativeConstantSetup
{
	virtual void setup(R_constant * C)
	{
		RCache.set_c(C, ps_ssfx_volumetric);
	}
}    ssfx_volumetric;

static class ssfx_water : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_water);
	}
}    ssfx_water;

static class ssfx_water_setup1 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_water_setup1);
	}
}    ssfx_water_setup1;

static class ssfx_water_setup2 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_water_setup2);
	}
}    ssfx_water_setup2;

static class ssfx_ao : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_ao);
	}
}    ssfx_ao;

static class ssfx_ao_setup1 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_ao_setup1);
	}
}    ssfx_ao_setup1;

static class ssfx_il : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_il);
	}
}    ssfx_il;

static class ssfx_il_setup1 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_il_setup1);
	}
}    ssfx_il_setup1;

static class ssfx_hud_hemi : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_hud_hemi, 0, 0, 0);
	}
}    ssfx_hud_hemi;

static class ssfx_issvp : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, Device.m_SecondViewport.IsSVPFrame(), 0, 0, 0);
	}
}    ssfx_issvp;

static class ssfx_bloom_1 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		Fvector4 BloomSetup = { 0,0,0,0 };
		if (ps_ssfx_bloom_use_presets)
		{
			BloomSetup.x = g_pGamePersistent->Environment().CurrentEnv->bloom_threshold;
			BloomSetup.y = g_pGamePersistent->Environment().CurrentEnv->bloom_exposure;
			BloomSetup.w = g_pGamePersistent->Environment().CurrentEnv->bloom_sky_intensity;
		}
		else
		{
			BloomSetup.x = ps_ssfx_bloom_1.x;
			BloomSetup.y = ps_ssfx_bloom_1.y;
			BloomSetup.w = ps_ssfx_bloom_1.w;
		}

		RCache.set_c(C, BloomSetup);
	}
}    ssfx_bloom_1;

static class ssfx_bloom_2 : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_bloom_2);
	}
}    ssfx_bloom_2;

static class ssfx_terrain_pom : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_terrain_pom);
	}
}    ssfx_terrain_pom;

static class ssfx_pom : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_pom);
	}
}    ssfx_pom;

static class ssfx_jitter : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		float JitterX = 0;
		float JitterY = 0;

#if defined(USE_DX11)
		if (ps_ssfx_taa.x > 0 && RImplementation.o.ssfx_taa)
		{
			static Fvector2 TAA_Offset[4] = 
			{
				{  0.0f, -1.0f },
				{ -1.0f,  0.0f },
				{  1.0f,  0.0f },
				{  0.0f,  1.0f }
			};

			JitterX = TAA_Offset[ Device.dwFrame % 4 ].x / Device.dwWidth;
			JitterY = TAA_Offset[ Device.dwFrame % 4 ].y / Device.dwHeight;
		}
		if (dlss::Configured())
		{
			const auto jitter = dlss::ClipJitter(dlss::RasterJitter(), {dlss::RenderWidth(), dlss::RenderHeight()});
			RCache.set_c(C, jitter.x, jitter.y, 0.0f, 0.0f);
			return;
		}
#endif

		RCache.set_c(C, JitterX * ps_ssfx_taa.y, JitterY * ps_ssfx_taa.y, ps_ssfx_taa.x, ps_ssfx_taa.w);

	}
}    ssfx_jitter;

#if defined(USE_DX11)
static class dlss_params : public NativeConstantSetup
{
    void setup(R_constant* C) override
    {
        RCache.set_c(C, dlss::Active() ? 1.0f : 0.0f, float(dlss::RenderWidth()), float(dlss::RenderHeight()), 0.0f);
    }
} binder_dlss_params;
static class dlss_temporal_jitter : public NativeConstantSetup
{
    void setup(R_constant* C) override
    {
        const auto current = dlss::RasterJitter();
        const auto previous = dlss::PreviousRasterJitter();
        const float width = float(dlss::RenderWidth()), height = float(dlss::RenderHeight());
        RCache.set_c(C, current.x / width, current.y / height, previous.x / width, previous.y / height);
    }
} binder_dlss_temporal_jitter;
#endif

static class ssfx_fTimeDelta : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, Device.fTimeDelta, (float)Device.dwFrame, 0, 0);
	}
}    ssfx_fTimeDelta;

static class ssfx_motionblur : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_motionblur);
	}
}    ssfx_motionblur;

static class ssfx_fog : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_fog);
	}
}    ssfx_fog;

static class ssfx_floravariation : public NativeConstantSetup
{
	virtual void setup(R_constant* C)
	{
		RCache.set_c(C, ps_ssfx_floravariation);
	}
}    ssfx_floravariation;

extern Fvector4 ps_vignette_control;
static class vignette_control : public NativeConstantSetup
{
	virtual void setup(R_constant *C)
	{
		RCache.set_c(C, ps_vignette_control.x, ps_vignette_control.y, ps_vignette_control.z, ps_vignette_control.w);
	}
} vignette_control;

class bus_binder : public NativeConstantSetup
{
    bool parallel_safe() const override { return false; }
	ShaderBus::lane* lane;

public:
	bus_binder(ShaderBus::lane* l) : lane(l)
	{
	}

	virtual void setup(R_constant* C)
	{
		lane->bound_frame = Device.dwFrame;
		RCache.set_c(C, lane->bound.x, lane->bound.y, lane->bound.z, lane->bound.w);
	}
};

// one binder per lane so the pass table dedup, which compares handler pointers, still matches
static xr_vector<bus_binder*> bus_binders;

// writes the lane's value on a table switch, each drawn object then writes its own
class bus_object_binder : public NativeConstantSetup
{
    bool parallel_safe() const override { return false; }
	ShaderBus::lane* lane;

public:
	bus_object_binder(ShaderBus::lane* l) : lane(l)
	{
	}

	virtual void setup(R_constant* C)
	{
		lane->bound_frame = Device.dwFrame;
		RCache.bus_object.map(C, lane);
	}
};

static xr_vector<bus_object_binder*> bus_object_binders;

#if defined(USE_DX10) || defined(USE_DX11)
// copies every bound row, the setter zeroes declared rows past the last one set
class bus_row_binder : public NativeConstantSetup
{
    bool parallel_safe() const override { return false; }
	ShaderBus::lane* lane;

public:
	bus_row_binder(ShaderBus::lane* l) : lane(l)
	{
	}

	virtual void setup(R_constant* C)
	{
		lane->bound_frame = Device.dwFrame;
		if (lane->rows_bound.empty())
			RCache.set_c_bytes(C, &lane->bound, sizeof(Fvector4));
		else
			RCache.set_c_bytes(C, &lane->rows_bound.front(), u32(lane->rows_bound.size() * sizeof(Fvector4)));
	}
};

static xr_vector<bus_row_binder*> bus_row_binders;
#endif

static const u32 bus_stages[] = { RC_dest_pixel, RC_dest_vertex, RC_dest_geometry, RC_dest_hull, RC_dest_domain, RC_dest_compute };

// declared size in 16 byte rows, the largest over every stage
static u32 bus_rows(R_constant* C)
{
	u32 rows = 1;
	for (u32 i = 0; i < sizeof(bus_stages) / sizeof(bus_stages[0]); ++i)
		if (C->destination & bus_stages[i])
			rows = _max(rows, (C->get_load(bus_stages[i]).size + 15) / 16);
	return rows;
}

// the vector binder serves a constant only where every stage declares one float to float4
static bool bus_fits_vector(R_constant* C)
{
	for (u32 i = 0; i < sizeof(bus_stages) / sizeof(bus_stages[0]); ++i)
	{
		if (!(C->destination & bus_stages[i]))
			continue;

		const R_constant_load& L = C->get_load(bus_stages[i]);
		if (L.cls != RC_1x1 && L.cls != RC_1x2 && L.cls != RC_1x3 && L.cls != RC_1x4)
			return false;
		if (L.size > sizeof(Fvector4))
			return false;
	}
	return true;
}

// Standart constant-binding
void CBlender_Compile::SetMapping()
{
	// matrices
	r_Constant("m_W", &binder_w);
	r_Constant("m_invW", &binder_invw);
	r_Constant("m_V", &binder_v);
	r_Constant("m_P", &binder_p);
	r_Constant("m_WV", &binder_wv);
	r_Constant("m_VP", &binder_vp);
	r_Constant("m_WVP", &binder_wvp);
	r_Constant("m_inv_V", &binder_inv_v);

	r_Constant("m_v_prev", &binder_v_prev);
	r_Constant("m_p_prev", &binder_p_prev);
	r_Constant("m_wv_prev", &binder_wv_prev);
	r_Constant("m_vp_prev", &binder_vp_prev);
	r_Constant("m_wvp_prev", &binder_wvp_prev);

	r_Constant("m_xform_v", &tree_binder_m_xform_v);
	r_Constant("m_xform", &tree_binder_m_xform);
	r_Constant("consts", &tree_binder_consts);
	r_Constant("wave", &tree_binder_wave);
	r_Constant("wind", &tree_binder_wind);
	r_Constant("c_scale", &tree_binder_c_scale);
	r_Constant("c_bias", &tree_binder_c_bias);
	r_Constant("c_sun", &tree_binder_c_sun);

	//hemi cube
	r_Constant("L_material", &binder_material);
	r_Constant("hemi_cube_pos_faces", &binder_hemi_cube_pos_faces);
	r_Constant("hemi_cube_neg_faces", &binder_hemi_cube_neg_faces);

	//	Igor	temp solution for the texgen functionality in the shader
	r_Constant("m_texgen", &binder_texgen);
	r_Constant("mVPTexgen", &binder_VPtexgen);

#ifndef _EDITOR
	// fog-params
	r_Constant("fog_plane", &binder_fog_plane);
	r_Constant("fog_params", &binder_fog_params);
	r_Constant("fog_color", &binder_fog_color);
	r_Constant("wind_params", &binder_wind_params);
#endif
	// time
	r_Constant("timers", &binder_times);
	r_Constant("timers_game", &binder_game_times);

	// eye-params
	r_Constant("eye_position", &binder_eye_P);
	r_Constant("eye_position_lerp", &binder_eye_PL); // crookr
	r_Constant("eye_direction", &binder_eye_D);
	r_Constant("eye_direction_lerp", &binder_eye_DL); // crookr
	r_Constant("eye_normal", &binder_eye_N);

#ifndef _EDITOR
	// global-lighting (env params)
	r_Constant("L_sun_color", &binder_sun0_color);
	r_Constant("L_sun_dir_w", &binder_sun0_dir_w);
	r_Constant("L_sun_dir_e", &binder_sun0_dir_e);
	//	r_Constant				("L_lmap_color",	&binder_lm_color);
	r_Constant("L_hemi_color", &binder_hemi_color);
	r_Constant("L_sky_color", &binder_pbr_sky_color); // IX-Ray PBR
	r_Constant("L_sky_rotation", &binder_sky_rotation); // IX-Ray PBR
	r_Constant("L_ambient", &binder_amb_color);
#endif

	r_Constant("screen_res", &binder_screen_res);
	r_Constant("ogse_c_screen", &binder_screen_params);
	r_Constant("near_far_plane", &binder_near_far_plane);
	// misc
	r_Constant("m_hud_params", &binder_hud_params);	//--#SM+#--
	r_Constant("m_hud_fov_params", &binder_hud_fov_params);
	r_Constant("m_script_params", &binder_script_params); //--#SM+#--
	r_Constant("m_blender_mode", &binder_blend_mode);	//--#SM+#--
	
	// Rain
	r_Constant("rain_params", &binder_rain_params);
	//Actor data
	r_Constant("actor_data", &binder_actor_data);
	//Image corrections
	r_Constant("pp_img_corrections", &pp_image_corrections);
	//Image corrections
	r_Constant("pp_img_cg", &pp_color_grading);
	
	// detail
	//if (bDetail	&& detail_scaler)
	//	Igor: bDetail can be overridden by no_detail_texture option.
	//	But shader can be deatiled implicitly, so try to set this parameter
	//	anyway.
	if (detail_scaler)
		r_Constant("dt_params", detail_scaler);

	// PDA
	r_Constant("pda_params", &binder_pda_params);

	// Screen Space Shaders	
	r_Constant("ssfx_floravariation", &ssfx_floravariation);
	r_Constant("ssfx_fog", &ssfx_fog);
	r_Constant("ssfx_timedelta", &ssfx_fTimeDelta);
	r_Constant("ssfx_motionblur", &ssfx_motionblur);
	r_Constant("ssfx_jitter", &ssfx_jitter);
#if defined(USE_DX11)
	r_Constant("dlss_params", &binder_dlss_params);
	r_Constant("dlss_temporal_jitter", &binder_dlss_temporal_jitter);
#endif
	r_Constant("ssfx_pom", &ssfx_pom);

	r_Constant("ssfx_terrain_pom", &ssfx_terrain_pom);
	r_Constant("ssfx_bloom_1", &ssfx_bloom_1);
	r_Constant("ssfx_bloom_2", &ssfx_bloom_2);

	r_Constant("ssfx_issvp", &ssfx_issvp);
	r_Constant("ssfx_hud_hemi", &ssfx_hud_hemi);
	r_Constant("ssfx_il_setup", &ssfx_il);
	r_Constant("ssfx_il_setup2", &ssfx_il_setup1);
	r_Constant("ssfx_ao_setup", &ssfx_ao);
	r_Constant("ssfx_ao_setup2", &ssfx_ao_setup1);
	r_Constant("ssfx_water", &ssfx_water);
	r_Constant("ssfx_water_setup1", &ssfx_water_setup1);
	r_Constant("ssfx_water_setup2", &ssfx_water_setup2);

	r_Constant("ssfx_volumetric", &ssfx_volumetric);
	r_Constant("ssfx_ssr_2", &ssfx_ssr_2);
	r_Constant("ssfx_terrain_offset", &ssfx_terrain_offset);
	r_Constant("ssfx_shadow_bias", &ssfx_shadow_bias);
	r_Constant("ssfx_wind_anim", &ssfx_wind_anim);
	r_Constant("ssfx_wind_anim_prev", &ssfx_wind_anim_prev);
	r_Constant("sky_color", &binder_sky_color);
	r_Constant("ssfx_wpn_dof_1", &ssfx_wpn_dof_1);
	r_Constant("ssfx_wpn_dof_2", &ssfx_wpn_dof_2);
	r_Constant("ssfx_blood_decals", &ssfx_blood_decals);
	r_Constant("ssfx_hud_drops_1", &ssfx_hud_drops_1);
	r_Constant("ssfx_hud_drops_2", &ssfx_hud_drops_2);
	r_Constant("ssfx_lightsetup_1", &ssfx_lightsetup_1);
	r_Constant("ssfx_is_underground", &ssfx_is_underground);
	r_Constant("ssfx_wetsurfaces_1", &ssfx_wetsurfaces_1);
	r_Constant("ssfx_wetsurfaces_2", &ssfx_wetsurfaces_2);
	r_Constant("ssfx_gloss", &ssfx_gloss);
	r_Constant("ssfx_florafixes_1", &ssfx_florafixes_1);
	r_Constant("ssfx_florafixes_2", &ssfx_florafixes_2);
	r_Constant("ssfx_wsetup_grass", &ssfx_wind_grass);
	r_Constant("ssfx_wsetup_trees", &ssfx_wind_trees);
	r_Constant("ssfx_lut", &ssfx_lut);

	// Shader stuff
	r_Constant("shader_param_1", &dev_param_1);
	r_Constant("shader_param_2", &dev_param_2);
	r_Constant("shader_param_3", &dev_param_3);
	r_Constant("shader_param_4", &dev_param_4);
	r_Constant("shader_param_5", &dev_param_5);
	r_Constant("shader_param_6", &dev_param_6);
	r_Constant("shader_param_7", &dev_param_7);
	r_Constant("shader_param_8", &dev_param_8);
	
	// Mark Switch
	r_Constant("markswitch_current", &markswitch_current);
	r_Constant("markswitch_count", &markswitch_count);
	r_Constant("markswitch_color", &markswitch_color);

	// Shader 3D Scopes
	r_Constant("s3ds_param_1", &s3ds_param_1);
	r_Constant("s3ds_param_2", &s3ds_param_2);
	r_Constant("s3ds_param_3", &s3ds_param_3);
	r_Constant("s3ds_param_4", &s3ds_param_4);

	// crookr
	r_Constant("fakescope_params1", &binder_fakescope_params);
	r_Constant("fakescope_params2", &binder_fakescope_ca);
	r_Constant("fakescope_params3", &binder_fakescope_params3);

	// other common
	for (u32 it = 0; it < DEV->v_constant_setup.size(); it++)
	{
		std::pair<shared_str, R_constant_setup*> cs = DEV->v_constant_setup[it];
		r_Constant(*cs.first, cs.second);
	}

	u32 object_lanes = 0;
	for (u32 it = 0; it < ctable.table.size(); it++)
	{
		R_constant* C = &*ctable.table[it];
		LPCSTR cname = C->name.c_str();
		if (!cname || 0 != strncmp(cname, "bus_", 4))
			continue;

		if (C->type != RC_float && C->type != RC_uint)
		{
			if (C->type == RC_int || C->type == RC_bool)
				ShaderBus::refuse(cname, "is an int or bool, a lane binds float or uint only");
			continue;
		}
		// uint lanes always take the byte copy so the raw bits arrive unchanged
		const bool fits = C->type == RC_float && bus_fits_vector(C);
		const u32 rows = bus_rows(C);

		// an obj_ lane takes its value from the drawn object
		const bool object = 0 == strncmp(cname, "bus_obj_", 8);
		if (object && !fits)
		{
			ShaderBus::refuse(cname, "is an object lane, which binds one float to float4 only");
			continue;
		}
		if (object && object_lanes == ShaderBus::max_object_lanes)
		{
			ShaderBus::refuse(cname, "is past the 16 object lanes one pass binds");
			continue;
		}
#if defined(USE_DX10) || defined(USE_DX11)
		if (rows > ShaderBus::max_rows)
		{
			ShaderBus::refuse(cname, "is larger than the 4096 rows a lane allows");
			continue;
		}
#else
		if (!fits)
		{
			ShaderBus::refuse(cname, "is an array or matrix, a lane binds one float to float4 only on this renderer");
			continue;
		}
#endif

		ShaderBus::lane* lane = ShaderBus::declare(cname, rows,
			C->type == RC_uint ? ShaderBus::kind_uint : ShaderBus::kind_float);
		if (!lane)
			continue;

		if (object)
		{
			++object_lanes;
			if (bus_object_binders.size() <= lane->index)
				bus_object_binders.resize(lane->index + 1, nullptr);
			if (!bus_object_binders[lane->index])
				bus_object_binders[lane->index] = xr_new<bus_object_binder>(lane);

			C->handler = bus_object_binders[lane->index];
			continue;
		}

#if defined(USE_DX10) || defined(USE_DX11)
		if (!fits)
		{
			if (bus_row_binders.size() <= lane->index)
				bus_row_binders.resize(lane->index + 1, nullptr);
			if (!bus_row_binders[lane->index])
				bus_row_binders[lane->index] = xr_new<bus_row_binder>(lane);

			C->handler = bus_row_binders[lane->index];
			continue;
		}
#endif

		if (bus_binders.size() <= lane->index)
			bus_binders.resize(lane->index + 1, nullptr);
		if (!bus_binders[lane->index])
			bus_binders[lane->index] = xr_new<bus_binder>(lane);

		C->handler = bus_binders[lane->index];
	}



	r_Constant("L_glowing", &binder_silencer_glowing);		//--DSR-- SilencerOverheat
	//--DSR-- HeatVision_start
	r_Constant("L_hotness", &binder_heatvision_hotness);
	r_Constant("heatvision_params1", &binder_heatvision_params1);
	r_Constant("heatvision_params2", &binder_heatvision_params2);
	r_Constant("heatvision_params3", &binder_heatvision_args1);
	r_Constant("heatvision_params4", &binder_heatvision_args2);
	//--DSR-- HeatVision_end

	r_Constant("vignette_control", &vignette_control);
}
