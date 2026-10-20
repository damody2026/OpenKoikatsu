// Koikano/hair_main_sun(_front), VS 0032/0048 and PS 0096/0104.
// UV is UE's flipped source UV1. Offsets follow ST, not precede it.
static const TCHAR* GKoikanoHairGlossUv = TEXT(R"OKHAIR(
float2 unityUV = float2(UV.x, 1.0 - UV.y);
unityUV = mad(unityUV, VertexST.xy, VertexST.zw);
if (OldHair != 0.0) unityUV = mad(unityUV, ST.xy, ST.zw);
unityUV.x = mad(Lambert, 0.005, unityUV.x);
unityUV.y += Tap * 0.008;
return float2(unityUV.x, 1.0 - unityUV.y);
)OKHAIR");
