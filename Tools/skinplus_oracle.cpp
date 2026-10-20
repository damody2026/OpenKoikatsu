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
#include <cmath>
#include <array>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT h) { if (FAILED(h)) throw std::runtime_error("HRESULT "+std::to_string(h)); }
static std::vector<char> Read(const std::string& path) { std::ifstream f(path,std::ios::binary); if(!f) throw std::runtime_error("missing "+path);return {std::istreambuf_iterator<char>(f),{}}; }
static ComPtr<ID3DBlob> Compile(const char* src,size_t size,const char* profile) { ComPtr<ID3DBlob> code,error;HRESULT h=D3DCompile(src,size,nullptr,nullptr,nullptr,"main",profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);if(FAILED(h)){if(error)std::cerr<<(const char*)error->GetBufferPointer();Check(h);}return code; }
int main(int argc,char** argv) try {
 if(argc!=4 && argc!=5) throw std::runtime_error("skinplus_oracle <nativeSource> <caseDirectory> <label> [counterexampleHlsl]");
 std::string sourceDir=argv[1],dir=std::string(argv[2])+"/",label=argv[3];
 int blobs,nt,ncb,ns,ncases,shadowFlag;std::ifstream config(dir+label+".config");
 if(!(config>>blobs>>nt>>ncb>>ns>>ncases>>shadowFlag) || nt<1 || nt>17 || ncb<1 || ncb>4 || ns<1 || ns>16 || ncases!=64) throw std::runtime_error("invalid oracle configuration");
 bool forward=label!="Outline"&&label.find("ShadowCaster")!=0,shadow=shadowFlag!=0;
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL fl;
 Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&fl,&context));
 char number[16];snprintf(number,sizeof(number),"%04d.dxbc",blobs);
 auto original=Read(sourceDir+"/"+number);
 auto source=Read(argc==5?argv[4]:dir+label+".hlsl");auto generated=Compile(source.data(),source.size(),"ps_5_0");
 if(argc==4){std::ofstream f(dir+label+".generated.dxbc",std::ios::binary);f.write((const char*)generated->GetBufferPointer(),generated->GetBufferSize());}
 ComPtr<ID3D11PixelShader> pixel[2];Check(device->CreatePixelShader(original.data(),original.size(),nullptr,&pixel[0]));Check(device->CreatePixelShader(generated->GetBufferPointer(),generated->GetBufferSize(),nullptr,&pixel[1]));
 std::string vs="struct O{float4 p:SV_POSITION;float4 c:COLOR0;float2 uv0:TEXCOORD0;float2 uv1:TEXCOORD1;float2 uv2:TEXCOORD2;float2 uv3:TEXCOORD3;";
 if(shadow) vs+="float4 s:TEXCOORD4;";
 vs+="float4 w:TEXCOORD5;float3 n:TEXCOORD6;float4 t:TEXCOORD7;float3 b:TEXCOORD8;};O main(uint id:SV_VertexID){O o;float2 p=id==0?float2(-1,-1):id==1?float2(-1,3):float2(3,-1);o.p=float4(p,.5,1);o.uv0=p*.5+.5;o.uv1=p*float2(.25,.35)+.5;o.uv2=p*float2(.45,.15)+.4;o.uv3=p*float2(.65,.55)+.3;o.c=float4(.4+p.x*.2,.8,.3+p.y*.2,1);o.w=float4(p.x*.05,1.4+p.y*.05,.02,1);o.n=normalize(float3(.07,-.1,-1));o.t=float4(1,0,0,1);o.b=normalize(cross(o.n,o.t.xyz));";
 if(shadow)vs+="o.s=float4(o.uv0*.7+.1,.5,1);";
 vs+="return o;}";
 if(label.find("ShadowCaster")==0)vs="struct O{float2 uv1:TEXCOORD1;float4 p:SV_POSITION;};O main(uint id:SV_VertexID){O o;float2 p=id==0?float2(-1,-1):id==1?float2(-1,3):float2(3,-1);o.p=float4(p,.5,1);o.uv1=p*float2(.25,.35)+.5;return o;}";
 auto vcode=Compile(vs.data(),vs.size(),"vs_5_0");ComPtr<ID3D11VertexShader> vertex;Check(device->CreateVertexShader(vcode->GetBufferPointer(),vcode->GetBufferSize(),nullptr,&vertex));context->VSSetShader(vertex.Get(),nullptr,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 {std::ofstream f(dir+label+".vertex.dxbc",std::ios::binary);f.write((const char*)vcode->GetBufferPointer(),vcode->GetBufferSize());}
 D3D11_TEXTURE2D_DESC td={};td.Width=td.Height=4;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> target;Check(device->CreateTexture2D(&td,nullptr,&target));ComPtr<ID3D11RenderTargetView> rtv;Check(device->CreateRenderTargetView(target.Get(),nullptr,&rtv));auto rt=rtv.Get();context->OMSetRenderTargets(1,&rt,nullptr);D3D11_VIEWPORT viewport={0,0,4,4,0,1};context->RSSetViewports(1,&viewport);
 td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> stage;Check(device->CreateTexture2D(&td,nullptr,&stage));
 std::vector<ComPtr<ID3D11ShaderResourceView>> textures[2];
 for(int mode=0;mode<2;++mode) for(int t=0;t<nt;++t){
  bool cube=false;std::vector<float> values(4*4*4*(cube?6:1));
  for(int side=0;side<(cube?6:1);++side) for(int y=0;y<4;++y)for(int x=0;x<4;++x){int yy=y;size_t at=(side*16+y*4+x)*4;for(int c=0;c<4;++c) values[at+c]=.18f+.055f*((t*3+x+yy*2+c*2+side)%11);if((forward&&(t==(shadow?3:2)||t==(shadow?4:3)))) {values[at]=1;values[at+1]=.25f+.10f*(x+yy);values[at+2]=1;values[at+3]=.40f+.03f*x-.02f*yy;}}
  D3D11_TEXTURE2D_DESC d={};d.Width=d.Height=4;d.MipLevels=1;d.ArraySize=cube?6:1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.MiscFlags=cube?D3D11_RESOURCE_MISC_TEXTURECUBE:0;
  D3D11_SUBRESOURCE_DATA init[6]={};for(int side=0;side<d.ArraySize;++side){init[side].pSysMem=values.data()+side*64;init[side].SysMemPitch=64;}ComPtr<ID3D11Texture2D> tex;Check(device->CreateTexture2D(&d,init,&tex));ComPtr<ID3D11ShaderResourceView> view;Check(device->CreateShaderResourceView(tex.Get(),nullptr,&view));textures[mode].push_back(view);
 }
 // Distinct sampler states exercise native shared-sampler sites; binding all
 // samplers identically would hide a wrong register or accidental duplication.
 std::vector<ComPtr<ID3D11SamplerState>> samplerStates(ns);ID3D11SamplerState* samplers[16]={};
 for(int s=0;s<ns;++s){D3D11_SAMPLER_DESC sd={};sd.Filter=s%3==1?D3D11_FILTER_MIN_MAG_MIP_POINT:D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=s%3==0?D3D11_TEXTURE_ADDRESS_WRAP:s%3==1?D3D11_TEXTURE_ADDRESS_CLAMP:D3D11_TEXTURE_ADDRESS_MIRROR;sd.MaxLOD=D3D11_FLOAT32_MAX;Check(device->CreateSamplerState(&sd,&samplerStates[s]));samplers[s]=samplerStates[s].Get();}context->PSSetSamplers(0,ns,samplers);
 double maxError=0;int kept=0,discarded=0;
 for(int c=0;c<ncases;++c){
  std::vector<ComPtr<ID3D11Buffer>> buffers;std::vector<ID3D11Buffer*> raw;
  for(int b=0;b<ncb;++b){auto data=Read(dir+label+"."+std::to_string(c)+".cb"+std::to_string(b)+".bin");D3D11_BUFFER_DESC d={};d.ByteWidth=(UINT)data.size();d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA init={data.data(),0,0};ComPtr<ID3D11Buffer> buf;Check(device->CreateBuffer(&d,&init,&buf));raw.push_back(buf.Get());buffers.push_back(buf);}context->PSSetConstantBuffers(0,ncb,raw.data());
  D3D11_RASTERIZER_DESC rd={};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.FrontCounterClockwise=c%2;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> raster;Check(device->CreateRasterizerState(&rd,&raster));context->RSSetState(raster.Get());
  std::array<std::array<float,64>,2> result;
  for(int mode=0;mode<2;++mode){std::vector<ID3D11ShaderResourceView*> views;for(auto& t:textures[mode])views.push_back(t.Get());context->PSSetShaderResources(0,nt,views.data());context->PSSetShader(pixel[mode].Get(),nullptr,0);float clear[4]={-7,-7,-7,-7};context->ClearRenderTargetView(rtv.Get(),clear);context->Draw(3,0);context->CopyResource(stage.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE m;Check(context->Map(stage.Get(),0,D3D11_MAP_READ,0,&m));for(int y=0;y<4;++y)memcpy(result[mode].data()+y*16,(char*)m.pData+y*m.RowPitch,64);context->Unmap(stage.Get(),0);}
  for(int p=0;p<16;++p){bool a=result[0][p*4]==-7,b=result[1][p*4]==-7;if(a!=b)throw std::runtime_error("discard mismatch case "+std::to_string(c));if(a){++discarded;continue;}++kept;for(int k=0;k<4;++k){double x=result[0][p*4+k],y=result[1][p*4+k];if(!std::isfinite(x)||!std::isfinite(y))throw std::runtime_error("non-finite output");double e=std::abs(x-y);maxError=std::max(maxError,e);if(e>2e-5)throw std::runtime_error("pixel mismatch case "+std::to_string(c)+" pixel "+std::to_string(p)+" channel "+std::to_string(k)+" native "+std::to_string(x)+" generated "+std::to_string(y));}}
 }
 if(kept==0||discarded==0)throw std::runtime_error("empty positive/alpha control");
 std::cout<<"PASS "<<label<<" cases=64 pixels=1024 kept="<<kept<<" discarded="<<discarded<<" max_error="<<maxError<<" threshold=2e-5 WARP\n";return 0;
} catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
