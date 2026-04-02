struct Uniforms
{
    float2 scale;
    float2 translate;
};

struct VSInput
{
    float2 position : POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

ParameterBlock<Uniforms> uniforms;
Texture2D fontTexture;
SamplerState fontSampler;

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position * uniforms.scale + uniforms.translate, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return input.color * fontTexture.Sample(fontSampler, input.uv);
}
