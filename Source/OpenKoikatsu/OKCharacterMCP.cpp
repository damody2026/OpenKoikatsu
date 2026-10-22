#include "OKCharacterController.h"
#include "OKHGShading.h"
#include "OKCharacterMCPRegistry.h"
#include "OKCharacterMCPGeometry.h"
#include "OKVideoMotionPanel.h"
#include "OKStudioScene.h"
#include "KKPreviewActor.h"
#include "KKCardWorkspace.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/EditableTextBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

using FMCPObject=TSharedPtr<FJsonObject>;
using FMCPRows=TArray<TSharedPtr<FJsonValue>>;
static int32 MCPAppearanceCategory(const FMCPObject& Item)
{
 double Category=0;if(Item->TryGetNumberField(TEXT("category"),Category))return int32(Category);
 const TSharedPtr<FJsonObject>* Entry=nullptr;
 return Item->TryGetObjectField(TEXT("entry"),Entry)?(*Entry)->GetIntegerField(TEXT("category_no")):-1;
}
struct FOKCharacterMCPSnapshot { FKKCharacterFile Card; FString Path,Source,Properties,Pending;int32 Outfit=0;bool Independent=false; };
struct FOKCharacterMCPState
{
 FString Root,Session,Identity,RequestId,Action,Source,BeforePath;
 FMCPObject Request;
 double Deadline=0,ReadyAt=0,HeartbeatAt=0;
 FString LoggedOperation;
 uint64 ReadyFrame=0;
 uint32 Fingerprint=0;
 int32 Revision=0,Phase=0,Selection=INDEX_NONE,Outfit=0,View=0,CaptureRevision=0;
 FMCPRows Images;
 FDelegateHandle ShotHandle;
 FIntPoint ShotSize=FIntPoint::ZeroValue;
 FIntRect Crop;
 TArray<FColor> ShotPixels;
 TMap<FString,FOKCharacterMCPSnapshot> Snapshots;
 TMap<TWeakObjectPtr<USceneComponent>,bool> HiddenVisibility;
 TWeakObjectPtr<AKKPreviewActor> CaptureActor;
 bool WasTicking=false,AutoLoadAttempted=false;
 float SavedYaw=0,SavedPitch=0;
};
namespace OKCharacterMCP
{
static FString Encode(const FMCPObject& O){FString T;if(O)FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&T));return T;}
static bool Busy(UOKCharacterController* G){return G->bLoadInFlight||G->PreparingPreview||G->bPropertyBusy||G->bAppearanceBusy||G->bAnimationPreparing||G->bPortableSaveInFlight||G->bApngCaptureInFlight;}
static FString Source(UOKCharacterController* G){if(auto* C=G->CardSources.Find(G->SelectedIndex))return C->Key;return G->Entries.IsValidIndex(G->SelectedIndex)?G->Entries[G->SelectedIndex].Path:FString();}
static void Refresh(UOKCharacterController* G,FOKCharacterMCPState& S)
{
 const FString I=FString::Printf(TEXT("%d|%d|%d|%s"),G->SelectedIndex,G->OutfitIndex,G->LoadGeneration,G->Entries.IsValidIndex(G->SelectedIndex)?*G->Entries[G->SelectedIndex].Path:TEXT(""));
 if(I!=S.Identity){S.Identity=I;S.Session=FGuid::NewGuid().ToString(EGuidFormats::Digits);++S.Revision;}
 auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("mods"),G->EditorModsJson());O->SetStringField(TEXT("nickname"),G->Selected.Nickname);O->SetArrayField(TEXT("controls"),OKCharacterMCPRegistry::Controls(G));for(auto E:G->PendingProperties)O->SetField(E.Key,E.Value);
 uint32 H=FCrc::StrCrc32(*Encode(O));const auto& B=G->Selected.Body.ShapeValueBody;const auto& F=G->Selected.Face.ShapeValueFace;
 H=HashCombine(H,FCrc::MemCrc32(B.GetData(),B.Num()*sizeof(float)));H=HashCombine(H,FCrc::MemCrc32(F.GetData(),F.Num()*sizeof(float)));H=HashCombine(H,FCrc::MemCrc32(G->ClothesStates,sizeof(G->ClothesStates)));
 if(H!=S.Fingerprint){S.Fingerprint=H;++S.Revision;}
}
static void RestoreCamera(FOKCharacterMCPState& S){if(auto* A=S.CaptureActor.Get()){A->OrbitYaw=S.SavedYaw;A->OrbitPitch=S.SavedPitch;A->ApplyOrbitCamera();A->SetActorTickEnabled(S.WasTicking);}S.CaptureActor.Reset();}
static void Finish(FOKCharacterMCPState& S,FMCPObject Result,const FString& Error=FString())
{
 if(S.ShotHandle.IsValid()){UGameViewportClient::OnScreenshotCaptured().Remove(S.ShotHandle);S.ShotHandle.Reset();}S.ShotPixels.Reset();
 RestoreCamera(S);auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("ok"),Error.IsEmpty());R->SetStringField(TEXT("session"),S.Session);R->SetNumberField(TEXT("revision"),S.Revision);if(Result)R->SetObjectField(TEXT("result"),Result.ToSharedRef());if(!Error.IsEmpty())R->SetStringField(TEXT("error"),Error);
 const FString P=S.Root/TEXT("responses")/(S.RequestId+TEXT(".json"));if(FFileHelper::SaveStringToFile(Encode(R),*(P+TEXT(".tmp")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))IFileManager::Get().Move(*P,*(P+TEXT(".tmp")),true);
 S.Request.Reset();S.Phase=0;S.RequestId.Empty();S.Images.Reset();
}
// bFingerprint is for a claimed request. The 0.5 s heartbeat must not take it:
// Refresh decodes the whole KKEx blob and serializes every control, including
// combo option lists, which stalls the game thread and steps stat unitgraph.
static FMCPObject State(UOKCharacterController* G,FOKCharacterMCPState& S,bool bFingerprint=true)
{
 if(bFingerprint)Refresh(G,S);auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("session"),S.Session);R->SetNumberField(TEXT("revision"),S.Revision);R->SetStringField(TEXT("card"),G->Entries[G->SelectedIndex].Path);R->SetNumberField(TEXT("outfit"),G->OutfitIndex);R->SetBoolField(TEXT("busy"),Busy(G));R->SetNumberField(TEXT("pending_properties"),G->PendingProperties.Num());R->SetStringField(TEXT("status"),Busy(G)?TEXT("busy"):G->PendingProperties.IsEmpty()?TEXT("applied"):TEXT("staged"));
 if(G->AnimationStatus)R->SetStringField(TEXT("animation_status"),G->AnimationStatus->GetText().ToString());if(G->PairStatus)R->SetStringField(TEXT("pair_status"),G->PairStatus->GetText().ToString());
 if(G->StudioWidget)R->SetObjectField(TEXT("studio"),G->StudioWidget->StateJson());
 if(G->VideoMotionPanel){R->SetStringField(TEXT("video_status"),G->VideoMotionPanel->StatusText());R->SetBoolField(TEXT("video_importing"),G->VideoMotionPanel->IsImporting());}R->SetStringField(TEXT("selected_clip"),G->SelectedGameClip);
 R->SetBoolField(TEXT("partner_loaded"),IsValid(G->PartnerActor)&&G->PartnerActor->Session.IsOpen());R->SetBoolField(TEXT("partner_loading"),G->bPartnerLoadInFlight);
 if(IsValid(G->PartnerActor)){R->SetNumberField(TEXT("partner_pose_mode"),G->PartnerActor->PoseMode);R->SetBoolField(TEXT("partner_ticking"),G->PartnerActor->IsActorTickEnabled());R->SetNumberField(TEXT("partner_anim_phase"),G->PartnerActor->AnimPhase);}
 if(IsValid(G->PreviewActor))R->SetNumberField(TEXT("anim_phase"),G->PreviewActor->AnimPhase);
 if(G->PropertyStatus)R->SetStringField(TEXT("property_status"),G->PropertyStatus->GetText().ToString());if(G->AppearanceStatus)R->SetStringField(TEXT("appearance_status"),G->AppearanceStatus->GetText().ToString());
 FMCPRows States;for(int32 I=0;I<9;++I)States.Add(MakeShared<FJsonValueNumber>(G->ClothesStates[I]));R->SetArrayField(TEXT("clothing_states"),States);
 if(auto* V=G->GetWorld()->GetGameViewport())if(V->Viewport){auto Size=V->Viewport->GetSizeXY();R->SetNumberField(TEXT("viewport_width"),Size.X);R->SetNumberField(TEXT("viewport_height"),Size.Y);}
 auto* A=G->PreviewActor.Get();R->SetNumberField(TEXT("pose_mode"),A->PoseMode);R->SetBoolField(TEXT("preview_ticking"),A->IsActorTickEnabled());auto Waiting=MakeShared<FJsonObject>();Waiting->SetBoolField(TEXT("mods_dirty"),G->bEditorModsDirty);Waiting->SetBoolField(TEXT("shape_dirty"),A->bShapeDirty);Waiting->SetBoolField(TEXT("materials_ready"),A->bPlaybackMaterialsReady);Waiting->SetBoolField(TEXT("shaders_compiling"),GShaderCompilingManager&&GShaderCompilingManager->IsCompiling());R->SetObjectField(TEXT("settling"),Waiting);auto C=MakeShared<FJsonObject>();C->SetNumberField(TEXT("yaw"),A->OrbitYaw);C->SetNumberField(TEXT("pitch"),A->OrbitPitch);C->SetNumberField(TEXT("distance"),A->OrbitDistance);C->SetArrayField(TEXT("focus"),{MakeShared<FJsonValueNumber>(A->OrbitFocus.X),MakeShared<FJsonValueNumber>(A->OrbitFocus.Y),MakeShared<FJsonValueNumber>(A->OrbitFocus.Z)});if(A->StudioLens){C->SetStringField(TEXT("projection"),A->StudioLens->ProjectionMode==ECameraProjectionMode::Orthographic?TEXT("orthographic"):TEXT("perspective"));C->SetNumberField(TEXT("ortho_width"),A->StudioLens->OrthoWidth);}R->SetObjectField(TEXT("camera"),C);R->SetBoolField(TEXT("sweep_active"),A->bProductSweep);R->SetBoolField(TEXT("sweep_latched"),A->bProductLightLatched);R->SetStringField(TEXT("sweep_ramp"),A->ProductRampLabel);R->SetNumberField(TEXT("sweep_slots"),A->ProductRampSlots);return R;
}
static void Page(UOKCharacterController* G,FOKCharacterMCPState& S,const FMCPRows& All,const TCHAR* Field=TEXT("parameters"))
{
 FString Query;S.Request->TryGetStringField(TEXT("query"),Query);double Offset=0,Limit=100;S.Request->TryGetNumberField(TEXT("offset"),Offset);S.Request->TryGetNumberField(TEXT("limit"),Limit);
 if(!FMath::IsFinite(Offset)||!FMath::IsFinite(Limit)||Offset<0||Offset>1000000||Limit<1||Limit>500||Offset!=FMath::FloorToDouble(Offset)||Limit!=FMath::FloorToDouble(Limit)){Finish(S,nullptr,TEXT("Invalid pagination"));return;}
 FMCPRows Rows;int32 Count=0;for(auto P:All){auto O=P->AsObject();FString Key,L;O->TryGetStringField(TEXT("key"),Key);O->TryGetStringField(TEXT("label"),L);if(!Query.IsEmpty()&&!Key.Contains(Query)&&!L.Contains(Query))continue;if(Count>=int32(Offset)&&Rows.Num()<int32(Limit))Rows.Add(P);++Count;}
 auto R=State(G,S);R->SetArrayField(Field,Rows);R->SetNumberField(TEXT("total"),Count);R->SetNumberField(TEXT("next_offset"),int32(Offset)+Rows.Num());
 if(S.Action==TEXT("catalog")){FMCPRows Targets;for(auto T:G->AppearanceTargets)Targets.Add(MakeShared<FJsonValueObject>(T));R->SetArrayField(TEXT("targets"),Targets);}Finish(S,R);
}
static bool Settled(UOKCharacterController* G,FOKCharacterMCPState& S)
{
 // Progressive material replacement invalidates the readiness flag. Drive its
 // nonblocking preparation/fence path so a settled editor can become ready.
 if(Busy(G)||G->bEditorModsDirty||G->PreviewActor->bShapeDirty||!G->PreviewActor->PreparePlaybackMaterials(false)||(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())){S.ReadyAt=0;return false;}if(!S.ReadyAt){S.ReadyAt=FPlatformTime::Seconds();S.ReadyFrame=GFrameCounter;return false;}return GFrameCounter>=S.ReadyFrame+4&&FPlatformTime::Seconds()-S.ReadyAt>=.3;
}
static void Capture(UOKCharacterController* G,FOKCharacterMCPState& S)
{
 Refresh(G,S);if(S.Action==TEXT("views")&&S.CaptureRevision!=S.Revision){Finish(S,nullptr,TEXT("Character edited during three-view capture; retry"));return;}
 auto* Client=G->GetWorld()->GetGameViewport();auto* V=Client?Client->Viewport:nullptr;if(!V){Finish(S,nullptr,TEXT("Rendered viewport unavailable"));return;}const FIntPoint Size=V->GetSizeXY();FIntRect Rect(0,0,Size.X,Size.Y);FString Target=TEXT("preview");S.Request->TryGetStringField(TEXT("target"),Target);
 if(Target==TEXT("preview")){if(!G->PreviewSpace){Finish(S,nullptr,TEXT("Preview area unavailable"));return;}const auto Geo=G->PreviewSpace->GetCachedGeometry();FVector2D A,B,Unused;USlateBlueprintLibrary::AbsoluteToViewport(G,Geo.LocalToAbsolute(FVector2D::ZeroVector),A,Unused);USlateBlueprintLibrary::AbsoluteToViewport(G,Geo.LocalToAbsolute(Geo.GetLocalSize()),B,Unused);Rect=FIntRect(FMath::CeilToInt(A.X),FMath::CeilToInt(A.Y),FMath::FloorToInt(B.X),FMath::FloorToInt(B.Y));}
 else if(Target==TEXT("rect")){const FMCPObject* R=nullptr;if(!S.Request->TryGetObjectField(TEXT("rect"),R)){Finish(S,nullptr,TEXT("rect is required"));return;}double X,Y,W,H;if(!(*R)->TryGetNumberField(TEXT("x"),X)||!(*R)->TryGetNumberField(TEXT("y"),Y)||!(*R)->TryGetNumberField(TEXT("width"),W)||!(*R)->TryGetNumberField(TEXT("height"),H)||!FMath::IsFinite(X)||!FMath::IsFinite(Y)||!FMath::IsFinite(W)||!FMath::IsFinite(H)||X<0||Y<0||W<1||H<1||X+W>Size.X||Y+H>Size.Y){Finish(S,nullptr,TEXT("rect must be within viewport pixels"));return;}Rect=FIntRect(int32(X),int32(Y),int32(X+W),int32(Y+H));}
 else if(Target!=TEXT("viewport")){Finish(S,nullptr,TEXT("Unknown screenshot target"));return;}
 Rect.Clip(FIntRect(0,0,Size.X,Size.Y));if(Rect.Width()<1||Rect.Height()<1||int64(Rect.Width())*Rect.Height()>16777216){Finish(S,nullptr,TEXT("Invalid or oversized capture area"));return;}
 if(S.Phase!=5){
  if(FScreenshotRequest::IsScreenshotRequested()||UGameViewportClient::OnScreenshotCaptured().IsBound()){Finish(S,nullptr,TEXT("Another screenshot consumer is active; retry"));return;}
  S.Crop=Rect;S.ShotPixels.Reset();const FString Id=S.RequestId;
  S.ShotHandle=UGameViewportClient::OnScreenshotCaptured().AddWeakLambda(G,[G,Id](int32 W,int32 H,const TArray<FColor>& Data){if(G->CharacterMCP&&G->CharacterMCP->RequestId==Id){auto& Pending=*G->CharacterMCP;Pending.ShotSize=FIntPoint(W,H);Pending.ShotPixels=Data;}});
  bool ShowUI=false;S.Request->TryGetBoolField(TEXT("ui"),ShowUI);FScreenshotRequest::RequestScreenshot(S.Root/TEXT("images")/(S.RequestId+TEXT(".capture.png")),ShowUI,false,false);S.Phase=5;return;
 }
 if(S.ShotPixels.IsEmpty())return;
 UGameViewportClient::OnScreenshotCaptured().Remove(S.ShotHandle);S.ShotHandle.Reset();Rect=S.Crop;
 if(S.ShotSize.X<Rect.Max.X||S.ShotSize.Y<Rect.Max.Y||S.ShotPixels.Num()!=S.ShotSize.X*S.ShotSize.Y){Finish(S,nullptr,TEXT("Viewport resized during capture"));return;}
 if(!S.ShotPixels.ContainsByPredicate([First=S.ShotPixels[0]](FColor P){return P!=First;})){Finish(S,nullptr,TEXT("Empty render capture; viewport not ready"));return;}
 TArray<FColor> Pixels;Pixels.Reserve(Rect.Width()*Rect.Height());for(int32 Y=Rect.Min.Y;Y<Rect.Max.Y;++Y)Pixels.Append(S.ShotPixels.GetData()+Y*S.ShotSize.X+Rect.Min.X,Rect.Width());S.ShotPixels.Reset();
 TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Rect.Width(),Rect.Height(),Pixels,PNG);const FString File=S.RequestId+FString::Printf(TEXT("_%d.png"),S.View),Path=S.Root/TEXT("images")/File;if(!FFileHelper::SaveArrayToFile(PNG,*(Path+TEXT(".tmp")))||!IFileManager::Get().Move(*Path,*(Path+TEXT(".tmp")),true)){Finish(S,nullptr,TEXT("Cannot save screenshot"));return;}
 auto R=State(G,S);R->SetStringField(TEXT("image"),File);R->SetNumberField(TEXT("width"),Rect.Width());R->SetNumberField(TEXT("height"),Rect.Height());R->SetNumberField(TEXT("x"),Rect.Min.X);R->SetNumberField(TEXT("y"),Rect.Min.Y);
 if(S.Action!=TEXT("views")){Finish(S,R);return;}R->SetStringField(TEXT("view"),S.View==0?TEXT("front"):S.View==1?TEXT("side"):TEXT("back"));S.Images.Add(MakeShared<FJsonValueObject>(R));++S.View;if(S.View==3){auto Result=State(G,S);Result->SetArrayField(TEXT("images"),S.Images);Finish(S,Result);return;}FString Side;S.Request->TryGetStringField(TEXT("side"),Side);G->PreviewActor->OrbitYaw=S.View==1?(Side==TEXT("left")?90:-90):180;G->PreviewActor->ApplyOrbitCamera();S.Phase=2;S.ReadyAt=0;
}
static void Camera(UOKCharacterController* G,FOKCharacterMCPState& S)
{
 auto* A=G->PreviewActor.Get();double Y=A->OrbitYaw,P=A->OrbitPitch,D=A->OrbitDistance,W=A->StudioLens?A->StudioLens->OrthoWidth:200;FVector Focus=A->OrbitFocus;S.Request->TryGetNumberField(TEXT("yaw"),Y);S.Request->TryGetNumberField(TEXT("pitch"),P);S.Request->TryGetNumberField(TEXT("distance"),D);S.Request->TryGetNumberField(TEXT("ortho_width"),W);
 const FMCPRows* F=nullptr;if(S.Request->TryGetArrayField(TEXT("focus"),F)){if(F->Num()!=3){Finish(S,nullptr,TEXT("focus requires three numbers"));return;}for(int32 I=0;I<3;++I){double N;if(!(*F)[I]->TryGetNumber(N)||!FMath::IsFinite(N)||FMath::Abs(N)>100000){Finish(S,nullptr,TEXT("Invalid focus"));return;}Focus[I]=N;}}
 FString Projection;S.Request->TryGetStringField(TEXT("projection"),Projection);if(!FMath::IsFinite(Y)||!FMath::IsFinite(P)||!FMath::IsFinite(D)||!FMath::IsFinite(W)||Y< -360||Y>360||P< -80||P>80||D<10||D>5000||W<1||W>10000||(!Projection.IsEmpty()&&Projection!=TEXT("orthographic")&&Projection!=TEXT("perspective"))){Finish(S,nullptr,TEXT("Camera values outside range"));return;}
 bool T=false;S.Request->TryGetBoolField(TEXT("tpose"),T);if(T)G->OnPoseTpose();A->OrbitYaw=Y;A->OrbitPitch=P;A->OrbitDistance=D;A->OrbitFocus=Focus;if(A->StudioLens){if(!Projection.IsEmpty())A->StudioLens->SetProjectionMode(Projection==TEXT("orthographic")?ECameraProjectionMode::Orthographic:ECameraProjectionMode::Perspective);A->StudioLens->SetOrthoWidth(W);}
 for(const auto Pair:{TPair<const TCHAR*,const TCHAR*>(TEXT("hide_hair"),TEXT("hair")),{TEXT("hide_clothes"),TEXT("clothes")},{TEXT("hide_accessories"),TEXT("acc")}}){bool Hide;if(S.Request->TryGetBoolField(Pair.Key,Hide))for(auto& Part:A->Parts)if(Part.Role.StartsWith(Pair.Value)){
  TArray<USceneComponent*> Components;if(Part.Comp)Components.Add(Part.Comp);const FName Tag=*FString::Printf(TEXT("KKSessionPart_%d"),Part.SessionIndex);for(auto Follow:A->SkelFollowers)if(Follow&&Follow->ComponentTags.Contains(Tag))Components.Add(Follow);
  for(auto* C:Components){if(Hide){if(!S.HiddenVisibility.Contains(C))S.HiddenVisibility.Add(C,C->IsVisible());C->SetVisibility(false,true);}else if(auto* Original=S.HiddenVisibility.Find(C)){C->SetVisibility(*Original,true);S.HiddenVisibility.Remove(C);}}
 }}
 A->ApplyOrbitCamera();++S.Revision;S.Phase=4;S.ReadyAt=0;
}
}

void UOKCharacterController::TickCharacterMCP()
{
 using namespace OKCharacterMCP;
 if(!FParse::Param(FCommandLine::Get(),TEXT("CharacterMCP"))||bInteractionOnly||!IsInViewport())return;
 if(!CharacterMCP){CharacterMCP=MakeShared<FOKCharacterMCPState>();auto& S=*CharacterMCP;if(!FParse::Value(FCommandLine::Get(),TEXT("CharacterMCPDir="),S.Root))S.Root=FPaths::ProjectSavedDir()/TEXT("CharacterMCP");S.Root=FPaths::ConvertRelativePathToFull(S.Root);for(const TCHAR* D:{TEXT("requests"),TEXT("responses"),TEXT("images"),TEXT("cards")})IFileManager::Get().MakeDirectory(*(S.Root/D),true);UE_LOG(LogTemp,Display,TEXT("Character MCP enabled: %s"),*S.Root);}
 auto& S=*CharacterMCP;
 if(FPlatformTime::Seconds()-S.HeartbeatAt>.5){const double HeartbeatBegan=FPlatformTime::Seconds();S.HeartbeatAt=HeartbeatBegan;auto Status=Entries.IsValidIndex(SelectedIndex)&&IsValid(PreviewActor)?State(this,S,false):MakeShared<FJsonObject>();if(!IsValid(PreviewActor))Status->SetBoolField(TEXT("busy"),true);Status->SetNumberField(TEXT("heartbeat"),double(FDateTime::UtcNow().ToUnixTimestamp()));Status->SetStringField(TEXT("operation"),S.Request?S.Action:FString());Status->SetNumberField(TEXT("phase"),S.Phase);const FString Path=S.Root/TEXT("status.json");if(FFileHelper::SaveStringToFile(Encode(Status),*(Path+TEXT(".tmp")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))IFileManager::Get().Move(*Path,*(Path+TEXT(".tmp")),true);const double HeartbeatMs=(FPlatformTime::Seconds()-HeartbeatBegan)*1000.;if(HeartbeatMs>8.)UE_LOG(LogTemp,Display,TEXT("OK mcp heartbeat %.1f ms"),HeartbeatMs);}
 if(S.Request){const FString Operation=S.RequestId+FString::FromInt(S.Phase);if(Operation!=S.LoggedOperation){S.LoggedOperation=Operation;UE_LOG(LogTemp,Display,TEXT("Character MCP operation=%s phase=%d busy=%d actor=%d"),*S.Action,S.Phase,Busy(this),IsValid(PreviewActor));}}
 if(!S.AutoLoadAttempted){S.AutoLoadAttempted=true;FString Card;if(FParse::Value(FCommandLine::Get(),TEXT("CharacterMCPCard="),Card)&&IFileManager::Get().FileExists(*Card)){
  bIndependentCards=!UKKCardWorkspace::PortableSourcePath(Card).IsEmpty();int32 Index=Entries.IndexOfByPredicate([&](const auto& E){return E.Path==Card;});if(Index==INDEX_NONE){FKKCardListEntry E;E.Path=Card;E.FileName=FPaths::GetCleanFilename(Card);Index=Entries.Add(E);}SelectIndex(Index);OnLoad3D();return;
 }}
 if(!S.Request){TArray<FString> Files;IFileManager::Get().FindFiles(Files,*(S.Root/TEXT("requests")/TEXT("*.json")),true,false);Files.Sort();for(const FString& File:Files){const FString Id=FPaths::GetBaseFilename(File);FGuid Guid;if(Id.Len()!=32||!FGuid::ParseExact(Id,EGuidFormats::Digits,Guid))continue;const FString Path=S.Root/TEXT("requests")/File,Claim=Path+TEXT(".claimed");if(!IFileManager::Get().Move(*Claim,*Path,false))continue;FString Text;FMCPObject R;const bool Valid=IFileManager::Get().FileSize(*Claim)<=1048576&&FFileHelper::LoadFileToString(Text,*Claim)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),R);IFileManager::Get().Delete(*Claim);S.RequestId=Id;if(!Valid){Finish(S,nullptr,TEXT("Invalid request JSON"));return;}
   S.Request=R;if(!R->TryGetStringField(TEXT("action"),S.Action))S.Action.Empty();if(!R->TryGetNumberField(TEXT("deadline"),S.Deadline))S.Deadline=0;S.Selection=SelectedIndex;S.Outfit=OutfitIndex;S.Source=Source(this);S.BeforePath=Entries.IsValidIndex(SelectedIndex)?Entries[SelectedIndex].Path:FString();Refresh(this,S);break;}}
 if(!S.Request)return;if(!FMath::IsFinite(S.Deadline)||FDateTime::UtcNow().ToUnixTimestamp()>S.Deadline){Finish(S,nullptr,TEXT("Request expired; re-query state before retrying"));return;}
 if(S.Phase>0&&Busy(this)){S.ReadyAt=0;return;}
 if(!Entries.IsValidIndex(SelectedIndex)||!bModelLayout||!IsValid(PreviewActor)||InteractionWidget||InteractionScene||bEnterInteractionAfterLoad){Finish(S,nullptr,TEXT("Open character edit mode and load a 3D character first"));return;}
 if(S.Phase==0){FString Expected;S.Request->TryGetStringField(TEXT("session"),Expected);if(S.Action!=TEXT("state")&&Expected!=S.Session){Finish(S,nullptr,TEXT("Character changed; obtain a new session"));return;}static const TSet<FString> Mutations={TEXT("set"),TEXT("reset"),TEXT("control_set"),TEXT("apply"),TEXT("appearance"),TEXT("add_accessory"),TEXT("restore"),TEXT("camera"),TEXT("save"),TEXT("action"),TEXT("load"),TEXT("product_sweep")};if(Mutations.Contains(S.Action)){double Revision;if(!S.Request->TryGetNumberField(TEXT("revision"),Revision)||Revision!=S.Revision){Finish(S,nullptr,TEXT("Stale revision; re-query state"));return;}}}
 else if(SelectedIndex!=S.Selection||OutfitIndex!=S.Outfit||Source(this)!=S.Source){Finish(S,nullptr,TEXT("Character changed during operation"));return;}
 if(S.Action==TEXT("state")){Finish(S,State(this,S));return;}if(S.Phase==0&&Busy(this)){Finish(S,nullptr,TEXT("Editor busy; wait and re-query state"));return;}if(S.Phase>0&&Busy(this)){S.ReadyAt=0;return;}
 if(S.Phase==4){if(!Settled(this,S))return;Finish(S,State(this,S));return;}
 if(S.Phase==5){Capture(this,S);return;}
 if(S.Phase==3){if(!Settled(this,S))return;FString Target,Item;S.Request->TryGetStringField(TEXT("target"),Target);S.Request->TryGetStringField(TEXT("item"),Item);if((S.Action==TEXT("apply")&&!PendingProperties.IsEmpty())||(S.Action==TEXT("appearance")&&AppearanceSelectedKeys.FindRef(Target)!=Item)||(S.Action==TEXT("add_accessory")&&Entries[SelectedIndex].Path==S.BeforePath)){Finish(S,nullptr,TEXT("Operation failed; inspect editor status"));return;}Finish(S,State(this,S));return;}
 if(S.Phase==2){if(!Settled(this,S))return;if(S.Action==TEXT("views")&&!S.CaptureActor.IsValid()){Refresh(this,S);S.CaptureRevision=S.Revision;S.CaptureActor=PreviewActor;S.WasTicking=PreviewActor->IsActorTickEnabled();S.SavedYaw=PreviewActor->OrbitYaw;S.SavedPitch=PreviewActor->OrbitPitch;PreviewActor->SetActorTickEnabled(false);PreviewActor->OrbitYaw=0;PreviewActor->OrbitPitch=0;PreviewActor->ApplyOrbitCamera();S.View=0;S.ReadyAt=0;return;}Capture(this,S);return;}
 // Video motion panel / partner (Docs/VIDEO_MOTION.md): the same paths as clicks.
 if(S.Action==TEXT("studio")){FString Op;S.Request->TryGetStringField(TEXT("op"),Op);
  if(Op==TEXT("enter")){OnEnterStudio();if(!StudioWidget){Finish(S,nullptr,TEXT("Cannot open studio"));return;}Finish(S,State(this,S));return;}
  if(!StudioWidget){Finish(S,nullptr,TEXT("Studio is not open"));return;}
  FString Error;auto Result=StudioWidget->Command(*S.Request,Error);if(!Result){Finish(S,nullptr,Error);return;}auto R=State(this,S);if(StudioWidget)R->SetObjectField(TEXT("studio"),Result);Finish(S,R);return;}
 if(S.Action==TEXT("video_import")){FString Path,Name;bool Static=false,Pair=false,MaleLeft=true;S.Request->TryGetStringField(TEXT("path"),Path);S.Request->TryGetStringField(TEXT("name"),Name);S.Request->TryGetBoolField(TEXT("static"),Static);S.Request->TryGetBoolField(TEXT("pair"),Pair);S.Request->TryGetBoolField(TEXT("male_left"),MaleLeft);
  if(!VideoMotionPanel||!VideoMotionPanel->StartImport(Path,Name,Static,Pair,MaleLeft)){Finish(S,nullptr,VideoMotionPanel?VideoMotionPanel->StatusText():TEXT("No video motion panel"));return;}Finish(S,State(this,S));return;}
 if(S.Action==TEXT("video_motion")){FString Item;S.Request->TryGetStringField(TEXT("item"),Item);if(!VideoMotionPanel||!VideoMotionPanel->SelectItem(Item)){Finish(S,nullptr,TEXT("Unknown video motion item"));return;}Finish(S,State(this,S));return;}
 if(S.Action==TEXT("partner")){FString Path;if(!S.Request->TryGetStringField(TEXT("path"),Path)||!IFileManager::Get().FileExists(*Path)){Finish(S,nullptr,TEXT("Provide an existing partner card path"));return;}LoadPartnerFromPath(Path);Finish(S,State(this,S));return;}
 if(S.Action==TEXT("controls")){Page(this,S,OKCharacterMCPRegistry::Controls(this),TEXT("controls"));return;}
 if(S.Action==TEXT("load")){
  FString Path;if(!PendingProperties.IsEmpty()||!S.Request->TryGetStringField(TEXT("path"),Path)||FPaths::IsRelative(Path)||!FPaths::GetExtension(Path).Equals(TEXT("png"),ESearchCase::IgnoreCase)||!IFileManager::Get().FileExists(*Path)){Finish(S,nullptr,TEXT("Apply/reset pending edits and provide an existing absolute PNG card path"));return;}
  bIndependentCards=!UKKCardWorkspace::PortableSourcePath(Path).IsEmpty();int32 Index=Entries.IndexOfByPredicate([&](const auto& E){return FPaths::IsSamePath(E.Path,Path);});if(Index==INDEX_NONE){FKKCardListEntry E;E.Path=Path;E.FileName=FPaths::GetCleanFilename(Path);Index=Entries.Add(E);}SelectIndex(Index);if(SelectedIndex!=Index){Finish(S,nullptr,TEXT("Card could not be read"));return;}OnLoad3D();S.Selection=SelectedIndex;S.Outfit=OutfitIndex;S.Source=Source(this);S.Phase=4;S.ReadyAt=0;return;
 }
 if(S.Action==TEXT("control_set")){FString Key,Error;if(!S.Request->TryGetStringField(TEXT("key"),Key)||!OKCharacterMCPRegistry::SetControl(this,Key,S.Request->TryGetField(TEXT("value")),Error)){Finish(S,nullptr,Error.IsEmpty()?TEXT("Missing control key"):Error);return;}S.Selection=SelectedIndex;S.Outfit=OutfitIndex;S.Source=Source(this);S.Phase=4;S.ReadyAt=0;return;}
 if(S.Action==TEXT("action")){FString Command;S.Request->TryGetStringField(TEXT("command"),Command);
  if(Command==TEXT("tpose"))OnPoseTpose();else if(Command==TEXT("walk"))OnPoseWalk();else if(Command==TEXT("pause"))OnPausePlayback();else if(Command==TEXT("replay"))OnReplayPlayback();else if(Command==TEXT("blink"))OnForceBlink();else if(Command==TEXT("reset_left_hand"))OnResetLeftHand();else if(Command==TEXT("reset_right_hand"))OnResetRightHand();else if(Command==TEXT("speak"))OnSpeak();else if(Command==TEXT("stop_speech"))OnStopSpeech();else if(Command==TEXT("load_speech"))OnLoadSpeech();else if(Command==TEXT("clothing")){double Kind;if(!S.Request->TryGetNumberField(TEXT("clothing_kind"),Kind)||Kind!=FMath::FloorToDouble(Kind)||!((Kind>=0&&Kind<=8)||(Kind>=100&&Kind<=102))){Finish(S,nullptr,TEXT("Invalid clothing_kind"));return;}OnClothesState(int32(Kind));}else{Finish(S,nullptr,TEXT("Unknown editor command"));return;}S.Phase=4;S.ReadyAt=0;return;
 }
 if(S.Action==TEXT("parameters")||S.Action==TEXT("get")||S.Action==TEXT("set")||S.Action==TEXT("reset")){
  if(!PropertyPanel){Finish(S,nullptr,TEXT("Property editor unavailable"));return;}if(EditorProperties.IsEmpty()||PropertySource!=Entries[SelectedIndex].Path||PropertyOutfit!=OutfitIndex){if(S.Phase==1){Finish(S,nullptr,TEXT("Could not read character properties"));return;}PropertyPanel->SetVisibility(ESlateVisibility::Collapsed);OnOpenProperties();S.Phase=1;return;}
  const auto All=OKCharacterMCPRegistry::Parameters(this);if(S.Action==TEXT("parameters")){Page(this,S,All);return;}const FMCPRows* Values=nullptr;FMCPRows Edits;
  if(S.Action==TEXT("get")||S.Action==TEXT("reset")){if(!S.Request->TryGetArrayField(TEXT("keys"),Values)||Values->IsEmpty()||Values->Num()>256){Finish(S,nullptr,TEXT("keys requires 1..256 entries"));return;}FMCPRows Found;for(auto K:*Values){if(K->Type!=EJson::String){Finish(S,nullptr,TEXT("Invalid key type"));return;}FMCPObject P;for(auto V:All)if(V->AsObject()->GetStringField(TEXT("key"))==K->AsString()){P=V->AsObject();break;}if(!P){Finish(S,nullptr,TEXT("Unknown parameter: ")+K->AsString());return;}Found.Add(MakeShared<FJsonValueObject>(P));if(S.Action==TEXT("reset")){auto E=MakeShared<FJsonObject>();E->SetStringField(TEXT("key"),K->AsString());auto D=P->TryGetField(TEXT("default"));if(!D){bool Apply=false;P->TryGetBoolField(TEXT("requires_apply"),Apply);if(Apply){for(auto Original:EditorProperties)if(Original->GetStringField(TEXT("key"))==K->AsString())D=Original->TryGetField(TEXT("value"));}if(!D){Finish(S,nullptr,TEXT("No reliable default: ")+K->AsString());return;}}E->SetField(TEXT("value"),D);Edits.Add(MakeShared<FJsonValueObject>(E));}}
   if(S.Action==TEXT("get")){auto R=State(this,S);R->SetArrayField(TEXT("parameters"),Found);Finish(S,R);return;}}
  else if(S.Request->TryGetArrayField(TEXT("edits"),Values))Edits=*Values;
  FString Error;if(!OKCharacterMCPRegistry::SetParameters(this,Edits,Error)){Finish(S,nullptr,Error);return;}S.Phase=4;S.ReadyAt=0;return;
 }
 if(S.Action==TEXT("apply")){if(PendingProperties.IsEmpty()){Finish(S,State(this,S));return;}OnApplyProperties();if(!bPropertyBusy){Finish(S,nullptr,TEXT("Cannot start apply"));return;}S.Phase=3;S.ReadyAt=0;return;}
 if(S.Action==TEXT("catalog")||S.Action==TEXT("appearance")||S.Action==TEXT("add_accessory")){
  if(AppearanceTargets.IsEmpty()){if(S.Phase==1){Finish(S,nullptr,TEXT("Appearance catalog unavailable; configure source game"));return;}OnOpenAppearance();S.Phase=1;return;}FString Target;S.Request->TryGetStringField(TEXT("target"),Target);FMCPObject T;for(auto V:AppearanceTargets)if(V->GetStringField(TEXT("key"))==Target){T=V;break;}
  if(S.Action==TEXT("catalog")){FMCPRows Items;for(auto V:AppearanceItems)if(!T||MCPAppearanceCategory(V)==T->GetIntegerField(TEXT("category")))Items.Add(MakeShared<FJsonValueObject>(V));Page(this,S,Items,TEXT("items"));return;}
  if(!PendingProperties.IsEmpty()){Finish(S,nullptr,TEXT("Apply or reset pending properties before selecting materials"));return;}if(S.Action==TEXT("appearance")){FString Item;if(!T||!S.Request->TryGetStringField(TEXT("item"),Item)){Finish(S,nullptr,TEXT("Unknown target or missing item"));return;}if(!Item.IsEmpty()&&!AppearanceItems.ContainsByPredicate([&](auto V){return V->GetStringField(TEXT("key"))==Item&&MCPAppearanceCategory(V)==T->GetIntegerField(TEXT("category"));})){Finish(S,nullptr,TEXT("Item does not belong to target category"));return;}AppearanceTargetKey=Target;ApplyAppearanceItem(Item);}else OnAddAccessorySlot();if(!bAppearanceBusy){Finish(S,nullptr,TEXT("Appearance operation did not start"));return;}S.Phase=3;S.ReadyAt=0;return;
 }
 if(S.Action==TEXT("snapshot")){if(S.Snapshots.Num()>=32){Finish(S,nullptr,TEXT("Snapshot limit reached (32)"));return;}FOKCharacterMCPSnapshot Snap;Snap.Card=Selected;Snap.Path=Entries[SelectedIndex].Path;Snap.Source=Source(this);Snap.Outfit=OutfitIndex;Snap.Independent=bIndependentCards;auto O=MakeShared<FJsonObject>();FMCPRows P;for(auto V:EditorProperties)P.Add(MakeShared<FJsonValueObject>(V));O->SetArrayField(TEXT("properties"),P);Snap.Properties=Encode(O);O=MakeShared<FJsonObject>();for(auto E:PendingProperties)O->SetField(E.Key,E.Value);Snap.Pending=Encode(O);const FString Id=FGuid::NewGuid().ToString(EGuidFormats::Digits);S.Snapshots.Add(Id,MoveTemp(Snap));auto R=State(this,S);R->SetStringField(TEXT("snapshot"),Id);Finish(S,R);return;}
 if(S.Action==TEXT("restore")){FString Id;S.Request->TryGetStringField(TEXT("snapshot"),Id);auto* Snap=S.Snapshots.Find(Id);if(!Snap||Snap->Source!=Source(this)||Snap->Outfit!=OutfitIndex){Finish(S,nullptr,TEXT("Snapshot belongs to another source character or outfit"));return;}Selected=Snap->Card;bIndependentCards=Snap->Independent;SetWorkingCardPath(Snap->Path);EditorProperties.Reset();FMCPObject O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Snap->Properties),O);if(O)for(auto P:O->GetArrayField(TEXT("properties")))EditorProperties.Add(P->AsObject());O.Reset();FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Snap->Pending),O);PendingProperties.Reset();if(O)for(auto E:O->Values)PendingProperties.Add(FString(*E.Key),E.Value);PropertySource=Snap->Path;PropertyOutfit=OutfitIndex;PreviewResourceSignature.Empty();OnLoad3D();S.Phase=3;S.ReadyAt=0;return;}
 if(S.Action==TEXT("camera")){Camera(this,S);return;}
 if(S.Action==TEXT("product_sweep")){FString Key;S.Request->TryGetStringField(TEXT("ramp"),Key);double Seconds=5;S.Request->TryGetNumberField(TEXT("seconds"),Seconds);if(!FMath::IsFinite(Seconds)||Seconds<1||Seconds>20){Finish(S,nullptr,TEXT("seconds must be 1..20"));return;}UTexture2D* Ramp=nullptr;if(!Key.IsEmpty()){FString RampError;Ramp=OKLoadRenderRamp(Key,RampError);if(!Ramp){Finish(S,nullptr,RampError);return;}}SuspendClothingWarmForCapture();FString Error;if(!PreviewActor->BeginProductCloseup(Key,Ramp,float(Seconds),Error)){Finish(S,nullptr,Error);return;}++S.Revision;Finish(S,State(this,S));return;}
 if(S.Action==TEXT("screenshot")||S.Action==TEXT("views")){S.Phase=2;S.ReadyAt=0;S.View=0;return;}
 if(S.Action==TEXT("measure")||S.Action==TEXT("geometry")){if(!Settled(this,S))return;FString Error;auto Geometry=OKCharacterMCPGeometry(PreviewActor,Error);if(!Geometry){Finish(S,nullptr,Error);return;}auto R=State(this,S);Geometry->SetStringField(TEXT("session"),S.Session);Geometry->SetNumberField(TEXT("revision"),S.Revision);for(auto E:Geometry->Values)if(E.Key!=TEXT("meshes"))R->SetField(FString(*E.Key),E.Value);if(S.Action==TEXT("geometry")){const FString Directory=S.Root/TEXT("geometry");IFileManager::Get().MakeDirectory(*Directory,true);const FString Path=Directory/(S.RequestId+TEXT(".json"));if(!FFileHelper::SaveStringToFile(Encode(Geometry.ToSharedRef()),*(Path+TEXT(".tmp")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)||!IFileManager::Get().Move(*Path,*(Path+TEXT(".tmp")),true)){Finish(S,nullptr,TEXT("Cannot save geometry"));return;}R->SetStringField(TEXT("geometry"),Path);}Finish(S,R);return;}
 if(S.Action==TEXT("save")){if(!PendingProperties.IsEmpty()){Finish(S,nullptr,TEXT("Apply staged properties before saving"));return;}FString Name;S.Request->TryGetStringField(TEXT("name"),Name);if(Name.IsEmpty()||Name.Len()>80){Finish(S,nullptr,TEXT("Invalid card name"));return;}for(TCHAR C:Name)if(!FChar::IsAlnum(C)&&C!=TEXT('_')&&C!=TEXT('-')&&C!=TEXT(' ')){Finish(S,nullptr,TEXT("Use letters, numbers, spaces, dash or underscore for card name"));return;}
  const FString Path=S.Root/TEXT("cards")/(Name+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png"));FString Error;const FString Pack=UKKCardWorkspace::PortableSourcePath(Entries[SelectedIndex].Path);if(Pack.EndsWith(TEXT(".okpreview"))&&IFileManager::Get().Copy(*FPaths::ChangeExtension(Path,TEXT("okpreview")),*Pack,false)!=COPY_OK){Finish(S,nullptr,TEXT("Failed copying portable resources"));return;}if(NicknameBox)Selected.Nickname=NicknameBox->GetText().ToString();if(!UKKCardWriter::SaveWithEdits(Selected,Path,Error)){Finish(S,nullptr,Error);return;}auto R=State(this,S);R->SetStringField(TEXT("saved_card"),Path);Finish(S,R);return;}
 Finish(S,nullptr,TEXT("Unknown action"));
}

void UOKCharacterController::CancelCharacterMCP()
{
 if(!CharacterMCP)return;auto& S=*CharacterMCP;if(S.Request)OKCharacterMCP::Finish(S,nullptr,TEXT("Character editor closed"));else OKCharacterMCP::RestoreCamera(S);CharacterMCP.Reset();
}
