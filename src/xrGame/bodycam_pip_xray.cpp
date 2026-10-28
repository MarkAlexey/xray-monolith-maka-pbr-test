#include "stdafx.h"
#include "bodycam_pip_adapter.h"

extern float g_fov;
extern int g_svp_world_cam_fx;

namespace Bodycam
{
PipRuntimeState ReadPipRuntime()
{
	auto& viewport = Device.m_SecondViewport;
	PipRuntimeState runtime;
	runtime.true_pip_enabled = Device.true_pip_on;
	runtime.geometry_ready = viewport.isCamReady;
	runtime.viewport_active = viewport.IsSVPActive();
	runtime.world_camera_effects = !!g_svp_world_cam_fx;
	runtime.main_fov = g_fov;
	return runtime;
}
} // namespace Bodycam
