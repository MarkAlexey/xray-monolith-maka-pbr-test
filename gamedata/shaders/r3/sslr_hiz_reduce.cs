//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: IX-Ray SSLR, one level of the Hi-Z min pyramid (port of IX-Ray develop's
// sslr_hiz_reduce.cs.hlsl, Stachowiak 2015). u_hiz_src = level n - 1, u_hiz_dst = level n,
// each texel the minimum (nearest, see sslr_hiz_copy.cs) of the 2x2 below it; on an odd-sized
// level the last row / column also covers the extra source texels.
//////////////////////////////////////////////////////////////////////////////////////////

RWTexture2D<float> u_hiz_src : register( u0 );
RWTexture2D<float> u_hiz_dst : register( u1 );

[numthreads( 8, 8, 1 )]
void main( uint3 DTid : SV_DispatchThreadID )
{
	uint2 src_size, dst_size;
	u_hiz_src.GetDimensions( src_size.x, src_size.y );
	u_hiz_dst.GetDimensions( dst_size.x, dst_size.y );
	if( any( DTid.xy >= dst_size ) )
		return;

	uint2 s = DTid.xy * 2;
	uint2 smax = src_size - 1;
	float m = min( min( u_hiz_src[min( s, smax )], u_hiz_src[min( s + uint2( 1, 0 ), smax )] ),
				   min( u_hiz_src[min( s + uint2( 0, 1 ), smax )], u_hiz_src[min( s + uint2( 1, 1 ), smax )] ) );

	bool extra_x = ( src_size.x & 1 ) && DTid.x == dst_size.x - 1;
	bool extra_y = ( src_size.y & 1 ) && DTid.y == dst_size.y - 1;
	if( extra_x )
		m = min( m, min( u_hiz_src[min( s + uint2( 2, 0 ), smax )], u_hiz_src[min( s + uint2( 2, 1 ), smax )] ) );
	if( extra_y )
		m = min( m, min( u_hiz_src[min( s + uint2( 0, 2 ), smax )], u_hiz_src[min( s + uint2( 1, 2 ), smax )] ) );
	if( extra_x && extra_y )
		m = min( m, u_hiz_src[min( s + uint2( 2, 2 ), smax )] );

	u_hiz_dst[DTid.xy] = m;
}
