#pragma once
#include <array>
#include <cstdint>
namespace PeakVolumetricDebug {
enum class Skip : unsigned { NotVolumetric, AdvancedPpOff, VolumetricOff, OtherMode,
    ScopeCone, NullLight, DistanceFade, DisabledInAccumulator, NoDraw, Count };
struct View {
    std::uint32_t candidates=0, entries=0, submissions=0;
    std::array<std::uint32_t,unsigned(Skip::Count)> skipped{};
    std::uint32_t Skipped() const { std::uint32_t n=0;for(auto v:skipped)n+=v;return n; }
};
struct State {
    std::uint32_t frame=0;
    bool valid=false,dx11Seen=false;
    std::array<View,2> views{};
    void Begin(std::uint32_t next) {
        if(!valid||frame!=next){frame=next;valid=true;dx11Seen=false;views={};}
    }
    View& For(std::uint32_t next,bool scope) { Begin(next);return views[scope?1:0]; }
};
inline const char* Reason(Skip reason) {
    switch(reason){
    case Skip::NotVolumetric:return "not marked volumetric";
    case Skip::AdvancedPpOff:return "advanced PP disabled";
    case Skip::VolumetricOff:return "volumetric lights disabled";
    case Skip::OtherMode:return "mode is not Peak";
    case Skip::ScopeCone:return "outside PiP light cone";
    case Skip::NullLight:return "null light";
    case Skip::DistanceFade:return "distance fade reached zero";
    case Skip::DisabledInAccumulator:return "volumetric flag cleared before draw";
    case Skip::NoDraw:return "draw_volume submitted no geometry";
    default:return "unknown";
    }
}
inline const char* Status(int mode,std::uint32_t submissions) {
    return submissions?"Peak draw submitted":mode==1?"Selected, no Peak draws this frame":"Peak not selected; no Peak draws this frame";
}
}
extern ENGINE_API PeakVolumetricDebug::State g_peak_volumetric_debug;
extern ECORE_API int ps_pfx_volumetric_debug;
extern ECORE_API int ps_pfx_volumetric_mode;
