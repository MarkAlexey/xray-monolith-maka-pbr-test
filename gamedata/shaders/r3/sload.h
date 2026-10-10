#ifndef SLOAD_H
#define SLOAD_H

#include "common.h"

#ifdef	MSAA_ALPHATEST_DX10_1
#if MSAA_SAMPLES == 2
static const float2 MSAAOffsets[2] = { float2(4,4), float2(-4,-4) };
#endif
#if MSAA_SAMPLES == 4
static const float2 MSAAOffsets[4] = { float2(-2,-6), float2(6,-2), float2(-6,2), float2(2,6) };
#endif
#if MSAA_SAMPLES == 8
static const float2 MSAAOffsets[8] = { float2(1,-3), float2(-1,3), float2(5,1), float2(-3,-5), 
								               float2(-5,5), float2(-7,-1), float2(3,7), float2(7,-7) };
#endif
#endif	//	MSAA_ALPHATEST_DX10_1

//////////////////////////////////////////////////////////////////////////////////////////
// Bumped surface loader                //
//////////////////////////////////////////////////////////////////////////////////////////
struct	surface_bumped
{
	float4	base;
	float3	normal;
	float	gloss;
	float	height;
#ifdef USE_PBR
	float4	pbr;	// IX-Ray PBR: x = metalness, y = roughness, z = ambient occlusion, w = subsurface
#endif
};

// Monolith: IX-Ray PBR. USE_PBR is set by the engine for textures whose THM material is
// "PBR (metallic/roughness)". Their bump textures follow IX-Ray's layout (sload.hlsli):
//   bump  : x = height, y = normal Y, w = normal X (z is unused)
//   bump# : x = metalness, y = roughness, z = subsurface, w = ambient occlusion
// The parallax code below reads the height from the matching channel.
#ifdef USE_PBR
#	define SLOAD_HEIGHT(tc)		s_bump.Sample( smp_base, tc ).x
#	define SLOAD_HEIGHT_LOD0(tc)	s_bump.SampleLevel( smp_base, tc, 0 ).x
#else
#	define SLOAD_HEIGHT(tc)		s_bumpX.Sample( smp_base, tc ).w
#	define SLOAD_HEIGHT_LOD0(tc)	s_bumpX.SampleLevel( smp_base, tc, 0 ).a
#endif

float4 tbase( float2 tc )
{
   return	s_base.Sample( smp_base, tc);
}

#if defined(ALLOW_STEEPPARALLAX) && defined(USE_STEEPPARALLAX)

static const float fParallaxStartFade = 8.0f;
static const float fParallaxStopFade = 12.0f;

void UpdateTC( inout p_bumped I)
{
	if (I.position.z < fParallaxStopFade)
	{
		const float maxSamples = 25;
		const float minSamples = 5;
		const float fParallaxOffset = -0.013;

		float3	 eye = mul (float3x3(I.M1.x, I.M2.x, I.M3.x,
									 I.M1.y, I.M2.y, I.M3.y,
									 I.M1.z, I.M2.z, I.M3.z), -I.position.xyz);

		eye = normalize(eye);
		
		//	Calculate number of steps
		float nNumSteps = lerp( maxSamples, minSamples, eye.z );

		float	fStepSize			= 1.0 / nNumSteps;
		float2	vDelta				= eye.xy * fParallaxOffset*1.2;
		float2	vTexOffsetPerStep	= fStepSize * vDelta;

		//	Prepare start data for cycle
		float2	vTexCurrentOffset	= I.tcdh;
		float	fCurrHeight			= 0.0;
		float	fCurrentBound		= 1.0;

		for( int i=0; i<nNumSteps; ++i )
		{
			if (fCurrHeight < fCurrentBound)
			{	
				vTexCurrentOffset += vTexOffsetPerStep;		
				fCurrHeight = SLOAD_HEIGHT_LOD0( vTexCurrentOffset.xy ); 
				fCurrentBound -= fStepSize;
			}
		}

/*
		[unroll(25)]	//	Doesn't work with [loop]
		for( ;fCurrHeight < fCurrentBound; fCurrentBound -= fStepSize )
		{
			vTexCurrentOffset += vTexOffsetPerStep;		
			fCurrHeight = SLOAD_HEIGHT_LOD0( vTexCurrentOffset.xy ); 
		}
*/
		//	Reconstruct previouse step's data
		vTexCurrentOffset -= vTexOffsetPerStep;
		float fPrevHeight = SLOAD_HEIGHT( vTexCurrentOffset.xy );

		//	Smooth tc position between current and previouse step
		float	fDelta2 = ((fCurrentBound + fStepSize) - fPrevHeight);
		float	fDelta1 = (fCurrentBound - fCurrHeight);
		float	fParallaxAmount = (fCurrentBound * fDelta2 - (fCurrentBound + fStepSize) * fDelta1 ) / ( fDelta2 - fDelta1 );
		float	fParallaxFade 	= smoothstep(fParallaxStopFade, fParallaxStartFade, I.position.z);
		float2	vParallaxOffset = vDelta * ((1- fParallaxAmount )*fParallaxFade);
		float2	vTexCoord = I.tcdh + vParallaxOffset;
	
		//	Output the result
		I.tcdh = vTexCoord;

#if defined(USE_TDETAIL) && defined(USE_STEEPPARALLAX)
		I.tcdbump = vTexCoord * dt_params;
#endif
	}

}

#elif	defined(USE_PARALLAX) || defined(USE_STEEPPARALLAX)

void UpdateTC( inout p_bumped I)
{
	float3	 eye = mul (float3x3(I.M1.x, I.M2.x, I.M3.x,
								 I.M1.y, I.M2.y, I.M3.y,
								 I.M1.z, I.M2.z, I.M3.z), -I.position.xyz);
								 
	float	height	= SLOAD_HEIGHT( I.tcdh );	//
			//height  /= 2;
			//height  *= 0.8;
			height	= height*(parallax.x) + (parallax.y);	//
	float2	new_tc  = I.tcdh + height * normalize(eye);	//

	//	Output the result
	I.tcdh.xy	= new_tc;
}

#else	//	USE_PARALLAX

void UpdateTC( inout p_bumped I)
{
	;
}

#endif	//	USE_PARALLAX

#ifdef USE_PBR
// Port of IX-Ray's SloadNew() USE_PBR path. Called at the end of sload_i(), after the legacy
// code, so it only has to replace the fields that mean something else for PBR textures.
// Detail bump textures are decoded IX-Ray style only when they are PBR too (USE_PBR_DETAIL);
// legacy detail bumps keep their normal and do not touch the material.
void sload_pbr( inout surface_bumped S, p_bumped I, float4 Nu, float4 NuE )
{
	float3 N;
	N.xy		= Nu.wy * 2.0 - 1.0;
	N.z			= sqrt( 1.0 - saturate( dot( N.xy, N.xy ) ) );

	S.height	= Nu.x;
	S.pbr		= float4( NuE.x, NuE.y, NuE.w, NuE.z );

#if defined(USE_TDETAIL) && defined(USE_TDETAIL_BUMP)
	float4 NDetail		= s_detailBump.Sample( smp_base, I.tcdbump );
	float4 NDetailX		= s_detailBumpX.Sample( smp_base, I.tcdbump );
#	ifdef USE_PBR_DETAIL
	float3 ND;
	ND.xy		= NDetail.wy * 2.0 - 1.0;
	ND.z		= sqrt( 1.0 - saturate( dot( ND.xy, ND.xy ) ) );
	N			+= ND;
	S.pbr.x		*= NDetailX.x * 2.0;
	S.pbr.y		*= NDetailX.y * 2.0;
	S.pbr.w		*= NDetailX.z;
	S.pbr.z		*= NDetailX.w;
#	else	//	USE_PBR_DETAIL
	float3 ND	= NDetail.wzy + NDetailX.xyz - 1.0;
	ND.y		= -ND.y;	// undo the flip below, legacy maps are already in the engine's convention
	N			+= ND;
#	endif	//	USE_PBR_DETAIL
#endif

#ifndef USE_DX_NORMAL_MAP
	N.y			= -N.y;		// IX-Ray PBR normal maps are OpenGL style (green up)
#endif
	S.normal	= N;

	S.pbr		= saturate( S.pbr );
	S.pbr.y		= max( 0.02, S.pbr.y );

	// Legacy-style gloss, only for passes that still read the albedo alpha (rain, mods)
	S.gloss		= ( 1.0 - S.pbr.y ) * ( 1.0 - S.pbr.y );
}
#endif	//	USE_PBR

surface_bumped sload_i( p_bumped I)
{
	surface_bumped	S;
   
	UpdateTC(I);	//	All kinds of parallax are applied here.

	float4 	Nu	= s_bump.Sample( smp_base, I.tcdh );		// IN:	normal.gloss
	float4 	NuE	= s_bumpX.Sample( smp_base, I.tcdh);	// IN:	normal_error.height

	S.base		= tbase(I.tcdh);				//	IN:  rgb.a
	S.normal	= Nu.wzy + (NuE.xyz - 1.0h);	//	(Nu.wzyx - .5h) + (E-.5)
	S.gloss		= Nu.x*Nu.x;					//	S.gloss = Nu.x*Nu.x;
	S.height	= NuE.w;
	//S.height	= 0;

#ifdef        USE_TDETAIL
#ifdef        USE_TDETAIL_BUMP
	float4 NDetail		= s_detailBump.Sample( smp_base, I.tcdbump);
	float4 NDetailX		= s_detailBumpX.Sample( smp_base, I.tcdbump);
	S.gloss				= S.gloss * NDetail.x * 2;
	//S.normal			+= NDetail.wzy-.5;
	S.normal			+= NDetail.wzy + NDetailX.xyz - 1.0h; //	(Nu.wzyx - .5h) + (E-.5)

	float4 detail		= s_detail.Sample( smp_base, I.tcdbump);
	S.base.rgb			= S.base.rgb * detail.rgb * 2;

//	S.base.rgb			= float3(1,0,0);
#else        //	USE_TDETAIL_BUMP
	float4 detail		= s_detail.Sample( smp_base, I.tcdbump);
	S.base.rgb			= S.base.rgb * detail.rgb * 2;
	S.gloss				= S.gloss * detail.w * 2;

#endif        //	USE_TDETAIL_BUMP
#endif

#ifdef USE_PBR
	sload_pbr( S, I, Nu, NuE );
#endif

	return S;
}

surface_bumped sload_i( p_bumped I, float2 pixeloffset )
{
	surface_bumped	S;
   
   // apply offset
#ifdef	MSAA_ALPHATEST_DX10_1
   I.tcdh.xy += pixeloffset.x * ddx(I.tcdh.xy) + pixeloffset.y * ddy(I.tcdh.xy);
#endif

	UpdateTC(I);	//	All kinds of parallax are applied here.

	float4 	Nu	= s_bump.Sample( smp_base, I.tcdh );		// IN:	normal.gloss
	float4 	NuE	= s_bumpX.Sample( smp_base, I.tcdh);	// IN:	normal_error.height

	S.base		= tbase(I.tcdh);				//	IN:  rgb.a
	S.normal	= Nu.wzyx + (NuE.xyz - 1.0h);	//	(Nu.wzyx - .5h) + (E-.5)
	S.gloss		= Nu.x*Nu.x;					//	S.gloss = Nu.x*Nu.x;
	S.height	= NuE.w;
	//S.height	= 0;

#ifdef        USE_TDETAIL
#ifdef        USE_TDETAIL_BUMP
#ifdef MSAA_ALPHATEST_DX10_1
#if ( (!defined(ALLOW_STEEPPARALLAX) ) && defined(USE_STEEPPARALLAX) )
   I.tcdbump.xy += pixeloffset.x * ddx(I.tcdbump.xy) + pixeloffset.y * ddy(I.tcdbump.xy);
#endif
#endif

	float4 NDetail		= s_detailBump.Sample( smp_base, I.tcdbump);
	float4 NDetailX		= s_detailBumpX.Sample( smp_base, I.tcdbump);
	S.gloss				= S.gloss * NDetail.x * 2;
	//S.normal			+= NDetail.wzy-.5;
	S.normal			+= NDetail.wzy + NDetailX.xyz - 1.0h; //	(Nu.wzyx - .5h) + (E-.5)

	float4 detail		= s_detail.Sample( smp_base, I.tcdbump);
	S.base.rgb			= S.base.rgb * detail.rgb * 2;

//	S.base.rgb			= float3(1,0,0);
#else        //	USE_TDETAIL_BUMP
#ifdef MSAA_ALPHATEST_DX10_1
   I.tcdbump.xy += pixeloffset.x * ddx(I.tcdbump.xy) + pixeloffset.y * ddy(I.tcdbump.xy);
#endif
	float4 detail		= s_detail.Sample( smp_base, I.tcdbump);
	S.base.rgb			= S.base.rgb * detail.rgb * 2;
	S.gloss				= S.gloss * detail.w * 2;
#endif        //	USE_TDETAIL_BUMP
#endif

#ifdef USE_PBR
	sload_pbr( S, I, Nu, NuE );
#endif

	return S;
}

surface_bumped sload ( p_bumped I)
{
      surface_bumped      S   = sload_i	(I);
	//	S.normal.z			*=	0.5;		//. make bump twice as contrast (fake, remove me if possible)
      return              S;
}

surface_bumped sload ( p_bumped I, float2 pixeloffset )
{
      surface_bumped      S   = sload_i	(I, pixeloffset );
	//	S.normal.z			*=	0.5;		//. make bump twice as contrast (fake, remove me if possible)
      return              S;
}

#endif
