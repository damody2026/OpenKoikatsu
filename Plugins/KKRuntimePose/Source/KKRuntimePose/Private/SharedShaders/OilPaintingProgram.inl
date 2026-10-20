// Exact installed pixel DXBC SHA256 80ba35ae2f46d0926444c4ba2d7968a85a4b5f82385a08902a22664e92350c2e
static const TCHAR* GOilPaintingProgram=
TEXT(R"OKSHADER(
precise float4 r0=0.0;
precise float4 r1=0.0;
precise float4 r2=0.0;
precise float4 r3=0.0;
precise float4 o0=0.0;
precise float4 x0[4];
precise float4 x1[4];
precise float4 x2[4];
x0[0].xyz=float3(asfloat(0x00000000u),asfloat(0x00000000u),asfloat(0x00000000u));
x0[1].xyz=float3(asfloat(0x00000000u),asfloat(0x00000000u),asfloat(0x00000000u));
x0[2].xyz=float3(asfloat(0x00000000u),asfloat(0x00000000u),asfloat(0x00000000u));
x0[3].xyz=float3(asfloat(0x00000000u),asfloat(0x00000000u),asfloat(0x00000000u));
x1[0].xyz=float3(asfloat(0x00000000u),asfloat(0x00000000u),asfloat(0x00000000u));
x1[1].xyz=float3(asfloat(0x00000000u),asfloat(0x00000000u),asfloat(0x00000000u));
x1[2].xyz=float3(asfloat(0x00000000u),asfloat(0x00000000u),asfloat(0x00000000u));
x1[3].xyz=float3(asfloat(0x00000000u),asfloat(0x00000000u),asfloat(0x00000000u));
r0.y=float(asint(-cb0[3].x));
r0.x=asfloat(0x00000000u);
x2[0].xy=r0.yy;
x2[1].xy=r0.yx;
x2[2].xy=r0.xy;
x2[3].xy=float2(asfloat(0x00000000u),asfloat(0x00000000u));
r0.x=asfloat(0x00000000u);
[loop] while(true){
r0.y=asfloat((asint(r0.x)>=asint(asfloat(0x00000004u)))?0xffffffffu:0u);
if(asuint(r0.y)!=0u) break;
r0.yz=x2[asint(r0.x)+0].xy;
r0.w=asfloat(0x00000000u);
[loop] while(true){
r1.x=asfloat((asint(cb0[3].x)<asint(r0.w))?0xffffffffu:0u);
if(asuint(r1.x)!=0u) break;
r1.x=float(asint(r0.w));
r1.z=asfloat(0x00000000u);
[loop] while(true){
r1.w=asfloat((asint(cb0[3].x)<asint(r1.z))?0xffffffffu:0u);
if(asuint(r1.w)!=0u) break;
r1.y=float(asint(r1.z));
r1.yw=(r0.yz+r1.xy);
r1.yw=mad(r1.yw,cb0[4].xy,v1.xy);
r2.xyzw=(Texture0.SampleLevel(Sampler0,float2((r1.yw).x,lerp((r1.yw).y,1.0-(r1.yw).y,UVFlip)),asfloat(0x00000000u))).xyzw;
r3.xyz=x0[asint(r0.x)+0].xyz;
r3.xyz=(r2.xyz+r3.xyz);
x0[asint(r0.x)+0].xyz=r3.xyz;
r3.xyz=x1[asint(r0.x)+0].xyz;
r2.xyz=mad(r2.xyz,r2.xyz,r3.xyz);
x1[asint(r0.x)+0].xyz=r2.xyz;
r1.z=asfloat(asuint(r1.z)+asuint(asfloat(0x00000001u)));
}
r0.w=asfloat(asuint(r0.w)+asuint(asfloat(0x00000001u)));
}
r0.x=asfloat(asuint(r0.x)+asuint(asfloat(0x00000001u)));
}
r0.x=asfloat(asuint(cb0[3].x)+asuint(asfloat(0x00000001u)));
r0.x=float(asint(r0.x));
r0.x=(r0.x*r0.x);
r1.xyzw=(Texture0.Sample(Sampler0,float2((v1.xy).x,lerp((v1.xy).y,1.0-(v1.xy).y,UVFlip)))).xyzw;
r0.yzw=x0[0].xyz;
r2.xyz=(r0.yzw/r0.xxx);
r0.yzw=x1[0].xyz;
r0.yzw=(r0.yzw/r0.xxx);
r0.yzw=mad(-r2.xyz,r2.xyz,r0.yzw);
r0.y=(abs(r0.z)+abs(r0.y));
r2.w=(abs(r0.w)+r0.y);
r0.y=asfloat((r2.w<asfloat(0x3f800000u))?0xffffffffu:0u);
r3.xyz=r1.xyz;
r3.w=asfloat(0x3f800000u);
r2.xyzw=float4((asuint(r0.y)!=0u?r2.x:r3.x),(asuint(r0.y)!=0u?r2.y:r3.y),(asuint(r0.y)!=0u?r2.z:r3.z),(asuint(r0.y)!=0u?r2.w:r3.w));
r0.yzw=x0[1].xyz;
r3.xyz=(r0.yzw/r0.xxx);
r0.yzw=x1[1].xyz;
r0.yzw=(r0.yzw/r0.xxx);
r0.yzw=mad(-r3.xyz,r3.xyz,r0.yzw);
r0.y=(abs(r0.z)+abs(r0.y));
r3.w=(abs(r0.w)+r0.y);
r0.y=asfloat((r3.w<r2.w)?0xffffffffu:0u);
r2.xyzw=float4((asuint(r0.y)!=0u?r3.x:r2.x),(asuint(r0.y)!=0u?r3.y:r2.y),(asuint(r0.y)!=0u?r3.z:r2.z),(asuint(r0.y)!=0u?r3.w:r2.w));
r0.yzw=x0[2].xyz;
r3.xyz=(r0.yzw/r0.xxx);
r0.yzw=x1[2].xyz;
r0.yzw=(r0.yzw/r0.xxx);
r0.yzw=mad(-r3.xyz,r3.xyz,r0.yzw);
r0.y=(abs(r0.z)+abs(r0.y));
r3.w=(abs(r0.w)+r0.y);
r0.y=asfloat((r3.w<r2.w)?0xffffffffu:0u);
r2.xyzw=float4((asuint(r0.y)!=0u?r3.x:r2.x),(asuint(r0.y)!=0u?r3.y:r2.y),(asuint(r0.y)!=0u?r3.z:r2.z),(asuint(r0.y)!=0u?r3.w:r2.w));
r0.yzw=x0[3].xyz;
r0.yzw=(r0.yzw/r0.xxx);
r1.xyz=x1[3].xyz;
r1.xyz=(r1.xyz/r0.xxx);
r1.xyz=mad(-r0.yzw,r0.yzw,r1.xyz);
r0.x=(abs(r1.y)+abs(r1.x));
r0.x=(abs(r1.z)+r0.x);
r0.x=asfloat((r0.x<r2.w)?0xffffffffu:0u);
o0.xyz=float3((asuint(r0.x)!=0u?r0.y:r2.x),(asuint(r0.x)!=0u?r0.z:r2.y),(asuint(r0.x)!=0u?r0.w:r2.z));
o0.w=r1.w;
return o0;
)OKSHADER")
;
