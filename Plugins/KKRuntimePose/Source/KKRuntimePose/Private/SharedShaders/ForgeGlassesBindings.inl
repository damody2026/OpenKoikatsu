// Installed Shader Forge/toon_glasses_lod0: pass 0, blob 24, ['DIRECTIONAL', 'LIGHTMAP_OFF', 'DIRLIGHTMAP_OFF', 'DYNAMICLIGHTMAP_OFF']
static const int32 GForgeGlassesCBSize[]={12,5,1};
static const FOKAdditionalUniform GForgeGlassesUniforms[]={
{TEXT("_LightColor0"),0,32,4,0,0},
{TEXT("_Color4"),0,64,4,0,0},
{TEXT("_MainTex_ST"),0,80,4,0,0},
{TEXT("_GlassRamp_ST"),0,96,4,0,0},
{TEXT("_Color"),0,112,4,0,0},
{TEXT("_NormalMap_ST"),0,128,4,0,0},
{TEXT("_ShadowColor"),0,144,4,0,0},
{TEXT("_ColorInverse"),0,160,1,0,0},
{TEXT("_ColorSort"),0,164,1,0,0},
{TEXT("_RimPower"),0,168,1,0,0},
{TEXT("_ambientshadowG"),0,176,4,0,0},
{TEXT("_WorldSpaceCameraPos"),1,64,3,0,0},
{TEXT("_WorldSpaceLightPos0"),2,0,4,0,0},
};
static const FOKAdditionalTexture GForgeGlassesTextures[]={
{TEXT("_NormalMap"),0,2,2},
{TEXT("_GlassRamp"),1,1,2},
{TEXT("_MainTex"),2,0,2},
};
static const FOKAdditionalProperty GForgeGlassesProperties[]={
{TEXT("_Color"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_GlassRamp"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_Color4"),0,false,FLinearColor(0.50196081399917603f,0.50196081399917603f,0.50196081399917603f,1.0f),TEXT("")},
{TEXT("_MainTex"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_ShadowColor"),0,false,FLinearColor(0.69434827566146851f,0.7576795220375061f,0.82352942228317261f,1.0f),TEXT("")},
{TEXT("_NormalMap"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("bump")},
{TEXT("_ColorInverse"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ColorSort"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_RimPower"),3,false,FLinearColor(1.0f,1.0f,9.0f,0.0f),TEXT("")},
{TEXT("_Cutoff"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
};
