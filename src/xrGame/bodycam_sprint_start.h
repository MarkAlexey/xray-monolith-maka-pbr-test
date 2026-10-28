#pragma once
// Classic movement presentation decisions, free of engine types: the sprint
// start (from sprint press until full sprint speed), run-loop pacing, the held
// sprint lowering, and the braking stride. The engine side supplies plain
// inputs and implements SprintStartHost; see bodycam_actor_movement.cpp.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Bodycam
{
// Below this planar speed (m/s) the body has effectively stopped.
constexpr float kBrakeEndSpeed = 0.7f;
// Braking from above this share of run speed keeps the run leg cycle.
constexpr float kBrakeRunShare = 0.6f;
// Share of sprint speed (actual and inertia-filtered) that counts as full sprint.
constexpr float kSprintFullShare = 0.90f;
// Sprint progress needs some range even when sprint starts near full speed.
constexpr float kSprintMinRange = 0.05f;
// Scripts get this long after sprint press to request a lowering before the
// weapon's own entry clip is tried.
constexpr float kSprintEntryDecideSeconds = 0.10f;
constexpr std::uint16_t kNoWeapon = std::uint16_t(-1);

// After movement input ends, inertia still carries the body. The legs keep
// their locomotion cycle until it slows, so the native step schedule plays
// the footsteps.
enum class BrakeDirection : std::uint8_t
{
	Forward,
	Back,
	Left,
	Right
};

struct BrakingStride
{
	bool active = false;
	bool run = false;
	BrakeDirection dir = BrakeDirection::Forward;
};

struct BrakingInput
{
	bool moving = false; // Movement input this frame.
	bool was_moving = false; // Movement input last frame.
	bool airborne_or_climbing = false;
	bool grounded = true;
	float speed = 0.f; // Planar speed, m/s.
	float forward = 0.f; // Planar velocity along the body's forward axis.
	float right = 0.f;
	float run_speed = 0.f; // Current native run speed, m/s.
};

inline void UpdateBrakingStride(BrakingStride& stride, const BrakingInput& in)
{
	const bool blocked = in.moving || in.airborne_or_climbing || !in.grounded || in.speed < kBrakeEndSpeed;
	// Starts only when movement input ends; being pushed never starts it.
	const bool starts = in.was_moving && !in.moving;
	if (blocked || !(stride.active || starts))
	{
		stride.active = false;
		return;
	}
	if (!stride.active)
	{
		// Direction and gait are fixed for the whole braking stride.
		if (std::fabs(in.forward) >= std::fabs(in.right))
			stride.dir = in.forward >= 0.f ? BrakeDirection::Forward : BrakeDirection::Back;
		else
			stride.dir = in.right < 0.f ? BrakeDirection::Left : BrakeDirection::Right;
		stride.run = in.speed > kBrakeRunShare * in.run_speed;
	}
	stride.active = true;
}

enum class SprintEntryDrive
{
	Hold, // Keep the entry clip at this progress.
	Finish, // Full sprint reached: play out the rest at the authored rate.
	Cancel // Sprint ended early: end the clip so the idle selection runs.
};

// What the sprint start needs from the engine. Calls happen in the order the
// presentation depends on; an implementation must not reorder them.
struct SprintStartHost
{
	virtual ~SprintStartHost() = default;
	// 0 at run speed, 1 at sprint speed, from the actual velocity and the
	// current gait speeds. Read at call time: scripts may change the speeds.
	virtual double ActualSprintProgress() = 0;
	// Scripts hear about the sprint at the press, as they natively would.
	virtual void NotifyScriptsOfSprint() = 0;
	// Request the weapon's authored run-to-sprint clip, if it has one; true
	// when one plays.
	virtual bool PlayEntryClip() = 0;
	virtual void DriveEntryClip(float progress, SprintEntryDrive drive) = 0;
};

struct SprintStart
{
	bool full = false; // Actual and inertia-filtered speed both reached sprint; latched until sprint ends.
	bool starting = false; // Inside the start window this frame.
	bool entry_decided = false; // The authored entry clip was requested or ruled out.
	bool entry_clip = false; // This start plays the weapon's authored entry clip.
	std::uint16_t entry_weapon = kNoWeapon; // The weapon the entry decision belongs to.
	float entry_timer = 0.f;
	float from = 0.f; // Speed fraction when sprint was pressed.
	float progress = 0.f; // 0 at sprint press, 1 at full sprint.
	float run_stride = 0.f; // Stride time of the run leg cycle, the cadence authored run loops match.
	std::uint16_t lower_queued = kNoWeapon; // Weapon whose script lowering waits for full sprint.
	// The sprint HUD clip, from ResolveSprintHudState.
	bool anim_ready = true;
	bool hud_active = false;
	bool hud_changed = false; // The HUD was notified this frame.
};

struct SprintStartInput
{
	bool sprinting = false;
	bool owned = false; // The sprint transition is enabled for the equipped weapon.
	bool aiming = false;
	float speed_fraction = 0.f; // Inertia-filtered speed over its target.
	float dt = 0.f;
	std::uint16_t weapon = kNoWeapon; // Equipped item id.
};

inline void UpdateSprintStart(SprintStart& state, const SprintStartInput& in, SprintStartHost& host)
{
	// Full sprint needs both the actual speed (blocked movement never
	// qualifies) and the inertia-filtered speed (Acceleration Time). Latched
	// until sprint ends, so speed noise cannot restart the start.
	if (!in.sprinting)
		state.full = false;
	else if (!state.full)
		state.full = in.speed_fraction >= kSprintFullShare && host.ActualSprintProgress() >= kSprintFullShare;

	const bool start = in.sprinting && in.owned && !state.full && !in.aiming;
	if (start)
	{
		if (!state.starting)
		{
			state.from = std::min(in.speed_fraction, kSprintFullShare - kSprintMinRange);
			state.entry_timer = 0.f;
			state.entry_decided = false;
			state.entry_clip = false;
			state.entry_weapon = in.weapon;
			// GAMMA's sprint lowering then asks early and is held.
			host.NotifyScriptsOfSprint();
		}
		// A weapon switched in during the start gets its own entry decision.
		if (in.weapon != state.entry_weapon)
		{
			state.entry_timer = 0.f;
			state.entry_decided = false;
			state.entry_clip = false;
			state.entry_weapon = in.weapon;
		}
		// The slower of the two conditions of full sprint, read after the
		// script notification.
		state.progress = std::min(std::clamp((in.speed_fraction - state.from) / (kSprintFullShare - state.from), 0.f, 1.f),
			std::clamp(float(host.ActualSprintProgress()) / kSprintFullShare, 0.f, 1.f));
		state.entry_timer += in.dt;
		if (!state.entry_decided && state.entry_timer >= kSprintEntryDecideSeconds)
		{
			state.entry_decided = true;
			// Not lowered by a script: use the weapon's own transition clip.
			if (state.lower_queued == kNoWeapon)
				state.entry_clip = host.PlayEntryClip();
		}
		host.DriveEntryClip(state.progress, SprintEntryDrive::Hold);
	}
	else
	{
		const bool reached = in.sprinting && state.full;
		if (state.starting)
			host.DriveEntryClip(1.f, reached ? SprintEntryDrive::Finish : SprintEntryDrive::Cancel);
		state.progress = reached ? 1.f : 0.f;
	}
	state.starting = start;
}

// Whether the sprint HUD clip may play, and whether the HUD has to be told.
struct SprintHudState
{
	bool active = false; // The HUD shows the sprint clip.
	bool ready = true; // The sprint HUD clip may play.
	bool notify = false; // The HUD has to restart its clip selection.
};

// full_speed: the sprint start is over (SprintStart::full). Until then the
// authored run presentation continues.
inline SprintHudState ResolveSprintHudState(
	bool current_active, bool raw_sprint, bool transition_owned, bool ads, bool moving, bool full_speed)
{
	SprintHudState state;
	state.ready = !transition_owned || !raw_sprint || (!ads && full_speed);
	state.active = raw_sprint && state.ready;
	// A full stop already sends the generic movement callback, which restarts
	// the HUD clip selection; a second notification would replay the clip.
	state.notify = state.active != current_active && (state.active || moving);
	return state;
}

// Playback rate for the authored run loop. During the sprint start the loop
// still plays while the legs already step at sprint cadence, so it is paced to
// the real footsteps; outside that window it is 1 (authored timing).
struct RunPaceInput
{
	float stride = 0.f; // Current leg stride time, seconds; 0 if unknown.
	bool running = false; // Moving at the run gait: not sprinting, not crouched.
	bool sprinting = false;
	bool owned = false;
};

inline float UpdateRunLoopPace(SprintStart& state, const RunPaceInput& in)
{
	if (in.stride > 0.f && in.running)
		state.run_stride = in.stride;
	const bool starting = in.sprinting && in.owned && !state.anim_ready;
	return starting && in.stride > 0.f && state.run_stride > 0.f ? std::clamp(state.run_stride / in.stride, 1.f, 2.f) : 1.f;
}

// A script lowering requested during the sprint start waits for full sprint.
struct SprintLoweringInput
{
	bool sprinting = false;
	bool owned = false;
	bool lowered = false; // The weapon is already lowered.
	bool can_lower = false; // A weapon is equipped and can be lowered.
	bool pending = false; // An action holds the weapon.
	std::uint16_t weapon = kNoWeapon; // Equipped weapon id.
};

// True when the request is held (the caller must not lower now).
inline bool DeferSprintLowering(SprintStart& state, const SprintLoweringInput& in)
{
	if (!in.can_lower || in.lowered || !in.owned || !in.sprinting || state.full)
		return false;
	state.lower_queued = in.weapon;
	return true;
}

// True when the held request must be carried out now.
inline bool CommitSprintLowering(SprintStart& state, const SprintLoweringInput& in)
{
	if (state.lower_queued == kNoWeapon)
		return false;
	// Sprint ended, weapon changed or already lowered: the request is void.
	if (!in.sprinting || in.weapon != state.lower_queued || in.lowered || !in.owned)
	{
		state.lower_queued = kNoWeapon;
		return false;
	}
	// Wait for full sprint speed and for any action holding the weapon.
	if (!state.full || in.pending)
		return false;
	state.lower_queued = kNoWeapon;
	return true;
}

inline void ResetSprintStart(SprintStart& state)
{
	// run_stride is a measurement of the run cycle, not per-sprint state.
	const float run_stride = state.run_stride;
	state = SprintStart{};
	state.run_stride = run_stride;
}
} // namespace Bodycam
