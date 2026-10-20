// Installed outline programs. Regenerate with Tools/generate_runtime_outlines.lua.
// Shader Forge/main_item; original DXBC d6dc1a91b8414692cbf62ee37ba129c691cb88e4eb34f7debe5c2edc15a5390b
static const int32 GForgeItemOutlineCBSize[]={16,1};
static const FOKAdditionalUniform GForgeItemOutlineUniforms[]={
{TEXT("_TimeEditor"),0,32,4,0,0},
{TEXT("_Color"),0,48,4,0,0},
{TEXT("_ColorMask_ST"),0,64,4,0,0},
{TEXT("_Color2"),0,80,4,0,0},
{TEXT("_Color3"),0,96,4,0,0},
{TEXT("_MainTex_ST"),0,112,4,0,0},
{TEXT("_ShadowColor"),0,128,4,0,0},
{TEXT("_DetailMask_ST"),0,144,4,0,0},
{TEXT("_Clock"),0,160,4,0,0},
{TEXT("_LineMask_ST"),0,176,4,0,0},
{TEXT("_ShadowExtendAnother"),0,192,1,0,0},
{TEXT("_AnotherRampFull"),0,196,1,0,0},
{TEXT("_ambientshadowG"),0,208,4,0,0},
{TEXT("_LineColorG"),0,224,4,0,0},
{TEXT("_DetailRLineR"),0,240,1,0,0},
{TEXT("_Time"),1,0,4,0,0},
};
static const TArray<FOKAdditionalTexture> GForgeItemOutlineTextures={
{TEXT("_MainTex"),0,1,2},
{TEXT("_ColorMask"),1,0,2},
{TEXT("_LineMask"),2,3,2},
{TEXT("_DetailMask"),3,2,2},
};
static const FOKAdditionalProperty GForgeItemOutlineProperties[]={
{TEXT("_AnotherRamp"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_MainTex"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_NormalMap"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("bump")},
{TEXT("_DetailMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_LineMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_ShadowColor"),0,false,FLinearColor(0.62800002098083496f,0.62800002098083496f,0.62800002098083496f,1.0f),TEXT("")},
{TEXT("_SpecularPower"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpeclarHeight"),3,false,FLinearColor(0.98000001907348633f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_rimpower"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_rimV"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ShadowExtend"),3,false,FLinearColor(1.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ShadowExtendAnother"),3,false,FLinearColor(1.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_AnotherRampFull"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_DetailBLineG"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_DetailRLineR"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_notusetexspecular"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_LineWidthS"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_Clock"),1,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_ColorMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_Color"),0,false,FLinearColor(1.0f,0.0f,0.0f,1.0f),TEXT("")},
{TEXT("_Color2"),0,false,FLinearColor(0.11724189668893814f,0.0f,1.0f,1.0f),TEXT("")},
{TEXT("_Color3"),0,false,FLinearColor(0.5f,0.5f,0.5f,1.0f),TEXT("")},
{TEXT("_Cutoff"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
};
static const TCHAR* GForgeItemOutlineProgram=
TEXT(R"OKSHADER(
precise float4 o0=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
precise float4 r2=0.0;
precise float4 r3=0.0;
precise float4 r4=0.0;
precise float4 r5=0.0;
r0.xy=mad(v1.xy,cb0[7].xy,cb0[7].zw);
r0.xyzw=(Texture0.Sample(Sampler1,float2((r0.xy).x,lerp((r0.xy).y,1.0-(r0.xy).y,UVFlip)))).xyzw;
r0.w=saturate((r0.w+r0.w));
r0.w=(r0.w+asfloat(0xbf000000u));
r0.w=asfloat((r0.w<asfloat(0x00000000u))?0xffffffffu:0u);
if(asuint(r0.w)!=0u) clip(-1.0);
r1.xyz=(cb0[3].xyz+float3(asfloat(0xbf800000u),asfloat(0xbf800000u),asfloat(0xbf800000u)));
r2.xy=mad(v1.xy,cb0[4].xy,cb0[4].zw);
r2.xyzw=(Texture1.Sample(Sampler0,float2((r2.xy).x,lerp((r2.xy).y,1.0-(r2.xy).y,UVFlip)))).xyzw;
r1.xyz=mad(r2.xxx,r1.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r3.xyz=(-r1.xyz+cb0[5].xyz);
r1.xyz=mad(r2.yyy,r3.xyz,r1.xyz);
r2.xyw=(-r1.xyz+cb0[6].xyz);
r1.xyz=mad(r2.zzz,r2.xyw,r1.xyz);
r0.xyz=(r0.xyz*r1.xyz);
r0.w=(cb0[2].y+cb1[0].y);
r0.w=(r0.w*cb0[10].w);
r0.w=(r0.w*cb0[10].z);
r1.x=sin(r0.w);
r2.x=cos(r0.w);
r3.z=r1.x;
r3.y=r2.x;
r3.x=-r1.x;
r1.xy=(v1.xy+-cb0[10].xy);
r2.x=dot(r1.xy,r3.yz);
r2.y=dot(r1.xy,r3.xy);
r1.xy=(r2.xy+cb0[10].xy);
r1.xy=mad(r1.xy,cb0[11].xy,cb0[11].zw);
r1.xyzw=(Texture2.Sample(Sampler3,float2((r1.xy).x,lerp((r1.xy).y,1.0-(r1.xy).y,UVFlip)))).xyzw;
r0.xyz=mad(r1.zzz,-r0.xyz,r0.xyz);
r1.xyw=(r0.yzx*cb0[8].yzx);
r0.w=asfloat((r1.x>=r1.y)?0xffffffffu:0u);
r0.w=asfloat(asuint(r0.w)&asuint(asfloat(0x3f800000u)));
r2.xy=r1.yx;
r3.xy=mad(r0.yz,cb0[8].yz,-r2.xy);
r0.xyz=(r0.xyz*cb0[14].xyz);
r2.zw=float2(asfloat(0xbf800000u),asfloat(0x3f2aaaabu));
r3.zw=float2(asfloat(0x3f800000u),asfloat(0xbf800000u));
r2.xyzw=mad(r0.wwww,r3.xyzw,r2.xyzw);
r0.w=asfloat((r1.w>=r2.x)?0xffffffffu:0u);
r0.w=asfloat(asuint(r0.w)&asuint(asfloat(0x3f800000u)));
r1.xyz=r2.xyw;
r2.xyw=r1.wyx;
r2.xyzw=(-r1.xyzw+r2.xyzw);
r1.xyzw=mad(r0.wwww,r2.xyzw,r1.xyzw);
r0.w=min(r1.y,r1.w);
r0.w=(-r0.w+r1.x);
r2.x=mad(r0.w,asfloat(0x40c00000u),asfloat(0x2edbe6ffu));
r1.y=(-r1.y+r1.w);
r1.y=(r1.y/r2.x);
r1.y=(r1.y+r1.z);
r1.x=(r1.x+asfloat(0x2edbe6ffu));
r0.w=(r0.w/r1.x);
r0.w=(r0.w*asfloat(0x3f000000u));
r1.xyz=(abs(r1.yyy)+float3(asfloat(0x00000000u),asfloat(0xbeaaaaabu),asfloat(0x3eaaaaabu)));
r1.xyz=frac(r1.xyz);
r1.xyz=mad(-r1.xyz,float3(asfloat(0x40000000u),asfloat(0x40000000u),asfloat(0x40000000u)),float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.xyz=saturate(mad(abs(r1.xyz),float3(asfloat(0x40400000u),asfloat(0x40400000u),asfloat(0x40400000u)),float3(asfloat(0xbf800000u),asfloat(0xbf800000u),asfloat(0xbf800000u))));
r1.xyz=(r1.xyz+float3(asfloat(0xbf800000u),asfloat(0xbf800000u),asfloat(0xbf800000u)));
r1.xyz=mad(r0.www,r1.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r2.xyz=float3(asfloat((asfloat(0x3f0e38e4u)<r1.x)?0xffffffffu:0u),asfloat((asfloat(0x3f0e38e4u)<r1.y)?0xffffffffu:0u),asfloat((asfloat(0x3f0e38e4u)<r1.z)?0xffffffffu:0u));
r3.xyz=mad(r1.xyz,float3(asfloat(0x3f666666u),asfloat(0x3f666666u),asfloat(0x3f666666u)),float3(asfloat(0xbf000000u),asfloat(0xbf000000u),asfloat(0xbf000000u)));
r3.xyz=mad(-r3.xyz,float3(asfloat(0x40000000u),asfloat(0x40000000u),asfloat(0x40000000u)),float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r4.xyzw=(-cb0[13].wxyz+float4(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r4.xyz=mad(-r4.xxx,r4.yzw,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.w=mad(cb0[13].w,asfloat(0x3f000000u),asfloat(0x3f000000u));
r1.w=(r0.w+r0.w);
r0.w=asfloat((asfloat(0x3f000000u)<r0.w)?0xffffffffu:0u);
r5.xyz=(r1.www*cb0[13].xyz);
r4.xyz=saturate(float3((asuint(r0.w)!=0u?r4.x:r5.x),(asuint(r0.w)!=0u?r4.y:r5.y),(asuint(r0.w)!=0u?r4.z:r5.z)));
r5.xyz=(-r4.xyz+float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.xyz=(r1.xyz*r4.xyz);
r1.xyz=(r1.xyz*float3(asfloat(0x3fe66666u),asfloat(0x3fe66666u),asfloat(0x3fe66666u)));
r3.xyz=mad(-r3.xyz,r5.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.xyz=saturate(float3((asuint(r2.x)!=0u?r3.x:r1.x),(asuint(r2.y)!=0u?r3.y:r1.y),(asuint(r2.z)!=0u?r3.z:r1.z)));
r2.xy=mad(v1.xy,cb0[9].xy,cb0[9].zw);
r2.xyzw=(Texture3.Sample(Sampler2,float2((r2.xy).x,lerp((r2.xy).y,1.0-(r2.xy).y,UVFlip)))).xyzw;
r2.yz=mad(v1.xy,cb0[11].xy,cb0[11].zw);
r3.xyzw=(Texture2.Sample(Sampler3,float2((r2.yz).x,lerp((r2.yz).y,1.0-(r2.yz).y,UVFlip)))).xyzw;
r0.w=(r2.x+-r3.x);
r0.w=mad(cb0[15].x,r0.w,r3.x);
r1.w=(-r0.w+asfloat(0x3f800000u));
r0.w=mad(cb0[12].y,r1.w,r0.w);
r1.w=(-cb0[12].x+asfloat(0x3f800000u));
r0.w=(-r0.w+r1.w);
r0.w=saturate((r0.w+asfloat(0x3f800000u)));
r0.w=mad(r0.w,asfloat(0x3f2b851fu),asfloat(0x3ea8f5c3u));
r1.xyz=(r0.www*r1.xyz);
r2.xyz=mad(-r0.xyz,r1.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xyz=(r0.xyz*r1.xyz);
r0.w=(cb0[14].w+asfloat(0xbf000000u));
r0.w=mad(-r0.w,asfloat(0x40000000u),asfloat(0x3f800000u));
r1.xyz=mad(-r0.www,r2.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.w=(cb0[14].w+cb0[14].w);
r0.xyz=(r0.xyz*r0.www);
r0.w=asfloat((asfloat(0x3f000000u)<cb0[14].w)?0xffffffffu:0u);
o0.xyz=saturate(float3((asuint(r0.w)!=0u?r1.x:r0.x),(asuint(r0.w)!=0u?r1.y:r0.y),(asuint(r0.w)!=0u?r1.z:r0.z)));
o0.w=asfloat(0x00000000u);
return o0;
)OKSHADER")
;
static const int32 GForgeItemOutlineVertexCBSize[]={16,5,7,21};
static const FOKAdditionalUniform GForgeItemOutlineVertexUniforms[]={
{TEXT("_DetailMask_ST"),0,144,4,0,0},
{TEXT("_linewidthG"),0,200,1,0,0},
{TEXT("_LineWidthS"),0,244,1,0,0},
{TEXT("_WorldSpaceCameraPos"),1,64,3,0,0},
{TEXT("unity_ObjectToWorld"),2,0,4,0,4},
{TEXT("unity_WorldToObject"),2,64,4,0,4},
{TEXT("unity_MatrixVP"),3,272,4,0,4},
};
static const TArray<FOKAdditionalTexture> GForgeItemOutlineVertexTextures={
{TEXT("_DetailMask"),0,0,2},
};
static const TCHAR* GForgeItemOutlineVertexProgram=
TEXT(R"OKSHADER(
float4 v0=OKPosition;
float4 v1=OKNormal;
float4 v2=OKUV0;
precise float4 o0=0.0;
precise float4 o1=0.0;
precise float4 o2=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
precise float4 r2=0.0;
r0.xyzw=(v0.yyyy*cb2[1].xyzw);
r0.xyzw=mad(cb2[0].xyzw,v0.xxxx,r0.xyzw);
r0.xyzw=mad(cb2[2].xyzw,v0.zzzz,r0.xyzw);
r0.xyzw=mad(cb2[3].xyzw,v0.wwww,r0.xyzw);
r1.xyz=(-r0.xyz+cb1[4].xyz);
o2.xyzw=r0.xyzw;
r0.x=dot(r1.xyz,r1.xyz);
r0.x=sqrt(r0.x);
r0.x=mad(r0.x,asfloat(0x3dcccccbu),asfloat(0x3e99999au));
r0.y=(cb0[12].z*asfloat(0x3ba3d70au));
r0.x=saturate((r0.x*r0.y));
r0.yz=mad(v2.xy,cb0[9].xy,cb0[9].zw);
r1.xyzw=(Texture0.SampleLevel(Sampler0,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.y=(-r1.z+asfloat(0x3f800000u));
r0.x=(r0.y*r0.x);
r0.x=(r0.x*cb0[15].y);
r1.x=cb2[4].x;
r1.y=cb2[5].x;
r1.z=cb2[6].x;
r0.y=dot(r1.xyz,r1.xyz);
r0.y=sqrt(r0.y);
r1.x=(asfloat(0x3f800000u)/r0.y);
r2.x=cb2[4].y;
r2.y=cb2[5].y;
r2.z=cb2[6].y;
r0.y=dot(r2.xyz,r2.xyz);
r0.y=sqrt(r0.y);
r1.y=(asfloat(0x3f800000u)/r0.y);
r2.x=cb2[4].z;
r2.y=cb2[5].z;
r2.z=cb2[6].z;
r0.y=dot(r2.xyz,r2.xyz);
r0.y=sqrt(r0.y);
r1.z=(asfloat(0x3f800000u)/r0.y);
r0.xyz=(r0.xxx/r1.xyz);
r0.xyz=mad(v1.xyz,r0.xyz,v0.xyz);
r1.xyzw=(r0.yyyy*cb2[1].xyzw);
r1.xyzw=mad(cb2[0].xyzw,r0.xxxx,r1.xyzw);
r0.xyzw=mad(cb2[2].xyzw,r0.zzzz,r1.xyzw);
r0.xyzw=(r0.xyzw+cb2[3].xyzw);
r1.xyzw=(r0.yyyy*cb3[18].xyzw);
r1.xyzw=mad(cb3[17].xyzw,r0.xxxx,r1.xyzw);
r1.xyzw=mad(cb3[19].xyzw,r0.zzzz,r1.xyzw);
o0.xyzw=mad(cb3[20].xyzw,r0.wwww,r1.xyzw);
o1.xy=v2.xy;
)OKSHADER")
;
// Shader Forge/main_hair_front; original DXBC a39a9d4ea0307e0d5f8e77d75f1b345731e0a7d2b2672122f66982a2b28f71a3
static const int32 GForgeHairFrontOutlineCBSize[]={11};
static const FOKAdditionalUniform GForgeHairFrontOutlineUniforms[]={
{TEXT("_LightColor0"),0,32,4,0,0},
{TEXT("_MainTex_ST"),0,48,4,0,0},
{TEXT("_LineColor"),0,64,4,0,0},
{TEXT("_AlphaMask_ST"),0,80,4,0,0},
{TEXT("_Color"),0,112,4,0,0},
{TEXT("_ColorMask_ST"),0,128,4,0,0},
{TEXT("_Color2"),0,144,4,0,0},
{TEXT("_Color3"),0,160,4,0,0},
};
static const TArray<FOKAdditionalTexture> GForgeHairFrontOutlineTextures={
{TEXT("_AlphaMask"),0,1,2},
{TEXT("_MainTex"),1,0,2},
{TEXT("_ColorMask"),2,2,2},
{TEXT("_DetailMask"),3,3,2},
};
static const FOKAdditionalProperty GForgeHairFrontOutlineProperties[]={
{TEXT("_AnotherRamp"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_MainTex"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_NormalMap"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("bump")},
{TEXT("_AlphaMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_DetailMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_HairGloss"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_ShadowColor"),0,false,FLinearColor(0.62800002098083496f,0.62800002098083496f,0.62800002098083496f,1.0f),TEXT("")},
{TEXT("_SpeclarHeight"),3,false,FLinearColor(0.98500001430511475f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_rimpower"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_rimV"),3,false,FLinearColor(0.75f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ShadowExtend"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ColorMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_Color"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_Color2"),0,false,FLinearColor(0.78431367874145508f,0.78431367874145508f,0.78431367874145508f,1.0f),TEXT("")},
{TEXT("_Color3"),0,false,FLinearColor(0.5f,0.5f,0.5f,1.0f),TEXT("")},
{TEXT("_LineColor"),0,false,FLinearColor(0.5f,0.5f,0.5f,1.0f),TEXT("")},
{TEXT("_Cutoff"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
};
static const TCHAR* GForgeHairFrontOutlineProgram=
TEXT(R"OKSHADER(
precise float4 o0=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
precise float4 r2=0.0;
precise float4 r3=0.0;
r0.xy=mad(v1.xy,cb0[5].xy,cb0[5].zw);
r0.xyzw=(Texture0.Sample(Sampler1,float2((r0.xy).x,lerp((r0.xy).y,1.0-(r0.xy).y,UVFlip)))).xyzw;
r0.yz=mad(v1.xy,cb0[3].xy,cb0[3].zw);
r1.xyzw=(Texture1.Sample(Sampler0,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)))).xyzw;
r0.x=mad(r0.x,r1.w,asfloat(0xbf000000u));
r0.x=asfloat((r0.x<asfloat(0x00000000u))?0xffffffffu:0u);
if(asuint(r0.x)!=0u) clip(-1.0);
r0.xyz=(cb0[7].xyz+float3(asfloat(0xbf800000u),asfloat(0xbf800000u),asfloat(0xbf800000u)));
r2.xy=mad(v1.xy,cb0[8].xy,cb0[8].zw);
r2.xyzw=(Texture2.Sample(Sampler2,float2((r2.xy).x,lerp((r2.xy).y,1.0-(r2.xy).y,UVFlip)))).xyzw;
r0.xyz=mad(r2.xxx,r0.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r3.xyz=(-r0.xyz+cb0[9].xyz);
r0.xyz=mad(r2.yyy,r3.xyz,r0.xyz);
r2.xyw=(-r0.xyz+cb0[10].xyz);
r0.xyz=mad(r2.zzz,r2.xyw,r0.xyz);
r2.xyz=mad(-r0.xyz,r1.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xyz=(r1.xyz*r0.xyz);
r0.xyz=(r0.xyz*cb0[4].xyz);
r0.xyz=(r0.xyz+r0.xyz);
r1.xyz=(cb0[4].xyz+float3(asfloat(0xbf000000u),asfloat(0xbf000000u),asfloat(0xbf000000u)));
r1.xyz=mad(-r1.xyz,float3(asfloat(0x40000000u),asfloat(0x40000000u),asfloat(0x40000000u)),float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.xyz=mad(-r1.xyz,r2.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r2.xyz=float3(asfloat((asfloat(0x3f000000u)<cb0[4].x)?0xffffffffu:0u),asfloat((asfloat(0x3f000000u)<cb0[4].y)?0xffffffffu:0u),asfloat((asfloat(0x3f000000u)<cb0[4].z)?0xffffffffu:0u));
r0.xyz=saturate(float3((asuint(r2.x)!=0u?r1.x:r0.x),(asuint(r2.y)!=0u?r1.y:r0.y),(asuint(r2.z)!=0u?r1.z:r0.z)));
r1.xyz=mad(cb0[2].xyz,float3(asfloat(0x3f19999au),asfloat(0x3f19999au),asfloat(0x3f19999au)),float3(asfloat(0x3ecccccdu),asfloat(0x3ecccccdu),asfloat(0x3ecccccdu)));
o0.xyz=(r0.xyz*r1.xyz);
o0.w=asfloat(0x00000000u);
return o0;
)OKSHADER")
;
static const int32 GForgeHairFrontOutlineVertexCBSize[]={12,5,4,21};
static const FOKAdditionalUniform GForgeHairFrontOutlineVertexUniforms[]={
{TEXT("_MainTex_ST"),0,48,4,0,0},
{TEXT("_AlphaMask_ST"),0,80,4,0,0},
{TEXT("_DetailMask_ST"),0,96,4,0,0},
{TEXT("_linewidthG"),0,176,1,0,0},
{TEXT("_WorldSpaceCameraPos"),1,64,3,0,0},
{TEXT("unity_ObjectToWorld"),2,0,4,0,4},
{TEXT("unity_MatrixVP"),3,272,4,0,4},
};
static const TArray<FOKAdditionalTexture> GForgeHairFrontOutlineVertexTextures={
{TEXT("_AlphaMask"),0,1,2},
{TEXT("_MainTex"),1,0,2},
{TEXT("_DetailMask"),2,2,2},
};
static const TCHAR* GForgeHairFrontOutlineVertexProgram=
TEXT(R"OKSHADER(
float4 v0=OKPosition;
float4 v1=OKNormal;
float4 v2=OKUV0;
precise float4 o0=0.0;
precise float4 o1=0.0;
precise float4 o2=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
r0.xy=mad(v2.xy,cb0[5].xy,cb0[5].zw);
r0.xyzw=(Texture0.SampleLevel(Sampler1,float2((r0.xy).x,lerp((r0.xy).y,1.0-(r0.xy).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.yz=mad(v2.xy,cb0[3].xy,cb0[3].zw);
r1.xyzw=(Texture1.SampleLevel(Sampler0,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.x=(r0.x*r1.w);
r1.xyzw=(v0.yyyy*cb2[1].xyzw);
r1.xyzw=mad(cb2[0].xyzw,v0.xxxx,r1.xyzw);
r1.xyzw=mad(cb2[2].xyzw,v0.zzzz,r1.xyzw);
r1.xyzw=mad(cb2[3].xyzw,v0.wwww,r1.xyzw);
r0.yzw=(r1.xyz+-cb1[4].xyz);
o2.xyzw=r1.xyzw;
r0.y=dot(r0.yzw,r0.yzw);
r0.y=sqrt(r0.y);
r0.y=mad(r0.y,asfloat(0x3dcccccbu),asfloat(0x3e99999au));
r0.z=(cb0[11].x*asfloat(0x3ba3d70au));
r0.y=(r0.y*r0.z);
r0.x=(r0.x*r0.y);
r0.yz=mad(v2.xy,cb0[6].xy,cb0[6].zw);
r1.xyzw=(Texture2.SampleLevel(Sampler2,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.y=(-r1.z+asfloat(0x3f800000u));
r0.x=(r0.y*r0.x);
r0.xyz=mad(v1.xyz,r0.xxx,v0.xyz);
r1.xyzw=(r0.yyyy*cb2[1].xyzw);
r1.xyzw=mad(cb2[0].xyzw,r0.xxxx,r1.xyzw);
r0.xyzw=mad(cb2[2].xyzw,r0.zzzz,r1.xyzw);
r0.xyzw=(r0.xyzw+cb2[3].xyzw);
r1.xyzw=(r0.yyyy*cb3[18].xyzw);
r1.xyzw=mad(cb3[17].xyzw,r0.xxxx,r1.xyzw);
r1.xyzw=mad(cb3[19].xyzw,r0.zzzz,r1.xyzw);
o0.xyzw=mad(cb3[20].xyzw,r0.wwww,r1.xyzw);
o1.xy=v2.xy;
)OKSHADER")
;
// Shader Forge/main_hair; original DXBC a39a9d4ea0307e0d5f8e77d75f1b345731e0a7d2b2672122f66982a2b28f71a3
static const int32 GForgeHairOutlineCBSize[]={11};
static const FOKAdditionalUniform GForgeHairOutlineUniforms[]={
{TEXT("_LightColor0"),0,32,4,0,0},
{TEXT("_MainTex_ST"),0,48,4,0,0},
{TEXT("_LineColor"),0,64,4,0,0},
{TEXT("_AlphaMask_ST"),0,80,4,0,0},
{TEXT("_Color"),0,112,4,0,0},
{TEXT("_ColorMask_ST"),0,128,4,0,0},
{TEXT("_Color2"),0,144,4,0,0},
{TEXT("_Color3"),0,160,4,0,0},
};
static const TArray<FOKAdditionalTexture> GForgeHairOutlineTextures={
{TEXT("_AlphaMask"),0,1,2},
{TEXT("_MainTex"),1,0,2},
{TEXT("_ColorMask"),2,2,2},
{TEXT("_DetailMask"),3,3,2},
};
static const FOKAdditionalProperty GForgeHairOutlineProperties[]={
{TEXT("_AnotherRamp"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_MainTex"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_NormalMap"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("bump")},
{TEXT("_AlphaMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_DetailMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_HairGloss"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_ShadowColor"),0,false,FLinearColor(0.62800002098083496f,0.62800002098083496f,0.62800002098083496f,1.0f),TEXT("")},
{TEXT("_SpeclarHeight"),3,false,FLinearColor(0.85000002384185791f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_rimpower"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_rimV"),3,false,FLinearColor(0.75f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ShadowExtend"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ColorMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_Color"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_Color2"),0,false,FLinearColor(0.78431367874145508f,0.78431367874145508f,0.78431367874145508f,1.0f),TEXT("")},
{TEXT("_Color3"),0,false,FLinearColor(0.5f,0.5f,0.5f,1.0f),TEXT("")},
{TEXT("_LineColor"),0,false,FLinearColor(0.5f,0.5f,0.5f,1.0f),TEXT("")},
{TEXT("_Cutoff"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
};
static const TCHAR* GForgeHairOutlineProgram=
TEXT(R"OKSHADER(
precise float4 o0=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
precise float4 r2=0.0;
precise float4 r3=0.0;
r0.xy=mad(v1.xy,cb0[5].xy,cb0[5].zw);
r0.xyzw=(Texture0.Sample(Sampler1,float2((r0.xy).x,lerp((r0.xy).y,1.0-(r0.xy).y,UVFlip)))).xyzw;
r0.yz=mad(v1.xy,cb0[3].xy,cb0[3].zw);
r1.xyzw=(Texture1.Sample(Sampler0,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)))).xyzw;
r0.x=mad(r0.x,r1.w,asfloat(0xbf000000u));
r0.x=asfloat((r0.x<asfloat(0x00000000u))?0xffffffffu:0u);
if(asuint(r0.x)!=0u) clip(-1.0);
r0.xyz=(cb0[7].xyz+float3(asfloat(0xbf800000u),asfloat(0xbf800000u),asfloat(0xbf800000u)));
r2.xy=mad(v1.xy,cb0[8].xy,cb0[8].zw);
r2.xyzw=(Texture2.Sample(Sampler2,float2((r2.xy).x,lerp((r2.xy).y,1.0-(r2.xy).y,UVFlip)))).xyzw;
r0.xyz=mad(r2.xxx,r0.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r3.xyz=(-r0.xyz+cb0[9].xyz);
r0.xyz=mad(r2.yyy,r3.xyz,r0.xyz);
r2.xyw=(-r0.xyz+cb0[10].xyz);
r0.xyz=mad(r2.zzz,r2.xyw,r0.xyz);
r2.xyz=mad(-r0.xyz,r1.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xyz=(r1.xyz*r0.xyz);
r0.xyz=(r0.xyz*cb0[4].xyz);
r0.xyz=(r0.xyz+r0.xyz);
r1.xyz=(cb0[4].xyz+float3(asfloat(0xbf000000u),asfloat(0xbf000000u),asfloat(0xbf000000u)));
r1.xyz=mad(-r1.xyz,float3(asfloat(0x40000000u),asfloat(0x40000000u),asfloat(0x40000000u)),float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.xyz=mad(-r1.xyz,r2.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r2.xyz=float3(asfloat((asfloat(0x3f000000u)<cb0[4].x)?0xffffffffu:0u),asfloat((asfloat(0x3f000000u)<cb0[4].y)?0xffffffffu:0u),asfloat((asfloat(0x3f000000u)<cb0[4].z)?0xffffffffu:0u));
r0.xyz=saturate(float3((asuint(r2.x)!=0u?r1.x:r0.x),(asuint(r2.y)!=0u?r1.y:r0.y),(asuint(r2.z)!=0u?r1.z:r0.z)));
r1.xyz=mad(cb0[2].xyz,float3(asfloat(0x3f19999au),asfloat(0x3f19999au),asfloat(0x3f19999au)),float3(asfloat(0x3ecccccdu),asfloat(0x3ecccccdu),asfloat(0x3ecccccdu)));
o0.xyz=(r0.xyz*r1.xyz);
o0.w=asfloat(0x00000000u);
return o0;
)OKSHADER")
;
static const int32 GForgeHairOutlineVertexCBSize[]={12,5,4,21};
static const FOKAdditionalUniform GForgeHairOutlineVertexUniforms[]={
{TEXT("_MainTex_ST"),0,48,4,0,0},
{TEXT("_AlphaMask_ST"),0,80,4,0,0},
{TEXT("_DetailMask_ST"),0,96,4,0,0},
{TEXT("_linewidthG"),0,176,1,0,0},
{TEXT("_WorldSpaceCameraPos"),1,64,3,0,0},
{TEXT("unity_ObjectToWorld"),2,0,4,0,4},
{TEXT("unity_MatrixVP"),3,272,4,0,4},
};
static const TArray<FOKAdditionalTexture> GForgeHairOutlineVertexTextures={
{TEXT("_AlphaMask"),0,1,2},
{TEXT("_MainTex"),1,0,2},
{TEXT("_DetailMask"),2,2,2},
};
static const TCHAR* GForgeHairOutlineVertexProgram=
TEXT(R"OKSHADER(
float4 v0=OKPosition;
float4 v1=OKNormal;
float4 v2=OKUV0;
precise float4 o0=0.0;
precise float4 o1=0.0;
precise float4 o2=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
r0.xy=mad(v2.xy,cb0[5].xy,cb0[5].zw);
r0.xyzw=(Texture0.SampleLevel(Sampler1,float2((r0.xy).x,lerp((r0.xy).y,1.0-(r0.xy).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.yz=mad(v2.xy,cb0[3].xy,cb0[3].zw);
r1.xyzw=(Texture1.SampleLevel(Sampler0,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.x=(r0.x*r1.w);
r1.xyzw=(v0.yyyy*cb2[1].xyzw);
r1.xyzw=mad(cb2[0].xyzw,v0.xxxx,r1.xyzw);
r1.xyzw=mad(cb2[2].xyzw,v0.zzzz,r1.xyzw);
r1.xyzw=mad(cb2[3].xyzw,v0.wwww,r1.xyzw);
r0.yzw=(r1.xyz+-cb1[4].xyz);
o2.xyzw=r1.xyzw;
r0.y=dot(r0.yzw,r0.yzw);
r0.y=sqrt(r0.y);
r0.y=mad(r0.y,asfloat(0x3dcccccbu),asfloat(0x3e99999au));
r0.z=(cb0[11].x*asfloat(0x3ba3d70au));
r0.y=(r0.y*r0.z);
r0.x=(r0.x*r0.y);
r0.yz=mad(v2.xy,cb0[6].xy,cb0[6].zw);
r1.xyzw=(Texture2.SampleLevel(Sampler2,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.y=(-r1.z+asfloat(0x3f800000u));
r0.x=(r0.y*r0.x);
r0.xyz=mad(v1.xyz,r0.xxx,v0.xyz);
r1.xyzw=(r0.yyyy*cb2[1].xyzw);
r1.xyzw=mad(cb2[0].xyzw,r0.xxxx,r1.xyzw);
r0.xyzw=mad(cb2[2].xyzw,r0.zzzz,r1.xyzw);
r0.xyzw=(r0.xyzw+cb2[3].xyzw);
r1.xyzw=(r0.yyyy*cb3[18].xyzw);
r1.xyzw=mad(cb3[17].xyzw,r0.xxxx,r1.xyzw);
r1.xyzw=mad(cb3[19].xyzw,r0.zzzz,r1.xyzw);
o0.xyzw=mad(cb3[20].xyzw,r0.wwww,r1.xyzw);
o1.xy=v2.xy;
)OKSHADER")
;
// Shader Forge/main_alpha; original DXBC 9204142e710d8744f70d9c5ebfc08abc2854402d22e2b0b748ebbeaf8df6b130
static const int32 GForgeAlphaMaskedOutlineCBSize[]={11};
static const FOKAdditionalUniform GForgeAlphaMaskedOutlineUniforms[]={
{TEXT("_LightColor0"),0,32,4,0,0},
{TEXT("_MainTex_ST"),0,48,4,0,0},
{TEXT("_LineColorG"),0,64,4,0,0},
{TEXT("_ShadowColor"),0,80,4,0,0},
{TEXT("_AlphaMask_ST"),0,96,4,0,0},
{TEXT("_outline"),0,128,1,0,0},
{TEXT("_ambientshadowG"),0,144,4,0,0},
{TEXT("_alpha_a"),0,160,1,0,0},
{TEXT("_alpha_b"),0,164,1,0,0},
};
static const TArray<FOKAdditionalTexture> GForgeAlphaMaskedOutlineTextures={
{TEXT("_MainTex"),0,0,2},
{TEXT("_AlphaMask"),1,1,2},
{TEXT("_DetailMask"),2,2,2},
};
static const FOKAdditionalProperty GForgeAlphaMaskedOutlineProperties[]={
{TEXT("_AnotherRamp"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_MainTex"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_NormalMap"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("bump")},
{TEXT("_DetailMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_AlphaMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_ShadowColor"),0,false,FLinearColor(0.62800002098083496f,0.62800002098083496f,0.62800002098083496f,1.0f),TEXT("")},
{TEXT("_SpecularColor"),0,false,FLinearColor(1.0f,1.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpeclarHeight"),3,false,FLinearColor(0.98500001430511475f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpecularPower"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_rimpower"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_rimV"),3,false,FLinearColor(0.75f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ShadowExtend"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_outline"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_DetailBLineGA"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_LiquidTiling"),1,false,FLinearColor(0.0f,0.0f,2.0f,2.0f),TEXT("")},
{TEXT("_liquidmask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_Texture2"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_Texture3"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("bump")},
{TEXT("_liquidftop"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidfbot"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidbtop"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidbbot"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidface"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_alpha_a"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_alpha_b"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_Cutoff"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
};
static const TCHAR* GForgeAlphaMaskedOutlineProgram=
TEXT(R"OKSHADER(
precise float4 o0=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
precise float4 r2=0.0;
precise float4 r3=0.0;
precise float4 r4=0.0;
precise float4 r5=0.0;
r0.x=(-cb0[8].x+asfloat(0x3f800000u));
r0.x=(asuint(v3.x)!=0u?asfloat(0x3f800000u):r0.x);
r0.yz=mad(v1.xy,cb0[6].xy,cb0[6].zw);
r1.xyzw=(Texture1.Sample(Sampler1,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)))).xyzw;
r0.yz=(-cb0[10].xy+float2(asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.yz=max(r0.yz,r1.xy);
r0.y=min(r0.z,r0.y);
r0.zw=mad(v1.xy,cb0[3].xy,cb0[3].zw);
r1.xyzw=(Texture0.Sample(Sampler0,float2((r0.zw).x,lerp((r0.zw).y,1.0-(r0.zw).y,UVFlip)))).xyzw;
r0.y=min(r0.y,r1.w);
r0.y=(r0.y*asfloat(0x41200000u));
r0.x=min(r0.x,r0.y);
r0.x=(r0.x+asfloat(0xbf000000u));
r0.x=asfloat((r0.x<asfloat(0x00000000u))?0xffffffffu:0u);
if(asuint(r0.x)!=0u) clip(-1.0);
r0.zw=float2(asfloat(0xbf800000u),asfloat(0x3f2aaaabu));
r2.zw=float2(asfloat(0x3f800000u),asfloat(0xbf800000u));
r3.xyw=(r1.yzx*cb0[5].yzx);
r0.xy=r3.yx;
r2.xy=mad(r1.yz,cb0[5].yz,-r0.xy);
r1.xyz=(r1.xyz*cb0[4].xyz);
r1.w=asfloat((r0.y>=r3.y)?0xffffffffu:0u);
r1.w=asfloat(asuint(r1.w)&asuint(asfloat(0x3f800000u)));
r0.xyzw=mad(r1.wwww,r2.xyzw,r0.xyzw);
r1.w=asfloat((r3.w>=r0.x)?0xffffffffu:0u);
r1.w=asfloat(asuint(r1.w)&asuint(asfloat(0x3f800000u)));
r3.xyz=r0.xyw;
r0.xyw=r3.wyx;
r0.xyzw=(-r3.xyzw+r0.xyzw);
r0.xyzw=mad(r1.wwww,r0.xyzw,r3.xyzw);
r1.w=min(r0.y,r0.w);
r1.w=(r0.x+-r1.w);
r2.x=mad(r1.w,asfloat(0x40c00000u),asfloat(0x2edbe6ffu));
r0.y=(-r0.y+r0.w);
r0.y=(r0.y/r2.x);
r0.y=(r0.y+r0.z);
r0.x=(r0.x+asfloat(0x2edbe6ffu));
r0.x=(r1.w/r0.x);
r0.x=(r0.x*asfloat(0x3f000000u));
r0.yzw=(abs(r0.yyy)+float3(asfloat(0x00000000u),asfloat(0xbeaaaaabu),asfloat(0x3eaaaaabu)));
r0.yzw=frac(r0.yzw);
r0.yzw=mad(-r0.yzw,float3(asfloat(0x40000000u),asfloat(0x40000000u),asfloat(0x40000000u)),float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.yzw=saturate(mad(abs(r0.yzw),float3(asfloat(0x40400000u),asfloat(0x40400000u),asfloat(0x40400000u)),float3(asfloat(0xbf800000u),asfloat(0xbf800000u),asfloat(0xbf800000u))));
r0.yzw=(r0.yzw+float3(asfloat(0xbf800000u),asfloat(0xbf800000u),asfloat(0xbf800000u)));
r0.xyz=mad(r0.xxx,r0.yzw,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r2.xyz=float3(asfloat((asfloat(0x3f0e38e4u)<r0.x)?0xffffffffu:0u),asfloat((asfloat(0x3f0e38e4u)<r0.y)?0xffffffffu:0u),asfloat((asfloat(0x3f0e38e4u)<r0.z)?0xffffffffu:0u));
r3.xyz=mad(r0.xyz,float3(asfloat(0x3f666666u),asfloat(0x3f666666u),asfloat(0x3f666666u)),float3(asfloat(0xbf000000u),asfloat(0xbf000000u),asfloat(0xbf000000u)));
r3.xyz=mad(-r3.xyz,float3(asfloat(0x40000000u),asfloat(0x40000000u),asfloat(0x40000000u)),float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r4.xyzw=(-cb0[9].wxyz+float4(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r4.xyz=mad(-r4.xxx,r4.yzw,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.w=mad(cb0[9].w,asfloat(0x3f000000u),asfloat(0x3f000000u));
r1.w=(r0.w+r0.w);
r0.w=asfloat((asfloat(0x3f000000u)<r0.w)?0xffffffffu:0u);
r5.xyz=(r1.www*cb0[9].xyz);
r4.xyz=saturate(float3((asuint(r0.w)!=0u?r4.x:r5.x),(asuint(r0.w)!=0u?r4.y:r5.y),(asuint(r0.w)!=0u?r4.z:r5.z)));
r5.xyz=(-r4.xyz+float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xyz=(r0.xyz*r4.xyz);
r0.xyz=(r0.xyz*float3(asfloat(0x3fe66666u),asfloat(0x3fe66666u),asfloat(0x3fe66666u)));
r3.xyz=mad(-r3.xyz,r5.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xyz=saturate(float3((asuint(r2.x)!=0u?r3.x:r0.x),(asuint(r2.y)!=0u?r3.y:r0.y),(asuint(r2.z)!=0u?r3.z:r0.z)));
r2.xyz=mad(-r1.xyz,r0.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xyz=(r0.xyz*r1.xyz);
r0.w=(cb0[4].w+asfloat(0xbf000000u));
r0.w=mad(-r0.w,asfloat(0x40000000u),asfloat(0x3f800000u));
r1.xyz=mad(-r0.www,r2.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.w=(cb0[4].w+cb0[4].w);
r0.xyz=(r0.xyz*r0.www);
r0.w=asfloat((asfloat(0x3f000000u)<cb0[4].w)?0xffffffffu:0u);
r0.xyz=saturate(float3((asuint(r0.w)!=0u?r1.x:r0.x),(asuint(r0.w)!=0u?r1.y:r0.y),(asuint(r0.w)!=0u?r1.z:r0.z)));
r1.xyz=mad(cb0[2].xyz,float3(asfloat(0x3f19999au),asfloat(0x3f19999au),asfloat(0x3f19999au)),float3(asfloat(0x3ecccccdu),asfloat(0x3ecccccdu),asfloat(0x3ecccccdu)));
o0.xyz=(r0.xyz*r1.xyz);
o0.w=asfloat(0x00000000u);
return o0;
)OKSHADER")
;
static const int32 GForgeAlphaMaskedOutlineVertexCBSize[]={11,5,4,21};
static const FOKAdditionalUniform GForgeAlphaMaskedOutlineVertexUniforms[]={
{TEXT("_MainTex_ST"),0,48,4,0,0},
{TEXT("_AlphaMask_ST"),0,96,4,0,0},
{TEXT("_DetailMask_ST"),0,112,4,0,0},
{TEXT("_alpha_a"),0,160,1,0,0},
{TEXT("_alpha_b"),0,164,1,0,0},
{TEXT("_linewidthG"),0,168,1,0,0},
{TEXT("_WorldSpaceCameraPos"),1,64,3,0,0},
{TEXT("unity_ObjectToWorld"),2,0,4,0,4},
{TEXT("unity_MatrixVP"),3,272,4,0,4},
};
static const TArray<FOKAdditionalTexture> GForgeAlphaMaskedOutlineVertexTextures={
{TEXT("_DetailMask"),0,2,2},
{TEXT("_MainTex"),1,0,2},
{TEXT("_AlphaMask"),2,1,2},
};
static const TCHAR* GForgeAlphaMaskedOutlineVertexProgram=
TEXT(R"OKSHADER(
float4 v0=OKPosition;
float4 v1=OKNormal;
float4 v2=OKUV0;
precise float4 o0=0.0;
precise float4 o1=0.0;
precise float4 o2=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
r0.xy=mad(v2.xy,cb0[6].xy,cb0[6].zw);
r0.xyzw=(Texture2.SampleLevel(Sampler1,float2((r0.xy).x,lerp((r0.xy).y,1.0-(r0.xy).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.zw=(-cb0[10].xy+float2(asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xy=max(r0.zw,r0.xy);
r0.x=min(r0.y,r0.x);
r0.yz=mad(v2.xy,cb0[3].xy,cb0[3].zw);
r1.xyzw=(Texture1.SampleLevel(Sampler0,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.x=min(r0.x,r1.w);
r1.xyzw=(v0.yyyy*cb2[1].xyzw);
r1.xyzw=mad(cb2[0].xyzw,v0.xxxx,r1.xyzw);
r1.xyzw=mad(cb2[2].xyzw,v0.zzzz,r1.xyzw);
r1.xyzw=mad(cb2[3].xyzw,v0.wwww,r1.xyzw);
r0.yzw=(r1.xyz+-cb1[4].xyz);
o2.xyzw=r1.xyzw;
r0.y=dot(r0.yzw,r0.yzw);
r0.y=sqrt(r0.y);
r0.y=mad(r0.y,asfloat(0x3dcccccbu),asfloat(0x3e99999au));
r0.z=(cb0[10].z*asfloat(0x3ba3d70au));
r0.y=(r0.y*r0.z);
r0.zw=mad(v2.xy,cb0[7].xy,cb0[7].zw);
r1.xyzw=(Texture0.SampleLevel(Sampler2,float2((r0.zw).x,lerp((r0.zw).y,1.0-(r0.zw).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.z=(-r1.z+asfloat(0x3f800000u));
r0.y=(r0.z*r0.y);
r0.x=(r0.x*r0.y);
r0.xyz=mad(v1.xyz,r0.xxx,v0.xyz);
r1.xyzw=(r0.yyyy*cb2[1].xyzw);
r1.xyzw=mad(cb2[0].xyzw,r0.xxxx,r1.xyzw);
r0.xyzw=mad(cb2[2].xyzw,r0.zzzz,r1.xyzw);
r0.xyzw=(r0.xyzw+cb2[3].xyzw);
r1.xyzw=(r0.yyyy*cb3[18].xyzw);
r1.xyzw=mad(cb3[17].xyzw,r0.xxxx,r1.xyzw);
r1.xyzw=mad(cb3[19].xyzw,r0.zzzz,r1.xyzw);
o0.xyzw=mad(cb3[20].xyzw,r0.wwww,r1.xyzw);
o1.xy=v2.xy;
)OKSHADER")
;
// Shader Forge/main_opaque; original DXBC 9ac2552e44318d9856c301afeae58c7e841b691e3010a1fbaed3e06782b9ab89
static const int32 GForgeOpaqueOutlineCBSize[]={13};
static const FOKAdditionalUniform GForgeOpaqueOutlineUniforms[]={
{TEXT("_LightColor0"),0,32,4,0,0},
{TEXT("_MainTex_ST"),0,48,4,0,0},
{TEXT("_LineColorG"),0,64,4,0,0},
{TEXT("_ShadowColor"),0,80,4,0,0},
{TEXT("_DetailMask_ST"),0,96,4,0,0},
{TEXT("_LineMask_ST"),0,112,4,0,0},
{TEXT("_alpha_a"),0,128,1,0,0},
{TEXT("_alpha_b"),0,132,1,0,0},
{TEXT("_AlphaMask_ST"),0,144,4,0,0},
{TEXT("_ambientshadowG"),0,176,4,0,0},
{TEXT("_DetailRLineR"),0,192,1,0,0},
{TEXT("_ShadowExtendAnother"),0,196,1,0,0},
};
static const TArray<FOKAdditionalTexture> GForgeOpaqueOutlineTextures={
{TEXT("_MainTex"),0,0,2},
{TEXT("_AlphaMask"),1,3,2},
{TEXT("_LineMask"),2,2,2},
{TEXT("_DetailMask"),3,1,2},
};
static const FOKAdditionalProperty GForgeOpaqueOutlineProperties[]={
{TEXT("_AnotherRamp"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_MainTex"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_NormalMap"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("bump")},
{TEXT("_DetailMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_LineMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_AlphaMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_ShadowColor"),0,false,FLinearColor(0.62800002098083496f,0.62800002098083496f,0.62800002098083496f,1.0f),TEXT("")},
{TEXT("_SpecularColor"),0,false,FLinearColor(1.0f,1.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpeclarHeight"),3,false,FLinearColor(0.98000001907348633f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpecularPower"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpecularPowerNail"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ShadowExtend"),3,false,FLinearColor(1.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ShadowExtendAnother"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_rimpower"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_rimV"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_alpha_a"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_alpha_b"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_DetailBLineG"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_DetailRLineR"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_notusetexspecular"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_liquidmask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_Texture2"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_Texture3"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("bump")},
{TEXT("_LiquidTiling"),1,false,FLinearColor(0.0f,0.0f,2.0f,2.0f),TEXT("")},
{TEXT("_liquidftop"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidfbot"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidbtop"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidbbot"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidface"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_Cutoff"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
};
static const TCHAR* GForgeOpaqueOutlineProgram=
TEXT(R"OKSHADER(
precise float4 o0=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
precise float4 r2=0.0;
precise float4 r3=0.0;
precise float4 r4=0.0;
precise float4 r5=0.0;
r0.xy=mad(v1.xy,cb0[9].xy,cb0[9].zw);
r0.xyzw=(Texture1.Sample(Sampler3,float2((r0.xy).x,lerp((r0.xy).y,1.0-(r0.xy).y,UVFlip)))).xyzw;
r0.zw=(-cb0[8].xy+float2(asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xy=max(r0.zw,r0.xy);
r0.x=min(r0.y,r0.x);
r0.yz=mad(v1.xy,cb0[3].xy,cb0[3].zw);
r1.xyzw=(Texture0.Sample(Sampler0,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)))).xyzw;
r0.x=min(r0.x,r1.w);
r0.x=(r0.x+asfloat(0xbf000000u));
r0.x=asfloat((r0.x<asfloat(0x00000000u))?0xffffffffu:0u);
if(asuint(r0.x)!=0u) clip(-1.0);
r0.zw=float2(asfloat(0xbf800000u),asfloat(0x3f2aaaabu));
r2.zw=float2(asfloat(0x3f800000u),asfloat(0xbf800000u));
r3.xyw=(r1.yzx*cb0[5].yzx);
r0.xy=r3.yx;
r2.xy=mad(r1.yz,cb0[5].yz,-r0.xy);
r1.w=asfloat((r0.y>=r3.y)?0xffffffffu:0u);
r1.w=asfloat(asuint(r1.w)&asuint(asfloat(0x3f800000u)));
r0.xyzw=mad(r1.wwww,r2.xyzw,r0.xyzw);
r1.w=asfloat((r3.w>=r0.x)?0xffffffffu:0u);
r1.w=asfloat(asuint(r1.w)&asuint(asfloat(0x3f800000u)));
r3.xyz=r0.xyw;
r0.xyw=r3.wyx;
r0.xyzw=(-r3.xyzw+r0.xyzw);
r0.xyzw=mad(r1.wwww,r0.xyzw,r3.xyzw);
r1.w=min(r0.y,r0.w);
r1.w=(r0.x+-r1.w);
r2.x=mad(r1.w,asfloat(0x40c00000u),asfloat(0x2edbe6ffu));
r0.y=(-r0.y+r0.w);
r0.y=(r0.y/r2.x);
r0.y=(r0.y+r0.z);
r0.x=(r0.x+asfloat(0x2edbe6ffu));
r0.x=(r1.w/r0.x);
r0.x=(r0.x*asfloat(0x3f000000u));
r0.yzw=(abs(r0.yyy)+float3(asfloat(0x00000000u),asfloat(0xbeaaaaabu),asfloat(0x3eaaaaabu)));
r0.yzw=frac(r0.yzw);
r0.yzw=mad(-r0.yzw,float3(asfloat(0x40000000u),asfloat(0x40000000u),asfloat(0x40000000u)),float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.yzw=saturate(mad(abs(r0.yzw),float3(asfloat(0x40400000u),asfloat(0x40400000u),asfloat(0x40400000u)),float3(asfloat(0xbf800000u),asfloat(0xbf800000u),asfloat(0xbf800000u))));
r0.yzw=(r0.yzw+float3(asfloat(0xbf800000u),asfloat(0xbf800000u),asfloat(0xbf800000u)));
r0.xyz=mad(r0.xxx,r0.yzw,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r2.xyz=float3(asfloat((asfloat(0x3f0e38e4u)<r0.x)?0xffffffffu:0u),asfloat((asfloat(0x3f0e38e4u)<r0.y)?0xffffffffu:0u),asfloat((asfloat(0x3f0e38e4u)<r0.z)?0xffffffffu:0u));
r3.xyz=mad(r0.xyz,float3(asfloat(0x3f666666u),asfloat(0x3f666666u),asfloat(0x3f666666u)),float3(asfloat(0xbf000000u),asfloat(0xbf000000u),asfloat(0xbf000000u)));
r3.xyz=mad(-r3.xyz,float3(asfloat(0x40000000u),asfloat(0x40000000u),asfloat(0x40000000u)),float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r4.xyzw=(-cb0[11].wxyz+float4(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r4.xyz=mad(-r4.xxx,r4.yzw,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.w=mad(cb0[11].w,asfloat(0x3f000000u),asfloat(0x3f000000u));
r1.w=(r0.w+r0.w);
r0.w=asfloat((asfloat(0x3f000000u)<r0.w)?0xffffffffu:0u);
r5.xyz=(r1.www*cb0[11].xyz);
r4.xyz=saturate(float3((asuint(r0.w)!=0u?r4.x:r5.x),(asuint(r0.w)!=0u?r4.y:r5.y),(asuint(r0.w)!=0u?r4.z:r5.z)));
r5.xyz=(-r4.xyz+float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xyz=(r0.xyz*r4.xyz);
r0.xyz=(r0.xyz*float3(asfloat(0x3fe66666u),asfloat(0x3fe66666u),asfloat(0x3fe66666u)));
r3.xyz=mad(-r3.xyz,r5.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xyz=saturate(float3((asuint(r2.x)!=0u?r3.x:r0.x),(asuint(r2.y)!=0u?r3.y:r0.y),(asuint(r2.z)!=0u?r3.z:r0.z)));
r2.xy=mad(v1.xy,cb0[6].xy,cb0[6].zw);
r2.xyzw=(Texture3.Sample(Sampler1,float2((r2.xy).x,lerp((r2.xy).y,1.0-(r2.xy).y,UVFlip)))).xyzw;
r2.yz=mad(v1.xy,cb0[7].xy,cb0[7].zw);
r3.xyzw=(Texture2.Sample(Sampler2,float2((r2.yz).x,lerp((r2.yz).y,1.0-(r2.yz).y,UVFlip)))).xyzw;
r0.w=(r2.x+-r3.x);
r0.w=mad(cb0[12].x,r0.w,r3.x);
r0.w=(-r0.w+asfloat(0x3f800000u));
r1.w=(-cb0[12].y+asfloat(0x3f800000u));
r0.w=max(r0.w,r1.w);
r0.xyz=saturate((r0.www*r0.xyz));
r0.xyz=(r0.xyz*r1.xyz);
r1.xyz=(r0.xyz*float3(asfloat(0x3f000000u),asfloat(0x3f000000u),asfloat(0x3f000000u)));
r0.xyz=mad(-r0.xyz,float3(asfloat(0x3f000000u),asfloat(0x3f000000u),asfloat(0x3f000000u)),float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.w=(cb0[4].w+cb0[4].w);
r1.xyz=(r1.xyz*r0.www);
r0.w=(cb0[4].w+asfloat(0xbf000000u));
r0.w=mad(-r0.w,asfloat(0x40000000u),asfloat(0x3f800000u));
r0.xyz=mad(-r0.www,r0.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.w=asfloat((asfloat(0x3f000000u)<cb0[4].w)?0xffffffffu:0u);
r0.xyz=saturate(float3((asuint(r0.w)!=0u?r0.x:r1.x),(asuint(r0.w)!=0u?r0.y:r1.y),(asuint(r0.w)!=0u?r0.z:r1.z)));
r1.xyz=mad(cb0[2].xyz,float3(asfloat(0x3f19999au),asfloat(0x3f19999au),asfloat(0x3f19999au)),float3(asfloat(0x3ecccccdu),asfloat(0x3ecccccdu),asfloat(0x3ecccccdu)));
o0.xyz=(r0.xyz*r1.xyz);
o0.w=asfloat(0x00000000u);
return o0;
)OKSHADER")
;
static const int32 GForgeOpaqueOutlineVertexCBSize[]={11,5,7,21};
static const FOKAdditionalUniform GForgeOpaqueOutlineVertexUniforms[]={
{TEXT("_DetailMask_ST"),0,96,4,0,0},
{TEXT("_linewidthG"),0,160,1,0,0},
{TEXT("_WorldSpaceCameraPos"),1,64,3,0,0},
{TEXT("unity_ObjectToWorld"),2,0,4,0,4},
{TEXT("unity_WorldToObject"),2,64,4,0,4},
{TEXT("unity_MatrixVP"),3,272,4,0,4},
};
static const TArray<FOKAdditionalTexture> GForgeOpaqueOutlineVertexTextures={
{TEXT("_DetailMask"),0,0,2},
};
static const TCHAR* GForgeOpaqueOutlineVertexProgram=
TEXT(R"OKSHADER(
float4 v0=OKPosition;
float4 v1=OKNormal;
float4 v2=OKUV0;
precise float4 o0=0.0;
precise float4 o1=0.0;
precise float4 o2=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
precise float4 r2=0.0;
r0.xyzw=(v0.yyyy*cb2[1].xyzw);
r0.xyzw=mad(cb2[0].xyzw,v0.xxxx,r0.xyzw);
r0.xyzw=mad(cb2[2].xyzw,v0.zzzz,r0.xyzw);
r0.xyzw=mad(cb2[3].xyzw,v0.wwww,r0.xyzw);
r1.xyz=(-r0.xyz+cb1[4].xyz);
o2.xyzw=r0.xyzw;
r0.x=dot(r1.xyz,r1.xyz);
r0.x=sqrt(r0.x);
r0.x=mad(r0.x,asfloat(0x3dcccccbu),asfloat(0x3e99999au));
r0.y=(cb0[10].x*asfloat(0x3ba3d70au));
r0.x=(r0.x*r0.y);
r0.yz=mad(v2.xy,cb0[6].xy,cb0[6].zw);
r1.xyzw=(Texture0.SampleLevel(Sampler0,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.y=(-r1.z+asfloat(0x3f800000u));
r0.x=(r0.y*r0.x);
r1.x=cb2[4].x;
r1.y=cb2[5].x;
r1.z=cb2[6].x;
r0.y=dot(r1.xyz,r1.xyz);
r0.y=sqrt(r0.y);
r1.x=(asfloat(0x3f800000u)/r0.y);
r2.x=cb2[4].y;
r2.y=cb2[5].y;
r2.z=cb2[6].y;
r0.y=dot(r2.xyz,r2.xyz);
r0.y=sqrt(r0.y);
r1.y=(asfloat(0x3f800000u)/r0.y);
r2.x=cb2[4].z;
r2.y=cb2[5].z;
r2.z=cb2[6].z;
r0.y=dot(r2.xyz,r2.xyz);
r0.y=sqrt(r0.y);
r1.z=(asfloat(0x3f800000u)/r0.y);
r0.xyz=(r0.xxx/r1.xyz);
r0.xyz=mad(v1.xyz,r0.xyz,v0.xyz);
r1.xyzw=(r0.yyyy*cb2[1].xyzw);
r1.xyzw=mad(cb2[0].xyzw,r0.xxxx,r1.xyzw);
r0.xyzw=mad(cb2[2].xyzw,r0.zzzz,r1.xyzw);
r0.xyzw=(r0.xyzw+cb2[3].xyzw);
r1.xyzw=(r0.yyyy*cb3[18].xyzw);
r1.xyzw=mad(cb3[17].xyzw,r0.xxxx,r1.xyzw);
r1.xyzw=mad(cb3[19].xyzw,r0.zzzz,r1.xyzw);
o0.xyzw=mad(cb3[20].xyzw,r0.wwww,r1.xyzw);
o1.xy=v2.xy;
)OKSHADER")
;
// Shader Forge/main_skin; original DXBC fe9cc4100c147dfbf899873eb714d6ec05533ce89c5223859d297a497ab4d8c9
static const int32 GForgeSkinOutlineCBSize[]={17};
static const FOKAdditionalUniform GForgeSkinOutlineUniforms[]={
{TEXT("_LightColor0"),0,32,4,0,0},
{TEXT("_MainTex_ST"),0,48,4,0,0},
{TEXT("_LineColorG"),0,64,4,0,0},
{TEXT("_overtex1_ST"),0,80,4,0,0},
{TEXT("_overcolor1"),0,96,4,0,0},
{TEXT("_overtex2_ST"),0,112,4,0,0},
{TEXT("_overcolor2"),0,128,4,0,0},
{TEXT("_DetailMask_ST"),0,144,4,0,0},
{TEXT("_overtex3_ST"),0,160,4,0,0},
{TEXT("_overcolor3"),0,176,4,0,0},
{TEXT("_alpha_a"),0,192,1,0,0},
{TEXT("_alpha_b"),0,196,1,0,0},
{TEXT("_AlphaMask_ST"),0,208,4,0,0},
{TEXT("_nipsize"),0,224,1,0,0},
{TEXT("_nip"),0,228,1,0,0},
{TEXT("_ambientshadowG"),0,240,4,0,0},
{TEXT("_nip_specular"),0,256,1,0,0},
{TEXT("_tex1mask"),0,260,1,0,0},
};
static const TArray<FOKAdditionalTexture> GForgeSkinOutlineTextures={
{TEXT("_AlphaMask"),0,5,2},
{TEXT("_MainTex"),1,0,2},
{TEXT("_overtex1"),2,1,2},
{TEXT("_overtex2"),3,2,2},
{TEXT("_overtex3"),4,4,2},
{TEXT("_DetailMask"),5,3,2},
};
static const FOKAdditionalProperty GForgeSkinOutlineProperties[]={
{TEXT("_MainTex"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_overcolor1"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_overtex1"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_overcolor2"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_overtex2"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_overcolor3"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_overtex3"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_NormalMap"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("bump")},
{TEXT("_NormalMapDetail"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("bump")},
{TEXT("_DetailMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_LineMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_AlphaMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_ShadowColor"),0,false,FLinearColor(0.62800002098083496f,0.62800002098083496f,0.62800002098083496f,1.0f),TEXT("")},
{TEXT("_SpecularColor"),0,false,FLinearColor(1.0f,1.0f,1.0f,0.0f),TEXT("")},
{TEXT("_DetailNormalMapScale"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpeclarHeight"),3,false,FLinearColor(0.98000001907348633f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpecularPower"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpecularPowerNail"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ShadowExtend"),3,false,FLinearColor(1.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_rimpower"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_rimV"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_nipsize"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_alpha_a"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_alpha_b"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_linetexon"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_notusetexspecular"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_nip"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_liquidmask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_Texture2"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_Texture3"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("bump")},
{TEXT("_LiquidTiling"),1,false,FLinearColor(0.0f,0.0f,2.0f,2.0f),TEXT("")},
{TEXT("_liquidftop"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidfbot"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidbtop"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidbbot"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidface"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_nip_specular"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_tex1mask"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_NormalMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_Cutoff"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
};
static const TCHAR* GForgeSkinOutlineProgram=
TEXT(R"OKSHADER(
precise float4 o0=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
precise float4 r2=0.0;
precise float4 r3=0.0;
precise float4 r4=0.0;
precise float4 r5=0.0;
r0.xy=mad(v1.xy,cb0[13].xy,cb0[13].zw);
r0.xyzw=(Texture0.Sample(Sampler5,float2((r0.xy).x,lerp((r0.xy).y,1.0-(r0.xy).y,UVFlip)))).xyzw;
r0.zw=(-cb0[12].xy+float2(asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xy=max(r0.zw,r0.xy);
r0.x=min(r0.y,r0.x);
r0.x=(r0.x+asfloat(0xbf000000u));
r0.x=asfloat((r0.x<asfloat(0x00000000u))?0xffffffffu:0u);
if(asuint(r0.x)!=0u) clip(-1.0);
r0.xy=(v1.zw+float2(asfloat(0xbf000000u),asfloat(0xbf000000u)));
r0.x=dot(r0.xy,r0.xy);
r0.x=sqrt(r0.x);
r0.x=saturate(mad(r0.x,asfloat(0x41855557u),asfloat(0xbf800000u)));
r0.yz=mad(cb0[14].xx,float2(asfloat(0xbfb33333u),asfloat(0x3f333333u)),float2(asfloat(0x40000000u),asfloat(0xbf000000u)));
r0.yz=mad(v1.zw,r0.yy,r0.zz);
r0.yz=(r0.yz+-v1.zw);
r0.xy=mad(r0.xx,r0.yz,v1.zw);
r0.zw=(v1.zw*v4.xx);
r0.xy=mad(r0.xy,v4.xx,-r0.zw);
r0.xy=mad(cb0[14].yy,r0.xy,r0.zw);
r0.xy=mad(r0.xy,cb0[5].xy,cb0[5].zw);
r0.xyzw=(Texture2.Sample(Sampler1,float2((r0.xy).x,lerp((r0.xy).y,1.0-(r0.xy).y,UVFlip)))).xyzw;
r1.x=(r0.y*cb0[16].x);
r1.xyz=mad(r1.xxx,float3(asfloat(0x3ea8f5c3u),asfloat(0x3ea8f5c3u),asfloat(0x3ea8f5c3u)),cb0[6].xyz);
r2.xyzw=(r0.xyzw*cb0[6].xyzw);
r0.xyz=mad(r0.xxx,r1.xyz,-r2.xyz);
r0.w=saturate(cb0[16].y);
r0.xyz=mad(r0.www,r0.xyz,r2.xyz);
r1.xy=mad(v1.xy,cb0[3].xy,cb0[3].zw);
r1.xyzw=(Texture1.Sample(Sampler0,float2((r1.xy).x,lerp((r1.xy).y,1.0-(r1.xy).y,UVFlip)))).xyzw;
r0.xyz=(r0.xyz+-r1.xyz);
r0.xyz=mad(r2.www,r0.xyz,r1.xyz);
r1.xy=(v2.xy*v4.zz);
r1.xy=mad(r1.xy,cb0[7].xy,cb0[7].zw);
r1.xyzw=(Texture3.Sample(Sampler2,float2((r1.xy).x,lerp((r1.xy).y,1.0-(r1.xy).y,UVFlip)))).xyzw;
r1.xyz=mad(cb0[8].xyz,r1.xyz,-r0.xyz);
r0.w=(r1.w*cb0[8].w);
r0.xyz=mad(r0.www,r1.xyz,r0.xyz);
r1.xy=mad(v2.zw,cb0[10].xy,cb0[10].zw);
r1.xyzw=(Texture4.Sample(Sampler4,float2((r1.xy).x,lerp((r1.xy).y,1.0-(r1.xy).y,UVFlip)))).xyzw;
r1.xyz=mad(r1.xyz,cb0[11].xyz,-r0.xyz);
r0.w=(r1.w*cb0[11].w);
r0.xyz=mad(r0.www,r1.xyz,r0.xyz);
r0.w=asfloat((r0.y>=r0.z)?0xffffffffu:0u);
r0.w=asfloat(asuint(r0.w)&asuint(asfloat(0x3f800000u)));
r1.xy=r0.zy;
r2.xy=(r0.yz+-r1.xy);
r1.zw=float2(asfloat(0xbf800000u),asfloat(0x3f2aaaabu));
r2.zw=float2(asfloat(0x3f800000u),asfloat(0xbf800000u));
r1.xyzw=mad(r0.wwww,r2.xywz,r1.xywz);
r0.w=asfloat((r0.x>=r1.x)?0xffffffffu:0u);
r0.w=asfloat(asuint(r0.w)&asuint(asfloat(0x3f800000u)));
r2.z=r1.w;
r1.w=r0.x;
r0.xyz=(r0.xyz*cb0[4].xyz);
r2.xyw=r1.wyx;
r2.xyzw=(-r1.xyzw+r2.xyzw);
r1.xyzw=mad(r0.wwww,r2.xyzw,r1.xyzw);
r0.w=min(r1.y,r1.w);
r0.w=(-r0.w+r1.x);
r2.x=mad(r0.w,asfloat(0x40c00000u),asfloat(0x2edbe6ffu));
r1.y=(-r1.y+r1.w);
r1.y=(r1.y/r2.x);
r1.y=(r1.y+r1.z);
r1.x=(r1.x+asfloat(0x2edbe6ffu));
r0.w=(r0.w/r1.x);
r0.w=(r0.w*asfloat(0x3f28f5c3u));
r1.xyz=(abs(r1.yyy)+float3(asfloat(0x00000000u),asfloat(0xbeaaaaabu),asfloat(0x3eaaaaabu)));
r1.xyz=frac(r1.xyz);
r1.xyz=mad(-r1.xyz,float3(asfloat(0x40000000u),asfloat(0x40000000u),asfloat(0x40000000u)),float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.xyz=saturate(mad(abs(r1.xyz),float3(asfloat(0x40400000u),asfloat(0x40400000u),asfloat(0x40400000u)),float3(asfloat(0xbf800000u),asfloat(0xbf800000u),asfloat(0xbf800000u))));
r1.xyz=(r1.xyz+float3(asfloat(0xbf800000u),asfloat(0xbf800000u),asfloat(0xbf800000u)));
r1.xyz=mad(r0.www,r1.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r2.xyz=float3(asfloat((asfloat(0x3f0e38e4u)<r1.x)?0xffffffffu:0u),asfloat((asfloat(0x3f0e38e4u)<r1.y)?0xffffffffu:0u),asfloat((asfloat(0x3f0e38e4u)<r1.z)?0xffffffffu:0u));
r3.xyz=mad(r1.xyz,float3(asfloat(0x3f666666u),asfloat(0x3f666666u),asfloat(0x3f666666u)),float3(asfloat(0xbf000000u),asfloat(0xbf000000u),asfloat(0xbf000000u)));
r3.xyz=mad(-r3.xyz,float3(asfloat(0x40000000u),asfloat(0x40000000u),asfloat(0x40000000u)),float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r4.xyzw=(-cb0[15].wxyz+float4(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r4.xyz=mad(-r4.xxx,r4.yzw,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.w=mad(cb0[15].w,asfloat(0x3f000000u),asfloat(0x3f000000u));
r1.w=(r0.w+r0.w);
r0.w=asfloat((asfloat(0x3f000000u)<r0.w)?0xffffffffu:0u);
r5.xyz=(r1.www*cb0[15].xyz);
r4.xyz=saturate(float3((asuint(r0.w)!=0u?r4.x:r5.x),(asuint(r0.w)!=0u?r4.y:r5.y),(asuint(r0.w)!=0u?r4.z:r5.z)));
r5.xyz=(-r4.xyz+float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.xyz=(r1.xyz*r4.xyz);
r1.xyz=(r1.xyz*float3(asfloat(0x3fe66666u),asfloat(0x3fe66666u),asfloat(0x3fe66666u)));
r3.xyz=mad(-r3.xyz,r5.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.xyz=saturate(float3((asuint(r2.x)!=0u?r3.x:r1.x),(asuint(r2.y)!=0u?r3.y:r1.y),(asuint(r2.z)!=0u?r3.z:r1.z)));
r0.xyz=(r0.xyz*r1.xyz);
r1.xy=mad(v1.xy,cb0[9].xy,cb0[9].zw);
r1.xyzw=(Texture5.Sample(Sampler3,float2((r1.xy).x,lerp((r1.xy).y,1.0-(r1.xy).y,UVFlip)))).xyzw;
r0.w=(-r1.y+asfloat(0x3f800000u));
r0.w=mad(r0.w,asfloat(0x3f000000u),asfloat(0x3f000000u));
r1.xyz=mad(-r0.xyz,r0.www,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xyz=(r0.www*r0.xyz);
r0.w=(cb0[4].w+asfloat(0xbf000000u));
r0.w=mad(-r0.w,asfloat(0x40000000u),asfloat(0x3f800000u));
r1.xyz=mad(-r0.www,r1.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.w=(cb0[4].w+cb0[4].w);
r0.xyz=(r0.xyz*r0.www);
r0.w=asfloat((asfloat(0x3f000000u)<cb0[4].w)?0xffffffffu:0u);
r0.xyz=saturate(float3((asuint(r0.w)!=0u?r1.x:r0.x),(asuint(r0.w)!=0u?r1.y:r0.y),(asuint(r0.w)!=0u?r1.z:r0.z)));
r1.xyz=mad(cb0[2].xyz,float3(asfloat(0x3f19999au),asfloat(0x3f19999au),asfloat(0x3f19999au)),float3(asfloat(0x3ecccccdu),asfloat(0x3ecccccdu),asfloat(0x3ecccccdu)));
o0.xyz=(r0.xyz*r1.xyz);
o0.w=asfloat(0x00000000u);
return o0;
)OKSHADER")
;
static const int32 GForgeSkinOutlineVertexCBSize[]={15,5,7,21};
static const FOKAdditionalUniform GForgeSkinOutlineVertexUniforms[]={
{TEXT("_DetailMask_ST"),0,144,4,0,0},
{TEXT("_linewidthG"),0,232,1,0,0},
{TEXT("_WorldSpaceCameraPos"),1,64,3,0,0},
{TEXT("unity_ObjectToWorld"),2,0,4,0,4},
{TEXT("unity_WorldToObject"),2,64,4,0,4},
{TEXT("unity_MatrixVP"),3,272,4,0,4},
};
static const TArray<FOKAdditionalTexture> GForgeSkinOutlineVertexTextures={
{TEXT("_DetailMask"),0,0,2},
};
static const TCHAR* GForgeSkinOutlineVertexProgram=
TEXT(R"OKSHADER(
float4 v0=OKPosition;
float4 v1=OKNormal;
float4 v2=OKUV0;
float4 v3=OKUV1;
float4 v4=OKUV2;
float4 v5=OKUV3;
float4 v6=OKColor;
precise float4 o0=0.0;
precise float4 o1=0.0;
precise float4 o2=0.0;
precise float4 o3=0.0;
precise float4 o4=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
precise float4 r2=0.0;
r0.xyzw=(v0.yyyy*cb2[1].xyzw);
r0.xyzw=mad(cb2[0].xyzw,v0.xxxx,r0.xyzw);
r0.xyzw=mad(cb2[2].xyzw,v0.zzzz,r0.xyzw);
r0.xyzw=mad(cb2[3].xyzw,v0.wwww,r0.xyzw);
r1.xyz=(-r0.xyz+cb1[4].xyz);
o3.xyzw=r0.xyzw;
r0.x=dot(r1.xyz,r1.xyz);
r0.x=sqrt(r0.x);
r0.x=mad(r0.x,asfloat(0x3dcccccbu),asfloat(0x3e99999au));
r0.y=(cb0[14].z*asfloat(0x3ba3d70au));
r0.x=(r0.x*r0.y);
r0.yz=mad(v2.xy,cb0[9].xy,cb0[9].zw);
r1.xyzw=(Texture0.SampleLevel(Sampler0,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.y=(-r1.z+asfloat(0x3f800000u));
r0.x=(r0.y*r0.x);
r1.x=cb2[4].x;
r1.y=cb2[5].x;
r1.z=cb2[6].x;
r0.y=dot(r1.xyz,r1.xyz);
r0.y=sqrt(r0.y);
r1.x=(asfloat(0x3f800000u)/r0.y);
r2.x=cb2[4].y;
r2.y=cb2[5].y;
r2.z=cb2[6].y;
r0.y=dot(r2.xyz,r2.xyz);
r0.y=sqrt(r0.y);
r1.y=(asfloat(0x3f800000u)/r0.y);
r2.x=cb2[4].z;
r2.y=cb2[5].z;
r2.z=cb2[6].z;
r0.y=dot(r2.xyz,r2.xyz);
r0.y=sqrt(r0.y);
r1.z=(asfloat(0x3f800000u)/r0.y);
r0.xyz=(r0.xxx/r1.xyz);
r0.xyz=mad(v1.xyz,r0.xyz,v0.xyz);
r1.xyzw=(r0.yyyy*cb2[1].xyzw);
r1.xyzw=mad(cb2[0].xyzw,r0.xxxx,r1.xyzw);
r0.xyzw=mad(cb2[2].xyzw,r0.zzzz,r1.xyzw);
r0.xyzw=(r0.xyzw+cb2[3].xyzw);
r1.xyzw=(r0.yyyy*cb3[18].xyzw);
r1.xyzw=mad(cb3[17].xyzw,r0.xxxx,r1.xyzw);
r1.xyzw=mad(cb3[19].xyzw,r0.zzzz,r1.xyzw);
o0.xyzw=mad(cb3[20].xyzw,r0.wwww,r1.xyzw);
o1.xy=v2.xy;
o1.zw=v3.xy;
o2.xy=v4.xy;
o2.zw=v5.xy;
o4.xyzw=v6.xyzw;
)OKSHADER")
;
// Koikano/main_skin; original DXBC 26427e67d0ad4eb11540012c964d63483222f9911798cf938511775200cdbc1b
static const int32 GKoikanoSkinOutlineCBSize[]={30};
static const FOKAdditionalUniform GKoikanoSkinOutlineUniforms[]={
{TEXT("_LightColor0"),0,32,4,0,0},
{TEXT("_alpha_a"),0,64,1,0,0},
{TEXT("_alpha_b"),0,68,1,0,0},
{TEXT("_MainTex_ST"),0,80,4,0,0},
{TEXT("_nipsize"),0,96,1,0,0},
{TEXT("_nip"),0,100,1,0,0},
{TEXT("_overcolor1"),0,112,4,0,0},
{TEXT("_nip_specular"),0,128,1,0,0},
{TEXT("_tex1mask"),0,132,1,0,0},
{TEXT("_overcolor2"),0,144,4,0,0},
{TEXT("_overcolor3"),0,160,4,0,0},
{TEXT("_ambientshadowG"),0,176,4,0,0},
{TEXT("_KanoVerShift"),0,304,1,0,0},
{TEXT("_LineColorG"),0,320,4,0,0},
{TEXT("_Cutoff"),0,464,1,0,0},
};
static const TArray<FOKAdditionalTexture> GKoikanoSkinOutlineTextures={
{TEXT("_overtex1"),0,2,2},
{TEXT("_MainTex"),1,1,2},
{TEXT("_overtex2"),2,3,2},
{TEXT("_overtex3"),3,4,2},
{TEXT("_AlphaMask"),4,0,2},
{TEXT("_DetailMask"),5,5,2},
};
static const FOKAdditionalProperty GKoikanoSkinOutlineProperties[]={
{TEXT("_Cutoff"),2,false,FLinearColor(0.5f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_alpha_a"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_alpha_b"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_linetexon"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_notusetexspecular"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_tex1mask"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_nip"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_overcolor1"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_overcolor2"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_overcolor3"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_SpecularColor"),0,false,FLinearColor(1.0f,1.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpeclarHeight"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpecularPower"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpecularPowerNail"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_DetailNormalMapScale"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ShadowExtend"),3,false,FLinearColor(1.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_nip_specular"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_nipsize"),3,false,FLinearColor(0.5f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_MainTex"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_DetailMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_LineMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_NormalMap"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("bump")},
{TEXT("_NormalMapDetail"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("bump")},
{TEXT("_overtex1"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_overtex2"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_overtex3"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_AlphaMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_NormalMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_FaceNormalG"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_LineWidthS"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_liquidmask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_Texture2"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_LiquidTiling"),1,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_liquidftop"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidfbot"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidbtop"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidbbot"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidface"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_RimScale"),2,false,FLinearColor(64.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_texcoord3"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_texcoord4"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_texcoord2"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_texcoord"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("__dirty"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
};
static const TCHAR* GKoikanoSkinOutlineProgram=
TEXT(R"OKSHADER(
precise float4 o0=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
precise float4 r2=0.0;
precise float4 r3=0.0;
precise float4 r4=0.0;
precise float4 r5=0.0;
r0.xyzw=(Texture4.Sample(Sampler0,float2((v1.xy).x,lerp((v1.xy).y,1.0-(v1.xy).y,UVFlip)))).xyzw;
r0.zw=(-cb0[4].xy+float2(asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xy=max(r0.zw,r0.xy);
r0.x=min(r0.y,r0.x);
r0.x=min(r0.x,asfloat(0x3f800000u));
r0.x=(r0.x+-cb0[29].x);
r0.x=asfloat((r0.x<asfloat(0x00000000u))?0xffffffffu:0u);
if(asuint(r0.x)!=0u) clip(-1.0);
r0.xy=(v1.zw+float2(asfloat(0xbf000000u),asfloat(0xbf000000u)));
r0.x=dot(r0.xy,r0.xy);
r0.x=sqrt(r0.x);
r0.x=(r0.x+asfloat(0xbd75c28fu));
r0.x=saturate((r0.x*asfloat(0x41855556u)));
r0.yz=mad(cb0[6].xx,float2(asfloat(0xbfb33333u),asfloat(0x3f333333u)),float2(asfloat(0x40000000u),asfloat(0xbf000000u)));
r0.yz=mad(v1.zw,r0.yy,r0.zz);
r0.yz=(r0.yz+-v1.zw);
r0.xy=mad(r0.xx,r0.yz,v1.zw);
r0.zw=(v1.zw*v5.xx);
r0.xy=mad(v5.xx,r0.xy,-r0.zw);
r0.xy=mad(cb0[6].yy,r0.xy,r0.zw);
r0.xyzw=(Texture0.Sample(Sampler2,float2((r0.xy).x,lerp((r0.xy).y,1.0-(r0.xy).y,UVFlip)))).xyzw;
r1.x=(r0.y*cb0[8].x);
r1.xyz=mad(r1.xxx,float3(asfloat(0x3ea8f5c3u),asfloat(0x3ea8f5c3u),asfloat(0x3ea8f5c3u)),cb0[7].xyz);
r2.xyzw=(r0.xyzw*cb0[7].xyzw);
r0.xyz=mad(r0.xxx,r1.xyz,-r2.xyz);
r0.w=saturate(cb0[8].y);
r0.xyz=mad(r0.www,r0.xyz,r2.xyz);
r1.xy=mad(v1.xy,cb0[5].xy,cb0[5].zw);
r1.xyzw=(Texture1.Sample(Sampler1,float2((r1.xy).x,lerp((r1.xy).y,1.0-(r1.xy).y,UVFlip)))).xyzw;
r0.xyz=(r0.xyz+-r1.xyz);
r0.xyz=mad(r2.www,r0.xyz,r1.xyz);
r1.xy=(v2.xy*v5.zz);
r1.xyzw=(Texture2.Sample(Sampler3,float2((r1.xy).x,lerp((r1.xy).y,1.0-(r1.xy).y,UVFlip)))).xyzw;
r1.xyz=mad(r1.xyz,cb0[9].xyz,-r0.xyz);
r0.w=(r1.w*cb0[9].w);
r0.xyz=mad(r0.www,r1.xyz,r0.xyz);
r1.xyzw=(Texture3.Sample(Sampler4,float2((v2.zw).x,lerp((v2.zw).y,1.0-(v2.zw).y,UVFlip)))).xyzw;
r1.xyz=mad(r1.xyz,cb0[10].xyz,-r0.xyz);
r0.w=(r1.w*cb0[10].w);
r0.xyz=mad(r0.www,r1.xyz,r0.xyz);
r0.w=asfloat((r0.y>=r0.z)?0xffffffffu:0u);
r0.w=asfloat(asuint(r0.w)&asuint(asfloat(0x3f800000u)));
r1.xy=r0.zy;
r2.xy=(r0.yz+-r1.xy);
r1.zw=float2(asfloat(0xbf800000u),asfloat(0x3f2aaaabu));
r2.zw=float2(asfloat(0x3f800000u),asfloat(0xbf800000u));
r1.xyzw=mad(r0.wwww,r2.xywz,r1.xywz);
r0.w=asfloat((r0.x>=r1.x)?0xffffffffu:0u);
r0.w=asfloat(asuint(r0.w)&asuint(asfloat(0x3f800000u)));
r2.z=r1.w;
r1.w=r0.x;
r2.xyw=r1.wyx;
r2.xyzw=(-r1.xyzw+r2.xyzw);
r1.xyzw=mad(r0.wwww,r2.xyzw,r1.xyzw);
r0.w=min(r1.y,r1.w);
r0.w=(-r0.w+r1.x);
r2.x=mad(r0.w,asfloat(0x40c00000u),asfloat(0x2edbe6ffu));
r1.y=(-r1.y+r1.w);
r1.y=(r1.y/r2.x);
r1.y=(r1.y+r1.z);
r1.x=(r1.x+asfloat(0x2edbe6ffu));
r0.w=(r0.w/r1.x);
r0.w=(r0.w*asfloat(0x3f28f5c3u));
r1.xyz=(abs(r1.yyy)+float3(asfloat(0x3f800000u),asfloat(0x3f2aaaabu),asfloat(0x3eaaaaabu)));
r1.xyz=frac(r1.xyz);
r1.xyz=mad(r1.xyz,float3(asfloat(0x40c00000u),asfloat(0x40c00000u),asfloat(0x40c00000u)),float3(asfloat(0xc0400000u),asfloat(0xc0400000u),asfloat(0xc0400000u)));
r1.xyz=saturate((abs(r1.xyz)+float3(asfloat(0xbf800000u),asfloat(0xbf800000u),asfloat(0xbf800000u))));
r1.xyz=(r1.xyz+float3(asfloat(0xbf800000u),asfloat(0xbf800000u),asfloat(0xbf800000u)));
r1.xyz=mad(r0.www,r1.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r2.xyz=float3(asfloat((asfloat(0x3f0e38e4u)<r1.x)?0xffffffffu:0u),asfloat((asfloat(0x3f0e38e4u)<r1.y)?0xffffffffu:0u),asfloat((asfloat(0x3f0e38e4u)<r1.z)?0xffffffffu:0u));
r3.xyz=mad(r1.xyz,float3(asfloat(0x3f666666u),asfloat(0x3f666666u),asfloat(0x3f666666u)),float3(asfloat(0xbf000000u),asfloat(0xbf000000u),asfloat(0xbf000000u)));
r3.xyz=mad(-r3.xyz,float3(asfloat(0x40000000u),asfloat(0x40000000u),asfloat(0x40000000u)),float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r4.xyz=mad(cb0[19].xxx,-cb0[11].xyz,cb0[11].xyz);
r5.xyz=(-r4.xyz+float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.w=mad(cb0[11].w,asfloat(0x3f000000u),asfloat(0x3f000000u));
r1.w=(-r0.w+asfloat(0x3f800000u));
r1.w=(r1.w+r1.w);
r5.xyz=mad(-r1.www,r5.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.w=(r0.w+r0.w);
r0.w=asfloat((asfloat(0x3f000000u)<r0.w)?0xffffffffu:0u);
r4.xyz=(r4.xyz*r1.www);
r4.xyz=saturate(float3((asuint(r0.w)!=0u?r5.x:r4.x),(asuint(r0.w)!=0u?r5.y:r4.y),(asuint(r0.w)!=0u?r5.z:r4.z)));
r5.xyz=(-r4.xyz+float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.xyz=(r1.xyz*r4.xyz);
r1.xyz=(r1.xyz*float3(asfloat(0x3fe66666u),asfloat(0x3fe66666u),asfloat(0x3fe66666u)));
r3.xyz=mad(-r3.xyz,r5.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.xyz=saturate(float3((asuint(r2.x)!=0u?r3.x:r1.x),(asuint(r2.y)!=0u?r3.y:r1.y),(asuint(r2.z)!=0u?r3.z:r1.z)));
r0.xyz=(r0.xyz*r1.xyz);
r1.xyz=mad(-r0.xyz,cb0[20].xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xyz=(r0.xyz*cb0[20].xyz);
r0.w=(-cb0[20].w+asfloat(0x3f800000u));
r0.w=(r0.w+r0.w);
r1.xyz=mad(-r0.www,r1.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.w=(cb0[20].w+cb0[20].w);
r0.xyz=(r0.xyz*r0.www);
r0.w=asfloat((asfloat(0x3f000000u)<cb0[20].w)?0xffffffffu:0u);
r0.xyz=saturate(float3((asuint(r0.w)!=0u?r1.x:r0.x),(asuint(r0.w)!=0u?r1.y:r0.y),(asuint(r0.w)!=0u?r1.z:r0.z)));
r1.xyz=saturate(mad(cb0[2].xyz,float3(asfloat(0x3f2b851eu),asfloat(0x3f2b851eu),asfloat(0x3f2b851eu)),float3(asfloat(0x3ea8f5c3u),asfloat(0x3ea8f5c3u),asfloat(0x3ea8f5c3u))));
o0.xyz=(r0.xyz*r1.xyz);
o0.w=asfloat(0x00000000u);
return o0;
)OKSHADER")
;
static const int32 GKoikanoSkinOutlineVertexCBSize[]={34,5,7,21};
static const FOKAdditionalUniform GKoikanoSkinOutlineVertexUniforms[]={
{TEXT("_DetailMask_ST"),0,352,4,0,0},
{TEXT("_linewidthG"),0,456,1,0,0},
{TEXT("_LineWidthS"),0,468,1,0,0},
{TEXT("_texcoord_ST"),0,480,4,0,0},
{TEXT("_texcoord2_ST"),0,496,4,0,0},
{TEXT("_texcoord3_ST"),0,512,4,0,0},
{TEXT("_texcoord4_ST"),0,528,4,0,0},
{TEXT("_WorldSpaceCameraPos"),1,64,3,0,0},
{TEXT("unity_ObjectToWorld"),2,0,4,0,4},
{TEXT("unity_WorldToObject"),2,64,4,0,4},
{TEXT("unity_MatrixVP"),3,272,4,0,4},
};
static const TArray<FOKAdditionalTexture> GKoikanoSkinOutlineVertexTextures={
{TEXT("_DetailMask"),0,0,2},
};
static const TCHAR* GKoikanoSkinOutlineVertexProgram=
TEXT(R"OKSHADER(
float4 v0=OKPosition;
float4 v1=OKTangent;
float4 v2=OKNormal;
float4 v3=OKUV0;
float4 v4=OKUV1;
float4 v5=OKUV2;
float4 v6=OKUV3;
float4 v7=OKColor;
precise float4 o0=0.0;
precise float4 o1=0.0;
precise float4 o2=0.0;
precise float4 o3=0.0;
precise float4 o4=0.0;
precise float4 o5=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
r0.xyz=(v0.yyy*cb2[1].xyz);
r0.xyz=mad(cb2[0].xyz,v0.xxx,r0.xyz);
r0.xyz=mad(cb2[2].xyz,v0.zzz,r0.xyz);
r0.xyz=mad(cb2[3].xyz,v0.www,r0.xyz);
r0.xyz=(-r0.xyz+cb1[4].xyz);
r0.x=dot(r0.xyz,r0.xyz);
r0.x=sqrt(r0.x);
r0.x=mad(r0.x,asfloat(0x3dcccccdu),asfloat(0x3f19999au));
r0.y=(cb0[28].z*asfloat(0x3b449ba6u));
r0.x=(r0.x*r0.y);
r0.yz=mad(v3.xy,cb0[22].xy,cb0[22].zw);
r1.xyzw=(Texture0.SampleLevel(Sampler0,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.y=(-r1.z+asfloat(0x3f800000u));
r0.x=(r0.y*r0.x);
r0.x=(r0.x*cb0[29].y);
r1.x=cb2[4].x;
r1.y=cb2[5].x;
r1.z=cb2[6].x;
r0.y=dot(r1.xyz,r1.xyz);
r0.y=sqrt(r0.y);
r0.y=(asfloat(0x3f800000u)/r0.y);
r0.x=(r0.x/r0.y);
r0.xyz=mad(v2.xyz,r0.xxx,v0.xyz);
r1.xyzw=(r0.yyyy*cb2[1].xyzw);
r1.xyzw=mad(cb2[0].xyzw,r0.xxxx,r1.xyzw);
r0.xyzw=mad(cb2[2].xyzw,r0.zzzz,r1.xyzw);
r1.xyzw=(r0.xyzw+cb2[3].xyzw);
o4.xyz=mad(cb2[3].xyz,v0.www,r0.xyz);
r0.xyzw=(r1.yyyy*cb3[18].xyzw);
r0.xyzw=mad(cb3[17].xyzw,r1.xxxx,r0.xyzw);
r0.xyzw=mad(cb3[19].xyzw,r1.zzzz,r0.xyzw);
o0.xyzw=mad(cb3[20].xyzw,r1.wwww,r0.xyzw);
o1.xy=mad(v3.xy,cb0[30].xy,cb0[30].zw);
o1.zw=mad(v4.xy,cb0[31].xy,cb0[31].zw);
o2.xy=mad(v5.xy,cb0[32].xy,cb0[32].zw);
o2.zw=mad(v6.xy,cb0[33].xy,cb0[33].zw);
r0.x=dot(v2.xyz,cb2[4].xyz);
r0.y=dot(v2.xyz,cb2[5].xyz);
r0.z=dot(v2.xyz,cb2[6].xyz);
r0.w=dot(r0.xyz,r0.xyz);
r0.w=rsqrt(r0.w);
o3.xyz=(r0.www*r0.xyz);
o5.xyzw=v7.xyzw;
)OKSHADER")
;
// Koikano/main_clothes_item; original DXBC c97e2b8fc1566dccd8a0b6c0682a71071f01e1574c4e5f2777c07dc82090f18d
static const int32 GKoikanoClothesItemOutlineCBSize[]={24};
static const FOKAdditionalUniform GKoikanoClothesItemOutlineUniforms[]={
{TEXT("_LightColor0"),0,32,4,0,0},
{TEXT("_MainTex_ST"),0,96,4,0,0},
{TEXT("_Color"),0,112,4,0,0},
{TEXT("_ColorMask_ST"),0,128,4,0,0},
{TEXT("_Color2"),0,144,4,0,0},
{TEXT("_Color3"),0,160,4,0,0},
{TEXT("_Color4"),0,176,4,0,0},
{TEXT("_KanoVerShift"),0,228,1,0,0},
{TEXT("_ambientshadowG"),0,288,4,0,0},
{TEXT("_LineColorG"),0,352,4,0,0},
{TEXT("_Cutoff"),0,372,1,0,0},
{TEXT("_LineWidthS"),0,380,1,0,0},
};
static const TArray<FOKAdditionalTexture> GKoikanoClothesItemOutlineTextures={
{TEXT("_MainTex"),0,0,2},
{TEXT("_ColorMask"),1,1,2},
{TEXT("_LineMask"),2,2,2},
};
static const FOKAdditionalProperty GKoikanoClothesItemOutlineProperties[]={
{TEXT("_Cutoff"),2,false,FLinearColor(0.5f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_AnotherRamp"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_ColorMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_MainTex"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_NormalMap"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("bump")},
{TEXT("_DetailMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_LineMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_AnimationMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_Color"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_Color2"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_Color3"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_Color4"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_ShadowColor"),0,false,FLinearColor(0.60000002384185791f,0.60000002384185791f,0.60000002384185791f,1.0f),TEXT("")},
{TEXT("_SpecularColor"),0,false,FLinearColor(1.0f,1.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpeclarHeight"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpecularPower"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpecularPowerNail"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ShadowExtend"),3,false,FLinearColor(1.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ShadowExtendAnother"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_FaceNormalG1"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_UPanner"),3,false,FLinearColor(0.0f,-1.0f,1.0f,0.0f),TEXT("")},
{TEXT("_VPanner"),3,false,FLinearColor(0.0f,-1.0f,1.0f,0.0f),TEXT("")},
{TEXT("_EmissionPower"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_EmissionBoost"),2,false,FLinearColor(3.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_LineWidthS"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_DetailBLineG"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_DetailRLineR"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_AnotherRampFull"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_AnotherRampMatcap"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_notusetexspecular"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_MultyColorAlpha"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_Dither"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_texcoord"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("__dirty"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
};
static const TCHAR* GKoikanoClothesItemOutlineProgram=
TEXT(R"OKSHADER(
precise float4 o0=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
precise float4 r2=0.0;
precise float4 r3=0.0;
precise float4 r4=0.0;
precise float4 r5=0.0;
r0.x=ceil(cb0[23].w);
r0.yz=mad(v1.xy,cb0[6].xy,cb0[6].zw);
r1.xyzw=(Texture0.Sample(Sampler0,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)))).xyzw;
r0.x=min(r0.x,r1.w);
r0.x=(r0.x+-cb0[23].y);
r0.x=asfloat((r0.x<asfloat(0x00000000u))?0xffffffffu:0u);
if(asuint(r0.x)!=0u) clip(-1.0);
r0.xyz=(cb0[7].xyz+float3(asfloat(0xbf800000u),asfloat(0xbf800000u),asfloat(0xbf800000u)));
r2.xy=mad(v1.xy,cb0[8].xy,cb0[8].zw);
r2.xyzw=(Texture1.Sample(Sampler1,float2((r2.xy).x,lerp((r2.xy).y,1.0-(r2.xy).y,UVFlip)))).xyzw;
r0.xyz=mad(r2.xxx,r0.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r3.xyz=(-r0.xyz+cb0[9].xyz);
r0.xyz=mad(r2.yyy,r3.xyz,r0.xyz);
r3.xyz=(-r0.xyz+cb0[10].xyz);
r0.xyz=mad(r2.zzz,r3.xyz,r0.xyz);
r3.xyz=(-r0.xyz+cb0[11].xyz);
r0.w=max(r2.y,r2.x);
r0.w=max(r2.z,r0.w);
r0.w=(-r0.w+asfloat(0x3f800000u));
r0.xyz=mad(r0.www,r3.xyz,r0.xyz);
r2.xyz=(r0.xyz*r1.xyz);
r3.xy=r2.zy;
r0.xy=mad(r1.yz,r0.yz,-r3.xy);
r1.x=asfloat((r3.y>=r2.z)?0xffffffffu:0u);
r1.x=asfloat(asuint(r1.x)&asuint(asfloat(0x3f800000u)));
r3.zw=float2(asfloat(0xbf800000u),asfloat(0x3f2aaaabu));
r0.zw=float2(asfloat(0x3f800000u),asfloat(0xbf800000u));
r0.xyzw=mad(r1.xxxx,r0.xywz,r3.xywz);
r1.x=asfloat((r2.x>=r0.x)?0xffffffffu:0u);
r1.x=asfloat(asuint(r1.x)&asuint(asfloat(0x3f800000u)));
r3.z=r0.w;
r0.w=r2.x;
r3.xyw=r0.wyx;
r3.xyzw=(-r0.xyzw+r3.xyzw);
r0.xyzw=mad(r1.xxxx,r3.xyzw,r0.xyzw);
r1.x=min(r0.y,r0.w);
r1.x=(r0.x+-r1.x);
r1.y=mad(r1.x,asfloat(0x40c00000u),asfloat(0x2edbe6ffu));
r0.y=(-r0.y+r0.w);
r0.y=(r0.y/r1.y);
r0.y=(r0.y+r0.z);
r0.x=(r0.x+asfloat(0x2edbe6ffu));
r0.x=(r1.x/r0.x);
r0.x=(r0.x*asfloat(0x3f28f5c3u));
r0.yzw=(abs(r0.yyy)+float3(asfloat(0x3f800000u),asfloat(0x3f2aaaabu),asfloat(0x3eaaaaabu)));
r0.yzw=frac(r0.yzw);
r0.yzw=mad(r0.yzw,float3(asfloat(0x40c00000u),asfloat(0x40c00000u),asfloat(0x40c00000u)),float3(asfloat(0xc0400000u),asfloat(0xc0400000u),asfloat(0xc0400000u)));
r0.yzw=saturate((abs(r0.yzw)+float3(asfloat(0xbf800000u),asfloat(0xbf800000u),asfloat(0xbf800000u))));
r0.yzw=(r0.yzw+float3(asfloat(0xbf800000u),asfloat(0xbf800000u),asfloat(0xbf800000u)));
r0.xyz=mad(r0.xxx,r0.yzw,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.xyz=float3(asfloat((asfloat(0x3f0e38e4u)<r0.x)?0xffffffffu:0u),asfloat((asfloat(0x3f0e38e4u)<r0.y)?0xffffffffu:0u),asfloat((asfloat(0x3f0e38e4u)<r0.z)?0xffffffffu:0u));
r3.xyz=mad(r0.xyz,float3(asfloat(0x3f666666u),asfloat(0x3f666666u),asfloat(0x3f666666u)),float3(asfloat(0xbf000000u),asfloat(0xbf000000u),asfloat(0xbf000000u)));
r3.xyz=mad(-r3.xyz,float3(asfloat(0x40000000u),asfloat(0x40000000u),asfloat(0x40000000u)),float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r4.xyz=mad(cb0[14].yyy,-cb0[18].xyz,cb0[18].xyz);
r4.xyz=mad(cb0[14].yyy,-r4.xyz,r4.xyz);
r5.xyz=(-r4.xyz+float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.w=mad(cb0[18].w,asfloat(0x3f000000u),asfloat(0x3f000000u));
r1.w=(-r0.w+asfloat(0x3f800000u));
r1.w=(r1.w+r1.w);
r5.xyz=mad(-r1.www,r5.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.w=(r0.w+r0.w);
r0.w=asfloat((asfloat(0x3f000000u)<r0.w)?0xffffffffu:0u);
r4.xyz=(r4.xyz*r1.www);
r4.xyz=saturate(float3((asuint(r0.w)!=0u?r5.x:r4.x),(asuint(r0.w)!=0u?r5.y:r4.y),(asuint(r0.w)!=0u?r5.z:r4.z)));
r5.xyz=(-r4.xyz+float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xyz=(r0.xyz*r4.xyz);
r0.xyz=(r0.xyz*float3(asfloat(0x3fe66666u),asfloat(0x3fe66666u),asfloat(0x3fe66666u)));
r3.xyz=mad(-r3.xyz,r5.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xyz=saturate(float3((asuint(r1.x)!=0u?r3.x:r0.x),(asuint(r1.y)!=0u?r3.y:r0.y),(asuint(r1.z)!=0u?r3.z:r0.z)));
r0.xyz=(r0.xyz*r2.xyz);
r1.xyz=mad(-r0.xyz,cb0[22].xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xyz=(r0.xyz*cb0[22].xyz);
r0.w=(-cb0[22].w+asfloat(0x3f800000u));
r0.w=(r0.w+r0.w);
r1.xyz=mad(-r0.www,r1.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.w=(cb0[22].w+cb0[22].w);
r0.xyz=(r0.xyz*r0.www);
r0.w=asfloat((asfloat(0x3f000000u)<cb0[22].w)?0xffffffffu:0u);
r0.xyz=saturate(float3((asuint(r0.w)!=0u?r1.x:r0.x),(asuint(r0.w)!=0u?r1.y:r0.y),(asuint(r0.w)!=0u?r1.z:r0.z)));
r1.xyz=saturate(mad(cb0[2].xyz,float3(asfloat(0x3f2b851eu),asfloat(0x3f2b851eu),asfloat(0x3f2b851eu)),float3(asfloat(0x3ea8f5c3u),asfloat(0x3ea8f5c3u),asfloat(0x3ea8f5c3u))));
o0.xyz=(r0.xyz*r1.xyz);
o0.w=asfloat(0x00000000u);
return o0;
)OKSHADER")
;
static const int32 GKoikanoClothesItemOutlineVertexCBSize[]={25,5,7,21};
static const FOKAdditionalUniform GKoikanoClothesItemOutlineVertexUniforms[]={
{TEXT("_LineMask_ST"),0,240,4,0,0},
{TEXT("_linewidthG"),0,376,1,0,0},
{TEXT("_LineWidthS"),0,380,1,0,0},
{TEXT("_texcoord_ST"),0,384,4,0,0},
{TEXT("_WorldSpaceCameraPos"),1,64,3,0,0},
{TEXT("unity_ObjectToWorld"),2,0,4,0,4},
{TEXT("unity_WorldToObject"),2,64,4,0,4},
{TEXT("unity_MatrixVP"),3,272,4,0,4},
};
static const TArray<FOKAdditionalTexture> GKoikanoClothesItemOutlineVertexTextures={
{TEXT("_LineMask"),0,0,2},
};
static const TCHAR* GKoikanoClothesItemOutlineVertexProgram=
TEXT(R"OKSHADER(
float4 v0=OKPosition;
float4 v1=OKTangent;
float4 v2=OKNormal;
float4 v3=OKUV0;
float4 v4=OKUV1;
float4 v5=OKUV2;
float4 v6=OKUV3;
float4 v7=OKColor;
precise float4 o0=0.0;
precise float4 o1=0.0;
precise float4 o2=0.0;
precise float4 o3=0.0;
precise float4 o4=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
r0.xyz=(v0.yyy*cb2[1].xyz);
r0.xyz=mad(cb2[0].xyz,v0.xxx,r0.xyz);
r0.xyz=mad(cb2[2].xyz,v0.zzz,r0.xyz);
r0.xyz=mad(cb2[3].xyz,v0.www,r0.xyz);
r0.xyz=(-r0.xyz+cb1[4].xyz);
r0.x=dot(r0.xyz,r0.xyz);
r0.x=sqrt(r0.x);
r0.x=mad(r0.x,asfloat(0x3dcccccdu),asfloat(0x3f19999au));
r0.y=(cb0[23].z*asfloat(0x3b449ba6u));
r0.x=(r0.x*r0.y);
r0.yz=mad(v3.xy,cb0[15].xy,cb0[15].zw);
r1.xyzw=(Texture0.SampleLevel(Sampler0,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.y=(-r1.z+asfloat(0x3f800000u));
r0.x=(r0.y*r0.x);
r0.x=(r0.x*cb0[23].w);
r1.x=cb2[4].x;
r1.y=cb2[5].x;
r1.z=cb2[6].x;
r0.y=dot(r1.xyz,r1.xyz);
r0.y=sqrt(r0.y);
r0.y=(asfloat(0x3f800000u)/r0.y);
r0.x=(r0.x/r0.y);
r0.xyz=mad(v2.xyz,r0.xxx,v0.xyz);
r1.xyzw=(r0.yyyy*cb2[1].xyzw);
r1.xyzw=mad(cb2[0].xyzw,r0.xxxx,r1.xyzw);
r0.xyzw=mad(cb2[2].xyzw,r0.zzzz,r1.xyzw);
r1.xyzw=(r0.xyzw+cb2[3].xyzw);
o3.xyz=mad(cb2[3].xyz,v0.www,r0.xyz);
r0.xyzw=(r1.yyyy*cb3[18].xyzw);
r0.xyzw=mad(cb3[17].xyzw,r1.xxxx,r0.xyzw);
r0.xyzw=mad(cb3[19].xyzw,r1.zzzz,r0.xyzw);
o0.xyzw=mad(cb3[20].xyzw,r1.wwww,r0.xyzw);
o1.xy=mad(v3.xy,cb0[24].xy,cb0[24].zw);
r0.x=dot(v2.xyz,cb2[4].xyz);
r0.y=dot(v2.xyz,cb2[5].xyz);
r0.z=dot(v2.xyz,cb2[6].xyz);
r0.w=dot(r0.xyz,r0.xyz);
r0.w=rsqrt(r0.w);
o2.xyz=(r0.www*r0.xyz);
o4.xyzw=float4(asfloat(0x00000000u),asfloat(0x00000000u),asfloat(0x00000000u),asfloat(0x00000000u));
)OKSHADER")
;
// Koikano/hair_main_sun; original DXBC aff1747b0b603a122b9c5bfcb629437a914e61f791c348d8580fc9baaf6066ac
static const int32 GKoikanoHairSun0OutlineCBSize[]={22};
static const FOKAdditionalUniform GKoikanoHairSun0OutlineUniforms[]={
{TEXT("_LineColor"),0,336,4,0,0},
};
static const TArray<FOKAdditionalTexture> GKoikanoHairSun0OutlineTextures={
{TEXT("_MainTex"),0,0,2},
{TEXT("_AlphaMask"),1,1,2},
{TEXT("_DetailMask"),2,2,2},
};
static const FOKAdditionalProperty GKoikanoHairSun0OutlineProperties[]={
{TEXT("_AnotherRamp"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_MainTex"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_NormalMap"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("bump")},
{TEXT("_AlphaMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_DetailMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_HairGloss"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_GlossColor"),0,false,FLinearColor(0.84905660152435303f,0.84905660152435303f,0.84905660152435303f,1.0f),TEXT("")},
{TEXT("_GlossinShadowonoff"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_ShadowColor"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_SpeclarHeight"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_rimpower"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_rimV"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ColorMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_Color"),0,false,FLinearColor(1.0f,0.0f,0.0f,1.0f),TEXT("")},
{TEXT("_ShadowExtend"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_Color2"),0,false,FLinearColor(0.0f,0.17825910449028015f,1.0f,1.0f),TEXT("")},
{TEXT("_Color3"),0,false,FLinearColor(0.0f,1.0f,0.052033420652151108f,1.0f),TEXT("")},
{TEXT("_ReverseColor01"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_Color2onoff"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_Color3onoff"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_LineColor"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_Cutoff"),2,false,FLinearColor(0.5f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_oldhair"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_texcoord2"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_texcoord"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("__dirty"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
};
static const TCHAR* GKoikanoHairSun0OutlineProgram=
TEXT(R"OKSHADER(
precise float4 o0=0.0;
o0.xyz=cb0[21].xyz;
o0.w=asfloat(0x00000000u);
return o0;
)OKSHADER")
;
static const int32 GKoikanoHairSun0OutlineVertexCBSize[]={21,5,7,21};
static const FOKAdditionalUniform GKoikanoHairSun0OutlineVertexUniforms[]={
{TEXT("_AlphaMask_ST"),0,64,4,0,0},
{TEXT("_MainTex_ST"),0,80,4,0,0},
{TEXT("_DetailMask_ST"),0,288,4,0,0},
{TEXT("_linewidthG"),0,320,1,0,0},
{TEXT("_WorldSpaceCameraPos"),1,64,3,0,0},
{TEXT("unity_ObjectToWorld"),2,0,4,0,4},
{TEXT("unity_WorldToObject"),2,64,4,0,4},
{TEXT("unity_MatrixVP"),3,272,4,0,4},
};
static const TArray<FOKAdditionalTexture> GKoikanoHairSun0OutlineVertexTextures={
{TEXT("_MainTex"),0,1,2},
{TEXT("_AlphaMask"),1,0,2},
{TEXT("_DetailMask"),2,2,2},
};
static const TCHAR* GKoikanoHairSun0OutlineVertexProgram=
TEXT(R"OKSHADER(
float4 v0=OKPosition;
float4 v1=OKTangent;
float4 v2=OKNormal;
float4 v3=OKUV0;
float4 v4=OKUV1;
float4 v5=OKUV2;
float4 v6=OKUV3;
float4 v7=OKColor;
precise float4 o0=0.0;
precise float4 o1=0.0;
precise float4 o2=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
precise float4 r2=0.0;
r0.xyz=(v0.yyy*cb2[1].xyz);
r0.xyz=mad(cb2[0].xyz,v0.xxx,r0.xyz);
r0.xyz=mad(cb2[2].xyz,v0.zzz,r0.xyz);
r0.xyz=mad(cb2[3].xyz,v0.www,r0.xyz);
r0.xyz=(-r0.xyz+cb1[4].xyz);
r0.x=dot(r0.xyz,r0.xyz);
r0.x=sqrt(r0.x);
r0.x=mad(r0.x,asfloat(0x3dcccccdu),asfloat(0x3e99999au));
r0.y=(cb0[20].x*asfloat(0x3ba3d70au));
r0.x=(r0.x*r0.y);
r0.yz=mad(v3.xy,cb0[5].xy,cb0[5].zw);
r1.xyzw=(Texture0.SampleLevel(Sampler1,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.yz=mad(v3.xy,cb0[4].xy,cb0[4].zw);
r2.xyzw=(Texture1.SampleLevel(Sampler0,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.y=(r1.w*r2.x);
r0.x=(r0.y*r0.x);
r0.yz=mad(v3.xy,cb0[18].xy,cb0[18].zw);
r1.xyzw=(Texture2.SampleLevel(Sampler2,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.y=(-r1.z+asfloat(0x3f800000u));
r0.x=(r0.y*r0.x);
r0.xyz=mad(v2.xyz,r0.xxx,v0.xyz);
r1.xyzw=(r0.yyyy*cb2[1].xyzw);
r1.xyzw=mad(cb2[0].xyzw,r0.xxxx,r1.xyzw);
r0.xyzw=mad(cb2[2].xyzw,r0.zzzz,r1.xyzw);
r1.xyzw=(r0.xyzw+cb2[3].xyzw);
o2.xyz=mad(cb2[3].xyz,v0.www,r0.xyz);
r0.xyzw=(r1.yyyy*cb3[18].xyzw);
r0.xyzw=mad(cb3[17].xyzw,r1.xxxx,r0.xyzw);
r0.xyzw=mad(cb3[19].xyzw,r1.zzzz,r0.xyzw);
o0.xyzw=mad(cb3[20].xyzw,r1.wwww,r0.xyzw);
r0.x=dot(v2.xyz,cb2[4].xyz);
r0.y=dot(v2.xyz,cb2[5].xyz);
r0.z=dot(v2.xyz,cb2[6].xyz);
r0.w=dot(r0.xyz,r0.xyz);
r0.w=rsqrt(r0.w);
o1.xyz=(r0.www*r0.xyz);
)OKSHADER")
;
// Koikano/main_clothes_opaque; original DXBC ea4bd59820d0751e625dc2319dc1e1912f68e35661cbda4bdd0622cf2f2db1c9
static const int32 GKoikanoClothesOpaqueOutlineCBSize[]={27};
static const FOKAdditionalUniform GKoikanoClothesOpaqueOutlineUniforms[]={
{TEXT("_MainTex_ST"),0,64,4,0,0},
{TEXT("_AlphaMaskuv"),0,80,1,0,0},
{TEXT("_AlphaMask_ST"),0,96,4,0,0},
{TEXT("_ReferenceAlphaMaskuv"),0,112,1,0,0},
{TEXT("_alpha_a"),0,116,1,0,0},
{TEXT("_alpha_b"),0,120,1,0,0},
{TEXT("_LineColorG"),0,192,4,0,0},
{TEXT("_Cutoff"),0,424,1,0,0},
};
static const TArray<FOKAdditionalTexture> GKoikanoClothesOpaqueOutlineTextures={
{TEXT("_MainTex"),0,0,2},
{TEXT("_AlphaMask"),1,1,2},
{TEXT("_DetailMask"),2,2,2},
};
static const FOKAdditionalProperty GKoikanoClothesOpaqueOutlineProperties[]={
{TEXT("_Cutoff"),2,false,FLinearColor(0.5f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_AnotherRamp"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_ColorMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_MainTex"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_NormalMap"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("bump")},
{TEXT("_DetailMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_LineMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_AlphaMask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_Color"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_Color2"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_Color3"),0,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_ShadowColor"),0,false,FLinearColor(0.60000002384185791f,0.60000002384185791f,0.60000002384185791f,1.0f),TEXT("")},
{TEXT("_SpecularColor"),0,false,FLinearColor(1.0f,1.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpeclarHeight"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpecularPower"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_SpecularPowerNail"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ShadowExtend"),3,false,FLinearColor(1.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ShadowExtendAnother"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_LineWidthS"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_AnotherRampMatcap"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_AlphaMaskuv"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_ReferenceAlphaMaskuv"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_alpha_a"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_alpha_b"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_DetailBLineG"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_DetailRLineR"),3,false,FLinearColor(0.0f,0.0f,1.0f,0.0f),TEXT("")},
{TEXT("_notusetexspecular"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_liquidmask"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_Texture2"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("black")},
{TEXT("_LiquidTiling"),1,false,FLinearColor(1.0f,1.0f,1.0f,1.0f),TEXT("")},
{TEXT("_liquidftop"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidfbot"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidbtop"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidbbot"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_liquidface"),3,false,FLinearColor(0.0f,0.0f,2.0f,0.0f),TEXT("")},
{TEXT("_ColorMaskUse"),2,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("")},
{TEXT("_texcoord2"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("_texcoord"),4,false,FLinearColor(0.0f,0.0f,0.0f,0.0f),TEXT("white")},
{TEXT("__dirty"),2,false,FLinearColor(1.0f,0.0f,0.0f,0.0f),TEXT("")},
};
static const TCHAR* GKoikanoClothesOpaqueOutlineProgram=
TEXT(R"OKSHADER(
precise float4 o0=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
r0.xyzw=mad(v1.xyzw,cb0[6].xyxy,cb0[6].zwzw);
r0.zw=(-r0.xy+r0.zw);
r0.xy=mad(cb0[5].xx,r0.zw,r0.xy);
r0.zw=(-v1.xy+v1.zw);
r0.zw=mad(cb0[5].xx,r0.zw,v1.xy);
r0.xy=(-r0.zw+r0.xy);
r0.xy=mad(cb0[7].xx,r0.xy,r0.zw);
r0.xyzw=(Texture1.Sample(Sampler1,float2((r0.xy).x,lerp((r0.xy).y,1.0-(r0.xy).y,UVFlip)))).xyzw;
r0.zw=(-cb0[7].yz+float2(asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xy=max(r0.zw,r0.xy);
r0.x=min(r0.y,r0.x);
r0.yz=mad(v1.xy,cb0[4].xy,cb0[4].zw);
r1.xyzw=(Texture0.Sample(Sampler0,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)))).xyzw;
r0.x=min(r0.x,r1.w);
r0.x=(r0.x+-cb0[26].z);
r0.x=asfloat((r0.x<asfloat(0x00000000u))?0xffffffffu:0u);
if(asuint(r0.x)!=0u) clip(-1.0);
r0.xyz=mad(-r1.xyz,cb0[12].xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.xyz=(r1.xyz*cb0[12].xyz);
r0.w=(-cb0[12].w+asfloat(0x3f800000u));
r0.w=(r0.w+r0.w);
r0.xyz=mad(-r0.www,r0.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.w=(cb0[12].w+cb0[12].w);
r1.xyz=(r1.xyz*r0.www);
r0.w=asfloat((asfloat(0x3f000000u)<cb0[12].w)?0xffffffffu:0u);
o0.xyz=saturate(float3((asuint(r0.w)!=0u?r0.x:r1.x),(asuint(r0.w)!=0u?r0.y:r1.y),(asuint(r0.w)!=0u?r0.z:r1.z)));
o0.w=asfloat(0x00000000u);
return o0;
)OKSHADER")
;
static const int32 GKoikanoClothesOpaqueOutlineVertexCBSize[]={29,5,7,21};
static const FOKAdditionalUniform GKoikanoClothesOpaqueOutlineVertexUniforms[]={
{TEXT("_DetailMask_ST"),0,304,4,0,0},
{TEXT("_linewidthG"),0,416,1,0,0},
{TEXT("_LineWidthS"),0,428,1,0,0},
{TEXT("_texcoord_ST"),0,432,4,0,0},
{TEXT("_texcoord2_ST"),0,448,4,0,0},
{TEXT("_WorldSpaceCameraPos"),1,64,3,0,0},
{TEXT("unity_ObjectToWorld"),2,0,4,0,4},
{TEXT("unity_WorldToObject"),2,64,4,0,4},
{TEXT("unity_MatrixVP"),3,272,4,0,4},
};
static const TArray<FOKAdditionalTexture> GKoikanoClothesOpaqueOutlineVertexTextures={
{TEXT("_DetailMask"),0,0,2},
};
static const TCHAR* GKoikanoClothesOpaqueOutlineVertexProgram=
TEXT(R"OKSHADER(
float4 v0=OKPosition;
float4 v1=OKTangent;
float4 v2=OKNormal;
float4 v3=OKUV0;
float4 v4=OKUV1;
float4 v5=OKUV2;
float4 v6=OKUV3;
float4 v7=OKColor;
precise float4 o0=0.0;
precise float4 o1=0.0;
precise float4 o2=0.0;
precise float4 o3=0.0;
precise float4 r0=0.0;
precise float4 r1=0.0;
r0.xyz=(v0.yyy*cb2[1].xyz);
r0.xyz=mad(cb2[0].xyz,v0.xxx,r0.xyz);
r0.xyz=mad(cb2[2].xyz,v0.zzz,r0.xyz);
r0.xyz=mad(cb2[3].xyz,v0.www,r0.xyz);
r0.xyz=(-r0.xyz+cb1[4].xyz);
r0.x=dot(r0.xyz,r0.xyz);
r0.x=sqrt(r0.x);
r0.x=mad(r0.x,asfloat(0x3dcccccdu),asfloat(0x3f19999au));
r0.y=(cb0[26].x*asfloat(0x3b449ba6u));
r0.x=(r0.x*r0.y);
r0.yz=mad(v3.xy,cb0[19].xy,cb0[19].zw);
r1.xyzw=(Texture0.SampleLevel(Sampler0,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r0.y=(-r1.z+asfloat(0x3f800000u));
r0.x=(r0.y*r0.x);
r0.x=(r0.x*cb0[26].w);
r1.x=cb2[4].x;
r1.y=cb2[5].x;
r1.z=cb2[6].x;
r0.y=dot(r1.xyz,r1.xyz);
r0.y=sqrt(r0.y);
r0.y=(asfloat(0x3f800000u)/r0.y);
r0.x=(r0.x/r0.y);
r0.xyz=mad(v2.xyz,r0.xxx,v0.xyz);
r1.xyzw=(r0.yyyy*cb2[1].xyzw);
r1.xyzw=mad(cb2[0].xyzw,r0.xxxx,r1.xyzw);
r0.xyzw=mad(cb2[2].xyzw,r0.zzzz,r1.xyzw);
r1.xyzw=(r0.xyzw+cb2[3].xyzw);
o3.xyz=mad(cb2[3].xyz,v0.www,r0.xyz);
r0.xyzw=(r1.yyyy*cb3[18].xyzw);
r0.xyzw=mad(cb3[17].xyzw,r1.xxxx,r0.xyzw);
r0.xyzw=mad(cb3[19].xyzw,r1.zzzz,r0.xyzw);
o0.xyzw=mad(cb3[20].xyzw,r1.wwww,r0.xyzw);
o1.xy=mad(v3.xy,cb0[27].xy,cb0[27].zw);
o1.zw=mad(v4.xy,cb0[28].xy,cb0[28].zw);
r0.x=dot(v2.xyz,cb2[4].xyz);
r0.y=dot(v2.xyz,cb2[5].xyz);
r0.z=dot(v2.xyz,cb2[6].xyz);
r0.w=dot(r0.xyz,r0.xyz);
r0.w=rsqrt(r0.w);
o2.xyz=(r0.www*r0.xyz);
)OKSHADER")
;
static const FKKRuntimeShaderLayout* OutlineLayout(EKKRuntimeMaterialRecipe Recipe){switch(Recipe){
case EKKRuntimeMaterialRecipe::ForgeItem:{static const auto L=BuildAdditionalLayout(MakeArrayView(GForgeItemOutlineCBSize),MakeArrayView(GForgeItemOutlineUniforms),MakeArrayView(GForgeItemOutlineTextures),MakeArrayView(GForgeItemOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::ForgeHairFront:{static const auto L=BuildAdditionalLayout(MakeArrayView(GForgeHairFrontOutlineCBSize),MakeArrayView(GForgeHairFrontOutlineUniforms),MakeArrayView(GForgeHairFrontOutlineTextures),MakeArrayView(GForgeHairFrontOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::ForgeHair:{static const auto L=BuildAdditionalLayout(MakeArrayView(GForgeHairOutlineCBSize),MakeArrayView(GForgeHairOutlineUniforms),MakeArrayView(GForgeHairOutlineTextures),MakeArrayView(GForgeHairOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::ForgeAlphaMasked:case EKKRuntimeMaterialRecipe::ForgeAlphaTranslucent:{static const auto L=BuildAdditionalLayout(MakeArrayView(GForgeAlphaMaskedOutlineCBSize),MakeArrayView(GForgeAlphaMaskedOutlineUniforms),MakeArrayView(GForgeAlphaMaskedOutlineTextures),MakeArrayView(GForgeAlphaMaskedOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::ForgeOpaque:{static const auto L=BuildAdditionalLayout(MakeArrayView(GForgeOpaqueOutlineCBSize),MakeArrayView(GForgeOpaqueOutlineUniforms),MakeArrayView(GForgeOpaqueOutlineTextures),MakeArrayView(GForgeOpaqueOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::ForgeSkin:{static const auto L=BuildAdditionalLayout(MakeArrayView(GForgeSkinOutlineCBSize),MakeArrayView(GForgeSkinOutlineUniforms),MakeArrayView(GForgeSkinOutlineTextures),MakeArrayView(GForgeSkinOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::KoikanoSkin:{static const auto L=BuildAdditionalLayout(MakeArrayView(GKoikanoSkinOutlineCBSize),MakeArrayView(GKoikanoSkinOutlineUniforms),MakeArrayView(GKoikanoSkinOutlineTextures),MakeArrayView(GKoikanoSkinOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::KoikanoClothesItem:case EKKRuntimeMaterialRecipe::KoikanoClothesItemAlpha:case EKKRuntimeMaterialRecipe::KoikanoClothesItemTranslucent:case EKKRuntimeMaterialRecipe::KoikanoClothesItemAlphaTranslucent:{static const auto L=BuildAdditionalLayout(MakeArrayView(GKoikanoClothesItemOutlineCBSize),MakeArrayView(GKoikanoClothesItemOutlineUniforms),MakeArrayView(GKoikanoClothesItemOutlineTextures),MakeArrayView(GKoikanoClothesItemOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::KoikanoHairSun0:case EKKRuntimeMaterialRecipe::KoikanoHairSun1:case EKKRuntimeMaterialRecipe::KoikanoHairSun2:case EKKRuntimeMaterialRecipe::KoikanoHairSun3:case EKKRuntimeMaterialRecipe::KoikanoHairSun4:case EKKRuntimeMaterialRecipe::KoikanoHairSun5:case EKKRuntimeMaterialRecipe::KoikanoHairSun6:case EKKRuntimeMaterialRecipe::KoikanoHairSun7:{static const auto L=BuildAdditionalLayout(MakeArrayView(GKoikanoHairSun0OutlineCBSize),MakeArrayView(GKoikanoHairSun0OutlineUniforms),MakeArrayView(GKoikanoHairSun0OutlineTextures),MakeArrayView(GKoikanoHairSun0OutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::KoikanoClothesOpaque:case EKKRuntimeMaterialRecipe::KoikanoClothesColor:{static const auto L=BuildAdditionalLayout(MakeArrayView(GKoikanoClothesOpaqueOutlineCBSize),MakeArrayView(GKoikanoClothesOpaqueOutlineUniforms),MakeArrayView(GKoikanoClothesOpaqueOutlineTextures),MakeArrayView(GKoikanoClothesOpaqueOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
default:return nullptr;}}
static FString OutlineProgram(EKKRuntimeMaterialRecipe Recipe){switch(Recipe){
case EKKRuntimeMaterialRecipe::ForgeItem:return GForgeItemOutlineProgram;
case EKKRuntimeMaterialRecipe::ForgeHairFront:return GForgeHairFrontOutlineProgram;
case EKKRuntimeMaterialRecipe::ForgeHair:return GForgeHairOutlineProgram;
case EKKRuntimeMaterialRecipe::ForgeAlphaMasked:case EKKRuntimeMaterialRecipe::ForgeAlphaTranslucent:return GForgeAlphaMaskedOutlineProgram;
case EKKRuntimeMaterialRecipe::ForgeOpaque:return GForgeOpaqueOutlineProgram;
case EKKRuntimeMaterialRecipe::ForgeSkin:return GForgeSkinOutlineProgram;
case EKKRuntimeMaterialRecipe::KoikanoSkin:return GKoikanoSkinOutlineProgram;
case EKKRuntimeMaterialRecipe::KoikanoClothesItem:case EKKRuntimeMaterialRecipe::KoikanoClothesItemAlpha:case EKKRuntimeMaterialRecipe::KoikanoClothesItemTranslucent:case EKKRuntimeMaterialRecipe::KoikanoClothesItemAlphaTranslucent:return GKoikanoClothesItemOutlineProgram;
case EKKRuntimeMaterialRecipe::KoikanoHairSun0:case EKKRuntimeMaterialRecipe::KoikanoHairSun1:case EKKRuntimeMaterialRecipe::KoikanoHairSun2:case EKKRuntimeMaterialRecipe::KoikanoHairSun3:case EKKRuntimeMaterialRecipe::KoikanoHairSun4:case EKKRuntimeMaterialRecipe::KoikanoHairSun5:case EKKRuntimeMaterialRecipe::KoikanoHairSun6:case EKKRuntimeMaterialRecipe::KoikanoHairSun7:return GKoikanoHairSun0OutlineProgram;
case EKKRuntimeMaterialRecipe::KoikanoClothesOpaque:case EKKRuntimeMaterialRecipe::KoikanoClothesColor:return GKoikanoClothesOpaqueOutlineProgram;
default:return {};}}
static const FKKRuntimeShaderLayout* OutlineVertexLayout(EKKRuntimeMaterialRecipe Recipe){switch(Recipe){
case EKKRuntimeMaterialRecipe::ForgeItem:{static const auto L=BuildAdditionalLayout(MakeArrayView(GForgeItemOutlineVertexCBSize),MakeArrayView(GForgeItemOutlineVertexUniforms),MakeArrayView(GForgeItemOutlineVertexTextures),MakeArrayView(GForgeItemOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::ForgeHairFront:{static const auto L=BuildAdditionalLayout(MakeArrayView(GForgeHairFrontOutlineVertexCBSize),MakeArrayView(GForgeHairFrontOutlineVertexUniforms),MakeArrayView(GForgeHairFrontOutlineVertexTextures),MakeArrayView(GForgeHairFrontOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::ForgeHair:{static const auto L=BuildAdditionalLayout(MakeArrayView(GForgeHairOutlineVertexCBSize),MakeArrayView(GForgeHairOutlineVertexUniforms),MakeArrayView(GForgeHairOutlineVertexTextures),MakeArrayView(GForgeHairOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::ForgeAlphaMasked:case EKKRuntimeMaterialRecipe::ForgeAlphaTranslucent:{static const auto L=BuildAdditionalLayout(MakeArrayView(GForgeAlphaMaskedOutlineVertexCBSize),MakeArrayView(GForgeAlphaMaskedOutlineVertexUniforms),MakeArrayView(GForgeAlphaMaskedOutlineVertexTextures),MakeArrayView(GForgeAlphaMaskedOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::ForgeOpaque:{static const auto L=BuildAdditionalLayout(MakeArrayView(GForgeOpaqueOutlineVertexCBSize),MakeArrayView(GForgeOpaqueOutlineVertexUniforms),MakeArrayView(GForgeOpaqueOutlineVertexTextures),MakeArrayView(GForgeOpaqueOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::ForgeSkin:{static const auto L=BuildAdditionalLayout(MakeArrayView(GForgeSkinOutlineVertexCBSize),MakeArrayView(GForgeSkinOutlineVertexUniforms),MakeArrayView(GForgeSkinOutlineVertexTextures),MakeArrayView(GForgeSkinOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::KoikanoSkin:{static const auto L=BuildAdditionalLayout(MakeArrayView(GKoikanoSkinOutlineVertexCBSize),MakeArrayView(GKoikanoSkinOutlineVertexUniforms),MakeArrayView(GKoikanoSkinOutlineVertexTextures),MakeArrayView(GKoikanoSkinOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::KoikanoClothesItem:case EKKRuntimeMaterialRecipe::KoikanoClothesItemAlpha:case EKKRuntimeMaterialRecipe::KoikanoClothesItemTranslucent:case EKKRuntimeMaterialRecipe::KoikanoClothesItemAlphaTranslucent:{static const auto L=BuildAdditionalLayout(MakeArrayView(GKoikanoClothesItemOutlineVertexCBSize),MakeArrayView(GKoikanoClothesItemOutlineVertexUniforms),MakeArrayView(GKoikanoClothesItemOutlineVertexTextures),MakeArrayView(GKoikanoClothesItemOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::KoikanoHairSun0:case EKKRuntimeMaterialRecipe::KoikanoHairSun1:case EKKRuntimeMaterialRecipe::KoikanoHairSun2:case EKKRuntimeMaterialRecipe::KoikanoHairSun3:case EKKRuntimeMaterialRecipe::KoikanoHairSun4:case EKKRuntimeMaterialRecipe::KoikanoHairSun5:case EKKRuntimeMaterialRecipe::KoikanoHairSun6:case EKKRuntimeMaterialRecipe::KoikanoHairSun7:{static const auto L=BuildAdditionalLayout(MakeArrayView(GKoikanoHairSun0OutlineVertexCBSize),MakeArrayView(GKoikanoHairSun0OutlineVertexUniforms),MakeArrayView(GKoikanoHairSun0OutlineVertexTextures),MakeArrayView(GKoikanoHairSun0OutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
case EKKRuntimeMaterialRecipe::KoikanoClothesOpaque:case EKKRuntimeMaterialRecipe::KoikanoClothesColor:{static const auto L=BuildAdditionalLayout(MakeArrayView(GKoikanoClothesOpaqueOutlineVertexCBSize),MakeArrayView(GKoikanoClothesOpaqueOutlineVertexUniforms),MakeArrayView(GKoikanoClothesOpaqueOutlineVertexTextures),MakeArrayView(GKoikanoClothesOpaqueOutlineProperties),EKKRuntimeMaterialRecipe::None);return &L;}
default:return nullptr;}}
static FString OutlineVertexProgram(EKKRuntimeMaterialRecipe Recipe){switch(Recipe){
case EKKRuntimeMaterialRecipe::ForgeItem:return GForgeItemOutlineVertexProgram;
case EKKRuntimeMaterialRecipe::ForgeHairFront:return GForgeHairFrontOutlineVertexProgram;
case EKKRuntimeMaterialRecipe::ForgeHair:return GForgeHairOutlineVertexProgram;
case EKKRuntimeMaterialRecipe::ForgeAlphaMasked:case EKKRuntimeMaterialRecipe::ForgeAlphaTranslucent:return GForgeAlphaMaskedOutlineVertexProgram;
case EKKRuntimeMaterialRecipe::ForgeOpaque:return GForgeOpaqueOutlineVertexProgram;
case EKKRuntimeMaterialRecipe::ForgeSkin:return GForgeSkinOutlineVertexProgram;
case EKKRuntimeMaterialRecipe::KoikanoSkin:return GKoikanoSkinOutlineVertexProgram;
case EKKRuntimeMaterialRecipe::KoikanoClothesItem:case EKKRuntimeMaterialRecipe::KoikanoClothesItemAlpha:case EKKRuntimeMaterialRecipe::KoikanoClothesItemTranslucent:case EKKRuntimeMaterialRecipe::KoikanoClothesItemAlphaTranslucent:return GKoikanoClothesItemOutlineVertexProgram;
case EKKRuntimeMaterialRecipe::KoikanoHairSun0:case EKKRuntimeMaterialRecipe::KoikanoHairSun1:case EKKRuntimeMaterialRecipe::KoikanoHairSun2:case EKKRuntimeMaterialRecipe::KoikanoHairSun3:case EKKRuntimeMaterialRecipe::KoikanoHairSun4:case EKKRuntimeMaterialRecipe::KoikanoHairSun5:case EKKRuntimeMaterialRecipe::KoikanoHairSun6:case EKKRuntimeMaterialRecipe::KoikanoHairSun7:return GKoikanoHairSun0OutlineVertexProgram;
case EKKRuntimeMaterialRecipe::KoikanoClothesOpaque:case EKKRuntimeMaterialRecipe::KoikanoClothesColor:return GKoikanoClothesOpaqueOutlineVertexProgram;
default:return {};}}
