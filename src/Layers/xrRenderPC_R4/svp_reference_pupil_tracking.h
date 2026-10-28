#pragma once
#include "svp_physical_optics.h"
#include <cmath>
#include <cstdint>
namespace SvpReferencePupil {
struct Identity {
    std::uint32_t session=0, route=0, optic=0, camera=0;
    bool operator==(const Identity& other) const { return session==other.session && route==other.route && optic==other.optic && camera==other.camera; }
};
struct Result { float x=0, y=0; bool active=false; };
struct Tracker {
    SvpPhysicalOptics::EyeTrackingState eye{};
    Identity identity{};
    Result cached{};
    std::uint32_t frame=0, clockMs=0;
    bool valid=false;
    void Reset() { *this=Tracker{}; }
    Result Update(bool enabled, bool admitted, Identity key, std::uint32_t now,
        float errorX, float errorY, float axialMetres, float reliefMm,
        float limitMm, float speed, float acceleration, bool suspended, std::uint32_t nowMs) {
        if(!enabled || !admitted || !std::isfinite(errorX) || !std::isfinite(errorY)
            || !std::isfinite(axialMetres) || axialMetres<=1e-6f
            || !std::isfinite(reliefMm) || reliefMm<=.01f
            || !std::isfinite(limitMm) || limitMm<0 || !std::isfinite(speed) || speed<0
            || !std::isfinite(acceleration) || acceleration<0) {
            Reset(); return {};
        }
        if(valid && !(identity==key)) Reset();
        if(valid && frame==now) return cached;
        // Frames can be skipped between captures, so step by elapsed game time.
        const float dt=valid?float(nowMs-clockMs)*.001f:0.f;
        identity=key;frame=now;clockMs=nowMs;valid=true;
        // The tracker works in millimetres on the eye-relief plane; the result
        // is its residual, back in metres at the ocular.
        const float toPlane=reliefMm/axialMetres;
        const SvpPhysicalOptics::Vec2 raw={errorX*toPlane,errorY*toPlane};
        const auto target=SvpPhysicalOptics::LimitEyeOffset(raw,limitMm);
        SvpPhysicalOptics::UpdateEyeTracking(eye,target,suspended,key.camera,now,dt,speed,acceleration);
        cached={(raw.x-eye.offset.x)/toPlane,(raw.y-eye.offset.y)/toPlane,true};
        return cached;
    }
    // Hands a tracked eye over to the current eye: the follower returns to the
    // current eye at its own speed and acceleration, then the tracker resets.
    Result Release(bool admitted, Identity key, std::uint32_t now, float errorX, float errorY,
        float axialMetres, float reliefMm, float speed, float acceleration, std::uint32_t nowMs) {
        if(!valid || !admitted || !(identity==key) || !std::isfinite(errorX) || !std::isfinite(errorY)
            || !std::isfinite(axialMetres) || axialMetres<=1e-6f
            || !std::isfinite(reliefMm) || reliefMm<=.01f || !std::isfinite(speed) || speed<0
            || !std::isfinite(acceleration) || acceleration<0) {
            Reset(); return {};
        }
        if(frame==now) return cached;
        const float dt=float(nowMs-clockMs)*.001f;
        frame=now;clockMs=nowMs;
        const float toPlane=reliefMm/axialMetres;
        const SvpPhysicalOptics::Vec2 raw={errorX*toPlane,errorY*toPlane};
        SvpPhysicalOptics::UpdateEyeTracking(eye,{},false,key.camera,now,dt,speed,acceleration);
        // Within a micrometre of the current eye the hand-over is complete.
        if(eye.offset.x*eye.offset.x+eye.offset.y*eye.offset.y<=1e-6f) { Reset(); return {}; }
        cached={(raw.x-eye.offset.x)/toPlane,(raw.y-eye.offset.y)/toPlane,true};
        return cached;
    }
};
// The scope eye of the published optics frame. tracked: the eye follows the
// tracked pupil (Classic, deadzone lag). current: the current eye (Classic,
// follow lag); a tracked eye is released to it gradually. Neither: Reference.
struct Eye {
    Tracker tracker{};
    // The current eye was shown for this optic: tracking taken up again starts
    // from it instead of re-centring.
    Identity shownIdentity{};
    bool shownCurrent=false;
    // Once per frame, before the draws: what the packet allows.
    void Refresh(bool packetRead, bool tracked, bool current, bool enabled, bool svpActive) {
        if(!packetRead || (!tracked && !current) || !enabled || !svpActive) { tracker.Reset(); shownCurrent=false; }
    }
    Result Update(bool tracked, bool current, bool admitted, Identity key, std::uint32_t now,
        float errorX, float errorY, float axialMetres, float reliefMm,
        float limitMm, float speed, float acceleration, bool suspended, std::uint32_t nowMs) {
        bool seed=false;
        if(!tracked) { shownCurrent=current; shownIdentity=key; }
        else { seed=!tracker.valid && shownCurrent && shownIdentity==key; shownCurrent=false; }
        if(current)
            return tracker.Release(admitted,key,now,errorX,errorY,axialMetres,reliefMm,speed,acceleration,nowMs);
        return tracker.Update(tracked,admitted,key,now,errorX,errorY,axialMetres,reliefMm,
            limitMm,speed,acceleration,suspended||seed,nowMs);
    }
};
}
