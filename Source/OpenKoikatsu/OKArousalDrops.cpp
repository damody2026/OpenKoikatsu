#include "OKArousalDrops.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"

namespace{
 constexpr int32 MaxDrops=160,MaxMarks=160;
 constexpr float Gravity=980.f,Swell=.5f,MarkMerge=6.f,MarkFirst=1.2f,MarkMost=7.f;
 // The sphere the drops and marks are made of: looked up each time (a static would outlive a collected asset).
 UStaticMesh* Sphere(){return LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));}
}

UMaterialInstanceDynamic* FOKArousalDrops::Material(AActor* Owner,uint8 Colour,bool bMark){
 auto& M=Materials[Colour+(bMark?OKArousal::Colours:0)];if(M.IsValid())return M.Get();
 UMaterialInterface* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/IndependentResources/PreviewMaterials/M_PreviewTranslucent.M_PreviewTranslucent"));
 if(!Parent){UE_LOG(LogTemp,Warning,TEXT("Arousal drops: no translucent material"));return nullptr;}
 M=UMaterialInstanceDynamic::Create(Parent,Owner);M->SetVectorParameterValue(TEXT("PreviewTint"),bMark?OKArousal::MarkTint(Colour):OKArousal::Tint(Colour));return M.Get();
}

UStaticMeshComponent* FOKArousalDrops::NewGlint(AActor* Owner){
 UStaticMesh* Mesh=Sphere();if(!Owner||!Mesh)return nullptr;
 if(!GlintMaterial.IsValid()){UMaterialInterface* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/IndependentResources/PreviewMaterials/M_PreviewTranslucent.M_PreviewTranslucent"));if(!Parent)return nullptr;
  GlintMaterial=UMaterialInstanceDynamic::Create(Parent,Owner);GlintMaterial->SetVectorParameterValue(TEXT("PreviewTint"),FLinearColor(4.f,4.f,4.f,1.f));} // brighter than white: it blooms, the way light shines off water
 auto* C=NewObject<UStaticMeshComponent>(Owner);C->SetStaticMesh(Mesh);C->SetMaterial(0,GlintMaterial.Get());C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 C->SetCastShadow(false);C->SetMobility(EComponentMobility::Movable);C->SetTranslucentSortPriority(1);C->RegisterComponent();C->SetWorldScale3D(FVector(.0001f));return C;
}

// Each highlight where the light would shine back: a drop's toward the camera and up, a third of its size; a wet mark's
// a flat streak a third of the way toward the camera.
void FOKArousalDrops::PlaceGlints(const FVector& Eye){
 for(FDrop& D:Drops){auto* C=D.Mesh.Get();auto* G=D.Glint.Get();if(!C||!G)continue;
  const FVector At=C->GetComponentLocation();const FVector S=C->GetComponentScale();const float R=float(S.X)*50.f;
  const FVector Toward=((Eye-At).GetSafeNormal()+FVector(0,0,.6f)).GetSafeNormal();
  G->SetWorldLocation(At+Toward*R*.6f);G->SetWorldScale3D(FVector(S.X*.24f,S.Y*.24f,FMath::Min(S.Z,S.X)*.24f));}
 for(FMark& K:Marks){auto* C=K.Mesh.Get();auto* G=K.Glint.Get();if(!C||!G)continue;
  FVector Toward=Eye-K.At;Toward.Z=0.;Toward=Toward.GetSafeNormal();const FVector Side(-Toward.Y,Toward.X,0.);
  G->SetWorldLocation(K.At+Toward*K.Radius*.35f+FVector(0,0,.03f));G->SetWorldRotation(Side.Rotation());
  G->SetWorldScale3D(FVector(K.Radius/50.f*.45f,K.Radius/50.f*.12f,.0004f));}
}

bool FOKArousalDrops::Spawn(AActor* Owner,int32 Who,FName Bone,const FVector& At,float Ground,uint8 Colour){
 UStaticMesh* Mesh=Sphere();if(!Owner||!Mesh||Drops.Num()>=MaxDrops)return false;
 UMaterialInstanceDynamic* M=Material(Owner,Colour,false);if(!M)return false;
 auto* C=NewObject<UStaticMeshComponent>(Owner);C->SetStaticMesh(Mesh);C->SetMaterial(0,M);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 C->SetCastShadow(false);C->SetMobility(EComponentMobility::Movable);C->RegisterComponent();C->SetWorldScale3D(FVector(.001f));C->SetWorldLocation(At);
 FDrop& D=Drops.AddDefaulted_GetRef();D.Mesh=C;D.Glint=NewGlint(Owner);D.Bone=Bone;D.Who=Who;D.Colour=Colour;D.Ground=Ground;MarkOwner=Owner;return true;
}

bool FOKArousalDrops::Squirt(AActor* Owner,int32 Who,const FVector& At,const FVector& Velocity,float Ground,uint8 Colour){
 if(!Spawn(Owner,Who,NAME_None,At,Ground,Colour))return false;
 FDrop& D=Drops.Last();D.Phase=1;D.Velocity=Velocity;if(auto* C=D.Mesh.Get())C->SetWorldScale3D(FVector(.005f));return true;
}

// Where it landed stays wet: into the mark of hers already there (it spreads), else a new one.
void FOKArousalDrops::Land(AActor* Owner,const FDrop& D,const FVector& At){
 FMark* Near=nullptr;float Best=MarkMerge;
 for(FMark& K:Marks)if(K.Who==D.Who&&K.Mesh.IsValid()){const float Far=FVector::Dist2D(K.At,At)-K.Radius;if(Far<Best){Best=Far;Near=&K;}}
 if(!Near&&Marks.Num()>=MaxMarks)for(FMark& K:Marks)if(K.Mesh.IsValid()&&(!Near||FVector::DistSquared(K.At,At)<FVector::DistSquared(Near->At,At)))Near=&K;
 if(Near){Near->Radius=FMath::Min(MarkMost,FMath::Sqrt(Near->Radius*Near->Radius+MarkFirst*MarkFirst*.5f));
  if(D.Colour!=OKArousal::Clear&&Near->Colour==OKArousal::Clear){Near->Colour=D.Colour;if(auto* M=Material(Owner,D.Colour,true))Near->Mesh->SetMaterial(0,M);}
  Near->Mesh->SetWorldScale3D(FVector(Near->Radius/50.f,Near->Radius/50.f,.0008f));return;}
 UStaticMesh* Mesh=Sphere();UMaterialInstanceDynamic* M=Material(Owner,D.Colour,true);if(!Mesh||!M)return;
 auto* C=NewObject<UStaticMeshComponent>(Owner);C->SetStaticMesh(Mesh);C->SetMaterial(0,M);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 C->SetCastShadow(false);C->SetMobility(EComponentMobility::Movable);C->RegisterComponent();
 FMark& K=Marks.AddDefaulted_GetRef();K.Mesh=C;K.Glint=NewGlint(Owner);K.Who=D.Who;K.Colour=D.Colour;K.At=FVector(At.X,At.Y,D.Ground+.05f);K.Radius=MarkFirst;
 C->SetWorldLocation(K.At);C->SetWorldScale3D(FVector(K.Radius/50.f,K.Radius/50.f,.0008f));
}

void FOKArousalDrops::Tick(float Dt,FWhere Where){
 AActor* Owner=MarkOwner.Get();
 for(int32 I=Drops.Num()-1;I>=0;--I){FDrop& D=Drops[I];auto* C=D.Mesh.Get();D.Age+=Dt;
  bool Gone=!C||D.Age>=OKArousal::DropLife;
  if(!Gone&&D.Phase==0){FVector At;if(Where(D.Who,D.Bone,At))C->SetWorldLocation(At-FVector(0,0,.6f));
   C->SetWorldScale3D(FVector(.004f+.004f*FMath::Min(1.f,D.Age/Swell)));if(D.Age>=Swell){D.Phase=1;D.Velocity=FVector::ZeroVector;}}
  else if(!Gone&&D.Phase==1){D.Velocity.Z-=Gravity*Dt;FVector P=C->GetComponentLocation()+D.Velocity*Dt;C->SetWorldScale3D(FVector(.006f,.006f,.012f));
   if(P.Z<=D.Ground){P.Z=D.Ground+.2f;D.Phase=2;if(Owner)Land(Owner,D,P);}C->SetWorldLocation(P);}
  // A bead where it fell, drying away until its ten seconds are up.
  else if(!Gone&&D.Phase==2){const float K=FMath::Clamp(1.f-D.Age/OKArousal::DropLife,.15f,1.f);C->SetWorldScale3D(FVector(.012f*K,.012f*K,.003f*K));}
  if(Gone){if(C)C->DestroyComponent();if(auto* G=D.Glint.Get())G->DestroyComponent();Drops.RemoveAtSwap(I);}
 }
 UWorld* World=Owner?Owner->GetWorld():nullptr;APlayerController* PC=World?World->GetFirstPlayerController():nullptr;
 if(PC&&PC->PlayerCameraManager)PlaceGlints(PC->PlayerCameraManager->GetCameraLocation());
}

void FOKArousalDrops::ClearMarks(int32 Who){
 for(int32 I=Marks.Num()-1;I>=0;--I)if(Marks[I].Who==Who){if(auto* C=Marks[I].Mesh.Get())C->DestroyComponent();if(auto* G=Marks[I].Glint.Get())G->DestroyComponent();Marks.RemoveAtSwap(I);}
}

void FOKArousalDrops::Reset(){
 for(FDrop& D:Drops){if(auto* C=D.Mesh.Get())C->DestroyComponent();if(auto* G=D.Glint.Get())G->DestroyComponent();}
 for(FMark& K:Marks){if(auto* C=K.Mesh.Get())C->DestroyComponent();if(auto* G=K.Glint.Get())G->DestroyComponent();}
 Drops.Reset();Marks.Reset();
}

float FOKArousalDrops::GroundBelow(const AActor* Owner,const FVector& At,TArrayView<const AActor* const> Ignore,float Fallback){
 UWorld* World=Owner?Owner->GetWorld():nullptr;if(!World)return Fallback;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(OKArousalGround),true);for(const AActor* A:Ignore)if(A)Params.AddIgnoredActor(A);
 FHitResult Hit;return World->LineTraceSingleByChannel(Hit,At,At-FVector(0,0,300.f),ECC_Visibility,Params)?float(Hit.ImpactPoint.Z):Fallback;
}
