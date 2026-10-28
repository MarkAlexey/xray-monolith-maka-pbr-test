// The lens owner's lighting from its ROS: hemisphere and sun visibility, as the engine samples them.
#ifndef SVP_LENS_OBJECT_LIGHT_H
#define SVP_LENS_OBJECT_LIGHT_H
uniform float4 svp_lens_object_light; // x raw hemi, y raw sun visibility, z owner has ROS, w reserved

float svp_optic_hemi_visibility()
{
	return saturate(svp_lens_object_light.x / 0.75);
}

float svp_optic_sun_visibility()
{
	return saturate(svp_lens_object_light.y / 0.75);
}
#endif
