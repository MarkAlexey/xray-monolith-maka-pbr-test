#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace NativeCameraSweep {
using Point=std::array<float,3>;
// The movement controller already separates the actor from live characters.
// Re-solving their rounded caps as camera contacts can lift the independent eye
// while the actor stays grounded. Static geometry and ragdoll shells remain
// camera obstacles; ordinary Classic queries retain the native policy.
inline bool IgnoreCharacterControllerContact(bool referenceEyeQuery,bool characterController)
{
    return referenceEyeQuery && characterController;
}
inline float DistanceSquared(const Point& a,const Point& b)
{
    float sum=0;for(unsigned i=0;i<3;++i)sum+=(a[i]-b[i])*(a[i]-b[i]);return sum;
}
// Camera history belongs to the moving rig, not to a previous world location.
inline Point Transport(const Point& previous,const Point& oldCarrier,const Point& carrier)
{
    Point result{};for(unsigned i=0;i<3;++i)result[i]=previous[i]+carrier[i]-oldCarrier[i];return result;
}
// The current native anchor and authored eye define the rig segment. Its camera
// volume supplies the allowance; neither old correction nor actor travel may
// increase this envelope. Projection is continuous at its boundary.
inline Point ClampToRig(const Point& point,const Point& anchor,const Point& requested,float allowance)
{
    Point segment{};float length2=0,along=0;
    for(unsigned i=0;i<3;++i){segment[i]=requested[i]-anchor[i];length2+=segment[i]*segment[i];along+=(point[i]-anchor[i])*segment[i];}
    const float t=length2>1e-12f?std::max(0.f,std::min(1.f,along/length2)):0.f;
    Point center{};for(unsigned i=0;i<3;++i)center[i]=anchor[i]+segment[i]*t;
    const float distance=std::sqrt(DistanceSquared(point,center));
    const float radius=std::max(allowance,0.f);
    if(distance<=radius)return point;
    Point result{};for(unsigned i=0;i<3;++i)result[i]=center[i]+(point[i]-center[i])*(radius/distance);
    return result;
}
inline bool InRig(const Point& point,const Point& anchor,const Point& requested,float allowance)
{
    return DistanceSquared(point,ClampToRig(point,anchor,requested,allowance))<=1e-10f;
}
// Release only the previous collision translation, never the authored eye motion.
// The returned candidate MUST still pass Sweep; easing is not a safety test.
inline Point ReleaseTarget(const Point& requested,const Point& correction,float dt)
{
    const float decay=std::exp(-10.f*std::max(dt,0.f)); // native nearwall default response rate
    Point result{};for(unsigned i=0;i<3;++i)result[i]=requested[i]+correction[i]*decay;
    return result;
}
struct Result { Point position; unsigned queries=0; bool blocked=false,budgetLimited=false,anchorBlocked=false; };
// The caller establishes a collision-resolved start. Each overlap uses the same
// final camera orientation/near-plane volume and native material filtering.
template<class Overlap>
Result Sweep(const Point& start,const Point& requested,Overlap overlap,
    float spacing=.02f,unsigned maxSteps=128,unsigned refinements=8)
{
    Result result{start};
    ++result.queries;
    if(overlap(start)){result.blocked=result.anchorBlocked=true;return result;}
    Point delta{};float squared=0;
    for(unsigned i=0;i<3;++i){delta[i]=requested[i]-start[i];squared+=delta[i]*delta[i];}
    const float distance=std::sqrt(squared);
    if(distance<=1e-6f){result.position=requested;return result;}
    const unsigned needed=unsigned(std::ceil(distance/spacing));
    const unsigned steps=std::min(needed,maxSteps);
    const auto at=[&](float fraction){Point p;for(unsigned i=0;i<3;++i)p[i]=start[i]+delta[i]*fraction;return p;};
    float safe=0;
    for(unsigned step=1;step<=steps;++step){
        const float fraction=std::min(1.f,float(step)*spacing/distance);
        const auto candidate=at(fraction);++result.queries;
        if(overlap(candidate)){
            result.blocked=true;float low=safe,high=fraction;
            for(unsigned n=0;n<refinements;++n){const float middle=(low+high)*.5f;++result.queries;
                if(overlap(at(middle)))high=middle;else low=middle;}
            result.position=at(low);return result;
        }
        safe=fraction;result.position=candidate;
    }
    result.budgetLimited=needed>maxSteps;
    // Never teleport the remaining segment if the bounded query budget expires.
    return result;
}
}
