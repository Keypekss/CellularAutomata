struct VertexIn
{
	float3 PosL  : POSITION;   
    float2 TexC  : TEXCOORD;
};

struct VertexOut
{
	float4 PosL  : SV_POSITION;   
    float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout;	
    
    vout.TexC = vin.TexC;
    vout.PosL = float4(vin.PosL, 1.0f);
    
    return vout;
}

Texture2D tex : register(t0);
SamplerState samPoint : register(s0);

float4 PS(VertexOut pin) : SV_Target
{
    float4 color = tex.Sample(samPoint, pin.TexC);
    
    return color;
}


