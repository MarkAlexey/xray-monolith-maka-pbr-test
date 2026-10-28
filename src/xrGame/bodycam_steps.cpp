// Stride queries and locomotion-phase synchronisation on the native step
// manager: the footstep clock bodycam presentation follows.
#include "stdafx.h"
#include "step_manager.h"
#include "bodycam_reference/gait_phase.h"
#include "../Include/xrRender/KinematicsAnimated.h"

bool CStepManager::has_stride() const
{
	return !m_step_info.disable && m_blend && !m_blend->stop_at_end && m_legs_count == 2 && m_step_info.params.cycles &&
		m_blend->timeTotal > 0;
}

float CStepManager::stride_time() const
{
	if (!has_stride() || m_blend->speed <= 0)
		return 0.f;
	// Same arithmetic update() uses to schedule each step sound.
	return m_blend->timeTotal / m_blend->speed / m_step_info.params.cycles;
}

bool CStepManager::foot_phase(float& phase) const
{
	using namespace BodycamReference;
	const float cycle = stride_time();
	const auto& params = m_step_info.params;
	const float left = params.step[0].time, right = params.step[1].time;
	if (cycle <= 0.f || left == right)
		return false;
	const float elapsed = float(s32(Device.dwTimeGlobal - m_time_anim_started)) / 1000.f;
	phase = FootPhase(WrapFootPhase(elapsed / cycle), left, right);
	return true;
}

bool CStepManager::synchronize_locomotion(float phase)
{
	using namespace BodycamReference;
	if (!has_stride() || !std::isfinite(phase))
		return false;
	const auto& params = m_step_info.params;
	const float left = params.step[0].time, right = params.step[1].time;
	if (left == right)
		return false;
	if (m_reference_phase_valid && m_reference_phase_frame == Device.dwFrame)
		return true;
	phase = WrapFootPhase(phase);
	const float leg = LegPhase(phase, left, right);
	const bool continuous = m_reference_phase_valid && m_reference_phase_frame + 1 == Device.dwFrame;
	m_reference_contacts = 0;
	if (continuous)
	{
		if (FootContactCrossed(m_reference_foot_phase, phase, 0))
			m_reference_contacts |= 1;
		if (FootContactCrossed(m_reference_foot_phase, phase, .5f))
			m_reference_contacts |= 2;
		if (leg < m_reference_leg_phase && WrapFootPhase(leg - m_reference_leg_phase) <= .5f)
			m_reference_cycle = u8((m_reference_cycle + 1) % params.cycles);
	}
	else
		m_reference_cycle = 0;
	m_reference_foot_phase = phase;
	m_reference_leg_phase = leg;
	m_reference_phase_frame = Device.dwFrame;
	m_reference_phase_valid = true;
	const float clip = (float(m_reference_cycle) + leg) / params.cycles;
	m_blend->timeCurrent = clip * m_blend->timeTotal;
	// update() schedules the step sounds from m_time_anim_started. Keeping it
	// at this phase lets that schedule take over on the frame synchronisation stops.
	if (m_blend->speed > 0)
		m_time_anim_started = Device.dwTimeGlobal - u32(1000 * clip * get_blend_time());
	m_step_info.cur_cycle = u8(m_reference_cycle + 1);
	for (u32 i = 0; i < m_legs_count; ++i)
	{
		m_step_info.activity[i].handled = params.step[i].time <= leg;
		m_step_info.activity[i].cycle = m_step_info.cur_cycle;
	}
	return true;
}
