struct FilterVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
FilterVertex main(float4 position : POSITION, float2 uv : TEXCOORD0)
{
    FilterVertex output;
    output.position = position;
    output.uv = uv;
    return output;
}
