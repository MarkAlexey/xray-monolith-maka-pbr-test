#ifndef HUD_BLOOD_H
#define HUD_BLOOD_H

//////////////////////////////////////////////////////////////////////////////////////////
// HUD blood (experiment): blood on the player's hands and weapons
//
// Called by SSS's first-person G-buffer shaders (deffer_base_hud_bump.ps,
// deffer_base_hud_aref_bump.ps), right before pack_gbuffer(), through the patch that
// tools/make_hud_blood_overlay.py applies:
//
//     hud_blood_apply( S, pbr, Ne, I.position.xyz, I.RDrops.xyz, I.M1, I.M2, I.M3, hb_pos.xy );
//
// (hb_pos = the pixel's SV_Position, which the patch adds to main())
//
// How it works
//  - Where: stamps from a texture atlas (spray / flow flipbooks, drops, smears, a teardrop,
//    see tools/make_blood_atlas.py), placed on a grid in the model's own space. SSS passes the
//    bind-pose (before skinning) position for its rain drops (I.RDrops.xyz), so the stamps
//    stick to fingers and moving parts. Each grid cell has a fixed random threshold: it shows
//    a stamp once the blood amount passes it, so stains grow, shrink and come back after a
//    load exactly the same way. No mask texture per model is needed.
//  - Projection: the axis the surface faces most (object space, worked out from the
//    position's screen derivatives), with a second axis blended in near 45 degrees.
//  - Flipbooks: spray stamps run from dense (fresh, much blood above their threshold) to
//    sparse (dry, or just above it), so a stain breaks up as it dries and as the amount falls
//    (flaking, rain, a wipe).
//    Flow stamps (drips) grow with the drip value and only appear on surfaces the down axis
//    lies in; they are never rotated.
//  - Colour: blood is a film that absorbs light (Beer-Lambert): transmittance
//    T = exp(-k * d), k per colour channel (fresh blood absorbs green and blue ~10x more than
//    red), d = thickness. The surface under it is seen through the film (base * T) and blends
//    towards the colour of thick blood by the film's opacity (1 - T.g):
//        base = lerp( base * T, thick_colour, 1 - T.g )
//    so thin blood tints cloth pink-red, thick blood is deep red on anything, blood on black
//    steel stays dark. Thickness = the stamp's mask times a blurred (lower mip) read of it:
//    thick in the middle, thin at the edges. Drying moves k and the thick colour to dried
//    blood (brown, absorbs red too); rain dilution thins the film (d * (1 - dilution)).
//    T is taken to gamma space (pow 1/2.2, a setting) since the G-buffer albedo is gamma.
//  - Surface: metalness fades with the film's opacity (thin blood on steel keeps a red-tinted
//    metal reflection), roughness wet -> dry, wet blood flattens the original normal, the
//    stamps' own normal on top; thin parts dry first.
//  - Metal under blood is dithered (LV's idea, row 11.w): a pixel is either metal or not, by a
//    4x4 Bayer threshold, instead of an in-between metalness that no real surface has. TAA /
//    DLSS / FSR average the pattern into a proper mix of steel and blood; without temporal AA
//    it can show as a fine 4x4 pattern on thin blood over metal.
//
// Data, all from the shader bus (docs/shader_bus.md), written by gamedata/scripts/hud_blood.script:
//   bus_hud_blood[]  rows below           $user$bus_hud_blood_mask / _na / _nb  the atlas
//   row 0  x spatter amount  y smear amount  z wetness (1 fresh, 0 dry)  w drip
//   row 1  x dilution  y seed  z debug view (1 coverage, 2 axes, 3 dryness, 4 grid)  w normal strength
//   row 2  x spatter cell (model units)  y smear cell  z max mip  w uv padding
//   row 3  rgb thick fresh blood colour, w wet roughness
//   row 4  rgb thick dried blood colour, w dry roughness
//   row 5  x spray tiles  y flow tiles  z drop tiles  w smear tiles
//   row 6  x drip tiles  y drip share  z drop share  w breakup band
//   row 7  xyz down axis (model space)  w edge drying
//   row 8  x atlas size in pixels  y projection normal (1 smooth, 0 per triangle)  z axis sharpness
//          w thickness blur (mip levels above the stamp's)
//   row 9  xyz absorption k of fresh blood, w thickness scale (depth of a full mask)
//   row 10 xyz absorption k of dried blood, w gamma of the G-buffer albedo (1 = linear)
//   row 11 x projection blend (0 weighted, 1 union)  y union fade start  z union fade end
//          w metal dither under blood (1 on)
//   row 12+ tiles u0 v0 u1 v1, in the order spray, flow, drops, smears, drip
//
// Cost: nothing while both amounts are 0 (one uniform branch). With blood: per projection
// axis 2 layers x 4 cells, 3 texture reads per cell that has a stamp under the pixel.
//
// Caveats
//  - Needs an exe with the shader bus 7+ AND the blender change that binds the three atlas
//    textures on HUD models (blender_deffer_model.cpp / Blender_Model_EbB.cpp, HUD branch).
//    Without the binding the atlas reads 0 and nothing shows; without the bus it compiles out.
//  - Projection axis (row 8.y): smooth normal (default) has no triangle seams but follows the
//    animation on bending parts; per triangle is fixed but steps at triangle edges.
//  - One wetness for all blood: a new hit makes old dried blood look fresh again.
//  - Flipbook frames step (no blending between frames), so a stain changes frame in small jumps.
//  - Only the bump HUD shaders are patched; HUD models with the flat shader get no blood.
//  - The model's down axis is a setting (row 7), the same for every HUD model.
//////////////////////////////////////////////////////////////////////////////////////////

#if defined(SHADER_BUS) && SHADER_BUS >= 7

#define HB_ROWS			72
#define HB_TILES		12
#define HB_PI2			6.2831853

uniform float4	bus_hud_blood[HB_ROWS];
Texture2D		s_hud_blood_mask;	// r spray, g flow, b drops / smears / drip
Texture2D		s_hud_blood_na;		// rg spray normal xy, ba flow normal xy (DirectX: +y = +v)
Texture2D		s_hud_blood_nb;		// rg drops / smears / drip normal xy

uint hb_pcg( uint v )
{
	uint s = v * 747796405u + 2891336453u;
	uint w = ( ( s >> ( ( s >> 28u ) + 4u ) ) ^ s ) * 277803737u;
	return ( w >> 22u ) ^ w;
}

// four randoms in [0,1) for a grid cell
float4 hb_rand4( int2 c, uint salt )
{
	uint h = hb_pcg( asuint( c.x ) ^ hb_pcg( asuint( c.y ) ^ hb_pcg( salt ) ) );
	float4 r;
	r.x = float( h & 0xFFFFu ) * ( 1.0 / 65536.0 );	h = hb_pcg( h );
	r.y = float( h & 0xFFFFu ) * ( 1.0 / 65536.0 );	h = hb_pcg( h );
	r.z = float( h & 0xFFFFu ) * ( 1.0 / 65536.0 );	h = hb_pcg( h );
	r.w = float( h & 0xFFFFu ) * ( 1.0 / 65536.0 );
	return r;
}

// in-plane axes of projection axis a; ey points down (+v of a stamp) when the down axis lies
// in the plane enough for drips
void hb_basis( int a, float3 down, out float3 ex, out float3 ey, out bool drips )
{
	float3 A	= a == 0 ? float3( 1, 0, 0 ) : ( a == 1 ? float3( 0, 1, 0 ) : float3( 0, 0, 1 ) );
	float3 dp	= down - A * dot( down, A );
	drips		= dot( dp, dp ) > 0.25;
	if ( drips )
		ey		= normalize( dp );
	else
		ey		= a == 1 ? float3( 0, 0, 1 ) : float3( 0, 1, 0 );
	ex			= cross( A, ey );
}

struct hb_acc
{
	float	cov;	// coverage
	float	th;		// thickness (mask x its blurred self)
	float2	nq;		// normal xy in the projection plane, weighted by coverage
	float	w;		// sum of the weights
};

// tile row of kind k, frame / variant i (clamped)
float4 hb_tile( int first, int count, int i )
{
	return bus_hud_blood[ HB_TILES + first + clamp( i, 0, count - 1 ) ];
}

// one stamp: sample, rotate its normal into the plane, accumulate
void hb_stamp( inout hb_acc acc, float2 uvl, float2x2 R, float4 rect, float lod_base, float tile_scale,
	int kind )
{
	if ( any( uvl < 0 ) || any( uvl > 1 ) )
		return;
	const float	pad	= bus_hud_blood[ 2 ].w;
	float2 uv		= lerp( rect.xy + pad, rect.zw - pad, uvl );
	float tile_px	= ( rect.z - rect.x ) * max( bus_hud_blood[ 8 ].x, 1 );
	float lod		= clamp( lod_base + log2( max( tile_px * tile_scale, 1e-6 ) ), 0, bus_hud_blood[ 2 ].z );

	float4 mk		= s_hud_blood_mask.SampleLevel( smp_base, uv, lod );
	float m;
	float2 n;
	if ( kind == 0 )		{ m = mk.r;	n = s_hud_blood_na.SampleLevel( smp_base, uv, lod ).rg; }
	else if ( kind == 1 )	{ m = mk.g;	n = s_hud_blood_na.SampleLevel( smp_base, uv, lod ).ba; }
	else					{ m = mk.b;	n = s_hud_blood_nb.SampleLevel( smp_base, uv, lod ).rg; }
	if ( m <= 0.002 )
		return;
	// thickness: the same mask a few mips blurrier, so drops are thick in the middle
	const float lodb	= min( lod + bus_hud_blood[ 8 ].w, bus_hud_blood[ 2 ].z );
	float4 mb		= s_hud_blood_mask.SampleLevel( smp_base, uv, lodb );
	const float mbk	= kind == 0 ? mb.r : ( kind == 1 ? mb.g : mb.b );
	acc.th			= max( acc.th, m * saturate( mbk * 1.6 ) );
	n				= n * 2 - 1;
	// uv = R * local + 0.5, so a normal (a gradient) goes back with R transposed
	float2 nq		= mul( n, R );
	acc.cov			= max( acc.cov, m );
	acc.nq			+= nq * m;
	acc.w			+= m;
}

// one layer (0 spatter / drips, 1 smears) of one projection
void hb_layer( inout hb_acc acc, int layer, float2 q, float qfoot, bool drips, uint salt )
{
	const float4 r0		= bus_hud_blood[ 0 ];
	const float4 r5		= bus_hud_blood[ 5 ];
	const float4 r6		= bus_hud_blood[ 6 ];
	const int n_spray	= int( r5.x ), n_flow = int( r5.y ), n_drops = int( r5.z ), n_smear = int( r5.w );
	const int n_drip	= int( r6.x );
	const int f_spray	= 0, f_flow = n_spray, f_drops = f_flow + n_flow, f_smear = f_drops + n_drops;
	const int f_drip	= f_smear + n_smear;

	const float amount	= layer == 0 ? r0.x : r0.y;
	const float cell	= layer == 0 ? bus_hud_blood[ 2 ].x : bus_hud_blood[ 2 ].y;
	if ( amount <= 0 || cell <= 0 )
		return;
	const float band	= max( r6.w, 0.01 );
	const float2 g		= q / cell;
	const float2 base	= floor( g - 0.5 );
	// texels per screen pixel = qfoot / cell / size * tile_px: log2 of the first part here
	const float lod0	= log2( max( qfoot / cell, 1e-8 ) );

	[unroll] for ( int j = 0; j < 2; ++j )
	[unroll] for ( int i = 0; i < 2; ++i )
	{
		const float2 c	= base + float2( i, j );
		const float4 r	= hb_rand4( int2( c ), salt );
		if ( r.x >= amount )
			continue;
		const float4 r2	= hb_rand4( int2( c ), salt ^ 0x9E3779B9u );
		const float e	= ( amount - r.x ) / band;	// how far above its threshold

		int kind, first, count, idx;
		float size;
		bool rotate		= true;
		if ( layer == 1 )
		{
			kind = 2; first = f_smear; count = n_smear; idx = int( r2.x * n_smear ); size = 0.9 + 0.1 * r2.z;
		}
		else if ( drips && n_flow > 0 && r.y < r6.y )
		{
			// drip: grows with the drip value (frame 0 is a small blob), breaks up in the last
			// quarter of its frames
			kind = 1; first = f_flow; count = n_flow; size = 1.0; rotate = false;
			const float grow_end	= n_flow * 0.75 - 1;
			float fr				= saturate( r0.w * 1.5 - r2.w * 0.5 ) * grow_end;
			if ( e < 1 )
				fr = max( fr, lerp( n_flow - 1, grow_end + 1, e ) );
			idx = int( fr );
		}
		else if ( r.y < r6.y + r6.z && ( n_drops > 0 || n_drip > 0 ) )
		{
			// a droplet, or on a wall a teardrop pointing down
			if ( drips && n_drip > 0 && ( r2.x < 0.3 || n_drops == 0 ) )
			{
				kind = 2; first = f_drip; count = n_drip; idx = 0; rotate = false; size = 0.35 + 0.15 * r2.z;
			}
			else
			{
				kind = 2; first = f_drops; count = n_drops; idx = int( r2.x * n_drops ); size = 0.3 + 0.25 * r2.z;
			}
		}
		else
		{
			if ( n_spray <= 0 )
				continue;
			kind = 0; first = f_spray; count = n_spray; size = 0.9 + 0.1 * r2.z;
			// dense while fresh and well above its threshold, sparser as it dries or fades
			idx = int( ( n_spray - 1 ) * saturate( ( 1 - e ) * 0.6 + ( 1 - r0.z ) * 0.6 ) + 0.5 );
		}
		if ( count <= 0 )
			continue;

		const float2 centre	= c + 0.25 + 0.5 * r.zw;
		const float2 l		= ( g - centre ) / size;
		float sn = 0, cs = 1;
		if ( rotate )
			sincos( r2.y * HB_PI2, sn, cs );
		const float2x2 R	= float2x2( cs, -sn, sn, cs );
		const float2 uvl	= mul( R, l ) + 0.5;
		hb_stamp( acc, uvl, R, hb_tile( first, count, idx ), lod0, 1.0 / size, kind );
	}
}

// eye-space direction of the plane's +x / +y (cotangent frame, no vertex tangents needed)
void hb_frame( float3 N, float3 dPx, float3 dPy, float2 dqx, float2 dqy, out float3 T, out float3 B )
{
	float3 dp2perp	= cross( dPy, N );
	float3 dp1perp	= cross( N, dPx );
	T				= dp2perp * dqx.x + dp1perp * dqy.x;
	B				= dp2perp * dqx.y + dp1perp * dqy.y;
	float invmax	= rsqrt( max( max( dot( T, T ), dot( B, B ) ), 1e-30 ) );
	T				*= invmax;
	B				*= invmax;
}

// S, pbr (x metal, y rough, z AO, w sss) and Ne (eye space, normalized) are changed in place.
// Pe = eye-space position, Po = bind-pose model position, M1..M3 = tangent to eye rows.
void hud_blood_apply( inout surface_bumped S, inout float4 pbr, inout float3 Ne, float3 Pe, float3 Po,
	float3 M1, float3 M2, float3 M3, float2 spos )
{
	const float4 r0	= bus_hud_blood[ 0 ];
	const float4 r1	= bus_hud_blood[ 1 ];

	// derivatives first: everything below may branch
	const float3 dPx	= ddx( Pe ), dPy = ddy( Pe );
	const float3 dox	= ddx( Po ), doy = ddy( Po );

	[branch] if ( r0.x <= 0 && r0.y <= 0 )
		return;

	const float3 Ng		= normalize( float3( M1.z, M2.z, M3.z ) );
	// projection axis: the smooth (interpolated) normal taken back into model space, or the
	// per-triangle normal of the bind-pose position. Smooth has no seams at triangle edges; on
	// bending parts (fingers) its axis follows the animation, so stamps near 45 degrees
	// cross-fade a little while the hand moves. Per triangle never moves but steps at edges.
	float3 no;
	if ( bus_hud_blood[ 8 ].y > 0.5 )
		no				= mul( Ng, (float3x3)m_WV );	// eye -> model: the rotation, transposed
	else
		no				= cross( dox, doy );
	no					= dot( no, no ) > 1e-20 ? normalize( no ) : float3( 0, 1, 0 );
	float3 wa			= pow( abs( no ), clamp( bus_hud_blood[ 8 ].z, 1, 16 ) );
	wa					/= max( wa.x + wa.y + wa.z, 1e-6 );

	// the two strongest axes
	int a1 = 0, a2 = 1;
	if ( wa.y > wa.x )		{ a1 = 1; a2 = 0; }
	if ( wa.z > wa[ a1 ] )	{ a2 = a1; a1 = 2; }
	else if ( wa.z > wa[ a2 ] )	a2 = 2;
	const float w1		= wa[ a1 ], w2 = wa[ a2 ];
	// blend of the two projections. Weighted: each pattern at its weight, so a stain in the
	// overlap is a thinner (pinker) ghost. Union: both patterns at full strength while the
	// weaker one's share is above "fade end", fading out down to "fade start", so stains stay
	// solid and the weaker axis cannot smear stamps along a grazing surface.
	const float4 r11	= bus_hud_blood[ 11 ];
	const bool uni		= r11.x > 0.5;
	const float f_lo	= saturate( r11.y ), f_hi = max( saturate( r11.z ), f_lo + 0.01 );
	const bool two		= uni ? w2 / max( w1 + w2, 1e-6 ) > f_lo : w2 > 0.1;

	float3 down			= bus_hud_blood[ 7 ].xyz;
	down				= dot( down, down ) > 1e-6 ? normalize( down ) : float3( 0, -1, 0 );
	const uint seed		= uint( max( r1.y, 0 ) );

	// debug 4: the spatter grid of the main projection. Each cell one flat colour with smooth
	// edges inside it: cells must be square patches that follow the surface. A cell pattern
	// that breaks along triangle edges means the position the stamps are placed with
	// (SSS's I.RDrops) jumps between triangles.
	if ( r1.z > 3.5 && r1.z < 4.5 )
	{
		float3 ex, ey;
		bool drips;
		hb_basis( a1, down, ex, ey, drips );
		const float2 g	= float2( dot( Po, ex ), dot( Po, ey ) ) / max( bus_hud_blood[ 2 ].x, 1e-4 );
		const float4 h	= hb_rand4( int2( floor( g ) ), 77u );
		S.base.rgb		= h.xyz * ( 0.6 + 0.4 * min( frac( g.x ), frac( g.y ) ) );
		return;
	}

	float cov = 0, th = 0, wsum = 0;
	float3 nb = 0;	// eye-space perturbation
	[unroll] for ( int k = 0; k < 2; ++k )
	{
		if ( k == 1 && !two )
			break;
		const int a		= k == 0 ? a1 : a2;
		const float wk	= k == 0 ? ( two ? w1 / ( w1 + w2 ) : 1.0 ) : w2 / ( w1 + w2 );
		float3 ex, ey;
		bool drips;
		hb_basis( a, down, ex, ey, drips );
		const float2 q		= float2( dot( Po, ex ), dot( Po, ey ) );
		const float2 dqx	= float2( dot( dox, ex ), dot( dox, ey ) );
		const float2 dqy	= float2( dot( doy, ex ), dot( doy, ey ) );
		const float qfoot	= max( length( dqx ), length( dqy ) );

		hb_acc acc;
		acc.cov = 0; acc.th = 0; acc.nq = 0; acc.w = 0;
		const uint salt		= seed * 8u + uint( a ) * 2u;
		hb_layer( acc, 0, q, qfoot, drips, salt );
		hb_layer( acc, 1, q, qfoot, drips, salt + 1u );
		if ( acc.cov <= 0 )
			continue;

		float3 T, B;
		hb_frame( Ng, dPx, dPy, dqx, dqy, T, B );
		const float2 nq	= acc.nq / max( acc.w, 1e-4 );
		const float f	= uni ? smoothstep( f_lo, f_hi, wk ) : wk;
		if ( uni )
		{
			cov			= max( cov, acc.cov * f );
			th			= max( th, acc.th * f );
		}
		else
		{
			cov			+= acc.cov * f;
			th			+= acc.th * f;
		}
		nb				+= ( T * nq.x + B * nq.y ) * acc.cov * f;
		wsum			+= f;
	}

	const float dbg	= r1.z;
	if ( dbg > 0.5 && dbg < 1.5 )
	{
		S.base.rgb	= float3( cov, cov * 0.2, 0 );	// debug 1: coverage
		return;
	}
	if ( dbg > 1.5 && dbg < 2.5 )
	{
		S.base.rgb	= wa;							// debug 2: projection axes
		return;
	}
	if ( cov <= 0.002 )
		return;

	const float dil		= saturate( r1.x );
	const float wet		= saturate( r0.z );
	const float a		= saturate( cov );
	// thin parts dry first
	const float dry		= saturate( ( 1 - wet ) * ( 1 + ( 1 - th ) * bus_hud_blood[ 7 ].w ) );
	if ( dbg > 2.5 )
	{
		S.base.rgb	= float3( dry, a, th );			// debug 3: dryness, coverage, thickness
		return;
	}

	// the film: Beer-Lambert transmittance over the surface, thick blood's own colour on top
	const float d		= th * bus_hud_blood[ 9 ].w * ( 1 - dil );
	const float3 kab	= lerp( bus_hud_blood[ 9 ].xyz, bus_hud_blood[ 10 ].xyz, dry );
	const float3 T		= pow( exp( -kab * d ), 1.0 / max( bus_hud_blood[ 10 ].w, 1 ) );
	const float op		= saturate( 1 - min( T.g, T.b ) );
	const float3 thick	= lerp( bus_hud_blood[ 3 ].rgb, bus_hud_blood[ 4 ].rgb, dry );
	S.base.rgb			= lerp( S.base.rgb * T, thick, op );

	const float rough	= lerp( bus_hud_blood[ 3 ].w, bus_hud_blood[ 4 ].w, dry );
	pbr.x				= pbr.x * ( 1 - op );
	if ( bus_hud_blood[ 11 ].w > 0.5 )
	{
		// LV: metal is metal or not; 4x4 Bayer threshold, averaged by temporal AA
		const uint2 bp	= uint2( spos ) & 3u;
		const uint bx	= bp.x ^ bp.y;
		const uint b4	= ( ( bx & 1u ) << 3 ) | ( ( bp.y & 1u ) << 2 ) | ( bx & 2u ) | ( ( bp.y & 2u ) >> 1 );
		pbr.x			= pbr.x > ( float( b4 ) + 0.5 ) * 0.0625 ? 1.0 : 0.0;
	}
	pbr.y				= max( 0.02, lerp( pbr.y, rough, a ) );
	S.gloss				= lerp( S.gloss, ( 1 - rough ) * ( 1 - rough ), a );

	// wet blood fills the surface's relief, dried blood shows its own crust
	const float strength	= bus_hud_blood[ 1 ].w;
	float3 N			= normalize( lerp( Ne, Ng, a * ( 1 - dry ) * 0.85 ) );
	N					+= nb * strength * lerp( 0.35, 1.0, dry );
	Ne					= normalize( N );
}

#else	// no shader bus 7: nothing

void hud_blood_apply( inout surface_bumped S, inout float4 pbr, inout float3 Ne, float3 Pe, float3 Po,
	float3 M1, float3 M2, float3 M3, float2 spos )
{
}

#endif

#endif	// HUD_BLOOD_H
