struct FBindingUVSearch {
 struct FPoint {FVertexID Vertex;FVector3f Position;FVector2f UV;};
 TMap<FName,TMap<FIntPoint,TArray<FPoint>>> Groups;
 FBindingUVSearch(const FMeshDescription& Source,const TArray<FMatrix44f>& Reference){
  FSkeletalMeshConstAttributes A(Source);TMap<FName,TSet<FVertexInstanceID>> Seen;
  for(auto T:Source.Triangles().GetElementIDs())for(auto I:Source.GetTriangleVertexInstances(T)){
   const FName Slot=A.GetPolygonGroupMaterialSlotNames()[Source.GetTrianglePolygonGroup(T)];
   auto& Visited=Seen.FindOrAdd(Slot);if(Visited.Contains(I))continue;Visited.Add(I);
   const auto V=Source.GetVertexInstanceVertex(I);const auto UV=A.GetVertexInstanceUVs().Get(I,0);
   FPoint P{V,FVector3f(VertexBlend(A.GetVertexSkinWeights().Get(V),Reference).TransformPosition(A.GetVertexPositions()[V])),UV};
   Groups.FindOrAdd(Slot).FindOrAdd(FIntPoint(FMath::FloorToInt(UV.X*128),FMath::FloorToInt(UV.Y*128))).Add(P);
  }
 }
 bool Find(FName Slot,FVector3f Position,FVector2f UV,FVertexID& Vertex,float& UVError)const{
  const auto* Grid=Groups.Find(Slot);if(!Grid)return false;
  const FIntPoint Cell(FMath::FloorToInt(UV.X*128),FMath::FloorToInt(UV.Y*128));float Best=MAX_flt;bool Found=false;
  for(int32 X=-4;X<=4;++X)for(int32 Y=-4;Y<=4;++Y)if(const auto* Points=Grid->Find(Cell+FIntPoint(X,Y)))for(const auto& P:*Points){
   const float Distance=(P.Position-Position).SizeSquared(),Delta=(P.UV-UV).SizeSquared();if(Distance>100)continue;
   const float Score=Distance+Delta*40000.f;if(Score<Best){Best=Score;Vertex=P.Vertex;UVError=FMath::Sqrt(Delta);Found=true;}
  }return Found;
 }
};
