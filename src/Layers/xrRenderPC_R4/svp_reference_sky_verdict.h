#pragma once
#include <array>

// Whether the Reference sky capture reproduces the installed sky (svp_reference_sky.inl). The
// capture is admitted only once a compared frame has passed; a wrapper that does not compile, or
// FailuresToReject failing frames in a row, reject it for the rest of the session.
class SvpReferenceSkyVerdict
{
public:
    static constexpr unsigned FailuresToReject = 3;

    bool verified() const { return state == State::Verified; }
    bool rejected() const { return state == State::Rejected; }

    // Records one compared frame; true when FailuresToReject frames in a row have failed and the
    // caller should reject the capture.
    bool sample(bool passed)
    {
        if (rejected()) return false;
        if (passed)
        {
            failures = 0;
            state = State::Verified;
            return false;
        }
        return ++failures >= FailuresToReject;
    }

    // True when this call rejected the capture.
    bool reject()
    {
        if (rejected()) return false;
        state = State::Rejected;
        return true;
    }

private:
    enum class State { Unverified, Verified, Rejected };
    State state = State::Unverified;
    unsigned failures = 0;
};

// Comparison results read back without waiting: a ring of Count copies, each free or submitted and
// not yet read. A slot is never overwritten before it is read.
template <unsigned Count>
class SvpReferenceSkyReadback
{
public:
    enum class Read { Ready, Pending, Failed };

    // The slot to copy this frame's result into, or -1 when every slot still waits for its read.
    int acquire() const { return pending[next] ? -1 : int(next); }
    void submitted()
    {
        pending[next] = true;
        next = (next + 1) % Count;
    }

    // Reads the submitted slots oldest first and stops at the first one still in flight.
    // read(slot, passed) maps a slot without waiting; result(passed) receives each completed one.
    // A Failed read drops its sample.
    template <typename ReadSlot, typename Result>
    void poll(ReadSlot&& read, Result&& result)
    {
        for (unsigned n = 0; n < Count; ++n)
        {
            const unsigned slot = (next + n) % Count;
            if (!pending[slot]) continue;
            bool passed = false;
            const Read status = read(slot, passed);
            if (status == Read::Pending) return;
            pending[slot] = false;
            if (status == Read::Ready) result(passed);
        }
    }

private:
    std::array<bool, Count> pending{};
    unsigned next = 0;
};
