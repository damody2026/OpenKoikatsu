// Installed Koikano/main_nose: pass 0, blob 8, ['DIRECTIONAL']
static const int32 GKoikanoNoseCBSize[]={7,5,1,8};
static const FOKAdditionalUniform GKoikanoNoseUniforms[]={
{TEXT("_MainTex_ST"),0,64,4,0,0},
{TEXT("_Color"),0,80,4,0,0},
{TEXT("_KanoVerShift"),0,96,1,0,0},
{TEXT("_WorldSpaceCameraPos"),1,64,3,0,0},
{TEXT("_WorldSpaceLightPos0"),2,0,4,0,0},
{TEXT("unity_WorldToObject"),3,64,4,0,4},
};
static const FOKAdditionalTexture GKoikanoNoseTextures[]={
{TEXT("_MainTex"),0,0,2},
};
static const FOKAdditionalProperty GKoikanoNoseProperties[]={
{TEXT("_Color"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_MainTex"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_texcoord"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("__dirty"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
};
