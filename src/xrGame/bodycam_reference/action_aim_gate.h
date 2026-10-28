#pragma once
namespace BodycamReference
{
// Semantic actions own animation until their active/retiring blend finishes.
// Hold ADS remembers intent; toggle ADS retains its native press/release policy.
class ActionAimGate
{
	bool releaseRequired_ = false;
	bool heldResumePending_ = false;

public:
	void Cancel(bool aiming)
	{
		releaseRequired_ = releaseRequired_ || aiming;
		heldResumePending_ = heldResumePending_ || aiming;
	}
	void Release() { releaseRequired_ = heldResumePending_ = false; }
	bool RejectStart(bool referenceOwned, bool semanticBusy, bool preserveAim = false, bool toggleAim = false)
	{
		if (!referenceOwned)
		{
			Release();
			return false;
		}
		if (!toggleAim)
		{
			releaseRequired_ = false;
			if (semanticBusy)
				heldResumePending_ = true;
			return semanticBusy;
		}
		heldResumePending_ = false;
		if (preserveAim)
		{
			releaseRequired_ = false;
			return semanticBusy;
		}
		if (semanticBusy)
			releaseRequired_ = true;
		return releaseRequired_;
	}
	bool ResumePending() const { return heldResumePending_; }
	bool ConsumeHeldResume(bool eligible, bool keyDown, bool semanticBusy, bool enginePending, bool aiming, bool toggleAim)
	{
		if (!eligible || !keyDown || toggleAim)
		{
			Release();
			return false;
		}
		if (!heldResumePending_ || semanticBusy || enginePending)
			return false;
		Release();
		return !aiming;
	}
};
}
