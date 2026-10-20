// Exact installed pixel DXBC SHA256 a885b2538123a9406c32a53cb23d7efd6e39bb104bb50ebc9e984fc211e17db0
static const TCHAR* GKoikanoNoseProgram=
TEXT(R"OKSHADER(
precise float4 r0=0.0;
precise float4 r1=0.0;
precise float4 o0=0.0;
r0.xyz=(-v3.xyz+cb1[4].xyz);
r0.w=dot(r0.xyz,r0.xyz);
r0.w=rsqrt(r0.w);
r0.xyz=mad(r0.xyz,r0.www,-v2.xyz);
r0.x=dot(r0.xyz,r0.xyz);
r0.x=sqrt(r0.x);
r0.x=mad(cb0[6].x,-r0.x,asfloat(0x3f800000u));
r0.yzw=(cb2[0].yyy*cb3[5].xyz);
r0.yzw=mad(cb3[4].xyz,cb2[0].xxx,r0.yzw);
r0.yzw=mad(cb3[6].xyz,cb2[0].zzz,r0.yzw);
r0.yzw=mad(cb3[7].xyz,cb2[0].www,r0.yzw);
r0.z=dot(r0.yzw,r0.yzw);
r0.z=rsqrt(r0.z);
r0.y=(r0.z*r0.y);
r0.z=asfloat((asfloat(0x00000000u)<r0.y)?0xffffffffu:0u);
r0.y=asfloat((r0.y<asfloat(0x00000000u))?0xffffffffu:0u);
r0.w=round(v1.x);
r1.x=(-r0.w+asfloat(0x3f800000u));
r0.y=asfloat(asuint(r0.y)&asuint(r1.x));
r0.y=(asuint(r0.z)!=0u?r0.w:r0.y);
r0.zw=mad(v1.xy,cb0[4].xy,cb0[4].zw);
r1.xyzw=(Texture0.Sample(Sampler0,float2((r0.zw).x,lerp((r0.zw).y,1.0-(r0.zw).y,UVFlip)))).xyzw;
r0.y=(r0.y*r1.w);
r0.z=(r1.w*cb0[5].w);
r0.z=(r0.x*r0.z);
r0.x=mad(r0.y,r0.x,-r0.z);
o0.w=mad(r1.z,r0.x,r0.z);
r0.x=(-r1.x+asfloat(0x3f800000u));
r0.xyz=(r0.xxx*cb0[5].xyz);
o0.xyz=mad(r0.xyz,r1.zzz,r1.yyy);
return o0;
)OKSHADER")
;
