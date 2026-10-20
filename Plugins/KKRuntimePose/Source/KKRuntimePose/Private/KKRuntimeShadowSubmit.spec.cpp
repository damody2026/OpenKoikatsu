#include "KKRuntimeShadowSubmit.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

namespace {
FOKRuntimeShadowMaterialFacts Fact(EKKRuntimeMaterialRecipe Recipe){FOKRuntimeShadowMaterialFacts F;F.Recipe=Recipe;return F;}
FKKRuntimePoseBone Bone(const TCHAR* Name,int32 Parent){FKKRuntimePoseBone B;B.Name=Name;B.Parent=Parent;B.Reference=FTransform::Identity;return B;}
FKKRuntimeMeshVertex VertexAt(FVector3f Position,uint8 Influences=1){FKKRuntimeMeshVertex V;V.Position=V.InitialPosition=Position;V.Normal=FVector3f(0,0,1);V.Tangent=FVector3f(1,0,0);V.UV[0]=FVector2f(Position.X,Position.Y);V.Influences=Influences;return V;}
FSkinWeightInfo WeightTo(uint16 Local,uint16 Amount=65535){FSkinWeightInfo W{};W.InfluenceBones[0]=Local;W.InfluenceWeights[0]=Amount;return W;}
FKKRuntimeRenderSection SectionOf(int32 Material,uint32 BaseVertex,uint32 Vertices,uint32 BaseIndex,uint32 Triangles,std::initializer_list<uint16> Bones){
 FKKRuntimeRenderSection S;S.Material=Material;S.BaseVertex=BaseVertex;S.NumVertices=Vertices;S.BaseIndex=BaseIndex;S.Triangles=Triangles;for(uint16 Bone:Bones)S.BoneMap.Add(Bone);return S;
}
TArray<TTuple<FVector3f,FVector3f,FVector3f>> Triples(const FKKRuntimeRenderMeshData& Mesh,TConstArrayView<int32> Sections){
 TArray<TTuple<FVector3f,FVector3f,FVector3f>> Out;
 for(int32 Index:Sections){const auto& S=Mesh.Sections[Index];for(uint32 T=0;T<S.Triangles;++T){const uint32 I=S.BaseIndex+T*3;Out.Emplace(Mesh.Vertices[Mesh.Indices[I]].Position,Mesh.Vertices[Mesh.Indices[I+1]].Position,Mesh.Vertices[Mesh.Indices[I+2]].Position);}}
 Out.Sort([](const auto& A,const auto& B){const FVector3f AV[]={A.Get<0>(),A.Get<1>(),A.Get<2>()},BV[]={B.Get<0>(),B.Get<1>(),B.Get<2>()};for(int32 K=0;K<3;++K)for(int32 C=0;C<3;++C)if((&AV[K].X)[C]!=( &BV[K].X)[C])return (&AV[K].X)[C]<( &BV[K].X)[C];return false;});
 return Out;
}
int32 SkeletonBone(const FKKRuntimeRenderMeshData& Mesh,int32 Section,int32 Vertex,int32 Influence){
 const auto& S=Mesh.Sections[Section];const auto& W=Mesh.Weights[Vertex];if(!W.InfluenceWeights[Influence])return INDEX_NONE;if(!S.BoneMap.IsValidIndex(W.InfluenceBones[Influence]))return -2;return S.BoneMap[W.InfluenceBones[Influence]];
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKRuntimeShadowCoverageTest,"OpenKoikatsu.Runtime.ShadowSubmit.Coverage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKRuntimeShadowCoverageTest::RunTest(const FString&){
 for(int32 Id=1;Id<=61;++Id){
  const auto Recipe=EKKRuntimeMaterialRecipe(Id);
  const auto Got=OKRuntimeShadowCoverage(Fact(Recipe));
  const bool Translucent=OKRuntimeSharedMaterialTranslucent(Recipe);
  const bool Solid=Recipe==EKKRuntimeMaterialRecipe::IBLDepth||Recipe==EKKRuntimeMaterialRecipe::IBLOpaque||Recipe==EKKRuntimeMaterialRecipe::OilPainting||(Id>=35&&Id<=46);
  EOKRuntimeShadowCoverage Want=EOKRuntimeShadowCoverage::OriginalMaterial;const TCHAR* Reason=TEXT("shader coverage clip");
  if(Translucent){Want=EOKRuntimeShadowCoverage::NotCaster;Reason=TEXT("translucent material does not cast a masked shadow");}
  else if(Solid){Want=OKRuntimeSharedMaterialTwoSided(Recipe)?EOKRuntimeShadowCoverage::SolidTwoSided:EOKRuntimeShadowCoverage::SolidOneSided;Reason=TEXT("");}
  if(!TestEqual(FString::FromInt(Id),int32(Got.Coverage),int32(Want))||!TestEqual(FString::FromInt(Id),FString(Got.Reason),FString(Reason)))return false;
  if(Want==EOKRuntimeShadowCoverage::SolidOneSided&&!TestEqual(TEXT("one-sided proxy"),int32(OKRuntimeShadowProxyRecipe(Want)),int32(EKKRuntimeMaterialRecipe::IBLDepth)))return false;
  if(Want==EOKRuntimeShadowCoverage::SolidTwoSided&&!TestEqual(TEXT("two-sided proxy"),int32(OKRuntimeShadowProxyRecipe(Want)),int32(EKKRuntimeMaterialRecipe::IBLOpaque)))return false;
 }
 if(!TestEqual(TEXT("original has no proxy"),int32(OKRuntimeShadowProxyRecipe(EOKRuntimeShadowCoverage::OriginalMaterial)),int32(EKKRuntimeMaterialRecipe::None)))return false;
 const FString Opaque=OKRuntimeSharedMaterialProgram(EKKRuntimeMaterialRecipe::ForgeOpaque),Clothes=OKRuntimeSharedMaterialProgram(EKKRuntimeMaterialRecipe::KoikanoClothesOpaque),Hair=OKRuntimeSharedMaterialProgram(EKKRuntimeMaterialRecipe::ForgeHair);
 const FString Depth=OKRuntimeSharedMaterialProgram(EKKRuntimeMaterialRecipe::IBLDepth),Ibl=OKRuntimeSharedMaterialProgram(EKKRuntimeMaterialRecipe::IBLOpaque);
 if(!TestTrue(TEXT("clipping opaque programs stay clipped"),Opaque.Contains(TEXT("clip("))&&Clothes.Contains(TEXT("clip("))&&Hair.Contains(TEXT("clip("))))return false;
 if(!TestTrue(TEXT("solid proxy programs have no coverage clip"),!Depth.Contains(TEXT("clip("))&&!Ibl.Contains(TEXT("clip("))&&Depth.Contains(TEXT("return float4(1,1,1,1)"))))return false;
 if(!TestEqual(TEXT("depth parent"),OKRuntimeSharedMaterialParentName(EKKRuntimeMaterialRecipe::IBLDepth),FString(TEXT("M_IBLDepth")))||!TestEqual(TEXT("ibl parent"),OKRuntimeSharedMaterialParentName(EKKRuntimeMaterialRecipe::IBLOpaque),FString(TEXT("M_IBLOpaque"))))return false;
 auto Blocked=[&](const TCHAR* Name,FOKRuntimeShadowMaterialFacts Facts,const TCHAR* Reason){const auto Got=OKRuntimeShadowCoverage(Facts);return TestEqual(Name,int32(Got.Coverage),int32(EOKRuntimeShadowCoverage::OriginalMaterial))&&TestEqual(Name,FString(Got.Reason),FString(Reason));};
 FOKRuntimeShadowMaterialFacts Offset=Fact(EKKRuntimeMaterialRecipe::IBLOpaque);Offset.WorldPositionOffset=true;
 FOKRuntimeShadowMaterialFacts DepthOffset=Fact(EKKRuntimeMaterialRecipe::OilPainting);DepthOffset.PixelDepthOffset=true;
 FOKRuntimeShadowMaterialFacts Displaced=Fact(EKKRuntimeMaterialRecipe::IBLOpaque);Displaced.Displacement=true;
 FOKRuntimeShadowMaterialFacts Sides=Fact(EKKRuntimeMaterialRecipe::IBLOpaque);Sides.TwoSidedObserved=true;Sides.TwoSided=false;
 FOKRuntimeShadowMaterialFacts Blend=Fact(EKKRuntimeMaterialRecipe::IBLOpaque);Blend.BlendObserved=true;Blend.Blend=BLEND_Translucent;
 FOKRuntimeShadowMaterialFacts MaskedCast=Fact(EKKRuntimeMaterialRecipe::ForgeAlphaTranslucent);MaskedCast.CastShadowAsMasked=true;
 FOKRuntimeShadowMaterialFacts Unknown;Unknown.BlendObserved=true;Unknown.Blend=BLEND_Masked;
 if(!Blocked(TEXT("wpo"),Offset,TEXT("world position offset"))||!Blocked(TEXT("pixel depth"),DepthOffset,TEXT("pixel depth offset"))||!Blocked(TEXT("displacement"),Displaced,TEXT("displacement")))return false;
 if(!Blocked(TEXT("two sided"),Sides,TEXT("two-sidedness differs from the recipe"))||!Blocked(TEXT("masked cast"),MaskedCast,TEXT("translucent material casts a masked shadow")))return false;
 if(!Blocked(TEXT("unknown"),Unknown,TEXT("unclassified material")))return false;
 const auto Dropped=OKRuntimeShadowCoverage(Blend);
 return TestEqual(TEXT("observed translucent is not a caster"),int32(Dropped.Coverage),int32(EOKRuntimeShadowCoverage::NotCaster));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKRuntimeShadowMeshTest,"OpenKoikatsu.Runtime.ShadowSubmit.Mesh",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKRuntimeShadowMeshTest::RunTest(const FString&){
 FKKRuntimeRenderMeshData Mesh;Mesh.Bones={Bone(TEXT("Root"),INDEX_NONE),Bone(TEXT("A"),0),Bone(TEXT("B"),0),Bone(TEXT("C"),0)};
 Mesh.Vertices={VertexAt(FVector3f(0,0,0)),VertexAt(FVector3f(1,0,0)),VertexAt(FVector3f(0,1,0)),VertexAt(FVector3f(2,0,0)),VertexAt(FVector3f(3,0,0)),VertexAt(FVector3f(2,1,0)),VertexAt(FVector3f(4,0,0)),VertexAt(FVector3f(5,0,0)),VertexAt(FVector3f(4,1,0)),VertexAt(FVector3f(6,0,0)),VertexAt(FVector3f(7,0,0)),VertexAt(FVector3f(6,1,0))};
 Mesh.Vertices[0].MouthClosed=FVector3f(0,0,1);Mesh.Vertices[0].MouthOpen=FVector3f(0,0,3);Mesh.Vertices[3].MouthClosed=FVector3f(1,0,0);Mesh.Vertices[3].MouthOpen=FVector3f(4,0,0);
 Mesh.Vertices[9].BlinkClosed=FVector3f(0,2,0);
 Mesh.Weights={WeightTo(0),WeightTo(0),WeightTo(0),WeightTo(0),WeightTo(0),WeightTo(0),WeightTo(0),WeightTo(1),WeightTo(1),WeightTo(0),WeightTo(0),WeightTo(0)};
 Mesh.Indices={0,1,2, 3,4,5, 0,1,2, 6,7,8, 9,10,11};
 Mesh.Sections={
  SectionOf(1,0,3,0,1,{1}),
  SectionOf(0,3,3,3,1,{2}),
  SectionOf(1,0,3,6,1,{1}),
  SectionOf(2,6,3,9,1,{1,2}),
  SectionOf(3,9,3,12,1,{3})
 };
 TArray<FOKRuntimeShadowMaterialFacts> Facts;Facts.SetNum(4);
 Facts[0]=Fact(EKKRuntimeMaterialRecipe::IBLOpaque);
 Facts[1]=Fact(EKKRuntimeMaterialRecipe::ForgeHair);
 Facts[2]=Fact(EKKRuntimeMaterialRecipe::KoikanoClothesOpaque);
 Facts[3]=Fact(EKKRuntimeMaterialRecipe::ForgeAlphaTranslucent);
 const auto Built=OKBuildRuntimeShadowMesh(Mesh,Facts,2);
 if(!TestTrue(TEXT("build"),Built.Error.IsEmpty())||!TestEqual(TEXT("casters"),Built.SourceCasterSections,4)||!TestEqual(TEXT("excluded"),Built.ExcludedSections,1))return false;
 if(!TestEqual(TEXT("solid sources"),Built.SolidSourceSections,1)||!TestEqual(TEXT("original sources"),Built.OriginalSourceSections,3))return false;
 if(!TestTrue(TEXT("reduced"),Built.Mesh.Sections.Num()<Built.SourceCasterSections)||!TestEqual(TEXT("proxy sections"),Built.Mesh.Sections.Num(),3))return false;
 TArray<int32> SourceCasters={0,1,2,3},BuiltSections;for(int32 I=0;I<Built.Mesh.Sections.Num();++I)BuiltSections.Add(I);
 if(!TestTrue(TEXT("winding and triangles"),Triples(Mesh,SourceCasters)==Triples(Built.Mesh,BuiltSections)))return false;
 int32 Solid=-1,Hair=-1,Clothes=-1;
 for(int32 I=0;I<Built.SectionKinds.Num();++I){
  if(Built.SectionKinds[I]==EOKRuntimeShadowSectionKind::SolidTwoSided)Solid=I;
  else if(Built.Mesh.Sections[I].Material==1)Hair=I;
  else if(Built.Mesh.Sections[I].Material==2)Clothes=I;
 }
 if(!TestTrue(TEXT("groups present"),Solid!=INDEX_NONE&&Hair!=INDEX_NONE&&Clothes!=INDEX_NONE&&Solid!=Hair&&Hair!=Clothes))return false;
 if(!TestEqual(TEXT("hair triangles merged"),int32(Built.Mesh.Sections[Hair].Triangles),2)||!TestEqual(TEXT("clothes material"),Built.Mesh.Sections[Clothes].Material,2))return false;
 if(!TestTrue(TEXT("uv kept"),Built.Mesh.Vertices[Built.Mesh.Sections[Clothes].BaseVertex].UV[0]==Mesh.Vertices[6].UV[0]))return false;
 const int32 SolidVertex=Built.Mesh.Sections[Solid].BaseVertex;
 if(!TestEqual(TEXT("solid bone"),SkeletonBone(Built.Mesh,Solid,SolidVertex,0),2))return false;
 const int32 SplitBone=SkeletonBone(Built.Mesh,Clothes,Built.Mesh.Sections[Clothes].BaseVertex+1,0);
 if(!TestTrue(TEXT("clothes palette"),Built.Mesh.Sections[Clothes].BoneMap.Num()<=2)||!TestEqual(TEXT("second influence"),SplitBone,2))return false;
 if(!TestTrue(TEXT("face copied"),Built.Face.IsValid()&&Built.Face->AffectedVertices==2))return false;
 FString Error;TArray<FVector3f> Positions;
 if(!TestTrue(TEXT("shadow face"),OKEvaluateRuntimeFacePositions(*Built.Face,1.f,1.f,0.f,nullptr,Positions,Error)))return false;
 const FVector3f Want0=Mesh.Vertices[0].Position+Mesh.Vertices[0].MouthOpen,Want3=Mesh.Vertices[3].Position+Mesh.Vertices[3].MouthOpen;
 bool Found0=false,Found3=false;for(const FVector3f& P:Positions){Found0|=P.Equals(Want0);Found3|=P.Equals(Want3);}
 if(!TestTrue(TEXT("mouth positions"),Found0&&Found3))return false;
 int32 BlinkVertex=INDEX_NONE;for(int32 V=0;V<Built.Mesh.Vertices.Num();++V)if(!Built.Mesh.Vertices[V].BlinkClosed.IsNearlyZero())BlinkVertex=V;
 if(!TestTrue(TEXT("excluded blink is absent"),BlinkVertex==INDEX_NONE))return false;
 for(const auto& Range:Built.Face->Ranges)if(!TestTrue(TEXT("face range inside shadow mesh"),Range.FirstVertex+Range.Count<=uint32(Built.Mesh.Vertices.Num())))return false;

 FKKRuntimeRenderMeshData Wide;Wide.Bones={Bone(TEXT("Root"),INDEX_NONE),Bone(TEXT("A"),0),Bone(TEXT("B"),0),Bone(TEXT("C"),0)};
 for(int32 T=0;T<3;++T){Wide.Vertices.Add(VertexAt(FVector3f(float(T),0,0)));Wide.Vertices.Add(VertexAt(FVector3f(float(T)+.5f,0,0)));Wide.Vertices.Add(VertexAt(FVector3f(float(T),1,0)));Wide.Weights.Add(WeightTo(0));Wide.Weights.Add(WeightTo(0));Wide.Weights.Add(WeightTo(0));const uint32 B=uint32(T*3);Wide.Indices.Append({B,B+1,B+2});Wide.Sections.Add(SectionOf(0,B,3,B,1,{uint16(T)}));}
 TArray<FOKRuntimeShadowMaterialFacts> WideFacts;WideFacts.Add(Fact(EKKRuntimeMaterialRecipe::IBLOpaque));
 const auto Split=OKBuildRuntimeShadowMesh(Wide,WideFacts,2);
 if(!TestTrue(TEXT("palette split"),Split.Error.IsEmpty()&&Split.Mesh.Sections.Num()==2))return false;
 if(!TestEqual(TEXT("first palette"),Split.Mesh.Sections[0].BoneMap.Num(),2)||!TestEqual(TEXT("second palette"),Split.Mesh.Sections[1].BoneMap.Num(),1))return false;
 if(!TestEqual(TEXT("split bone identity"),SkeletonBone(Split.Mesh,1,Split.Mesh.Sections[1].BaseVertex,0),2))return false;
 FKKRuntimeRenderMeshData TooWide;TooWide.Bones=Mesh.Bones;TooWide.Vertices={VertexAt(FVector3f(0,0,0)),VertexAt(FVector3f(1,0,0)),VertexAt(FVector3f(0,1,0))};TooWide.Weights={WeightTo(0),WeightTo(1),WeightTo(2)};TooWide.Indices={0,1,2};TooWide.Sections={SectionOf(0,0,3,0,1,{0,1,2})};
 const auto Rejected=OKBuildRuntimeShadowMesh(TooWide,WideFacts,2);
 return TestFalse(TEXT("no partial palette drop"),Rejected.Error.IsEmpty()||Rejected.Mesh.Sections.Num()>0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOKRuntimeShadowFaceChannelsTest,"OpenKoikatsu.Runtime.ShadowSubmit.FaceChannels",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FOKRuntimeShadowFaceChannelsTest::RunTest(const FString&){
 auto Stream=MakeShared<FKKRuntimeMouthStream,ESPMode::ThreadSafe>();Stream->PatternCount[0]=3;Stream->PatternCount[1]=0;Stream->PatternCount[2]=2;
 FKKRuntimeFaceState Face;Face.MouthPattern=2;Face.EyePattern=1;Face.BrowPattern=1;Face.EyeOpening=.5f;Face.EyeBlend=true;
 FString Error;TestFalse(TEXT("Full display state is invalid on absent shadow eyes"),OKValidateRuntimeFaceState(Stream,Face,Error));
 const auto Projected=OKRuntimeShadowFaceChannels(*Stream,Face);
 TestTrue(TEXT("Absent eyes are suppressed only on shadow mesh"),Projected.EyePattern==-1&&Projected.EyeOpening==-1&&!Projected.EyeBlend);
 TestTrue(TEXT("Present mouth and brow remain authored"),Projected.MouthPattern==2&&Projected.BrowPattern==1&&Face.EyePattern==1);
 TestTrue(TEXT("Projected endpoint validates"),OKValidateRuntimeFaceState(Stream,Projected,Error));
 Face.MouthPattern=9;const auto Invalid=OKRuntimeShadowFaceChannels(*Stream,Face);
 TestFalse(TEXT("Invalid present channel is not clamped"),OKValidateRuntimeFaceState(Stream,Invalid,Error));
 return !HasAnyErrors();
}
#endif // WITH_DEV_AUTOMATION_TESTS
