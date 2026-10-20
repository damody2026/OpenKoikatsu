// Installed DA/Chara/Oil Painting: pass 0, blob 1, []
static const int32 GOilPaintingCBSize[]={5};
static const FOKAdditionalUniform GOilPaintingUniforms[]={
{TEXT("_Radius"),0,48,1,1,0},
{TEXT("_MainTex_TexelSize"),0,64,4,0,0},
};
static const FOKAdditionalTexture GOilPaintingTextures[]={
{TEXT("_MainTex"),0,0,2},
};
static const FOKAdditionalProperty GOilPaintingProperties[]={
{TEXT("_MainTex"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_Radius"),3,false,FLinearColor(0.0f,0.0f,10.0f,0.0f),TEXT("")},
};
