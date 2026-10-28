#include "bodycam_pip_adapter.h"

namespace Bodycam
{
PipView PipAdapter::Update(const PipInput& input, const PipRuntimeState& runtime)
{
	const bool eligible = runtime.true_pip_enabled && input.weapon_zoomed && input.primary_sight;
	if (!eligible)
		m_session_active = false;
	else if (runtime.geometry_ready || runtime.viewport_active)
		m_session_active = true;

	PipView view;
	view.active = m_session_active;
	view.freeze_world_pose = m_session_active && !runtime.world_camera_effects;
	view.main_fov = m_session_active ? runtime.main_fov : input.requested_fov;
	return view;
}
} // namespace Bodycam
