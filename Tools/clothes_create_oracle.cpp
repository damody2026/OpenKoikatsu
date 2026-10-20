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
using Microsoft::WRL::ComPtr;
static void Check(HRESULT h){if(FAILED(h))throw std::runtime_error("HRESULT "+std::to_string(h));}
static std::vector<char> Read(std::string p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("missing "+p);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv)try{
 if(argc!=6)throw std::runtime_error("clothes_create_oracle <dxbc> <folder> <cb> <sampler:0=base,1=bilinear,2=trilinear> <hardware:0|1>");
 std::string dir=std::string(argv[2])+"/";int filter=std::stoi(argv[4]);
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL fl;
 Check(D3D11CreateDevice(nullptr,std::stoi(argv[5])?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&fl,&ctx));
 auto ps=Read(argv[1]);ComPtr<ID3D11PixelShader> pixel;Check(device->CreatePixelShader(ps.data(),ps.size(),nullptr,&pixel));
 const char* vs="struct O{float4 p:SV_POSITION;float2 uv:TEXCOORD1;};O main(uint id:SV_VertexID){O o;float2 p=id==0?float2(-1,-1):id==1?float2(-1,3):float2(3,-1);o.p=float4(p,0,1);o.uv=p*.5+.5;return o;}";
 ComPtr<ID3DBlob> vc,error;Check(D3DCompile(vs,strlen(vs),nullptr,nullptr,nullptr,"main","vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vc,&error));ComPtr<ID3D11VertexShader> vertex;Check(device->CreateVertexShader(vc->GetBufferPointer(),vc->GetBufferSize(),nullptr,&vertex));
 std::ifstream config(dir+"textures.config");unsigned width,height;config>>width>>height;
 std::vector<ComPtr<ID3D11Texture2D>> textures;std::vector<ComPtr<ID3D11ShaderResourceView>> views;std::vector<ID3D11ShaderResourceView*> raw;
 for(int i=0;i<5;++i){unsigned w,h,fmt,mips;config>>w>>h>>fmt>>mips;if(!config||!w||!h||!mips)throw std::runtime_error("invalid texture config");auto data=Read(dir+"t"+std::to_string(i)+".bin");
  D3D11_TEXTURE2D_DESC d={};d.Width=w;d.Height=h;d.MipLevels=mips;d.ArraySize=1;d.Format=(DXGI_FORMAT)fmt;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  std::vector<D3D11_SUBRESOURCE_DATA> init(mips);size_t offset=0;
  for(unsigned m=0;m<mips;++m){bool bc=fmt==71||fmt==72||fmt==77||fmt==78||fmt==98||fmt==99;unsigned pitch=bc?((w+3)/4)*(fmt==71||fmt==72?8:16):w*4;unsigned rows=bc?(h+3)/4:h;size_t size=(size_t)pitch*rows;if(offset+size>data.size())throw std::runtime_error("short mip payload");init[m].pSysMem=data.data()+offset;init[m].SysMemPitch=pitch;offset+=size;w=std::max(1u,w/2);h=std::max(1u,h/2);}
  ComPtr<ID3D11Texture2D> tex;Check(device->CreateTexture2D(&d,init.data(),&tex));ComPtr<ID3D11ShaderResourceView> view;Check(device->CreateShaderResourceView(tex.Get(),nullptr,&view));raw.push_back(view.Get());textures.push_back(tex);views.push_back(view);
 }
 auto constants=Read(dir+argv[3]+".bin");if(constants.size()!=256)throw std::runtime_error("expected 256 CB bytes");D3D11_BUFFER_DESC bd={};bd.ByteWidth=256;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA ci={constants.data(),0,0};ComPtr<ID3D11Buffer> cb;Check(device->CreateBuffer(&bd,&ci,&cb));auto cr=cb.Get();ctx->PSSetConstantBuffers(0,1,&cr);
 D3D11_SAMPLER_DESC sd={};sd.Filter=filter>=3?D3D11_FILTER_ANISOTROPIC:filter==2?D3D11_FILTER_MIN_MAG_MIP_LINEAR:D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;sd.MaxAnisotropy=filter>=3?(1u<<(filter-2)):1;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sd.MaxLOD=filter==0?0:D3D11_FLOAT32_MAX;ComPtr<ID3D11SamplerState> ss;Check(device->CreateSamplerState(&sd,&ss));ID3D11SamplerState* samplers[5]={ss.Get(),ss.Get(),ss.Get(),ss.Get(),ss.Get()};ctx->PSSetSamplers(0,5,samplers);
 D3D11_TEXTURE2D_DESC td={};td.Width=width;td.Height=height;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> target,stage;Check(device->CreateTexture2D(&td,nullptr,&target));ComPtr<ID3D11RenderTargetView> rt;Check(device->CreateRenderTargetView(target.Get(),nullptr,&rt));td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Check(device->CreateTexture2D(&td,nullptr,&stage));
 D3D11_RASTERIZER_DESC rd={};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> rs;Check(device->CreateRasterizerState(&rd,&rs));ctx->RSSetState(rs.Get());D3D11_VIEWPORT vp={0,0,(float)width,(float)height,0,1};ctx->RSSetViewports(1,&vp);auto rr=rt.Get();ctx->OMSetRenderTargets(1,&rr,nullptr);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ctx->VSSetShader(vertex.Get(),nullptr,0);ctx->PSSetShader(pixel.Get(),nullptr,0);ctx->PSSetShaderResources(0,5,raw.data());ctx->Draw(3,0);ctx->CopyResource(stage.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped;Check(ctx->Map(stage.Get(),0,D3D11_MAP_READ,0,&mapped));std::ofstream out(dir+argv[3]+".s"+argv[4]+".h"+argv[5]+".rgba8",std::ios::binary);for(unsigned y=0;y<height;++y)out.write((char*)mapped.pData+y*mapped.RowPitch,width*4);ctx->Unmap(stage.Get(),0);std::cout<<"PASS native DXBC replay "<<width<<"x"<<height<<"\n";
 return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
