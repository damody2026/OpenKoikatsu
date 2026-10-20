// Bake every loaded part's own Unreal material into its UV layout.
//
// Each part is redrawn as a flat sheet: vertex positions are its UV0
// coordinates, every normal faces the orthographic camera, and the
// other vertex streams (overlay UVs, COLOR0) are unchanged. The camera
// light of the toon graphs therefore sits on the normal, so each texel
// records the fully lit colour of the real material, including colour
// masks, ramps and overlays the extractor never bakes into _MainTex.
// ok-vertex-color-card samples these images as the source albedo.
#include "KKPreviewActor.h"
#include "KKCardReader.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ProceduralMeshComponent.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "ShaderCompiler.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace OKAlbedoBake
{
// Sheet width in centimetres; the target maps it to the full image.
static constexpr double SheetWidth=100.0;

struct FJob : TSharedFromThis<FJob>
{
    TWeakObjectPtr<AKKPreviewActor> Actor;
    TWeakObjectPtr<USceneCaptureComponent2D> Camera;
    TWeakObjectPtr<UProceduralMeshComponent> Sheet;
    FString Output;
    int32 MaxSize=1024;
    int32 Next=0, Current=-1, Waits=0, Size=0;
    bool Pending=false, Prepared=false, Finished=false, CompiledSinceCapture=false;
    int32 Captures=0;
    uint64 Frame=0, ObservedFrame=MAX_uint64;
    double Started=FPlatformTime::Seconds();
    FVector Center=FVector::ZeroVector;
    FBox2D Box{ForceInit};
    FTimerHandle Timer;
    TArray<TSharedPtr<FJsonValue>> Rows, Skipped;

    void Finish(const FString& Error)
    {
        const auto KeepAlive=AsShared();
        if(Finished)return;Finished=true;
        if(AKKPreviewActor* A=Actor.Get())A->GetWorldTimerManager().ClearTimer(Timer);
        if(!Error.IsEmpty()){UE_LOG(LogTemp,Error,TEXT("Albedo bake: %s"),*Error);FPlatformMisc::RequestExitWithStatus(false,1);return;}
        auto Root=MakeShared<FJsonObject>();
        Root->SetNumberField(TEXT("schema"),1);
        Root->SetStringField(TEXT("space"),TEXT("UE UV0 (u, 1-v); image x=(u-box.x0)/(box.x1-box.x0), y=(1-v-box.y0)/(box.y1-box.y0)"));
        Root->SetStringField(TEXT("encoding"),TEXT("RGB sRGB-encoded linear scene colour, A coverage"));
        Root->SetArrayField(TEXT("parts"),Rows);Root->SetArrayField(TEXT("skipped"),Skipped);
        FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Root,Writer);
        if(!FFileHelper::SaveStringToFile(Text,*(Output/TEXT("bake.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {UE_LOG(LogTemp,Error,TEXT("Albedo bake: cannot write manifest"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        UE_LOG(LogTemp,Display,TEXT("Albedo bake PASS: %d parts, %d skipped -> %s"),Rows.Num(),Skipped.Num(),*Output);
        FPlatformMisc::RequestExitWithStatus(false,0);
    }

    void Skip(const FKKLoadedPart& Part,const TCHAR* Reason)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("index"),Part.SessionIndex);
        Row->SetStringField(TEXT("role"),Part.Role);Row->SetStringField(TEXT("reason"),Reason);
        Skipped.Add(MakeShared<FJsonValueObject>(Row));
    }

    // Rebuild the sheet for one part. False skips the part.
    bool Prepare(const FKKLoadedPart& Part)
    {
        UProceduralMeshComponent* S=Sheet.Get();
        UMaterialInterface* Material=Part.SurfaceMat.Get();
        const int32 N=Part.BaseVertices.Num();
        if(!Material){Skip(Part,TEXT("no surface material"));return false;}
        if(N==0||Part.Triangles.Num()<3||Part.UVs.Num()!=N){Skip(Part,TEXT("no UV0 triangles"));return false;}
        if(Part.EyeSort!=0||Part.GpuEyeMaterial){Skip(Part,TEXT("eye layer"));return false;}
        Box=FBox2D(ForceInit);
        for(int32 T:Part.Triangles)if(Part.UVs.IsValidIndex(T))Box+=Part.UVs[T];
        if(!Box.bIsValid){Skip(Part,TEXT("invalid UV"));return false;}
        const FVector2D Extent=Box.GetSize();
        if(!FMath::IsFinite(Extent.X)||!FMath::IsFinite(Extent.Y)||Extent.X>64||Extent.Y>64){Skip(Part,TEXT("UV range"));return false;}
        Box.Max.X=FMath::Max(Box.Max.X,Box.Min.X+1e-4);Box.Max.Y=FMath::Max(Box.Max.Y,Box.Min.Y+1e-4);
        // Resolution follows the bound texture, scaled by the UV span it covers.
        int32 Source=512;
        if(Part.Texture)Source=FMath::Max(Part.Texture->GetSizeX(),Part.Texture->GetSizeY());
        const double Span=FMath::Max(Box.GetSize().X,Box.GetSize().Y);
        Size=FMath::Clamp(int32(FMath::RoundUpToPowerOfTwo(uint32(FMath::Max(64.0,Source*FMath::Min(Span,4.0))))),128,MaxSize);
        // Camera looks along -Y; image right is +X and image down is -Z.
        TArray<FVector> Positions;TArray<FVector> Normals;TArray<FProcMeshTangent> Tangents;
        Positions.Reserve(N);Normals.Init(FVector(0,1,0),N);Tangents.Init(FProcMeshTangent(FVector(1,0,0),false),N);
        const FVector2D Scale(1.0/Box.GetSize().X,1.0/Box.GetSize().Y);
        for(int32 V=0;V<N;++V)
        {
            const double X=(Part.UVs[V].X-Box.Min.X)*Scale.X,Y=(Part.UVs[V].Y-Box.Min.Y)*Scale.Y;
            Positions.Add(Center+FVector((X-.5)*SheetWidth,0,(.5-Y)*SheetWidth));
        }
        // Wind every triangle toward the camera so one-sided graphs draw too.
        TArray<int32> Triangles;Triangles.Reserve(Part.Triangles.Num());
        for(int32 T=0;T+2<Part.Triangles.Num();T+=3)
        {
            const int32 A=Part.Triangles[T],B=Part.Triangles[T+1],C=Part.Triangles[T+2];
            if(!Positions.IsValidIndex(A)||!Positions.IsValidIndex(B)||!Positions.IsValidIndex(C))continue;
            const bool Front=FVector::DotProduct(FVector::CrossProduct(Positions[B]-Positions[A],Positions[C]-Positions[A]),FVector(0,1,0))>=0;
            Triangles.Append(Front?TArray<int32>{A,B,C}:TArray<int32>{A,C,B});
        }
        TArray<FColor> Colors=Part.Colors.Num()==N?Part.Colors:TArray<FColor>();
        if(Colors.Num()!=N)Colors.Init(FColor::White,N);
        auto Channel=[N](const TArray<FVector2D>& UV){return UV.Num()==N?UV:TArray<FVector2D>();};
        S->ClearAllMeshSections();
        S->CreateMeshSection(0,Positions,Triangles,Normals,Part.UVs,Channel(Part.UV4),Channel(Part.UV2),Channel(Part.UV3),Colors,Tangents,false);
        S->SetMaterial(0,Material);
        // The procedural body draws its overlay as a second translucent section.
        if(Part.OverMat&&Part.UV4.Num()==N)
        {
            TArray<FVector2D> OverlayUV;OverlayUV.Reserve(N);
            for(const FVector2D& T:Part.UV4)OverlayUV.Add(FVector2D(T.X,1.0f-T.Y));
            TArray<FVector> Raised=Positions;for(FVector& P:Raised)P.Y+=.08f;
            S->CreateMeshSection(1,Raised,Triangles,Normals,OverlayUV,TArray<FVector2D>(),TArray<FVector2D>(),TArray<FVector2D>(),Colors,Tangents,false);
            S->SetMaterial(1,Part.OverMat);
        }
        S->MarkRenderStateDirty();
        UE_LOG(LogTemp,Display,TEXT("Albedo bake sheet part=%d verts=%d tris=%d sections=%d bounds=%s material=%s"),Part.SessionIndex,N,Triangles.Num()/3,S->GetNumSections(),*S->Bounds.GetBox().ToString(),*Material->GetName());
        UTextureRenderTarget2D* Target=Camera->TextureTarget;
        if(Target->SizeX!=Size){Target->InitCustomFormat(Size,Size,PF_FloatRGBA,true);Target->UpdateResourceImmediate(true);}
        return true;
    }

    bool Save(const FKKLoadedPart& Part)
    {
        TArray<FLinearColor> Pixels;
        auto* Resource=Camera->TextureTarget->GameThread_GetRenderTargetResource();
        if(!Resource||!Resource->ReadLinearColorPixels(Pixels)||Pixels.Num()!=Size*Size)return false;
        // Scene colour HDR stores inverse opacity in alpha.
        TArray<FColor> Encoded;Encoded.SetNumUninitialized(Pixels.Num());
        int32 Covered=0;double AlphaMin=1,AlphaMax=0;
        for(int32 I=0;I<Pixels.Num();++I)
        {
            const FLinearColor P=Pixels[I];
            const float Coverage=FMath::Clamp(1.f-P.A,0.f,1.f);
            AlphaMin=FMath::Min<double>(AlphaMin,P.A);AlphaMax=FMath::Max<double>(AlphaMax,P.A);
            const float Unpremultiply=Coverage>1e-3f&&Coverage<1.f?1.f/Coverage:1.f;
            FColor C=FLinearColor(FMath::Max(P.R*Unpremultiply,0.f),FMath::Max(P.G*Unpremultiply,0.f),FMath::Max(P.B*Unpremultiply,0.f),1.f).ToFColorSRGB();
            C.A=uint8(FMath::RoundToInt(Coverage*255.f));if(C.A)++Covered;
            Encoded[I]=C;
        }
        auto Wrapper=FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper").CreateImageWrapper(EImageFormat::PNG);
        if(!Wrapper->SetRaw(Encoded.GetData(),Encoded.Num()*sizeof(FColor),Size,Size,ERGBFormat::BGRA,8))return false;
        const FString File=FString::Printf(TEXT("part_%03d.png"),Part.SessionIndex);
        const TArray64<uint8>& Png=Wrapper->GetCompressed();
        if(!FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(Png.GetData(),Png.Num()),*(Output/File)))return false;
        auto Row=MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("index"),Part.SessionIndex);Row->SetStringField(TEXT("role"),Part.Role);
        Row->SetStringField(TEXT("file"),File);Row->SetNumberField(TEXT("size"),Size);
        Row->SetStringField(TEXT("blend"),Part.Blend);Row->SetBoolField(TEXT("unlit"),Part.bUnlit);
        Row->SetNumberField(TEXT("shader_kind"),Part.ShaderKind);Row->SetNumberField(TEXT("covered_pixels"),Covered);
        Row->SetArrayField(TEXT("box"),{MakeShared<FJsonValueNumber>(Box.Min.X),MakeShared<FJsonValueNumber>(Box.Min.Y),MakeShared<FJsonValueNumber>(Box.Max.X),MakeShared<FJsonValueNumber>(Box.Max.Y)});
        Row->SetArrayField(TEXT("shadow_color"),{MakeShared<FJsonValueNumber>(Part.ShadowColor.R),MakeShared<FJsonValueNumber>(Part.ShadowColor.G),MakeShared<FJsonValueNumber>(Part.ShadowColor.B)});
        Rows.Add(MakeShared<FJsonValueObject>(Row));
        UE_LOG(LogTemp,Display,TEXT("Albedo bake part=%d role=%s size=%d covered=%d alpha=[%.3f,%.3f]"),Part.SessionIndex,*Part.Role,Size,Covered,AlphaMin,AlphaMax);
        return true;
    }

    void Step()
    {
        AKKPreviewActor* A=Actor.Get();
        if(!A||!Camera.IsValid()||!Sheet.IsValid()){Finish(TEXT("actor closed"));return;}
        if(GFrameCounter==ObservedFrame)return;
        ObservedFrame=GFrameCounter;++Waits;
        if(FPlatformTime::Seconds()-Started>900){Finish(TEXT("timed out"));return;}
        // Materials compile their sheet permutation on first draw; a capture
        // taken while that compile ran shows nothing and must be repeated.
        if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling()){CompiledSinceCapture=true;return;}
        if(Pending)
        {
            if(GFrameCounter<Frame+4)return;
            if((CompiledSinceCapture||Captures<2)&&Captures<20)
            {Camera->CaptureScene();Frame=GFrameCounter;CompiledSinceCapture=false;++Captures;return;}
            if(!Save(A->Parts[Current])){Finish(FString::Printf(TEXT("cannot save part %d"),A->Parts[Current].SessionIndex));return;}
            Pending=false;
        }
        // Let the character's own materials compile before the first sheet.
        if(Waits<60)return;
        if(!Prepared)
        {
            while(Next<A->Parts.Num()&&!Prepare(A->Parts[Next]))++Next;
            if(Next>=A->Parts.Num()){Sheet->DestroyComponent();Camera->DestroyComponent();Finish(FString());return;}
            Current=Next++;Prepared=true;Frame=GFrameCounter;
            return;
        }
        // The rebuilt sheet needs a render frame (and maybe a new shader
        // permutation, gated above) before it is captured.
        if(GFrameCounter<Frame+2)return;
        Camera->CaptureScene();Frame=GFrameCounter;Pending=true;Prepared=false;CompiledSinceCapture=false;Captures=1;
    }
};
}

static FAutoConsoleCommandWithWorld BakeCardAlbedo(
    TEXT("ok.BakeCardAlbedo"),TEXT("Bake each part's material of OK_ALBEDO_CARD into its UV layout under OK_ALBEDO_OUTPUT"),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        const FString Source=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ALBEDO_CARD"));
        const FString Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ALBEDO_OUTPUT"));
        const FString OutfitSetting=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ALBEDO_OUTFIT"));
        const FString SizeSetting=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_ALBEDO_SIZE"));
        auto Fail=[](const FString& Error){UE_LOG(LogTemp,Error,TEXT("Albedo bake: %s"),*Error);FPlatformMisc::RequestExitWithStatus(false,1);};
        if(!World||Source.IsEmpty()||Output.IsEmpty()){Fail(TEXT("Missing world/OK_ALBEDO_CARD/OK_ALBEDO_OUTPUT"));return;}
        IFileManager::Get().MakeDirectory(*Output,true);
        FKKCharacterFile Card;FString Error;
        if(!UKKCardReader::LoadFromFile(Source,Card,Error)){Fail(Error);return;}
        auto* Actor=World->SpawnActor<AKKPreviewActor>();
        const int32 Outfit=OutfitSetting.IsEmpty()?0:FCString::Atoi(*OutfitSetting);
        if(!Actor||!Actor->Session.Open(FString(),Source,Outfit,Error)||!Actor->LoadSession(Error)){Fail(Error);return;}
        Actor->ApplyShapes(Card.Face.ShapeValueFace,Card.Body.ShapeValueBody);Actor->SetupStudioView();
        Actor->SetActorTickEnabled(false);
        auto Job=MakeShared<OKAlbedoBake::FJob>();Job->Actor=Actor;Job->Output=Output;
        if(!SizeSetting.IsEmpty())Job->MaxSize=FMath::Clamp(FCString::Atoi(*SizeSetting),128,4096);
        // Studio lights are placed for the character; draw the sheets where it stands.
        FVector Origin,Extent;Actor->GetActorBounds(false,Origin,Extent);Job->Center=Origin;
        auto* Sheet=NewObject<UProceduralMeshComponent>(Actor);
        Sheet->SetupAttachment(Actor->GetRootComponent());Actor->AddInstanceComponent(Sheet);Sheet->RegisterComponent();
        Sheet->SetAbsolute(true,true,true);Sheet->SetWorldTransform(FTransform::Identity);
        Sheet->SetCastShadow(false);Sheet->SetCollisionEnabled(ECollisionEnabled::NoCollision);Job->Sheet=Sheet;
        auto* Capture=NewObject<USceneCaptureComponent2D>(Actor);
        Actor->AddInstanceComponent(Capture);Capture->RegisterComponent();Job->Camera=Capture;
        Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
        Capture->ProjectionType=ECameraProjectionMode::Orthographic;Capture->OrthoWidth=OKAlbedoBake::SheetWidth;
        Capture->CaptureSource=ESceneCaptureSource::SCS_SceneColorHDR;
        Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
        Capture->ShowOnlyComponent(Sheet);
        Capture->ShowFlags.SetTemporalAA(false);Capture->ShowFlags.SetMotionBlur(false);Capture->ShowFlags.SetFog(false);
        Capture->SetWorldLocationAndRotation(Origin+FVector(0,500,0),FVector(0,-1,0).Rotation());
        auto* Target=NewObject<UTextureRenderTarget2D>(Capture);Target->ClearColor=FLinearColor(0,0,0,1);
        Target->InitCustomFormat(Job->MaxSize,Job->MaxSize,PF_FloatRGBA,true);Target->UpdateResourceImmediate(true);
        Capture->TextureTarget=Target;
        Actor->GetWorldTimerManager().SetTimer(Job->Timer,FTimerDelegate::CreateLambda([Job](){Job->Step();}),.04f,true);
    }));
