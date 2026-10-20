// Exact installed DXBC; SHA256 623248717606bc14320683cb52c73e6bfbca0ce44038861153b6ea197aeba956
static const TCHAR* GForgeEyeWhiteForwardProgram=
TEXT(R"OKSHADER(
precise float4 r0=0.0;
precise float4 r1=0.0;
precise float4 r2=0.0;
precise float4 r3=0.0;
precise float4 o0=0.0;
r0.xy=mad(v1.xy,cb0[3].xy,cb0[3].zw);
r0.xyzw=(Texture0.Sample(Sampler0,float2((r0.xy).x,lerp((r0.xy).y,1.0-(r0.xy).y,UVFlip)))).xyzw;
r1.x=(r0.w+asfloat(0xbf000000u));
r1.x=asfloat((r1.x<asfloat(0x00000000u))?0xffffffffu:0u);
if(asuint(r1.x)!=0u) clip(-1.0);
r1.xyzw=(-cb0[6].wxyz+float4(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.xyz=mad(-r1.xxx,r1.yzw,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.w=mad(cb0[6].w,asfloat(0x3f000000u),asfloat(0x3f000000u));
r2.x=(r1.w+r1.w);
r1.w=asfloat((asfloat(0x3f000000u)<r1.w)?0xffffffffu:0u);
r2.xyz=(r2.xxx*cb0[6].xyz);
r1.xyz=saturate(float3((asuint(r1.w)!=0u?r1.x:r2.x),(asuint(r1.w)!=0u?r1.y:r2.y),(asuint(r1.w)!=0u?r1.z:r2.z)));
r2.xyz=(-r1.xyz+float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.xyz=(r1.xyz*cb0[5].xyz);
r1.xyz=(r1.xyz+r1.xyz);
r3.xyz=(cb0[5].xyz+float3(asfloat(0xbf000000u),asfloat(0xbf000000u),asfloat(0xbf000000u)));
r3.xyz=mad(-r3.xyz,float3(asfloat(0x40000000u),asfloat(0x40000000u),asfloat(0x40000000u)),float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r2.xyz=mad(-r3.xyz,r2.xyz,float3(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r3.xyz=float3(asfloat((asfloat(0x3f000000u)<cb0[5].x)?0xffffffffu:0u),asfloat((asfloat(0x3f000000u)<cb0[5].y)?0xffffffffu:0u),asfloat((asfloat(0x3f000000u)<cb0[5].z)?0xffffffffu:0u));
r1.xyz=saturate(float3((asuint(r3.x)!=0u?r2.x:r1.x),(asuint(r3.y)!=0u?r2.y:r1.y),(asuint(r3.z)!=0u?r2.z:r1.z)));
r2.xyz=(r0.xyz*cb0[4].xyz);
r1.xyz=(r1.xyz*r2.xyz);
r0.xyz=mad(r0.xyz,cb0[4].xyz,-r1.xyz);
o0.w=r0.w;
r0.w=dot(v3.xyz,v3.xyz);
r0.w=rsqrt(r0.w);
r2.xyz=(r0.www*v3.xyz);
r0.w=dot(cb1[0].xyz,cb1[0].xyz);
r0.w=rsqrt(r0.w);
r3.xyz=(r0.www*cb1[0].xyz);
r0.w=dot(r3.xyz,r2.xyz);
r2.xy=mad(r0.ww,cb0[7].xy,cb0[7].zw);
r2.xyzw=(Texture1.Sample(Sampler1,float2((r2.xy).x,lerp((r2.xy).y,1.0-(r2.xy).y,UVFlip)))).xyzw;
r0.xyz=mad(r2.xxx,r0.xyz,r1.xyz);
r1.xyz=mad(cb0[2].xyz,float3(asfloat(0x3f19999au),asfloat(0x3f19999au),asfloat(0x3f19999au)),float3(asfloat(0x3ecccccdu),asfloat(0x3ecccccdu),asfloat(0x3ecccccdu)));
r1.xyz=max(r1.xyz,cb0[6].xyz);
o0.xyz=(r0.xyz*r1.xyz);
return o0;
)OKSHADER")
;
