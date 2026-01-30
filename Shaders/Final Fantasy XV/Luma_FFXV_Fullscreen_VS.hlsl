// Fullscreen vertex shader that outputs position and UV coordinates
// Compatible with motion vector decode pixel shader

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VS_OUTPUT main(uint vertexIdx : SV_VertexID)
{
    VS_OUTPUT output;
    
    // Generate UV from vertex ID (0,1,2,3 for TRIANGLESTRIP quad)
    // This creates a fullscreen quad covering [0,1] UV range
    float2 texcoord = float2(vertexIdx & 1, vertexIdx >> 1);
    
    // Transform UV to clip space [-1,1]
    // Note: Y is flipped because UV Y goes down, clip space Y goes up
    output.Position = float4((texcoord.x - 0.5) * 2, -(texcoord.y - 0.5) * 2, 0, 1);
    output.TexCoord = texcoord;
    
    return output;
}
