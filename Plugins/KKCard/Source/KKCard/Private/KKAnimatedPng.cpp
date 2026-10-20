#include "KKAnimatedPng.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

namespace KKApng
{
    constexpr uint8 Signature[] = {137,80,78,71,13,10,26,10};
    struct Chunk { int32 Offset; uint32 Size; FString Type; };
    uint32 Be32(const uint8* P) {return (uint32(P[0])<<24)|(uint32(P[1])<<16)|(uint32(P[2])<<8)|P[3];}
    uint16 Be16(const uint8* P) {return (uint16(P[0])<<8)|P[1];}
    void Put32(TArray<uint8>& Out,uint32 N) {for(int I=3;I>=0;--I) Out.Add(uint8(N>>(I*8)));}
    void Put16(TArray<uint8>& Out,uint16 N) {Out.Add(uint8(N>>8));Out.Add(uint8(N));}
    uint32 Crc(const uint8* Bytes,int32 Size)
    {
        static const TArray<uint32> Table=[](){TArray<uint32> T;for(uint32 I=0;I<256;++I){uint32 V=I;for(int B=0;B<8;++B)V=(V>>1)^((V&1)?0xedb88320u:0);T.Add(V);}return T;}();
        uint32 V=~0u;for(int32 I=0;I<Size;++I) V=Table[(V^Bytes[I])&255]^(V>>8);return ~V;
    }
    void Add(TArray<uint8>& Out,const ANSICHAR* Type,const uint8* Data,int32 Size)
    {
        Put32(Out,Size);const int32 Begin=Out.Num();Out.Append(reinterpret_cast<const uint8*>(Type),4);
        if(Size) Out.Append(Data,Size);Put32(Out,Crc(Out.GetData()+Begin,Size+4));
    }
    bool Chunks(const TArray<uint8>& Bytes,TArray<Chunk>& Out,FString& Error)
    {
        Out.Reset();if(Bytes.Num()<20 || FMemory::Memcmp(Bytes.GetData(),Signature,8)) {Error=TEXT("Invalid PNG signature");return false;}
        for(int64 At=8;At+12<=Bytes.Num();)
        {
            uint32 Size=Be32(Bytes.GetData()+At);int64 Next=At+12+Size;
            if(Next>Bytes.Num()) {Error=TEXT("Truncated PNG chunk");return false;}
            ANSICHAR Name[5]={};FMemory::Memcpy(Name,Bytes.GetData()+At+4,4);
            const FString Type=ANSI_TO_TCHAR(Name);
            // Card assets have their own checksum and are not decoded as imagery.
            if(Type!=TEXT("okPv") && Crc(Bytes.GetData()+At+4,int32(Size)+4)!=Be32(Bytes.GetData()+Next-4))
            {Error=TEXT("PNG chunk CRC mismatch");return false;}
            Out.Add({int32(At),Size,Type});At=Next;
            if(Type==TEXT("IEND")) {if(Size!=0){Error=TEXT("Invalid IEND");return false;}return true;}
        }
        Error=TEXT("Missing PNG IEND");return false;
    }
}

bool FKKAnimatedPng::BuildPreview(const TArray<uint8>& SourcePreview,const TArray<TArray<uint8>>& Views,
    TArray<uint8>& Output,FString& Error)
{
    if(Views.Num()!=4) {Output.Reset();Error=TEXT("APNG requires four views");return false;}
    return BuildFrames(SourcePreview,Views,{2,1,1,1},Output,Error);
}

bool FKKAnimatedPng::BuildFrames(const TArray<uint8>& SourcePreview,const TArray<TArray<uint8>>& Views,
    const TArray<uint16>& Seconds,TArray<uint8>& Output,FString& Error)
{
    using namespace KKApng;Output.Reset();Error.Reset();
    const int32 Count=Views.Num();
    if(Count<1||Seconds.Num()!=Count) {Error=TEXT("APNG frame and duration counts differ");return false;}
    TArray<Chunk> Source;if(!Chunks(SourcePreview,Source,Error)) return false;
    TArray<TArray<Chunk>> Inputs;Inputs.SetNum(Count);TArray<uint8> Header;
    for(int I=0;I<Count;++I)
    {
        if(!Chunks(Views[I],Inputs[I],Error)) return false;
        if(Inputs[I].IsEmpty() || Inputs[I][0].Type!=TEXT("IHDR") || Inputs[I][0].Size!=13)
        {Error=TEXT("Missing APNG view IHDR");return false;}
        const uint8* H=Views[I].GetData()+Inputs[I][0].Offset+8;
        // Any size, the same for every view (the first one's): 768x1024 cards, 1280x720 OK scene cards.
        if(Be32(H)==0 || Be32(H+4)==0 || H[8]!=8 || (H[9]!=6 && H[9]!=2) || H[12]!=0)
        {Error=TEXT("Views must be non-interlaced RGB/RGBA PNGs");return false;}
        if(I==0) Header.Append(H,13);
        else if(FMemory::Memcmp(Header.GetData(),H,13)) {Error=TEXT("APNG view pixel formats differ");return false;}
        if(!Inputs[I].ContainsByPredicate([](const Chunk& C){return C.Type==TEXT("IDAT");}))
        {Error=TEXT("Empty APNG frame");return false;}
    }
    Output.Append(Signature,8);Add(Output,"IHDR",Header.GetData(),13);
    // Keep the first image's color information, not the old preview's profile.
    for(const Chunk& C:Inputs[0]) if(C.Type==TEXT("sRGB")||C.Type==TEXT("gAMA")||C.Type==TEXT("cHRM")||C.Type==TEXT("iCCP"))
        Output.Append(Views[0].GetData()+C.Offset,C.Size+12);
    TArray<uint8> Control;Put32(Control,uint32(Count));Put32(Control,0);Add(Output,"acTL",Control.GetData(),Control.Num());
    uint32 Sequence=0;
    for(int I=0;I<Count;++I)
    {
        TArray<uint8> Frame;Put32(Frame,Sequence++);Put32(Frame,Be32(Header.GetData()));Put32(Frame,Be32(Header.GetData()+4));Put32(Frame,0);Put32(Frame,0);
        Put16(Frame,Seconds[I]);Put16(Frame,1);Frame.Add(0);Frame.Add(0);Add(Output,"fcTL",Frame.GetData(),Frame.Num());
        for(const Chunk& C:Inputs[I]) if(C.Type==TEXT("IDAT"))
        {
            if(I==0) Add(Output,"IDAT",Views[I].GetData()+C.Offset+8,C.Size);
            else {TArray<uint8> Data;Put32(Data,Sequence++);Data.Append(Views[I].GetData()+C.Offset+8,C.Size);Add(Output,"fdAT",Data.GetData(),Data.Num());}
        }
    }
    // Preserve private card chunks verbatim (including the shared-resource snapshot).
    for(const Chunk& C:Source)
    {
        const uint8* Type=SourcePreview.GetData()+C.Offset+4;
        if((Type[0]&32) && (Type[1]&32) && C.Type!=TEXT("acTL") && C.Type!=TEXT("fcTL") && C.Type!=TEXT("fdAT"))
            Output.Append(SourcePreview.GetData()+C.Offset,C.Size+12);
    }
    Add(Output,"IEND",nullptr,0);return true;
}

bool FKKAnimatedPng::FitCardImage(const TArray<uint8>& SourcePreview,TArray<uint8>& Output,FString& Error)
{
    Output.Reset();Error.Reset();
    // An earlier OK card APNG: its first frame is already the fitted original image.
    TArray<FKKPngFrame> Frames;uint32 Plays=0;FString ReadError;
    const TArray<uint8>& Original=Read(SourcePreview,Frames,Plays,ReadError)&&Frames.Num()>1?Frames[0].Png:SourcePreview;
    auto& Module=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TSharedPtr<IImageWrapper> Decoder=Module.CreateImageWrapper(EImageFormat::PNG);TArray<uint8> Pixels;
    if(!Decoder.IsValid()||!Decoder->SetCompressed(Original.GetData(),Original.Num())||!Decoder->GetRaw(ERGBFormat::BGRA,8,Pixels))
    {Error=TEXT("Cannot decode the card image");return false;}
    const int32 W=Decoder->GetWidth(),H=Decoder->GetHeight();
    if(W<=0||H<=0||Pixels.Num()!=W*H*4){Error=TEXT("Invalid card image");return false;}
    constexpr int32 CW=768,CH=1024;
    // Same backdrop as the view captures (0.18 linear grey); the whole image fits, aspect kept, centred.
    TArray<uint8> Canvas;Canvas.SetNumUninitialized(CW*CH*4);
    for(int32 I=0;I<CW*CH;++I){Canvas[I*4]=118;Canvas[I*4+1]=118;Canvas[I*4+2]=118;Canvas[I*4+3]=255;}
    const double Scale=FMath::Min(double(CW)/W,double(CH)/H);
    const int32 DW=FMath::Clamp(FMath::RoundToInt(W*Scale),1,CW),DH=FMath::Clamp(FMath::RoundToInt(H*Scale),1,CH);
    const int32 X0=(CW-DW)/2,Y0=(CH-DH)/2;
    // Catmull-Rom (bicubic) weights: a ~3x upscale of a 252x352 card stays sharper than bilinear.
    auto Weights=[](double T,double Out[4])
    {
        const double T2=T*T,T3=T2*T;
        Out[0]=.5*(-T3+2*T2-T);Out[1]=.5*(3*T3-5*T2+2);Out[2]=.5*(-3*T3+4*T2+T);Out[3]=.5*(T3-T2);
    };
    for(int32 Y=0;Y<DH;++Y)for(int32 X=0;X<DW;++X)
    {
        // Sample at the destination texel centre; source alpha composites over the backdrop.
        const double SX=FMath::Clamp((X+.5)/Scale-.5,0.,double(W-1)),SY=FMath::Clamp((Y+.5)/Scale-.5,0.,double(H-1));
        const int32 IX=FMath::FloorToInt(SX),IY=FMath::FloorToInt(SY);
        double WX[4],WY[4];Weights(SX-IX,WX);Weights(SY-IY,WY);
        double C[4]={0,0,0,0};
        for(int32 J=0;J<4;++J)for(int32 I=0;I<4;++I)
        {
            const int32 PX=FMath::Clamp(IX-1+I,0,W-1),PY=FMath::Clamp(IY-1+J,0,H-1);const double Wt=WX[I]*WY[J];
            for(int32 K=0;K<4;++K)C[K]+=Wt*Pixels[(PY*W+PX)*4+K];
        }
        uint8* D=&Canvas[((Y0+Y)*CW+X0+X)*4];const double A=FMath::Clamp(C[3]/255.,0.,1.);
        for(int32 K=0;K<3;++K)D[K]=uint8(FMath::Clamp(FMath::RoundToInt(FMath::Clamp(C[K],0.,255.)*A+D[K]*(1.-A)),0,255));
    }
    TSharedPtr<IImageWrapper> Encoder=Module.CreateImageWrapper(EImageFormat::PNG);
    if(!Encoder.IsValid()||!Encoder->SetRaw(Canvas.GetData(),Canvas.Num(),CW,CH,ERGBFormat::BGRA,8)){Error=TEXT("Cannot encode the card image");return false;}
    // zlib level 9: the card is saved once and read many times (~20% smaller than the default level).
    const TArray64<uint8>& Png=Encoder->GetCompressed(-9);Output.Append(Png.GetData(),int32(Png.Num()));
    return true;
}

bool FKKAnimatedPng::BuildOkCardPreview(const TArray<uint8>& SourcePreview,const TArray<TArray<uint8>>& TPoseViews,
    TArray<uint8>& Output,FString& Error)
{
    Output.Reset();
    if(TPoseViews.Num()!=3){Error=TEXT("OK card APNG requires the three T-pose views");return false;}
    TArray<TArray<uint8>> Frames;Frames.AddDefaulted();
    if(!FitCardImage(SourcePreview,Frames[0],Error))return false;
    Frames.Append(TPoseViews);
    return BuildFrames(SourcePreview,Frames,{3,1,1,1},Output,Error);
}

bool FKKAnimatedPng::Read(const TArray<uint8>& Bytes,TArray<FKKPngFrame>& Frames,uint32& Plays,FString& Error)
{
    using namespace KKApng;Frames.Reset();Plays=0;Error.Reset();TArray<Chunk> ChunksIn;
    if(!Chunks(Bytes,ChunksIn,Error)) return false;
    if(ChunksIn.IsEmpty()||ChunksIn[0].Type!=TEXT("IHDR")||ChunksIn[0].Size!=13) {Error=TEXT("Missing IHDR");return false;}
    const uint8* H=Bytes.GetData()+16;
    const uint32 Width=Be32(H),Height=Be32(H+4);
    if(!Width||!Height||Width>2048||Height>2048) {Error=TEXT("Preview canvas exceeds limit");return false;}
    TArray<uint8> Prefix;Prefix.Append(Signature,8);Prefix.Append(Bytes.GetData()+8,25);
    uint32 Count=0,Sequence=0;bool Data=false,Animated=false;int32 Current=INDEX_NONE;
    auto Fail=[&](const TCHAR* Text){Frames.Reset();Error=Text;return false;};
    for(const Chunk& C:ChunksIn)
    {
        const uint8* P=Bytes.GetData()+C.Offset+8;
        if(C.Type==TEXT("acTL"))
        {
            if(Animated||Data||C.Size!=8) return Fail(TEXT("Invalid animation control"));
            Count=Be32(P);Plays=Be32(P+4);if(Count<1||Count>16) return Fail(TEXT("Animation frame count exceeds limit"));Animated=true;
        }
        else if(C.Type==TEXT("fcTL"))
        {
            if(!Animated||C.Size!=26||Be32(P)!=Sequence++) return Fail(TEXT("APNG frame sequence mismatch"));
            if(Current!=INDEX_NONE) {if(!Data)return Fail(TEXT("Empty APNG frame"));Add(Frames[Current].Png,"IEND",nullptr,0);}
            if(Be32(P+4)!=Width||Be32(P+8)!=Height||Be32(P+12)||Be32(P+16)||P[24]!=0||P[25]!=0)
                return Fail(TEXT("Unsupported APNG frame layout; complete SOURCE frames required"));
            Current=Frames.AddDefaulted();Frames[Current].Png=Prefix;
            Frames[Current].Seconds=FMath::Max(0.01,double(Be16(P+20))/double(Be16(P+22)?Be16(P+22):100));Data=false;
            if(Frames.Num()>int32(Count)) return Fail(TEXT("APNG has extra frames"));
        }
        else if(C.Type==TEXT("IDAT"))
        {
            if(!Animated) continue;
            if(Current!=0) return Fail(TEXT("APNG default image must be its first frame"));
            Add(Frames[0].Png,"IDAT",P,C.Size);Data=true;
        }
        else if(C.Type==TEXT("fdAT"))
        {
            if(Current<1||C.Size<4||Be32(P)!=Sequence++) return Fail(TEXT("APNG data sequence mismatch"));
            Add(Frames[Current].Png,"IDAT",P+4,C.Size-4);Data=true;
        }
        else if(Current==INDEX_NONE && (C.Type==TEXT("sRGB")||C.Type==TEXT("gAMA")||C.Type==TEXT("cHRM")||C.Type==TEXT("iCCP")||C.Type==TEXT("PLTE")||C.Type==TEXT("tRNS")))
            Prefix.Append(Bytes.GetData()+C.Offset,C.Size+12);
    }
    if(!Animated) {Error=TEXT("Static PNG");return false;}
    if(!Data||Frames.Num()!=int32(Count)) return Fail(TEXT("Incomplete APNG frames"));
    Add(Frames.Last().Png,"IEND",nullptr,0);return true;
}

int32 FKKAnimatedPng::FrameAt(const TArray<double>& Durations,double Elapsed,uint32 Plays)
{
    if(Durations.IsEmpty()) return INDEX_NONE;
    double Total=0;for(double D:Durations) {if(!FMath::IsFinite(D)||D<=0)return INDEX_NONE;Total+=D;}
    if(!FMath::IsFinite(Elapsed)||Elapsed<0) Elapsed=0;
    if(Plays && Elapsed>=Total*Plays) return Durations.Num()-1;
    double At=FMath::Fmod(Elapsed,Total);
    for(int32 I=0;I<Durations.Num();++I) {if(At<Durations[I])return I;At-=Durations[I];}return 0;
}
