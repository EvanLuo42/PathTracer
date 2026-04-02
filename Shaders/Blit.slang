Texture2D<float4> gInput;
SamplerState gSampler;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    VSOutput output;
    output.uv = float2((vid << 1) & 2, vid & 2);
    output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return gInput.Sample(gSampler, input.uv);
}
