#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <string>
#include <iostream>
#include <cmath>
#include <cstring>
using Microsoft::WRL::ComPtr;
static void CheckAt(HRESULT h,int line){if(FAILED(h))throw std::runtime_error("HRESULT "+std::to_string(h)+" at line "+std::to_string(line));}
#define Check(h) CheckAt((h),__LINE__)
static std::vector<char> Read(const std::string& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("missing "+p);return {std::istreambuf_iterator<char>(f),{}};}
static ComPtr<ID3DBlob> Compile(const std::vector<char>& s,const char* profile){ComPtr<ID3DBlob>b,e;auto h=D3DCompile(s.data(),s.size(),nullptr,nullptr,nullptr,"main",profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b,&e);if(FAILED(h)){if(e)std::cerr<<(char*)e->GetBufferPointer();Check(h);}return b;}
struct Semantic{std::string name;UINT index,count;};
int main(int argc,char**argv)try{
 if(argc!=4&&argc!=5)throw std::runtime_error("vertex_oracle <nativeSource> <cases> <label> [negativeHlsl]");
 std::string dir=std::string(argv[2])+"/",label=argv[3];
 std::ifstream config(dir+label+".config");int blob,ncb,nt,ns,cases,vertices,ni,no;
 if(!(config>>blob>>ncb>>nt>>ns>>cases>>vertices>>ni>>no)||ncb<1||ncb>4||nt>1||ns>1||ni<1||no<1||no>12||vertices!=32||cases!=32)throw std::runtime_error("configuration");
 std::vector<Semantic> inputs(ni),outputs(no);UINT components=0;
 for(auto&s:inputs)if(!(config>>s.name>>s.index>>s.count))throw std::runtime_error("input signature");
 for(auto&s:outputs){if(!(config>>s.name>>s.index>>s.count))throw std::runtime_error("output signature");components+=s.count;}
 ComPtr<ID3D11Device>device;ComPtr<ID3D11DeviceContext>ctx;D3D_FEATURE_LEVEL fl;
 Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&fl,&ctx));
 char number[16];snprintf(number,sizeof(number),"%04d.dxbc",blob);auto native=Read(std::string(argv[1])+"/"+number);
 auto generated=Compile(Read(argc==5?argv[4]:dir+label+".hlsl"),"vs_5_0");
 if(argc==4){std::ofstream f(dir+label+".generated.dxbc",std::ios::binary);f.write((char*)generated->GetBufferPointer(),generated->GetBufferSize());}
 ComPtr<ID3D11VertexShader>vs[2];Check(device->CreateVertexShader(native.data(),native.size(),nullptr,&vs[0]));Check(device->CreateVertexShader(generated->GetBufferPointer(),generated->GetBufferSize(),nullptr,&vs[1]));
 std::vector<D3D11_INPUT_ELEMENT_DESC>layout;
 for(UINT i=0;i<inputs.size();++i)layout.push_back({inputs[i].name.c_str(),inputs[i].index,DXGI_FORMAT_R32G32B32A32_FLOAT,0,i*16,D3D11_INPUT_PER_VERTEX_DATA,0});
 ComPtr<ID3D11InputLayout>il[2];Check(device->CreateInputLayout(layout.data(),layout.size(),native.data(),native.size(),&il[0]));Check(device->CreateInputLayout(layout.data(),layout.size(),generated->GetBufferPointer(),generated->GetBufferSize(),&il[1]));
 // A common passthrough GS captures all declared semantic components before
 // rasterization. Unwritten native signature entries are deliberately excluded.
 std::string gs="struct O{";
 for(int i=0;i<no;++i)gs+="float"+std::to_string(outputs[i].count)+" a"+std::to_string(i)+":"+outputs[i].name+std::to_string(outputs[i].index)+";";
 gs+="};[maxvertexcount(1)]void main(point O input[1],inout PointStream<O> stream){stream.Append(input[0]);}";
 auto gcode=Compile(std::vector<char>(gs.begin(),gs.end()),"gs_5_0");ComPtr<ID3D11ShaderReflection>reflection;
 Check(D3DReflect(gcode->GetBufferPointer(),gcode->GetBufferSize(),IID_ID3D11ShaderReflection,&reflection));D3D11_SHADER_DESC desc;Check(reflection->GetDesc(&desc));
 std::vector<D3D11_SO_DECLARATION_ENTRY>entries;
 for(auto&s:outputs){bool found=false;for(UINT i=0;i<desc.OutputParameters;++i){D3D11_SIGNATURE_PARAMETER_DESC p;Check(reflection->GetOutputParameterDesc(i,&p));if(s.name==p.SemanticName&&s.index==p.SemanticIndex){UINT start=0;while(start<4&&!(p.Mask&(1<<start)))++start;UINT mask=((1<<s.count)-1)<<start;if(mask!=p.Mask)throw std::runtime_error("non-contiguous output mask");entries.push_back({0,s.name.c_str(),s.index,0,(BYTE)s.count,0});found=true;break;}}if(!found)throw std::runtime_error("missing output semantic");}
 UINT soStride=components*4;ComPtr<ID3D11GeometryShader>geometry;Check(device->CreateGeometryShaderWithStreamOutput(gcode->GetBufferPointer(),gcode->GetBufferSize(),entries.data(),entries.size(),&soStride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&geometry));
 ctx->GSSetShader(geometry.Get(),nullptr,0);ctx->PSSetShader(nullptr,nullptr,0);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
 auto vertexData=Read(dir+label+".vertices.bin");if(vertexData.size()!=vertices*ni*16)throw std::runtime_error("vertex data width");
 D3D11_BUFFER_DESC bd={};bd.ByteWidth=vertexData.size();bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;D3D11_SUBRESOURCE_DATA init={vertexData.data(),0,0};ComPtr<ID3D11Buffer>vb;Check(device->CreateBuffer(&bd,&init,&vb));UINT stride=ni*16,offset=0;auto vertexBuffer=vb.Get();ctx->IASetVertexBuffers(0,1,&vertexBuffer,&stride,&offset);
 bd.ByteWidth=vertices*soStride;bd.BindFlags=D3D11_BIND_STREAM_OUTPUT;ComPtr<ID3D11Buffer>out;Check(device->CreateBuffer(&bd,nullptr,&out));bd.BindFlags=0;bd.Usage=D3D11_USAGE_STAGING;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Buffer>stage;Check(device->CreateBuffer(&bd,nullptr,&stage));
 ComPtr<ID3D11ShaderResourceView>texture;ComPtr<ID3D11SamplerState>sampler;
 if(nt){float values[64];for(int y=0;y<4;++y)for(int x=0;x<4;++x)for(int c=0;c<4;++c)values[(y*4+x)*4+c]=.1f+.07f*((y*3+x*2+c)%10);D3D11_TEXTURE2D_DESC td={};td.Width=td.Height=4;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA data={values,64,0};ComPtr<ID3D11Texture2D>t;Check(device->CreateTexture2D(&td,&data,&t));Check(device->CreateShaderResourceView(t.Get(),nullptr,&texture));auto raw=texture.Get();ctx->VSSetShaderResources(0,1,&raw);D3D11_SAMPLER_DESC sd={};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sd.MaxLOD=D3D11_FLOAT32_MAX;Check(device->CreateSamplerState(&sd,&sampler));auto sm=sampler.Get();ctx->VSSetSamplers(0,1,&sm);}
 D3D11_QUERY_DESC qd={D3D11_QUERY_SO_STATISTICS,0};ComPtr<ID3D11Query>statistics;Check(device->CreateQuery(&qd,&statistics));
 double maxError=0;UINT compared=0;
 for(int c=0;c<cases;++c){std::vector<ComPtr<ID3D11Buffer>>buffers;std::vector<ID3D11Buffer*>raw;
  for(int b=0;b<ncb;++b){auto bytes=Read(dir+label+"."+std::to_string(c)+".cb"+std::to_string(b)+".bin");D3D11_BUFFER_DESC d={};d.ByteWidth=bytes.size();d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA data={bytes.data(),0,0};ComPtr<ID3D11Buffer>buf;Check(device->CreateBuffer(&d,&data,&buf));raw.push_back(buf.Get());buffers.push_back(buf);}ctx->VSSetConstantBuffers(0,ncb,raw.data());
  std::vector<float>results[2];
  for(int mode=0;mode<2;++mode){ctx->IASetInputLayout(il[mode].Get());ctx->VSSetShader(vs[mode].Get(),nullptr,0);auto target=out.Get();UINT off=0;ctx->SOSetTargets(1,&target,&off);ctx->Begin(statistics.Get());ctx->Draw(vertices,0);ctx->End(statistics.Get());ID3D11Buffer*none=nullptr;ctx->SOSetTargets(1,&none,&off);ctx->CopyResource(stage.Get(),out.Get());D3D11_MAPPED_SUBRESOURCE m;Check(ctx->Map(stage.Get(),0,D3D11_MAP_READ,0,&m));results[mode].resize(vertices*components);memcpy(results[mode].data(),m.pData,vertices*soStride);ctx->Unmap(stage.Get(),0);D3D11_QUERY_DATA_SO_STATISTICS stats={};auto h=ctx->GetData(statistics.Get(),&stats,sizeof(stats),0);if(h!=S_OK||stats.NumPrimitivesWritten!=vertices||stats.PrimitivesStorageNeeded!=vertices)throw std::runtime_error("incomplete stream output");}
  for(UINT i=0;i<vertices*components;++i){double a=results[0][i],b=results[1][i];if(!std::isfinite(a)||!std::isfinite(b))throw std::runtime_error("non-finite vertex output");double error=std::abs(a-b);maxError=std::max(maxError,error);if(error>2e-5)throw std::runtime_error("vertex mismatch case "+std::to_string(c)+" component "+std::to_string(i)+" native "+std::to_string(a)+" generated "+std::to_string(b));++compared;}
 }
 std::cout<<"PASS "<<label<<" cases="<<cases<<" vertices="<<cases*vertices<<" components="<<compared<<" max_error="<<maxError<<" threshold=2e-5 WARP\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
