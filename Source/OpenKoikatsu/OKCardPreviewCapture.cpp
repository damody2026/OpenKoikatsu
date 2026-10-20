#include "OKCardPreviewCapture.h"
#include "KKPreviewActor.h"
#include "KKAnimatedPng.h"
#include "KKCardReader.h"
#include "KKCardWriter.h"
#include "OKGalleryWidget.h"
#include "Components/Image.h"
#include "Components/EditableTextBox.h"
#include "KKCardWorkspace.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UObject/UObjectIterator.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PoseableMeshComponent.h"
#include "Materials/Material.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "ProceduralMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalRenderPublic.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "TimerManager.h"
#include "ShaderCompiler.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"

namespace OKCardPreviewCapture
{
struct FJob : TSharedFromThis<FJob>
{
    TWeakObjectPtr<AKKPreviewActor> Actor;
    TWeakObjectPtr<USceneCaptureComponent2D> Camera;
    FComplete Complete;
    TArray<TArray<uint8>> Views;
    // Read-back pixels (linear-target bytes); converted to sRGB and PNG-encoded off the game thread at the end.
    TArray<TArray<FColor>> RawViews;
    FBox Body{ForceInit}, Face{ForceInit};
    bool WasTicking=false, Pending=false, Finished=false;
    TWeakObjectPtr<UWorld> World;
    int32 View=0, Waits=0;
    // Body-only (OK card): views 1..3 are captured and View starts at 1.
    bool BodyOnly=false;
    uint64 Frame=0, ObservedFrame=MAX_uint64;
    double Started=FPlatformTime::Seconds();
    FTimerHandle Timer;

    void Finish(FString Error)
    {
        // ClearTimer releases the delegate's last reference to this job.
        const auto KeepAlive=AsShared();
        if(Finished)return;Finished=true;
        if(auto* W=World.Get())W->GetTimerManager().ClearTimer(Timer);
        if(AKKPreviewActor* A=Actor.Get()) {A->GetWorldTimerManager().ClearTimer(Timer);A->SetActorTickEnabled(WasTicking);}
        if(auto* C=Camera.Get()) C->DestroyComponent();
        if(!Error.IsEmpty()||RawViews.IsEmpty()){Complete(MoveTemp(Views),MoveTemp(Error));return;}
        // zlib level 9 (~20% smaller frames than the encoder's default 3) costs ~0.7 s per view: the views are
        // encoded in parallel on workers, and the caller continues on the game thread.
        Async(EAsyncExecution::ThreadPool,[Raw=MoveTemp(RawViews),Done=MoveTemp(Complete)]() mutable
        {
            TArray<TArray<uint8>> Encoded;Encoded.SetNum(Raw.Num());TArray<bool> Ok;Ok.Init(false,Raw.Num());
            ParallelFor(Raw.Num(),[&](int32 I)
            {
                // The linear target avoids renderer gamma being applied twice. PNG
                // BGRA bytes must then be encoded to sRGB once, including dark colors.
                for(FColor& P:Raw[I]) P=FLinearColor(P.R/255.f,P.G/255.f,P.B/255.f,1.f).ToFColorSRGB();
                auto Wrapper=FModuleManager::GetModuleChecked<IImageWrapperModule>("ImageWrapper").CreateImageWrapper(EImageFormat::PNG);
                if(!Wrapper.IsValid()||!Wrapper->SetRaw(Raw[I].GetData(),Raw[I].Num()*sizeof(FColor),768,1024,ERGBFormat::BGRA,8))return;
                const TArray64<uint8>& Png=Wrapper->GetCompressed(-9);
                Encoded[I].Append(Png.GetData(),int32(Png.Num()));Ok[I]=!Encoded[I].IsEmpty();
            });
            FString EncodeError=Ok.Contains(false)?TEXT("無法編碼預覽"):FString();
            AsyncTask(ENamedThreads::GameThread,[Encoded=MoveTemp(Encoded),EncodeError,Done=MoveTemp(Done)]() mutable{Done(MoveTemp(Encoded),EncodeError);});
        });
    }
    void Step()
    {
        auto* A=Actor.Get();auto* C=Camera.Get();
        if(!A||!C){Finish(TEXT("角色或世界已關閉"));return;}
        // Timers may run multiple times during a slow frame; require rendered frames.
        if(GFrameCounter==ObservedFrame) return;
        ObservedFrame=GFrameCounter;
        // Shader compilation and slow frames can invoke a timer many times.
        // Count actual frames and allow cold material preparation a bounded 5m.
        if(FPlatformTime::Seconds()-Started>300){Finish(TEXT("預覽視圖擷取逾時"));return;}
        ++Waits;
        if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())return;
        if(Pending)
        {
            if(GFrameCounter<Frame+4) return;
            TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);
            if(!C->TextureTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)||Pixels.Num()!=768*1024)
            {Finish(TEXT("無法讀取預覽視圖 render target"));return;}
            RawViews.Add(MoveTemp(Pixels));
            Pending=false;++View;
            if(View==4){Finish(FString());return;}
        }
        // Allow initial texture/shader streaming, the studio scene (and a T-pose switch) to settle.
        if(RawViews.IsEmpty() && Waits<90) return;
        const FBox& Bounds=View==0?Face:Body;
        const FVector Look=Bounds.GetCenter();
        const FVector Axis=View==2?FVector(-1,0,0):View==3?FVector(0,-1,0):FVector(0,1,0);
        C->SetWorldLocationAndRotation(Look+Axis*500,(Look-(Look+Axis*500)).Rotation());
        const FVector Size=Bounds.GetSize();
        C->OrthoWidth=View==0?FMath::Max(Size.X,Size.Z*.75)*1.25:FMath::Max3(Size.X,Size.Y,Size.Z*.75)*1.10;
        C->CaptureScene();Frame=GFrameCounter;Pending=true;
    }
};

static void Start(AKKPreviewActor* Actor,FComplete Complete,bool bBodyOnly);

void Begin(AKKPreviewActor* Actor,FComplete Complete){Start(Actor,MoveTemp(Complete),false);}

void BeginTPoseViews(AKKPreviewActor* Actor,FComplete Complete)
{
    if(!IsValid(Actor)||!Actor->GetWorld()){Complete({},TEXT("請先載入角色 3D"));return;}
    // T-pose first; the bounds are read a few frames later so the views frame the posed (wider) character.
    Actor->SetPoseMode(0);
    TWeakObjectPtr<AKKPreviewActor> Weak(Actor);
    FTimerHandle Delay;
    Actor->GetWorldTimerManager().SetTimer(Delay,FTimerDelegate::CreateLambda([Weak,Complete=MoveTemp(Complete)]() mutable
    {
        if(AKKPreviewActor* A=Weak.Get())Start(A,MoveTemp(Complete),true);else Complete({},TEXT("角色或世界已關閉"));
    }),.3f,false);
}

static void Start(AKKPreviewActor* Actor,FComplete Complete,bool bBodyOnly)
{
    if(!IsValid(Actor)||!Actor->GetWorld()){Complete({},TEXT("請先載入角色 3D"));return;}
    auto Job=MakeShared<FJob>();Job->Actor=Actor;Job->World=Actor->GetWorld();Job->Complete=MoveTemp(Complete);
    Job->BodyOnly=bBodyOnly;Job->View=bBodyOnly?1:0;
    // The views are encoded on workers; the module is loaded here, on the game thread.
    FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
    Job->WasTicking=Actor->IsActorTickEnabled();Actor->SetActorTickEnabled(false);
    Actor->EnsurePrivateGpuQueryGeometry();
    TSet<FName> FaceMeshes;
    for(const auto& P:Actor->Parts) if(P.bFaceSphereNormals) FaceMeshes.Add(*FString::Printf(TEXT("Skel_%d"),P.SessionIndex));
    TArray<FVector> Points;
    for(UPoseableMeshComponent* Mesh:Actor->SkelFollowers) if(IsValid(Mesh)&&Mesh->IsVisible())
    {
        TArray<FFinalSkinVertex> Vertices;Mesh->GetCPUSkinnedVertices(Vertices,0);
        for(const auto& V:Vertices)
        {
            const FVector P=Mesh->GetComponentTransform().TransformPosition(FVector(V.Position));
            Points.Add(P);Job->Body+=P;if(FaceMeshes.Contains(Mesh->GetFName()))Job->Face+=P;
        }
    }
    for(const auto& Part:Actor->Parts) if(IsValid(Part.Comp)&&Part.Comp->IsVisible())
        for(int32 S=0;S<Part.Comp->GetNumSections();++S) if(auto* Section=Part.Comp->GetProcMeshSection(S);Section&&Section->bSectionVisible)
            for(const auto& V:Section->ProcVertexBuffer)
            {const FVector P=Part.Comp->GetRenderMatrix().TransformPosition(V.Position);Points.Add(P);Job->Body+=P;if(Part.bFaceSphereNormals)Job->Face+=P;}
    if(!Job->Body.IsValid){Job->Finish(TEXT("角色沒有可擷取的模型"));return;}
    if(!Job->Face.IsValid)
    {
        const FVector Center=Job->Body.GetCenter();const double Top=Job->Body.Max.Z;
        Job->Face=FBox(FVector(Center.X-12,Center.Y-12,Top-32),FVector(Center.X+12,Center.Y+12,Top));
    }
    // Include nearby hair above the face, without pulling T-pose hands into the crop.
    const FBox Face=Job->Face;const FVector Center=Face.GetCenter();
    for(const auto& P:Points) if(P.Z>=Face.Min.Z-2&&P.Z<=Face.Max.Z+18&&FMath::Abs(P.X-Center.X)<Face.GetSize().X*.85&&FMath::Abs(P.Y-Center.Y)<30)Job->Face+=P;
    auto* Capture=NewObject<USceneCaptureComponent2D>(Actor);
    Actor->AddInstanceComponent(Capture);Capture->RegisterComponent();Job->Camera=Capture;
    Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;Capture->ProjectionType=ECameraProjectionMode::Orthographic;
    Capture->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->ShowOnlyActorComponents(Actor);
    for(TActorIterator<AActor> It(Actor->GetWorld());It;++It) if(It->ActorHasTag(TEXT("OKStudioBackdrop")))Capture->ShowOnlyActorComponents(*It);
    Capture->ShowFlags.SetTemporalAA(false);Capture->ShowFlags.SetMotionBlur(false);
    if(Actor->StudioCam)Capture->PostProcessSettings=Actor->StudioCam->GetCameraComponent()->PostProcessSettings;
    Capture->PostProcessBlendWeight=1;
    auto* Target=NewObject<UTextureRenderTarget2D>(Capture);Target->ClearColor=FLinearColor(.18f,.18f,.18f,1.f);
    // Read tonemapped color in a linear target and explicitly encode PNG sRGB.
    Target->InitCustomFormat(768,1024,PF_B8G8R8A8,true);Target->UpdateResourceImmediate(true);Capture->TextureTarget=Target;
    Actor->GetWorldTimerManager().SetTimer(Job->Timer,FTimerDelegate::CreateLambda([Job](){Job->Step();}),.04f,true);
}
}

static FAutoConsoleCommandWithWorld ExportApngCard(
    TEXT("ok.ExportApngCard"),TEXT("Export portable card from OK_APNG_SOURCE_CARD to OK_APNG_OUTPUT as an OK card APNG (card image 3 s, T-pose front/side/back 1 s each)"),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        FString Source=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_APNG_SOURCE_CARD"));
        FString Output=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_APNG_OUTPUT"));
        const FString OutfitSetting=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_APNG_OUTFIT"));
        const int32 Outfit=OutfitSetting.IsEmpty()?0:FCString::Atoi(*OutfitSetting);
        FString ViewDirectory=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_APNG_VIEW_DIRECTORY"));
        if(ViewDirectory.IsEmpty())ViewDirectory=FPaths::ProjectSavedDir()/TEXT("APNGCards");
        auto Fail=[](const FString& Error){UE_LOG(LogTemp,Error,TEXT("APNG export: %s"),*Error);FPlatformMisc::RequestExitWithStatus(false,1);};
        if(!World||Source.IsEmpty()||Output.IsEmpty()){Fail(TEXT("Missing world/source/output"));return;}
        IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("APNGCards")),true);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true);
        FKKCharacterFile Card;FString Error;
        if(!UKKCardReader::LoadFromFile(Source,Card,Error)){Fail(Error);return;}
        const FString Height=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_APNG_BODY_HEIGHT"));
        const FString Bust=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_APNG_BODY_BUST"));
        if(!Height.IsEmpty()&&Card.Body.ShapeValueBody.IsValidIndex(0))Card.Body.ShapeValueBody[0]=FCString::Atof(*Height);
        if(!Bust.IsEmpty()&&Card.Body.ShapeValueBody.IsValidIndex(4))Card.Body.ShapeValueBody[4]=FCString::Atof(*Bust);
        auto* Actor=World->SpawnActor<AKKPreviewActor>();
        if(!Actor||!Actor->Session.Open(FString(),Source,Outfit,Error)||!Actor->LoadSession(Error)){Fail(Error);return;}
        Actor->ApplyShapes(Card.Face.ShapeValueFace,Card.Body.ShapeValueBody);Actor->SetupStudioView();
        // Diagnostic capture: distinguish missing neck geometry from an alpha mask.
        if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_APNG_SOLID_BODY"))==TEXT("1"))
        {
            auto* Material=UMaterial::GetDefaultMaterial(MD_Surface);
            for(const auto& Part:Actor->Parts)if(Part.Role==TEXT("body"))
            {
                const FName Name(*FString::Printf(TEXT("Skel_%d"),Part.SessionIndex));
                for(UPoseableMeshComponent* Mesh:Actor->SkelFollowers)if(IsValid(Mesh)&&Mesh->GetFName()==Name)
                    for(int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)Mesh->SetMaterial(Slot,Material);
                if(IsValid(Part.Comp))for(int32 Slot=0;Slot<Part.Comp->GetNumMaterials();++Slot)Part.Comp->SetMaterial(Slot,Material);
            }
        }
        // Low-poly companion follows the same shaped session the views show.
        FString LowPolyError;
        auto LowPoly=FOkLowPoly::Capture(Actor->Session,LowPolyError);
        OKCardPreviewCapture::BeginTPoseViews(Actor,[Card=MoveTemp(Card),Output,ViewDirectory,Fail,LowPoly,LowPolyError](TArray<TArray<uint8>> Views,FString Error) mutable
        {
            if(!Error.IsEmpty()){Fail(Error);return;}
            IFileManager::Get().MakeDirectory(*ViewDirectory,true);
            for(int32 I=0;I<Views.Num();++I)FFileHelper::SaveArrayToFile(Views[I],*(ViewDirectory/FString::Printf(TEXT("view%d.png"),I)));
            TArray<uint8> Preview;
            if(!FKKAnimatedPng::BuildOkCardPreview(Card.Header.PreviewPng,Views,Preview,Error)){Fail(Error);return;}
            Card.Header.PreviewPng=MoveTemp(Preview);
            if(!UKKCardWriter::SaveWithEdits(Card,Output,Error)){Fail(Error);return;}
            UE_LOG(LogTemp,Display,TEXT("APNG export PASS: %s; 768x1024, 4 frames (card image 3 s, T-pose front/side/back 1 s), infinite loop"),*Output);
            // The card is already saved; a missing companion is reported, not fatal.
            FString Summary;
            if(LowPoly&&LowPoly->Export(Output,0,Summary,LowPolyError))
            {UE_LOG(LogTemp,Display,TEXT("APNG low-poly PASS: %s; %s"),*FOkLowPoly::GlbPath(Output),*Summary);}
            else {UE_LOG(LogTemp,Error,TEXT("APNG low-poly FAILED: %s"),*LowPolyError);}
            FPlatformMisc::RequestExitWithStatus(false,0);
        });
    }));

static FAutoConsoleCommandWithWorld VerifyApngGallery(
    TEXT("ok.VerifyApngGallery"),TEXT("Check real gallery APNG playback from OK_APNG_OUTPUT"),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        const FString Path=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_APNG_OUTPUT"));
        UOKCharacterController* Gallery=nullptr;
        for(TObjectIterator<UOKCharacterController> It;It;++It)if(It->GetWorld()==World&&It->PreviewImage){Gallery=*It;break;}
        auto Fail=[](const TCHAR* Error){UE_LOG(LogTemp,Error,TEXT("APNG gallery: %s"),Error);FPlatformMisc::RequestExitWithStatus(false,1);};
        if(!Gallery){Fail(TEXT("Gallery widget not found"));return;}
        Gallery->Entries.Reset();FKKCardListEntry Entry;Entry.Path=Path;Entry.FileName=FPaths::GetCleanFilename(Path);Gallery->Entries.Add(Entry);
        Gallery->SelectIndex(0);Gallery->DecodeRemainingPreviewFramesNow();
        if(Gallery->AnimatedPreviewTextures.Num()!=4){Fail(TEXT("Four preview textures missing"));return;}
        const double Times[]={0,1.99,2,3,4,5,7};const int32 Frames[]={0,0,1,2,3,0,1};
        for(int32 I=0;I<UE_ARRAY_COUNT(Times);++I)
        {
            Gallery->AnimatedPreviewElapsed=Times[I];Gallery->NativeTick(FGeometry(),0);
            if(Gallery->AnimatedPreviewFrame!=Frames[I]||Gallery->PreviewImage->GetBrush().GetResourceObject()!=Gallery->AnimatedPreviewTextures[Frames[I]].Get())
            {Fail(TEXT("Displayed texture does not match APNG time"));return;}
        }
        Gallery->AnimatedPreviewElapsed=0;Gallery->NativeTick(FGeometry(),0);
        struct FPlaybackCheck {FTimerHandle Timer;double Start=FPlatformTime::Seconds();int32 Seen=0;bool Screenshot=false;};
        auto Check=MakeShared<FPlaybackCheck>();TWeakObjectPtr<UOKCharacterController> Weak(Gallery);
        World->GetTimerManager().SetTimer(Check->Timer,FTimerDelegate::CreateLambda([World,Check,Weak,Fail]()
        {
            const auto KeepAlive=Check;
            const auto LocalFail=Fail;
            auto* G=Weak.Get();
            if(!G||FPlatformTime::Seconds()-Check->Start>20){World->GetTimerManager().ClearTimer(Check->Timer);LocalFail(TEXT("Live widget playback timeout"));return;}
            if(G->AnimatedPreviewFrame>=0)Check->Seen|=1<<G->AnimatedPreviewFrame;
            if(!Check->Screenshot&&G->AnimatedPreviewElapsed>.25)
            {FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("APNGCards/gallery.png"),true,false);Check->Screenshot=true;}
            if(G->AnimatedPreviewElapsed<5.2)return;
            World->GetTimerManager().ClearTimer(Check->Timer);
            if(Check->Seen!=15||G->AnimatedPreviewFrame!=0){LocalFail(TEXT("Engine tick did not play all four frames and loop"));return;}
            UE_LOG(LogTemp,Display,TEXT("APNG gallery PASS: engine tick observed all 4 UImage textures and 5s loop; deterministic boundaries 2/1/1/1 seconds"));
            FPlatformMisc::RequestExitWithStatus(false,0);
        }),.1f,true);
    }));

static FAutoConsoleCommandWithWorld VerifyApngExportUI(
    TEXT("ok.VerifyApngExportUI"),TEXT("Load portable APNG, edit body and exercise gallery export button"),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        const FString Source=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_APNG_OUTPUT"));
        UOKCharacterController* Gallery=nullptr;
        for(TObjectIterator<UOKCharacterController> It;It;++It)if(It->GetWorld()==World&&It->PreviewImage){Gallery=*It;break;}
        auto Fail=[](const FString& Error){UE_LOG(LogTemp,Error,TEXT("APNG export UI: %s"),*Error);FPlatformMisc::RequestExitWithStatus(false,1);};
        if(!Gallery){Fail(TEXT("No gallery"));return;}
        Gallery->Entries.Reset();FKKCardListEntry Entry;Entry.Path=Source;Entry.FileName=FPaths::GetCleanFilename(Source);Gallery->Entries.Add(Entry);Gallery->SelectIndex(0);
        if(Gallery->Selected.Body.ShapeValueBody.Num()!=44){Fail(TEXT("Card body missing"));return;}
        Gallery->Selected.Body.ShapeValueBody[0]=.222f;Gallery->Selected.Body.ShapeValueBody[4]=.444f;
        Gallery->NicknameBox->SetText(FText::FromString(TEXT("APNG UI export")));
        const FString Folder=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("APNGCards/UiExport")/FGuid::NewGuid().ToString());
        Gallery->OutputDirectoryInput->SetText(FText::FromString(Folder));
        const FString Output=UKKCardWorkspace::NewCardPath(Folder,Source);
        Gallery->OnLoad3D();
        struct FExportCheck {FTimerHandle Timer;double Start=FPlatformTime::Seconds();bool Capturing=false;bool OutfitCapture=false;int32 Captured=-1;FString CaptureError;};
        auto Check=MakeShared<FExportCheck>();TWeakObjectPtr<UOKCharacterController> Weak(Gallery);
        World->GetTimerManager().SetTimer(Check->Timer,FTimerDelegate::CreateLambda([World,Check,Weak,Output,Fail]()
        {
            const auto KeepAlive=Check;auto* G=Weak.Get();
            const auto LocalFail=Fail;const FString SavedOutput=Output;
            auto Stop=[&](const FString& Error){World->GetTimerManager().ClearTimer(Check->Timer);LocalFail(Error);};
            if(!G||FPlatformTime::Seconds()-Check->Start>600){Stop(TEXT("Export UI timeout"));return;}
            if(!Check->Capturing)
            {
                if(G->bLoadInFlight)return;
                if(!G->PreviewActor||G->PreviewCardPath!=G->Entries[0].Path||G->PreviewOutfitIndex!=G->OutfitIndex){Stop(TEXT("3D load failed"));return;}
                if(!Check->CaptureError.IsEmpty()){Stop(Check->CaptureError);return;}
                if(Check->OutfitCapture)return;
                if(Check->Captured<G->OutfitIndex)
                {
                    Check->OutfitCapture=true;const int32 Outfit=G->OutfitIndex;
                    OKCardPreviewCapture::Begin(G->PreviewActor,[Check,Outfit](TArray<TArray<uint8>> Views,FString Error)
                    {
                        const FString Folder=FPaths::ProjectSavedDir()/TEXT("APNGCards/AllOutfitViews");
                        IFileManager::Get().MakeDirectory(*Folder,true);
                        for(int32 I=0;I<Views.Num();++I)if(!FFileHelper::SaveArrayToFile(Views[I],*(Folder/FString::Printf(TEXT("outfit%d_view%d.png"),Outfit,I))))Error=TEXT("Cannot save outfit views");
                        Check->Captured=Outfit;Check->CaptureError=Error;Check->OutfitCapture=false;
                        UE_LOG(LogTemp,Display,TEXT("APNG outfit render: %d, %d views, %s"),Outfit,Views.Num(),*Error);
                    });return;
                }
                if(G->OutfitIndex+1<G->Selected.Outfits.Num())
                {G->OnOutfitNext();return;}
                G->OnSaveApng();Check->Capturing=true;
                if(!G->bApngCaptureInFlight||G->GetIsEnabled()){Stop(TEXT("Export button did not start capture"));return;}
                return;
            }
            if(G->bApngCaptureInFlight)return;
            FKKCharacterFile Card;FString Error;TArray<FKKPngFrame> Frames;uint32 Plays;
            if(!G->GetIsEnabled()||!UKKCardReader::LoadFromFile(Output,Card,Error)||!FKKAnimatedPng::Read(Card.Header.PreviewPng,Frames,Plays,Error))
            {Stop(Error.IsEmpty()?TEXT("Export button failed"):Error);return;}
            FOkSession Session;
            if(Frames.Num()!=4||Plays!=0||Card.Nickname!=TEXT("APNG UI export")||!Session.Open(FString(),Output,0,Error)||
                !FMath::IsNearlyEqual(Session.ShapeBody()[0],.222f)||!FMath::IsNearlyEqual(Session.ShapeBody()[4],.444f))
            {Stop(Error.IsEmpty()?TEXT("Exported APNG lost body/nickname/animation"):Error);return;}
            if(Card.Outfits.Num()!=G->Selected.Outfits.Num()||Session.CoordinateCount()!=Card.Outfits.Num())
            {Stop(TEXT("Exported APNG lost outfit count"));return;}
            for(int32 I=1;I<Card.Outfits.Num();++I)
            {
                FOkSession Outfit;
                if(!Outfit.Open(FString(),Output,I,Error)||!FMath::IsNearlyEqual(Outfit.ShapeBody()[0],.222f)||!FMath::IsNearlyEqual(Outfit.ShapeBody()[4],.444f))
                {Stop(Error.IsEmpty()?TEXT("Exported outfit lost saved shapes"):Error);return;}
            }
            World->GetTimerManager().ClearTimer(Check->Timer);
            UE_LOG(LogTemp,Display,TEXT("APNG export UI PASS: %s; loaded/edited/captured/reopened without game root"),*SavedOutput);
            FPlatformMisc::RequestExitWithStatus(false,0);
        }),.2f,true);
    }));
