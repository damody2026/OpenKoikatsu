#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <string>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT h) { if(FAILED(h))throw std::runtime_error("HRESULT "+std::to_string(h)); }
int main(int argc,char** argv) try {
 if(argc!=2)throw std::runtime_error("compute_oracle <hlsl>");
 std::ifstream f(argv[1],std::ios::binary);if(!f)throw std::runtime_error("missing source");
 std::string source{std::istreambuf_iterator<char>(f),{}};
 ComPtr<ID3DBlob> code,error;
 HRESULT h=D3DCompile(source.data(),source.size(),argv[1],nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
 if(FAILED(h)&&error)std::cerr<<(const char*)error->GetBufferPointer();Check(h);
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
 Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context));
 ComPtr<ID3D11ComputeShader> shader;Check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));
 D3D11_BUFFER_DESC desc={};desc.ByteWidth=512*16;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=16;
 ComPtr<ID3D11Buffer> output,staging;Check(device->CreateBuffer(&desc,nullptr,&output));
 D3D11_UNORDERED_ACCESS_VIEW_DESC u={};u.Format=DXGI_FORMAT_UNKNOWN;u.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;u.Buffer.NumElements=512;
 ComPtr<ID3D11UnorderedAccessView> view;Check(device->CreateUnorderedAccessView(output.Get(),&u,&view));
 desc.BindFlags=0;desc.MiscFlags=0;desc.StructureByteStride=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Check(device->CreateBuffer(&desc,nullptr,&staging));
 context->CSSetShader(shader.Get(),nullptr,0);auto raw=view.Get();context->CSSetUnorderedAccessViews(0,1,&raw,nullptr);context->Dispatch(8,1,1);context->CopyResource(staging.Get(),output.Get());
 D3D11_MAPPED_SUBRESOURCE m;Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m));
 auto data=(float*)m.pData;double maximum=0;
 for(int i=0;i<512;++i)for(int c=0;c<2;++c){double a=data[4*i+c],b=data[4*i+c+2];if(!std::isfinite(a)||!std::isfinite(b))throw std::runtime_error("non-finite coordinate");maximum=std::max(maximum,std::abs(a-b));}
 context->Unmap(staging.Get(),0);if(maximum>1e-6)throw std::runtime_error("UV mismatch max="+std::to_string(maximum));
 std::cout<<"PASS cases=512 taps=2 UV max="<<maximum<<" threshold=1e-6 WARP source="<<argv[1]<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
