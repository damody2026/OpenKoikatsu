#include "OKBodyInteraction.h"
#include "KKPreviewActor.h"
#include "KKExactSkinning.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace {
FVector Vector(const TSharedPtr<FJsonObject>& O){return FVector(O->GetNumberField(TEXT("x")),O->GetNumberField(TEXT("y")),O->GetNumberField(TEXT("z")));}
// Native Unity units/directions to actor-local Unreal units/directions.
FVector ToUnreal(const FVector& V){return FVector(-V.X,V.Z,V.Y)*100.;}
int32 Area(const FString& Tag){
 static const TCHAR* Tags[]={TEXT("head"),TEXT("bodyup"),TEXT("bodydown"),TEXT("armL"),TEXT("armR"),TEXT("legL"),TEXT("legR")};
 for(int32 I=0;I<UE_ARRAY_COUNT(Tags);++I)if(Tag==FString(TEXT("H/Aibu/Hit/Reaction/"))+Tags[I])return I;
 return INDEX_NONE;
}
double SphereHit(const FVector& O,const FVector& D,const FVector& C,double R){
 const FVector Q=O-C;const double B=FVector::DotProduct(Q,D),C0=Q.SizeSquared()-R*R,H=B*B-C0;
 if(C0<=0||H<0)return TNumericLimits<double>::Max();
 const double T=-B-FMath::Sqrt(H);return T>=0?T:TNumericLimits<double>::Max();
}
double CapsuleHit(const FVector& O,const FVector& D,const FVector& A,const FVector& B,double R){
 const FVector AB=B-A,Q=O-A;const double L=AB.SizeSquared();
 if(L<1e-12)return SphereHit(O,D,A,R);
 // Unity Physics.Raycast ignores a collider when the origin is inside it.
 const double Along=FMath::Clamp(FVector::DotProduct(Q,AB)/L,0.,1.);
 if((Q-AB*Along).SizeSquared()<=R*R)return TNumericLimits<double>::Max();
 const double AD=FVector::DotProduct(AB,D),AQ=FVector::DotProduct(AB,Q),DQ=FVector::DotProduct(D,Q);
 const double AA=L-AD*AD,BB=L*DQ-AQ*AD,CC=L*Q.SizeSquared()-AQ*AQ-R*R*L,H=BB*BB-AA*CC;
 double Best=FMath::Min(SphereHit(O,D,A,R),SphereHit(O,D,B,R));
 if(AA>1e-12&&H>=0){const double T=(-BB-FMath::Sqrt(H))/AA,Y=AQ+T*AD;if(T>=0&&Y>=0&&Y<=L)Best=FMath::Min(Best,T);}
 return Best;
}
}
bool FOKBodyInteraction::Prepare(AKKPreviewActor* Actor,FString& Error){
 const double Started=FPlatformTime::Seconds();
 Cancel();Owner.Reset();Catalog.Reset();Colliders.Reset();ActiveReactions.Reset();PairKey.Reset();StateName.Reset();++Revision;
 if(!IsValid(Actor)||!Actor->Session.IsOpen()){Error=TEXT("角色尚未載入");return false;}
 const FString Root=FPaths::ProjectContentDir()/TEXT("IndependentResources/BodyInteractions");
 TArray<uint8> Definition;FString Json;TSharedPtr<FJsonObject> Next;
 if(!FFileHelper::LoadFileToArray(Definition,*(Root/TEXT("hit-reaction-v1.json")))||!FFileHelper::LoadFileToString(Json,*(Root/TEXT("catalog-v1.json")))||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Next)){
  Error=TEXT("缺少共享身體互動資料");return false;
 }
 TArray<FOKBodyTouchCollider> Shapes;
 for(const auto& Value:Next->GetArrayField(TEXT("colliders"))){
  const auto O=Value->AsObject();FOKBodyTouchCollider C;
  C.Bone=O->GetStringField(TEXT("bone"));C.Tag=O->GetStringField(TEXT("tag"));C.Center=Vector(O->GetObjectField(TEXT("center")));
  C.Radius=O->GetNumberField(TEXT("radius"));C.Height=O->GetNumberField(TEXT("height"));C.Axis=O->GetIntegerField(TEXT("direction"));C.Sphere=O->GetStringField(TEXT("type"))==TEXT("SphereCollider");
  if(C.Radius<=0||C.Axis<0||C.Axis>2||C.Center.ContainsNaN()){Error=TEXT("互動碰撞資料無效");return false;}
  float Matrix[16];if(Actor->Session.BoneWorldMatrix(C.Bone,Matrix))Shapes.Add(MoveTemp(C));
 }
 if(Shapes.IsEmpty()){Error=TEXT("角色沒有可用的原作互動碰撞骨架");return false;}
 if(!Actor->Session.PrepareBodyReaction(Definition,Error))return false;
 Owner=Actor;Catalog=MoveTemp(Next);Colliders=MoveTemp(Shapes);
 UE_LOG(LogTemp,Display,TEXT("OK body interaction prepared colliders=%d prepare_ms=%.3f"),Colliders.Num(),(FPlatformTime::Seconds()-Started)*1000.);return true;
}
void FOKBodyInteraction::SetContext(const FString& Pair,const FString& State){
 if(PairKey==Pair&&StateName==State)return;
 Cancel();PairKey=Pair;StateName=State;++Revision;
 ActiveReactions.SetNum(Colliders.Num());for(auto& R:ActiveReactions)R.Reset();
 if(!Catalog)return;
 FString Name;if(!Catalog->GetObjectField(TEXT("pairs"))->TryGetStringField(PairKey,Name)||Name.IsEmpty())return;
 const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;if(!Catalog->GetObjectField(TEXT("reactions"))->TryGetArrayField(Name,Rows))return;
 for(int32 I=0;I<Colliders.Num();++I){const int32 Kind=Area(Colliders[I].Tag);if(Kind==INDEX_NONE)continue;
  for(const auto& V:*Rows){const auto O=V->AsObject();if(O->GetStringField(TEXT("anim"))==StateName&&O->GetIntegerField(TEXT("areaID"))==Kind)ActiveReactions[I]=O;}
 }
}
const TSharedPtr<FJsonObject> FOKBodyInteraction::Reaction(int32 Collider)const{
 return IsReady()&&ActiveReactions.IsValidIndex(Collider)?ActiveReactions[Collider]:nullptr;
}
bool FOKBodyInteraction::IsAvailable()const{for(int32 I=0;I<Colliders.Num();++I)if(Reaction(I))return true;return false;}
int32 FOKBodyInteraction::Query(const FVector& Origin,const FVector& Direction,double& Distance)const{
 Distance=TNumericLimits<double>::Max();if(!IsReady()||Direction.IsNearlyZero())return INDEX_NONE;
 const FVector D=Direction.GetSafeNormal();int32 Hit=INDEX_NONE;
 for(int32 I=0;I<Colliders.Num();++I){
  const auto& C=Colliders[I];float Raw[16];if(!Owner->Session.BoneWorldMatrix(C.Bone,Raw))continue;
  const FMatrix M=OKSkinPaletteToUnreal(Raw)*Owner->GetActorTransform().ToMatrixWithScale();
  const FVector Center=M.TransformPosition(ToUnreal(C.Center));
  // PhysX capsules use axial scale for length, maximum perpendicular scale
  // for radius; transforming the ray into local space creates an ellipsoid.
  FVector UnityAxis=FVector::ZeroVector;UnityAxis[C.Axis]=1.;const FVector Axis=M.TransformVector(ToUnreal(UnityAxis)/100.);
  double Scale[3];for(int32 J=0;J<3;++J){FVector V=FVector::ZeroVector;V[J]=1.;Scale[J]=FVector(M.TransformVector(ToUnreal(V)/100.)).Size();}
  const double Radius=100.*C.Radius*(C.Sphere?FMath::Max3(Scale[0],Scale[1],Scale[2]):FMath::Max(Scale[(C.Axis+1)%3],Scale[(C.Axis+2)%3]));
  const double Half=C.Sphere?0.:FMath::Max(0.,50.*C.Height*Axis.Size()-Radius);
  const FVector Extent=Axis.GetSafeNormal()*Half;
  const double T=CapsuleHit(Origin,D,Center-Extent,Center+Extent,Radius);
  if(T<Distance){Distance=T;Hit=I;}
 }
 // A disallowed/ordinary surface blocks the deeper reaction collider.
 return Hit!=INDEX_NONE&&Reaction(Hit)?Hit:INDEX_NONE;
}
bool FOKBodyInteraction::Press(int32 Collider){
 const auto Row=Reaction(Collider);if(!Row||Owner->Session.IsBodyReactionPlaying())return false;
 const auto& Variants=Row->GetArrayField(TEXT("lstParam"));if(Variants.IsEmpty())return false;
 const auto V=Variants[FMath::RandRange(0,Variants.Num()-1)]->AsObject();TArray<FVector3f> Forces;
 for(const auto& Entry:V->GetArrayField(TEXT("lstMinMax"))){
  const auto O=Entry->AsObject();const FVector A=Vector(O->GetObjectField(TEXT("min"))),B=Vector(O->GetObjectField(TEXT("max")));
  const FVector F=FVector(FMath::FRandRange(A.X,B.X),FMath::FRandRange(A.Y,B.Y),FMath::FRandRange(A.Z,B.Z)).GetSafeNormal();Forces.Add(FVector3f(F));
 }
 uint8 Release=0;for(const auto& Id:Row->GetArrayField(TEXT("lstReleaseEffector"))){const int32 I=Id->AsNumber();if(I>=0&&I<4)Release|=1<<I;}
 const bool Applied=Owner->Session.BodyReactionHit(V->GetIntegerField(TEXT("id")),Forces,Row->GetNumberField(TEXT("weight")),Release);
 if(Applied)Owner->bBodyReactionPoseDirty=true;return Applied;
}
void FOKBodyInteraction::Cancel(){if(Owner.IsValid()){Owner->Session.ClearBodyReaction();Owner->bBodyReactionPoseDirty=true;}}
