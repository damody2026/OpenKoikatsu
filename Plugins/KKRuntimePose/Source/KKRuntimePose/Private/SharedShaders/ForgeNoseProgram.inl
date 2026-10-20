// Exact installed DXBC; SHA256 875ff86184832554b4664cdca4196b992f56af88c2615284f90bcc55544b667c
static const TCHAR* GForgeNoseForwardProgram=
TEXT(R"OKSHADER(
precise float4 r0=0.0;
precise float4 o0=0.0;
r0.xy=mad(v1.xy,cb0[2].xy,cb0[2].zw);
r0.xyzw=(Texture0.Sample(Sampler0,float2((r0.xy).x,lerp((r0.xy).y,1.0-(r0.xy).y,UVFlip)))).xyzw;
r0.x=(-r0.x+asfloat(0x3f800000u));
o0.xyz=mad(cb0[3].xyz,r0.xxx,r0.yyy);
o0.w=(r0.w*cb0[3].w);
return o0;
)OKSHADER")
;
