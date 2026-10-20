#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <string>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cstdio>
#include <dxgi.h>
#include <mutex>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT h){if(FAILED(h))throw std::runtime_error("HRESULT "+std::to_string(h));}

struct OKClothesTexture {const unsigned char*data;size_t size;unsigned width,height,format,mips,filter,wrap_u,wrap_v,aniso;float bias;};

// One D3D11 device for the process. Creating a device per bake (clothes, face, every texture readback) cost
// ~0.1-0.15 s each and set a floor under every mesh load of a card open. The immediate context is not thread-safe:
// every use holds GpuLock. A removed/reset device is dropped and made again on the next call.
struct SharedGpu {ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11RasterizerState> raster;};
static std::mutex GpuLock;
static SharedGpu Gpu;
static SharedGpu& SharedDevice(){
 if(Gpu.device){HRESULT r=Gpu.device->GetDeviceRemovedReason();if(SUCCEEDED(r))return Gpu;Gpu=SharedGpu{};}
 SharedGpu g;D3D_FEATURE_LEVEL fl;
 Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&g.device,&fl,&g.ctx));
 const char* vs="struct O{float4 p:SV_POSITION;float2 uv:TEXCOORD1;};O main(uint id:SV_VertexID){O o;float2 p=id==0?float2(-1,-1):id==1?float2(-1,3):float2(3,-1);o.p=float4(p,0,1);o.uv=p*.5+.5;return o;}";
 ComPtr<ID3DBlob> vc,error;Check(D3DCompile(vs,strlen(vs),nullptr,nullptr,nullptr,"main","vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vc,&error));
 Check(g.device->CreateVertexShader(vc->GetBufferPointer(),vc->GetBufferSize(),nullptr,&g.vertex));
 D3D11_RASTERIZER_DESC rd={};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;Check(g.device->CreateRasterizerState(&rd,&g.raster));
 Gpu=g;return Gpu;
}
static int bake_impl(const unsigned char*ps,size_t ps_size,const OKClothesTexture*input,const float*constants,unsigned width,unsigned height,unsigned quality,bool srgb_output,unsigned char*output,size_t output_size,char*error,size_t error_size,unsigned count=5,unsigned constant_bytes=256,const unsigned*sampler_bindings=nullptr) noexcept {
 try {if(!ps||!input||!constants||!output||!width||!height||width>16384||height>16384||!count||count>8||!constant_bytes||constant_bytes%16||output_size!=(size_t)width*height*4)throw std::runtime_error("invalid bake arguments");
 std::lock_guard<std::mutex> hold(GpuLock);
 SharedGpu& shared=SharedDevice();ComPtr<ID3D11Device> device=shared.device;ComPtr<ID3D11DeviceContext> ctx=shared.ctx;ComPtr<ID3D11VertexShader> vertex=shared.vertex;
 // Nothing from an earlier bake stays bound (its textures and targets are released with it).
 struct Clear{ID3D11DeviceContext*c;~Clear(){c->ClearState();}} clear{ctx.Get()};
 ComPtr<ID3D11PixelShader> pixel;Check(device->CreatePixelShader(ps,ps_size,nullptr,&pixel));

 std::vector<ComPtr<ID3D11Texture2D>> textures;std::vector<ComPtr<ID3D11ShaderResourceView>> views;std::vector<ID3D11ShaderResourceView*> raw;
 for(unsigned i=0;i<count;++i){unsigned w=input[i].width,h=input[i].height,fmt=input[i].format,mips=input[i].mips;if(!w||!h||!mips||mips>16)throw std::runtime_error("invalid texture input");
  D3D11_TEXTURE2D_DESC d={};d.Width=w;d.Height=h;d.MipLevels=mips;d.ArraySize=1;d.Format=(DXGI_FORMAT)fmt;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  std::vector<D3D11_SUBRESOURCE_DATA> init(mips);size_t offset=0;
  for(unsigned m=0;m<mips;++m){bool bc=fmt==71||fmt==72||fmt==77||fmt==78||fmt==98||fmt==99;unsigned pitch=bc?((w+3)/4)*(fmt==71||fmt==72?8:16):w*4;unsigned rows=bc?(h+3)/4:h;size_t size=(size_t)pitch*rows;if(offset+size>input[i].size)throw std::runtime_error("short mip payload");init[m].pSysMem=input[i].data+offset;init[m].SysMemPitch=pitch;offset+=size;w=std::max(1u,w/2);h=std::max(1u,h/2);}
  ComPtr<ID3D11Texture2D> tex;Check(device->CreateTexture2D(&d,init.data(),&tex));ComPtr<ID3D11ShaderResourceView> view;Check(device->CreateShaderResourceView(tex.Get(),nullptr,&view));raw.push_back(view.Get());textures.push_back(tex);views.push_back(view);
 }
 D3D11_BUFFER_DESC bd={};bd.ByteWidth=constant_bytes;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA ci={constants,0,0};ComPtr<ID3D11Buffer> cb;Check(device->CreateBuffer(&bd,&ci,&cb));auto cr=cb.Get();ctx->PSSetConstantBuffers(0,1,&cr);
 std::vector<ComPtr<ID3D11SamplerState>> states;std::vector<ID3D11SamplerState*> samplers(count,nullptr);
 // Unity texture-to-sampler bindings: Main=0, Pattern1=1, Mask=2, Pattern2=3, Pattern3=4.
 const unsigned clothes_bindings[5]={4,0,1,2,3};
 const unsigned*bindings=sampler_bindings?sampler_bindings:clothes_bindings;
 for(unsigned s=0;s<count;++s){if(bindings[s]>=count)throw std::runtime_error("invalid sampler binding");const auto&t=input[bindings[s]];D3D11_SAMPLER_DESC sd={};
  unsigned aniso=quality==0?1:quality==2&&t.aniso>0?std::max(9u,std::min(16u,t.aniso)):std::max(1u,std::min(16u,t.aniso));
  sd.Filter=t.filter==0?D3D11_FILTER_MIN_MAG_MIP_POINT:aniso>1?D3D11_FILTER_ANISOTROPIC:t.filter==2?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  sd.MaxAnisotropy=aniso;sd.MipLODBias=t.bias;sd.MaxLOD=D3D11_FLOAT32_MAX;
  auto address=[](unsigned mode){return mode==0?D3D11_TEXTURE_ADDRESS_WRAP:mode==2?D3D11_TEXTURE_ADDRESS_MIRROR:mode==3?D3D11_TEXTURE_ADDRESS_MIRROR_ONCE:D3D11_TEXTURE_ADDRESS_CLAMP;};
  sd.AddressU=address(t.wrap_u);sd.AddressV=address(t.wrap_v);sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
  ComPtr<ID3D11SamplerState> state;Check(device->CreateSamplerState(&sd,&state));samplers[s]=state.Get();states.push_back(state);
 }ctx->PSSetSamplers(0,count,samplers.data());
 D3D11_TEXTURE2D_DESC td={};td.Width=width;td.Height=height;td.MipLevels=td.ArraySize=1;td.Format=srgb_output?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> target,stage;Check(device->CreateTexture2D(&td,nullptr,&target));ComPtr<ID3D11RenderTargetView> rt;Check(device->CreateRenderTargetView(target.Get(),nullptr,&rt));td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Check(device->CreateTexture2D(&td,nullptr,&stage));
 ctx->RSSetState(shared.raster.Get());D3D11_VIEWPORT vp={0,0,(float)width,(float)height,0,1};ctx->RSSetViewports(1,&vp);auto rr=rt.Get();ctx->OMSetRenderTargets(1,&rr,nullptr);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ctx->VSSetShader(vertex.Get(),nullptr,0);ctx->PSSetShader(pixel.Get(),nullptr,0);ctx->PSSetShaderResources(0,count,raw.data());ctx->Draw(3,0);ctx->CopyResource(stage.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped;Check(ctx->Map(stage.Get(),0,D3D11_MAP_READ,0,&mapped));for(unsigned y=0;y<height;++y)memcpy(output+(size_t)y*width*4,(char*)mapped.pData+y*mapped.RowPitch,width*4);ctx->Unmap(stage.Get(),0);
 // Unbind and flush: the driver frees released resources only after a flush (a batch of cards kept every bake's textures).
 ctx->ClearState();ctx->Flush();
return 0;}catch(const std::exception&e){if(error&&error_size)snprintf(error,error_size,"%s",e.what());return -1;}catch(...){return -2;}
}
extern "C" int ok_clothes_gpu_bake(const unsigned char*ps,size_t ps_size,const OKClothesTexture*input,const float*constants,unsigned width,unsigned height,unsigned quality,unsigned char*output,size_t output_size,char*error,size_t error_size) noexcept {
 return bake_impl(ps,ps_size,input,constants,width,height,quality,true,output,output_size,error,error_size);
}
extern "C" int ok_face_create_gpu_bake(const unsigned char*ps,size_t ps_size,const OKClothesTexture*input,const float*constants,unsigned width,unsigned height,unsigned quality,unsigned char*output,size_t output_size,char*error,size_t error_size) noexcept {
 const unsigned bindings[8]={0,2,1,3,4,5,6,7};
 return bake_impl(ps,ps_size,input,constants,width,height,quality,true,output,output_size,error,error_size,8,17*16,bindings);
}
extern "C" int ok_clothes_gpu_identity(char*out,size_t size) noexcept {try {
 std::lock_guard<std::mutex> hold(GpuLock);ComPtr<ID3D11Device>d=SharedDevice().device;
 ComPtr<IDXGIDevice>gd;Check(d.As(&gd));ComPtr<IDXGIAdapter>a;Check(gd->GetAdapter(&a));DXGI_ADAPTER_DESC desc;Check(a->GetDesc(&desc));LARGE_INTEGER driver={};Check(a->CheckInterfaceSupport(__uuidof(IDXGIDevice),&driver));
 snprintf(out,size,"%04x:%04x:%08x:%u:driver%lld",desc.VendorId,desc.DeviceId,desc.SubSysId,desc.Revision,driver.QuadPart);return 0;
 }catch(...){if(out&&size)snprintf(out,size,"unavailable");return -1;}}

extern "C" int ok_texture_gpu_readback(const OKClothesTexture* input,unsigned width,unsigned height,unsigned char*output,size_t output_size,char*error,size_t error_size) noexcept {
 try {
  // Compiled once for the process (every texture read back used to compile it again).
  static ComPtr<ID3DBlob> shader;static std::once_flag compiled;static HRESULT compiledResult=S_OK;
  std::call_once(compiled,[]{const char* source="Texture2D<float4> T:register(t4);SamplerState S:register(s0);float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD1):SV_TARGET{return T.SampleLevel(S,uv,0);}";
   ComPtr<ID3DBlob> diagnostics;compiledResult=D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"main","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&diagnostics);});
  Check(compiledResult);
  float constants[64]={};
  bool srgb=input[4].format==29||input[4].format==72||input[4].format==78||input[4].format==99;
  return bake_impl((const unsigned char*)shader->GetBufferPointer(),shader->GetBufferSize(),input,constants,width,height,0,srgb,output,output_size,error,error_size);
 }catch(const std::exception&e){if(error&&error_size)snprintf(error,error_size,"%s",e.what());return -1;}catch(...){return -2;}
}
