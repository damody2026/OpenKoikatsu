// Installed Shader Forge/main_item_ditherd: pass 0, blob 8, ['DIRECTIONAL']
static const int32 GForgeDitherCBSize[]={6,7};
static const FOKAdditionalUniform GForgeDitherUniforms[]={
{TEXT("_LightColor0"),0,32,4,0,0},
{TEXT("_Color"),0,48,4,0,0},
{TEXT("_MainTex_ST"),0,64,4,0,0},
{TEXT("_Cutoff"),0,80,1,0,0},
{TEXT("_ProjectionParams"),1,80,4,0,0},
{TEXT("_ScreenParams"),1,96,4,0,0},
};
static const FOKAdditionalTexture GForgeDitherTextures[]={
{TEXT("_MainTex"),0,0,2},
};
static const FOKAdditionalProperty GForgeDitherProperties[]={
{TEXT("_Color"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_MainTex"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_Cutoff"),3,false,FLinearColor(1.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_shadowcolor"),0,false,FLinearColor(0.5f,0.5f,0.5f,1.0f),TEXT("")},
{TEXT("_node_1554"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_Cutoff"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
};
