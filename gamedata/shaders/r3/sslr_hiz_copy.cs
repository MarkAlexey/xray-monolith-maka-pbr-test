//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray SSLR, Hi-Z pyramid level 0 (Stachowiak 2015, "Stochastic Screen-Space
// Reflections"; IX-Ray develop 454d9ecd)
//
// Output: u_hiz = -1 / view depth. Unlike the hardware depth IX-Ray stores, -1/z is linear along
// a screen-space ray (it is what sslr_render.cs interpolates) and grows with distance, so the
// min-reduction keeps the nearest surface of each cell. Sky and first-person (HUD) pixels get 0,
// the farthest value: world rays pass behind the weapon and through the sky, as with the
// linear march, which skips those samples.
//////////////////////////////////////////////////////////////////////////////////////////

#include "sslr_common.h"

RWTexture2D<float> u_hiz : register( u0 );

[numthreads( 8, 8, 1 )]
void main( uint3 DTid : SV_DispatchThreadID )
{
	if( any( float2( DTid.xy ) >= sslr_screen.xy ) )
		return;

	float z = s_position.Load( int3( DTid.xy, 0 ) ).z;
	u_hiz[DTid.xy] = ( sslr_is_sky( z ) || sslr_is_hud( z ) ) ? 0.0 : -rcp( z );
}
