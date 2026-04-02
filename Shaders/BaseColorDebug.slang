import Scene;

Texture2D<uint2> gVBuffer;
SceneData gScene;
RWTexture2D<float4> gOutput;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 tid: SV_DispatchThreadID)
{
    int2 size;
    gOutput.GetDimensions(size.x, size.y);
    if (any(tid.xy >= size))
        return;

    uint2 vbufData = gVBuffer[tid.xy];
    VBufferHit hit = UnpackVBuffer(vbufData);

    if (!hit.isValid)
    {
        gOutput[tid.xy] = float4(0.05, 0.05, 0.05, 1.0);
        return;
    }

    Vertex v = gScene.GetVertex(hit.instanceID, hit.primitiveID, hit.barycentrics);
    Material mat = gScene.GetMaterial(hit.instanceID);
    float4 baseColor = gScene.SampleBaseColor(mat, v.texCoord);
    gOutput[tid.xy] = float4(baseColor.rgb, 1.0);
}
