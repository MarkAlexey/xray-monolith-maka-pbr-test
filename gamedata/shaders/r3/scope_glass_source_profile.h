// Rear glass surface tilt against normalised radius: radians, outward-positive.
#ifndef SCOPE_GLASS_SOURCE_PROFILE_H
#define SCOPE_GLASS_SOURCE_PROFILE_H
float scope_glass_source_rear_tilt(float radius)
{
 float r=saturate(radius);
 if(r <= 0.195090280) return lerp(0.000000000, 0.037385649, (r - 0.000000000) / 0.195090280);
 if(r <= 0.382683441) return lerp(0.037385649, 0.077356680, (r - 0.195090280) / 0.187593161);
 if(r <= 0.555570106) return lerp(0.077356680, 0.123325948, (r - 0.382683441) / 0.172886665);
 if(r <= 0.707106841) return lerp(0.123325948, 0.183581928, (r - 0.555570106) / 0.151536735);
 if(r <= 0.831469566) return lerp(0.183581928, 0.276668380, (r - 0.707106841) / 0.124362725);
 if(r <= 0.923879577) return lerp(0.276668380, 0.440395689, (r - 0.831469566) / 0.092410011);
 if(r <= 0.980785300) return lerp(0.440395689, 0.539998132, (r - 0.923879577) / 0.056905723);
 return 1.074155193;
}
#endif
