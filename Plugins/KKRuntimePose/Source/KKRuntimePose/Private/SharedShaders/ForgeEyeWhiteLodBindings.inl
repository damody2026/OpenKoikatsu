// Installed Shader Forge/toon_eyew_lod0: pass 0, blob 8, ['DIRECTIONAL']
static const int32 GForgeEyeWhiteLodCBSize[]={8,1};
static const FOKAdditionalUniform GForgeEyeWhiteLodUniforms[]={
{TEXT("_LightColor0"),0,32,4,0,0},
{TEXT("_MainTex_ST"),0,48,4,0,0},
{TEXT("_Color"),0,64,4,0,0},
{TEXT("_shadowcolor"),0,80,4,0,0},
{TEXT("_ambientshadowG"),0,96,4,0,0},
{TEXT("_RampG_ST"),0,112,4,0,0},
{TEXT("_WorldSpaceLightPos0"),1,0,4,0,0},
};
static const FOKAdditionalTexture GForgeEyeWhiteLodTextures[]={
{TEXT("_MainTex"),0,0,2},
{TEXT("_RampG"),1,1,2},
};
static const FOKAdditionalProperty GForgeEyeWhiteLodProperties[]={
{TEXT("_Color"),0,false,FLinearColor(0.5f,0.5f,0.5f,1.0f),TEXT("")},
{TEXT("_MainTex"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_shadowcolor"),0,false,FLinearColor(0.62982350587844849f,0.64032888412475586f,0.74699997901916504f,1.0f),TEXT("")},
{TEXT("_Cutoff"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
};
