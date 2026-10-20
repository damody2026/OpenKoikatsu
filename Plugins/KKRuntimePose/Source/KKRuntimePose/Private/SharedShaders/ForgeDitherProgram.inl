// Exact installed pixel DXBC SHA256 d018cd0d51b2cfb77cb7faf63b11eaacc6a7ae3334b55d4e3229271e0089ae2c
static const TCHAR* GForgeDitherProgram=
TEXT(R"OKSHADER(
precise float4 r0=0.0;
precise float4 r1=0.0;
precise float4 r2=0.0;
precise float4 o0=0.0;
r0.x=(cb1[5].x*-cb1[5].x);
r1.xy=(v2.xy/v2.ww);
r1.z=(r0.x*r1.y);
r0.xy=mad(r1.xz,float2(asfloat(0x3f000000u),asfloat(0x3f000000u)),float2(asfloat(0x3f000000u),asfloat(0x3f000000u)));
r0.xy=(r0.xy*cb1[6].xy);
r0.xy=floor(r0.xy);
r0.xy=(r0.xy*float2(asfloat(0x3e800000u),asfloat(0x3e800000u)));
r0.zw=float2(asfloat((r0.x>=-r0.x)?0xffffffffu:0u),asfloat((r0.y>=-r0.y)?0xffffffffu:0u));
r0.xy=frac(abs(r0.xy));
r0.xy=float2((asuint(r0.z)!=0u?r0.x:-r0.x),(asuint(r0.w)!=0u?r0.y:-r0.y));
r0.xy=(r0.xy*float2(asfloat(0x40800000u),asfloat(0x40800000u)));
r0.xy=trunc(r0.xy);
r1.xyzw=(-r0.xxxx+float4(asfloat(0x00000000u),asfloat(0x3f800000u),asfloat(0x40000000u),asfloat(0x40400000u)));
r0.xyzw=(-r0.yyyy+float4(asfloat(0x00000000u),asfloat(0x3f800000u),asfloat(0x40000000u),asfloat(0x40400000u)));
r0.xyzw=min(abs(r0.xyzw),float4(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r0.xyzw=(-r0.xyzw+float4(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.xyzw=min(abs(r1.xyzw),float4(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r1.xyzw=(-r1.xyzw+float4(asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u),asfloat(0x3f800000u)));
r2.x=dot(float4(asfloat(0x3d70f0f1u),asfloat(0x3f078788u),asfloat(0x3e34b4b5u),asfloat(0x3f25a5a6u)),r0.xyzw);
r2.y=dot(float4(asfloat(0x3f43c3c4u),asfloat(0x3e969697u),asfloat(0x3f61e1e2u),asfloat(0x3ed2d2d3u)),r0.xyzw);
r2.z=dot(float4(asfloat(0x3e70f0f1u),asfloat(0x3f34b4b5u),asfloat(0x3df0f0f1u),asfloat(0x3f169697u)),r0.xyzw);
r2.w=dot(float4(asfloat(0x3f70f0f1u),asfloat(0x3ef0f0f1u),asfloat(0x3f52d2d3u),asfloat(0x3eb4b4b5u)),r0.xyzw);
r0.x=dot(r2.xyzw,r1.xyzw);
r0.yz=mad(v1.xy,cb0[4].xy,cb0[4].zw);
r1.xyzw=(Texture0.Sample(Sampler0,float2((r0.yz).x,lerp((r0.yz).y,1.0-(r0.yz).y,UVFlip)))).xyzw;
r0.y=mad(cb0[5].x,asfloat(0xbf000000u),r1.w);
r1.xyz=(r1.xyz*cb0[3].xyz);
r0.z=(r0.y+asfloat(0xbfc00000u));
o0.w=mad(r0.y,asfloat(0x40000000u),asfloat(0xbf800000u));
r0.x=(r0.x+r0.z);
r0.x=round(r0.x);
r0.x=asfloat((r0.x<asfloat(0x00000000u))?0xffffffffu:0u);
if(asuint(r0.x)!=0u) clip(-1.0);
r0.xyz=mad(cb0[2].xyz,float3(asfloat(0x3f19999au),asfloat(0x3f19999au),asfloat(0x3f19999au)),float3(asfloat(0x3ecccccdu),asfloat(0x3ecccccdu),asfloat(0x3ecccccdu)));
o0.xyz=(r0.xyz*r1.xyz);
return o0;
)OKSHADER")
;
