// Fullscreen quad from g_combine (FVF::F_TL). Its positions are already in clip space.
struct WorldVertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
WorldVertex main(float4 position : POSITIONT, float2 uv : TEXCOORD0)
{
    WorldVertex output;
    output.position=position;
    output.uv=uv;
    return output;
}
