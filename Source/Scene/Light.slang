enum LightType : int
{
    Directional = 0,
    Point = 1,
    Spot = 2,
};

struct Light
{
    float3 color;
    float intensity;

    float3 position;
    LightType type;

    float3 direction;
    float range;

    float innerConeAngle;
    float outerConeAngle;
    float _pad[2];

    float GetDistanceAttenuation(float distance)
    {
        if (type == LightType.Directional)
            return 1.0;
        float att = 1.0 / (distance * distance);
        if (range < 1e30)
        {
            float ratio = distance / range;
            float clamped = saturate(1.0 - ratio * ratio * ratio * ratio);
            att *= clamped * clamped;
        }
        return att;
    }

    float GetSpotAngleAttenuation(float3 toSurface)
    {
        if (type != LightType.Spot)
            return 1.0;
        float cosAngle = dot(direction, toSurface);
        float cosOuter = cos(outerConeAngle);
        float cosInner = cos(innerConeAngle);
        return saturate((cosAngle - cosOuter) / max(cosInner - cosOuter, 1e-5));
    }

    void Evaluate(float3 surfacePos, out float3 radiance, out float3 lightDir, out float dist)
    {
        if (type == LightType.Directional)
        {
            lightDir = -direction;
            dist = 1e30;
            radiance = color * intensity;
        }
        else
        {
            float3 toLight = position - surfacePos;
            dist = length(toLight);
            lightDir = toLight / dist;

            float att = GetDistanceAttenuation(dist);
            float spotAtt = GetSpotAngleAttenuation(-lightDir);
            radiance = color * intensity * att * spotAtt;
        }
    }
};
