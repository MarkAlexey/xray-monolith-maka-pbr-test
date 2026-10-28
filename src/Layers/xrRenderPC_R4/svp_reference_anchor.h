#pragma once
#include <cmath>

namespace SvpReferenceAnchor
{
inline bool FreshOcular(unsigned lensFrame,unsigned renderFrame,bool hasRoot,bool hasVisual)
{return lensFrame==renderFrame&&hasRoot&&hasVisual;}
// Expresses the weapon's aimed eye position in the ocular frame (right, up,
// forward; metres). Fails unless the frame is orthonormal and the eye is behind the ocular.
template<class Vector>
bool Resolve(const Vector& authoredEyeWorld,const Vector& ocularCenter,
    const Vector& right,const Vector& up,const Vector& forward,float radius,Vector& result)
{
    const auto finite=[](const Vector& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
    if(!finite(authoredEyeWorld)||!finite(ocularCenter)||!finite(right)||!finite(up)||!finite(forward)||
        !std::isfinite(radius)||radius<=1e-6f)return false;
    // The shader scales by radius alone, which holds for a rigid lens only.
    if(std::abs(right.dotproduct(right)-1.f)>.001f||std::abs(up.dotproduct(up)-1.f)>.001f||
        std::abs(forward.dotproduct(forward)-1.f)>.001f||std::abs(right.dotproduct(up))>.001f||
        std::abs(right.dotproduct(forward))>.001f||std::abs(up.dotproduct(forward))>.001f)return false;
    Vector delta;delta.sub(authoredEyeWorld,ocularCenter);
    result.set(delta.dotproduct(right),delta.dotproduct(up),delta.dotproduct(forward));
    return finite(result)&&result.z < -1e-6f;
}
}
