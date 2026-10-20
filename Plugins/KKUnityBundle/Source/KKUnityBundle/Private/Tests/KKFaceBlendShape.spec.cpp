#include "KKFaceBlendShape.h"
#include "KKProceduralDeform.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "KKRustExtract.h"
#include "ok_extract.h"
#include <limits>
#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKAnimationStreamsTest,"OpenKoikatsu.AnimationCatalog.MutableStreams",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKAnimationStreamsTest::RunTest(const FString&){
 auto* Comp=NewObject<UProceduralMeshComponent>();FProcMeshSection Source;
 for(int32 I=0;I<3;++I){FProcMeshVertex V;V.Position=FVector(I,0,0);V.Normal=FVector::UpVector;V.UV0=FVector2D(.1,I);V.UV1=FVector2D(.2,I);V.UV2=FVector2D(.3,I);V.UV3=FVector2D(.4,I);V.Color=FColor(1,2,3,4);V.Tangent=FProcMeshTangent(FVector::RightVector,true);Source.ProcVertexBuffer.Add(V);}Source.ProcIndexBuffer={0,1,2};Comp->SetProcMeshSection(0,Source);
 TArray<FVector> P,N;TArray<FProcMeshTangent> T;for(const auto& V:Source.ProcVertexBuffer){P.Add(V.Position+FVector(0,2,3));N.Add(FVector::ForwardVector);T.Add(FProcMeshTangent(FVector::UpVector,false));}
 Comp->UpdateMeshSection(0,P,N,TArray<FVector2D>(),TArray<FColor>(),T);
 const auto* Section=Comp->GetProcMeshSection(0);for(int32 I=0;I<3;++I){const auto& V=Section->ProcVertexBuffer[I];const auto& Rest=Source.ProcVertexBuffer[I];TestEqual(TEXT("Pose updated"),V.Position,P[I]);TestEqual(TEXT("Normal updated"),V.Normal,N[I]);TestEqual(TEXT("Tangent updated"),V.Tangent.TangentX,T[I].TangentX);TestEqual(TEXT("UV0 retained"),V.UV0,Rest.UV0);TestEqual(TEXT("UV1 retained"),V.UV1,Rest.UV1);TestEqual(TEXT("UV2 retained"),V.UV2,Rest.UV2);TestEqual(TEXT("UV3 retained"),V.UV3,Rest.UV3);TestEqual(TEXT("Colour retained"),V.Color,Rest.Color);}
 TestTrue(TEXT("Topology retained"),Section->ProcIndexBuffer==Source.ProcIndexBuffer);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKSourceGeometryTest,"OpenKoikatsu.AnimationCatalog.SourceGeometrySnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKSourceGeometryTest::RunTest(const FString&){
 TArray<float> Snapshot;float P[3]={1,2,3},N[3]={0,0,1},T[4]={1,0,0,1};
 TestTrue(TEXT("First pose dirty"),OKSyncSourceGeometry(Snapshot,1,P,N,T));
 TestFalse(TEXT("Identical pose retained"),OKSyncSourceGeometry(Snapshot,1,P,N,T));
 P[0]=std::nextafter(P[0],2.f);TestTrue(TEXT("One ULP position change retained"),OKSyncSourceGeometry(Snapshot,1,P,N,T));
 N[0]=.01f;TestTrue(TEXT("Normal-only change retained"),OKSyncSourceGeometry(Snapshot,1,P,N,T));
 T[3]=-1;TestTrue(TEXT("Tangent handedness change retained"),OKSyncSourceGeometry(Snapshot,1,P,N,T));
 TestTrue(TEXT("Absent stream invalidates snapshot"),OKSyncSourceGeometry(Snapshot,1,P,nullptr,T));
 TestFalse(TEXT("Identical absent stream retained"),OKSyncSourceGeometry(Snapshot,1,P,nullptr,T));
 Snapshot.Reset();TestTrue(TEXT("Section rebuild invalidates snapshot"),OKSyncSourceGeometry(Snapshot,1,P,nullptr,T));
 TestEqual(TEXT("Bounded snapshot storage"),Snapshot.Num(),7);
 const float Original=P[0];P[0]+=3;
 TestTrue(TEXT("Bypassed snapshot permits ordinary pose updates"),OKSyncSourceGeometry(Snapshot,1,P,nullptr,T,false));
 P[0]=Original;
 TestTrue(TEXT("Re-enabled snapshot republishes old pose after bypass changed output"),OKSyncSourceGeometry(Snapshot,1,P,nullptr,T));
 TestFalse(TEXT("Republished pose is then stable"),OKSyncSourceGeometry(Snapshot,1,P,nullptr,T));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKCompactDrawTest,"OpenKoikatsu.AnimationCatalog.CompactProceduralDraw",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKCompactDrawTest::RunTest(const FString&){
 auto* Comp=NewObject<UProceduralMeshComponent>();FProcMeshSection Source;
 for(int32 I=0;I<100;++I){FProcMeshVertex V;V.Position=FVector(I,I*.2,0);V.Normal=FVector::UpVector;V.UV0=FVector2D(I*.01,.1);V.UV1=FVector2D(.2,I*.01);V.UV2=FVector2D(.3,I*.01);V.UV3=FVector2D(.4,I*.01);V.Color=FColor(I,2,3);V.Tangent=FProcMeshTangent(FVector::RightVector,true);Source.ProcVertexBuffer.Add(V);}
 Source.ProcIndexBuffer={40,7,81,81,7,40};Comp->SetProcMeshSection(0,Source);
 TArray<TArray<int32>> Map;OKCompactProceduralSections(Comp,100,Map);
 const auto* Draw=Comp->GetProcMeshSection(0);TestEqual(TEXT("Only three referenced vertices retained"),Draw->ProcVertexBuffer.Num(),3);
 for(int32 I=0;I<Draw->ProcIndexBuffer.Num();++I)TestEqual(TEXT("Triangle identity and winding retained"),uint32(Map[0][Draw->ProcIndexBuffer[I]]),Source.ProcIndexBuffer[I]);
 for(int32 I=0;I<3;++I){const auto& A=Draw->ProcVertexBuffer[I];const auto& B=Source.ProcVertexBuffer[Map[0][I]];TestEqual(TEXT("Source position retained"),A.Position,B.Position);TestEqual(TEXT("UV0"),A.UV0,B.UV0);TestEqual(TEXT("UV1"),A.UV1,B.UV1);TestEqual(TEXT("UV2"),A.UV2,B.UV2);TestEqual(TEXT("UV3"),A.UV3,B.UV3);TestEqual(TEXT("Colour"),A.Color,B.Color);TestEqual(TEXT("Normal"),A.Normal,B.Normal);TestEqual(TEXT("Tangent"),A.Tangent.TangentX,B.Tangent.TangentX);TestEqual(TEXT("Tangent sign"),A.Tangent.bFlipTangentY,B.Tangent.bFlipTangentY);}
 const auto Before=Map;OKCompactProceduralSections(Comp,100,Map);TestTrue(TEXT("Repeated compaction retains source mapping"),Map==Before);
 Comp->SetProcMeshSection(0,Source);OKCompactProceduralSections(Comp,100,Map);TestTrue(TEXT("Full shape rebuild restores mapping"),Map==Before);
 return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKEyeLookPlaybackTest,
    "OpenKoikatsu.FaceBlendShape.EyeLookPlayback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKKEyeLookPlaybackTest::RunTest(const FString& Parameters)
{
    int32 Cards=0,Steps=0;
    for(const TCHAR* Card:{TEXT("[Card] March_7th_keiyuu"),TEXT("Alice_keiyuu"),TEXT("Hayase_yuuka_keiyuu_V1_02 For KKS"),TEXT("KK_091207"),TEXT("KK_384283")})
    {
        FOkSession Session;FString Error;
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),FString(TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/"))+Card+TEXT(".png"),0,Error)) {AddError(Error);return false;}
        if(!TestEqual(TEXT("actual rig states survive session cache"),Session.EyeLookPatternCount(),6)) return false;
        Session.SetAnimSceneMode(4);Session.ApplyAnimFrame(.4f,0.f);
        float Rates[3]={0.f,0.f,0.f};FVector Target(.4f,1.5f,2.f);
        if(!TestTrue(TEXT("world target after animation"),Session.StepEyeLook(Target,1,1.f/60.f,true,Rates))) return false;
        for(float R:Rates) if(!TestTrue(TEXT("finite normalized source rates"),FMath::IsFinite(R)&&FMath::Abs(R)<=1.f)) return false;
        const FVector Frozen(Rates[0],Rates[1],Rates[2]);
        if(!TestTrue(TEXT("dt zero holds source gaze"),Session.StepEyeLook(FVector(-1.f,3.f,1.f),1,0.f,true,Rates)) ||
            !TestTrue(TEXT("held rates"),Frozen.Equals(FVector(Rates[0],Rates[1],Rates[2]),0.f))) return false;
        if(!TestTrue(TEXT("disabled holds gaze"),Session.StepEyeLook(Target,4,.1f,false,Rates)) ||
            !TestTrue(TEXT("disabled rates"),Frozen.Equals(FVector(Rates[0],Rates[1],Rates[2]),0.f))) return false;
        if(!TestFalse(TEXT("reject malformed target"),Session.StepEyeLook(FVector(std::numeric_limits<float>::quiet_NaN(),0,0),1,.1f,true,Rates))) return false;
        if(!TestFalse(TEXT("reject malformed dt"),Session.StepEyeLook(Target,1,-.1f,true,Rates))) return false;
        if(!TestFalse(TEXT("reject invalid pattern"),Session.StepEyeLook(Target,6,.1f,true,Rates))) return false;
        for(int32 Pattern=0;Pattern<Session.EyeLookPatternCount();++Pattern)
            for(int32 Frame=0;Frame<4;++Frame) {
                Session.ApplyAnimFrame(.4f+.01f*Frame,1.f/60.f);
                if(!TestTrue(TEXT("all source states on animated rig"),Session.StepEyeLook(Target,Pattern,1.f/60.f,true,Rates))) return false;
                for(float R:Rates) if(!TestTrue(TEXT("valid state rates"),FMath::IsFinite(R)&&FMath::Abs(R)<=1.f)) return false;
                ++Steps;
            }
        ++Cards;
    }
    AddInfo(FString::Printf(TEXT("EyeLookPlayback verified %d cards %d animated state steps"),Cards,Steps));
    return TestEqual(TEXT("five cards"),Cards,5)&&TestEqual(TEXT("six states each card"),Steps,120);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKNativeMouthWeightsTest,
	"OpenKoikatsu.FaceBlendShape.NativeWeights",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKNativeMouthWeightsTest::RunTest(const FString& Parameters)
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Tests/MouthShapes276/GameMouthMarchSourceFinal.json.mouth-shapes.tsv");
	FString Text;
	if (!TestTrue(TEXT("actual installed game's FBS fixture exists"), FFileHelper::LoadFileToString(Text, *Path))) return false;
	TArray<FString> Lines; Text.ParseIntoArrayLines(Lines);
	struct FCase { int32 Pattern; float Rate; };
	TMap<int32, FCase> Cases;
	TMap<FString, FIntPoint> Patterns;
	TMap<int32, TSet<int32>> UsedChannels;
	int32 Targets = 0, DeclaredCases = 0, Compared = 0;
	for (const FString& Line : Lines)
	{
		TArray<FString> F; Line.ParseIntoArray(F, TEXT("\t"), false);
		if (F.Num() == 2 && F[0] == TEXT("target_count")) Targets = FCString::Atoi(*F[1]);
		if (F.Num() == 2 && F[0] == TEXT("cases")) DeclaredCases = FCString::Atoi(*F[1]);
		if (F.Num() == 4 && F[0] == TEXT("case")) Cases.Add(FCString::Atoi(*F[1]), {FCString::Atoi(*F[2]), FCString::Atof(*F[3])});
		if (F.Num() == 5 && F[0] == TEXT("pattern"))
		{
			const int32 Target = FCString::Atoi(*F[1]);
			const FIntPoint Pair(FCString::Atoi(*F[3]), FCString::Atoi(*F[4]));
			Patterns.Add(F[1] + TEXT("|") + F[2], Pair);
			if (Pair.X >= 0) UsedChannels.FindOrAdd(Target).Add(Pair.X);
			if (Pair.Y >= 0) UsedChannels.FindOrAdd(Target).Add(Pair.Y);
		}
	}
	if (!TestEqual(TEXT("nonempty native mouth targets"), Targets, 5)
		|| !TestEqual(TEXT("complete native cases"), DeclaredCases, 10)
		|| !TestEqual(TEXT("all native cases loaded"), Cases.Num(), DeclaredCases)) return false;
	for (const FString& Line : Lines)
	{
		TArray<FString> F; Line.ParseIntoArray(F, TEXT("\t"), false);
		if (F.Num() != 6 || F[0] != TEXT("channel")) continue;
		const int32 Target = FCString::Atoi(*F[2]), Channel = FCString::Atoi(*F[3]);
		const TSet<int32>* Used = UsedChannels.Find(Target);
		if (!Used || !Used->Contains(Channel)) continue; // Other FBS controllers keep their own channels.
		const FCase* Case = Cases.Find(FCString::Atoi(*F[1]));
		if (!Case) { AddError(TEXT("native channel references missing case")); return false; }
		const FIntPoint* Pair = Patterns.Find(F[2] + TEXT("|") + FString::FromInt(Case->Pattern));
		if (!Pair) { AddError(TEXT("native target has no authored pattern")); return false; }
		const int32 Open = OKFbsOpenPercent(Case->Rate);
		const float Expected = (Pair->X == Channel ? 100 - Open : 0) + (Pair->Y == Channel ? Open : 0);
		if (!FMath::IsNearlyEqual(FCString::Atof(*F[5]), Expected, 1e-5f))
			AddError(FString::Printf(TEXT("game FBS case %s target %d channel %s: native=%s expected=%.5f"), *F[1], Target, *F[4], *F[5], Expected));
		++Compared;
	}
	TestTrue(TEXT("native channels include positive and reset weights"), Compared > 2000);
	TestEqual(TEXT("fixed rate lower clamp"), OKFbsOpenPercent(-1.f), 0);
	TestEqual(TEXT("fixed rate upper clamp"), OKFbsOpenPercent(2.f), 100);
	UE_LOG(LogTemp, Display, TEXT("KK native FBS weights targets=%d cases=%d channel_comparisons=%d"), Targets, Cases.Num(), Compared);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKSharedMouthPassTest,
	"OpenKoikatsu.FaceBlendShape.SharedDeformation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKSharedMouthPassTest::RunTest(const FString& Parameters)
{
	UProceduralMeshComponent* Comp = NewObject<UProceduralMeshComponent>();
	const TArray<FVector> Rest = {FVector(0,0,0), FVector(1,0,0), FVector(0,0,1)};
	const TArray<int32> Tris = {0,1,2}, BackTris = {2,1,0};
	const TArray<FVector> Normals = {FVector::ForwardVector, FVector::ForwardVector, FVector::ForwardVector};
	const TArray<FVector> BackNormals = {-FVector::ForwardVector, -FVector::ForwardVector, -FVector::ForwardVector};
	const TArray<FVector2D> UV = {FVector2D(0,0), FVector2D(1,0), FVector2D(0,1)};
	const TArray<FVector2D> OverlayUV = {FVector2D(.2,.3), FVector2D(.4,.5), FVector2D(.6,.7)};
	const TArray<FColor> Colors = {FColor::Red, FColor::Green, FColor::Blue};
	TArray<FProcMeshTangent> Tangents;
	Comp->CreateMeshSection(0, Rest, Tris, Normals, UV, Colors, Tangents, false);
	TArray<FVector> Overlay = Rest;
	for (FVector& V : Overlay) V += FVector(0,.08,0);
	Comp->CreateMeshSection(1, Overlay, Tris, Normals, OverlayUV, Colors, Tangents, false);
	Comp->CreateMeshSection(2, Rest, BackTris, BackNormals, UV, Colors, Tangents, false);
	TArray<TArray<FVector>> Offsets, SourceNormals;
	for (int32 Section = 0; Section < 3; ++Section)
	{
		Offsets.AddDefaulted(); SourceNormals.AddDefaulted();
		for (int32 Vertex = 0; Vertex < Rest.Num(); ++Vertex)
		{
			const FProcMeshVertex& V = Comp->GetProcMeshSection(Section)->ProcVertexBuffer[Vertex];
			Offsets.Last().Add(V.Position - Rest[Vertex]); SourceNormals.Last().Add(V.Normal);
		}
	}
	for (int32 Frame = 0; Frame < 3; ++Frame)
	{
		TArray<FVector> Deformed = Rest;
		if (Frame != 2) { Deformed[0].Z += .4 * (Frame + 1); Deformed[1].X += .25 * (Frame + 1); }
		TestEqual(TEXT("overlay and outline share each new mouth frame"), OKUpdateSecondarySections(Comp, Deformed, Offsets, SourceNormals), 2);
		for (int32 Section = 1; Section < 3; ++Section)
			for (int32 Vertex = 0; Vertex < Rest.Num(); ++Vertex)
			{
				const FProcMeshVertex& V = Comp->GetProcMeshSection(Section)->ProcVertexBuffer[Vertex];
				TestTrue(TEXT("same deformation with rest offset, no accumulation"), V.Position.Equals(Deformed[Vertex] + Offsets[Section][Vertex], 1e-6));
				TestEqual(TEXT("pass UV retained"), V.UV0, Section == 1 ? OverlayUV[Vertex] : UV[Vertex]);
				TestEqual(TEXT("pass colour retained"), V.Color, Colors[Vertex]);
				TestEqual(TEXT("inverted outline normal retained"), V.Normal, SourceNormals[Section][Vertex]);
			}
	}
	TestEqual(TEXT("outline winding retained"), Comp->GetProcMeshSection(2)->ProcIndexBuffer[0], uint32(2));
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKNativeFaceControlsTest,
    "OpenKoikatsu.FaceBlendShape.NativeFaceControls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKNativeFaceControlsTest::RunTest(const FString& Parameters)
{
    int32 TotalCases = 0, Compared = 0, SourcePairs = 0, ShadowPairs = 0;
    for (const auto& Fixture : {TPair<FString,FString>(TEXT("GameMarchFace282"),TEXT("[Card] March_7th_keiyuu")),
        TPair<FString,FString>(TEXT("GameAliceFace282"),TEXT("Alice_keiyuu"))})
    {
        FString Text, Error;
        if (!TestTrue(TEXT("actual installed-game fixture"), FFileHelper::LoadFileToString(Text,
            *(FPaths::ProjectSavedDir()/TEXT("Tests/FaceControls282")/(Fixture.Key+TEXT(".json.face-controls.tsv")))))) return false;
        FOkSession Session;
        if (!TestTrue(TEXT("load actual card's runtime FBS metadata"), Session.Open(
            TEXT("C:/portable/KoikatuSunshine"), TEXT("C:/portable/KoikatuSunshine/UserData/chara/female")/(Fixture.Value+TEXT(".png")),0,Error)))
        { AddError(Error); return false; }
        TMap<FString,int32> Parts;
        for (int32 Part = 0; Part < Session.PartCount(); ++Part)
        {
            OkMeshView View = {};
            if (Session.PartAt(Part,View) && View.name && Session.FacePatternCount(Part,1)+Session.FacePatternCount(Part,2)+Session.FacePatternCount(Part,0)>0)
                Parts.Add(UTF8_TO_TCHAR(View.name),Part);
        }
        // Unity's second eyeline material shares the same SMR/FBS deformation.
        // Our separately emitted material pass must retain its source pairs,
        // baked weights and transformed deltas, even with a different name.
        const int32* Shadow = Parts.Find(TEXT("cf_O_eyeline_kage"));
        const int32* Eyeline = Parts.Find(TEXT("cf_O_eyeline"));
        if (!TestNotNull(TEXT("second eyeline material retains controller"),Shadow)
            || !TestNotNull(TEXT("source eyeline controller"),Eyeline)) return false;
        for (int32 Control=0;Control<=2;++Control)
        {
            const int32 Count=Session.FacePatternCount(*Eyeline,Control);
            if(!TestEqual(TEXT("material-pass controller count"),Session.FacePatternCount(*Shadow,Control),Count)) return false;
            for(int32 Pattern=0;Pattern<Count;++Pattern)
            {
                int32 Close=-2,Open=-2,ShadowClose=-3,ShadowOpen=-3;
                if(!Session.FacePair(*Eyeline,Control,Pattern,Close,Open)
                    ||!Session.FacePair(*Shadow,Control,Pattern,ShadowClose,ShadowOpen)
                    ||!TestEqual(TEXT("material-pass close channel"),ShadowClose,Close)
                    ||!TestEqual(TEXT("material-pass open channel"),ShadowOpen,Open)) return false;
                ++ShadowPairs;
            }
        }
        if(!TestEqual(TEXT("material-pass morph count"),Session.MorphCount(*Shadow),Session.MorphCount(*Eyeline))) return false;
        for(int32 Morph=0;Morph<Session.MorphCount(*Eyeline);++Morph)
        {
            OkMorphView Source={},Copy={};
            if(!Session.MorphAt(*Eyeline,Morph,Source)||!Session.MorphAt(*Shadow,Morph,Copy)
                ||!TestEqual(TEXT("material-pass delta count"),Copy.n,Source.n)
                ||!TestEqual(TEXT("material-pass baked weight"),Session.FaceBaselineWeight(*Shadow,Morph),Session.FaceBaselineWeight(*Eyeline,Morph))) return false;
            for(int32 Delta=0;Delta<Source.n;++Delta)
            {
                if(!TestEqual(TEXT("material-pass vertex index"),Copy.indices[Delta],Source.indices[Delta])) return false;
                for(int32 Axis=0;Axis<3;++Axis)
                    if(!TestEqual(TEXT("material-pass transformed delta"),Copy.deltas[Delta*3+Axis],Source.deltas[Delta*3+Axis])) return false;
            }
        }
        TMap<FString,TArray<TPair<FName,FName>>> Patterns;
        TMap<FString,FName> Names;
        TMap<int32,FIntPoint> Cases;
        TMap<int32,float> Rates, Defaults;
        TArray<FString> Lines; Text.ParseIntoArrayLines(Lines);
        int32 Declared = -1;
        for (const FString& Line : Lines)
        {
            TArray<FString> F; Line.ParseIntoArray(F,TEXT("\t"),false);
            if (F[0]==TEXT("default")) Defaults.Add(FCString::Atoi(*F[1]),FCString::Atof(*F[2]));
            if (F[0]==TEXT("name")) Names.Add(F[1]+TEXT("|")+F[2],FName(*F[3]));
            if (F[0]==TEXT("case")) { const int32 Id=FCString::Atoi(*F[1]); Cases.Add(Id,FIntPoint(FCString::Atoi(*F[2]),FCString::Atoi(*F[3]))); Rates.Add(Id,FCString::Atof(*F[4])); }
            if (F[0]==TEXT("cases")) Declared=FCString::Atoi(*F[1]);
        }
        if (!TestEqual(TEXT("complete native cases"),Cases.Num(),Declared)) return false;
        for (const FString& Line : Lines)
        {
            TArray<FString> F; Line.ParseIntoArray(F,TEXT("\t"),false);
            if (F[0]!=TEXT("pattern")) continue;
            const int32 Control=FCString::Atoi(*F[1]), Pattern=FCString::Atoi(*F[3]);
            const int32* Part=Parts.Find(F[2]);
            if (!TestNotNull(FString::Printf(TEXT("native target exists on selected head: %s"),*F[2]),Part)) return false;
            int32 Close=-2, Open=-2;
            if (!TestTrue(TEXT("source target pair through C ABI"),Session.FacePair(*Part,Control,Pattern,Close,Open))) return false;
            if (!TestEqual(TEXT("native close index"),Close,FCString::Atoi(*F[4]))
                || !TestEqual(TEXT("native open index"),Open,FCString::Atoi(*F[5]))) return false;
            auto NameAt=[&](int32 Index)->FName { OkMorphView M={}; return Index>=0 && Session.MorphAt(*Part,Index,M) && M.name ? FName(UTF8_TO_TCHAR(M.name)):NAME_None; };
            TArray<TPair<FName,FName>>& Target=Patterns.FindOrAdd(F[2]+TEXT("|")+F[1]);
            if(Target.Num()<=Pattern) Target.SetNum(Pattern+1);
            Target[Pattern]={NameAt(Close),NameAt(Open)};
            ++SourcePairs;
        }
        // Re-evaluate each source controller using the runtime helper, then
        // compare every channel to independently captured SetBlendShapeWeight.
        TMap<FString,TMap<FName,float>> Expected;
        for (const FString& Line : Lines)
        {
            TArray<FString> F; Line.ParseIntoArray(F,TEXT("\t"),false);
            if (F[0]!=TEXT("weight")) continue;
            const int32 Id=FCString::Atoi(*F[1]);
            const FString Key=F[1]+TEXT("|")+F[2];
            if (!Expected.Contains(Key))
            {
                TMap<FName,float>& Weights=Expected.Add(Key);
                for(int32 Control : {2,1,0})
                {
                    const auto* Target=Patterns.Find(F[2]+TEXT("|")+FString::FromInt(Control));
                    if(Target) OKFbsWriteController(Weights,*Target,Cases[Id].X==Control?Cases[Id].Y:0,
                        Cases[Id].X==Control?Rates[Id]:Defaults.FindRef(Control));
                }
            }
            const FName* Name=Names.Find(F[2]+TEXT("|")+F[3]);
            if (!TestNotNull(TEXT("native channel identity"),Name)) return false;
            const float Wanted=Expected[Key].FindRef(*Name);
            const float Actual=FCString::Atof(*F[4]);
            if(!FMath::IsNearlyEqual(Wanted,Actual,1e-5f))
            {AddError(FString::Printf(TEXT("%s case %d %s %s native=%f runtime=%f"),*Fixture.Key,Id,*F[2],*Name->ToString(),Actual,Wanted));return false;}
            ++Compared;
        }
        TotalCases+=Cases.Num();
    }
    TestEqual(TEXT("all March and mod Alice eye/brow controls"),TotalCases,276);
    TestTrue(TEXT("native positive/reset channel coverage"),Compared>80000);
    TestEqual(TEXT("both selected heads' second material follows every eye pattern"),ShadowPairs,58);
    UE_LOG(LogTemp,Display,TEXT("KK native face controls cases=%d channel_comparisons=%d source_pairs=%d"),TotalCases,Compared,SourcePairs);
    UE_LOG(LogTemp,Display,TEXT("KK source material-pass face pairs=%d"),ShadowPairs);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKNativeFaceTimeTest,
    "OpenKoikatsu.FaceBlendShape.NativeFaceTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKNativeFaceTimeTest::RunTest(const FString& Parameters)
{
    int32 FrameCount=0, WeightCount=0, BlinkCount=0, PairCount=0, Settings=0;
    for(const auto& Fixture : {TPair<FString,FString>(TEXT("GameMarchTime283"),TEXT("[Card] March_7th_keiyuu")),
        TPair<FString,FString>(TEXT("GameAliceTime283"),TEXT("Alice_keiyuu"))})
    {
        FString Text,Error;
        if(!TestTrue(TEXT("native timed FBS fixture"),FFileHelper::LoadFileToString(Text,
            *(FPaths::ProjectSavedDir()/TEXT("Tests/FaceTime283")/(Fixture.Key+TEXT(".json.face-time.tsv")))))) return false;
        FOkSession Session;
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),TEXT("C:/portable/KoikatuSunshine/UserData/chara/female")/(Fixture.Value+TEXT(".png")),0,Error))
        { AddError(Error); return false; }
        TMap<FString,int32> Parts;
        for(int32 Part=0;Part<Session.PartCount();++Part)
        {
            OkMeshView View={};
            if(Session.PartAt(Part,View)&&View.name) Parts.Add(UTF8_TO_TCHAR(View.name),Part);
        }
        TArray<FString> Lines;Text.ParseIntoArrayLines(Lines);
        TMap<FString,FName> Names;
        TMap<FString,TArray<TPair<FName,FName>>> Patterns;
        for(const FString& Line:Lines)
        {
            TArray<FString> F;Line.ParseIntoArray(F,TEXT("\t"),false);
            if(F[0]==TEXT("setting"))
            {
                if(!TestTrue(TEXT("actual selected head runtime metadata"),FMath::IsNearlyEqual(Session.FaceRuntimeSetting(FCString::Atoi(*F[1])),FCString::Atof(*F[2]),1e-6f))) return false;
                ++Settings;
            }
            if(F[0]==TEXT("name")) Names.Add(F[1]+TEXT("|")+F[2],FName(*F[3]));
            if(F[0]!=TEXT("pattern")) continue;
            const int32* Part=Parts.Find(F[2]);
            if(!TestNotNull(TEXT("timed source target retained"),Part)) return false;
            const int32 Control=FCString::Atoi(*F[1]),Pattern=FCString::Atoi(*F[3]);
            int32 Close=-2,Open=-2;
            if(!Session.FacePair(*Part,Control,Pattern,Close,Open)
                ||!TestEqual(TEXT("source close"),Close,FCString::Atoi(*F[4]))
                ||!TestEqual(TEXT("source open"),Open,FCString::Atoi(*F[5]))) return false;
            auto NameAt=[&](int32 Index)->FName {OkMorphView M={};return Index>=0&&Session.MorphAt(*Part,Index,M)&&M.name?FName(UTF8_TO_TCHAR(M.name)):NAME_None;};
            auto& Target=Patterns.FindOrAdd(F[2]+TEXT("|")+F[1]);
            if(Target.Num()<=Pattern) Target.SetNum(Pattern+1);
            Target[Pattern]={NameAt(Close),NameAt(Open)};
            ++PairCount;
        }
        FOKFbsTransition States[3];
        TMap<FString,TMap<FName,float>> Expected;
        FOKFbsBlink Blink;Blink.Frequency=3;
        int32 LocalFrames=0,LocalBlinks=0;
        for(const FString& Line:Lines)
        {
            TArray<FString> F;Line.ParseIntoArray(F,TEXT("\t"),false);
            if(F[0]==TEXT("frame"))
            {
                const int32 Control=FCString::Atoi(*F[2]),Step=FCString::Atoi(*F[3]),Pattern=FCString::Atoi(*F[4]);
                if(Step==0) for(auto& State:States) State.Reset(0);
                if(Pattern>=0) States[Control].Change(Pattern,FCString::Atoi(*F[5])!=0);
                for(auto& State:States) State.Advance(FCString::Atof(*F[6]));
                if(!TestTrue(TEXT("native TimeProgressCtrl progress"),FMath::IsNearlyEqual(States[Control].Alpha(),FCString::Atof(*F[7]),1e-6f))) return false;
                Expected.Reset();++LocalFrames;++FrameCount;
            }
            if(F[0]==TEXT("weight"))
            {
                if(!Expected.Contains(F[2]))
                {
                    auto& Weights=Expected.Add(F[2]);
                    for(int32 Control:{2,1,0})
                        if(const auto* Target=Patterns.Find(F[2]+TEXT("|")+FString::FromInt(Control)))
                            OKFbsWriteTransition(Weights,*Target,States[Control],Control==1?.61337f:.8f);
                }
                const FName* Name=Names.Find(F[2]+TEXT("|")+F[3]);
                if(!TestNotNull(TEXT("native timed channel"),Name)) return false;
                const float Wanted=Expected[F[2]].FindRef(*Name),Actual=FCString::Atof(*F[4]);
                if(!FMath::IsNearlyEqual(Wanted,Actual,2e-5f))
                {AddError(FString::Printf(TEXT("timed FBS mismatch %s frame=%s %s %s wanted=%g actual=%g"),*Fixture.Key,*F[1],*F[2],*Name->ToString(),Wanted,Actual));return false;}
                ++WeightCount;
            }
            if(F[0]==TEXT("blink"))
            {
                const float Time=FCString::Atof(*F[2]),CommandFloat=FCString::Atof(*F[4]),CalcFloat=FCString::Atof(*F[6]);
                const int32 CommandInt=FCString::Atoi(*F[5]),CalcInt=FCString::Atoi(*F[7]);
                auto CF=[&](float Min,float Max){return CommandFloat;};auto CI=[&](int32 Min,int32 Max){return CommandInt;};
                if(F[3]==TEXT("fixed")) Blink.FixedFlags=1;
                if(F[3]==TEXT("resume")) Blink.FixedFlags=0;
                if(F[3]==TEXT("close")) Blink.ForceClose(Time,CF,CI);
                if(F[3]==TEXT("open")) Blink.ForceOpen(Time,CF);
                if(F[3]==TEXT("frequency0")) Blink.SetFrequency(0,Time,CI);
                if(F[3]==TEXT("speed")) Blink.SetSpeed(.2f);
                Blink.Calculate(Time,[&](float Min,float Max){return CalcFloat;},[&](int32 Min,int32 Max){return CalcInt;});
                bool Good=Blink.FixedFlags==FCString::Atoi(*F[8])&&Blink.Frequency==FCString::Atoi(*F[9])
                    &&Blink.Mode==FCString::Atoi(*F[11])&&Blink.Count==FCString::Atoi(*F[12]);
                const float Values[]={Blink.BaseSpeed,Blink.CalcSpeed,Blink.Deadline,Blink.OpenRate};
                const int32 Columns[]={10,13,14,15};
                for(int32 Index=0;Index<4;++Index) Good&=FMath::IsNearlyEqual(Values[Index],FCString::Atof(*F[Columns[Index]]),3e-5f);
                if(!Good) {AddError(FString::Printf(TEXT("native blink trace mismatch %s frame=%s mode=%d count=%d rate=%g"),*Fixture.Key,*F[1],Blink.Mode,Blink.Count,Blink.OpenRate));return false;}
                ++BlinkCount;++LocalBlinks;
            }
            if(F[0]==TEXT("frames")&&!TestEqual(TEXT("complete timed trace"),LocalFrames,FCString::Atoi(*F[1]))) return false;
            if(F[0]==TEXT("blinks")&&!TestEqual(TEXT("complete blink trace"),LocalBlinks,FCString::Atoi(*F[1]))) return false;
        }
        if(!TestEqual(TEXT("all transition cases"),LocalFrames,72)||!TestEqual(TEXT("all blink cases"),LocalBlinks,240)) return false;
    }
    UE_LOG(LogTemp,Display,TEXT("KK native face time settings=%d pairs=%d frames=%d channel_comparisons=%d blink_frames=%d"),Settings,PairCount,FrameCount,WeightCount,BlinkCount);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKNativeEyeGazeTest,
    "OpenKoikatsu.FaceBlendShape.NativeEyeGaze",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKKNativeEyeGazeTest::RunTest(const FString& Parameters)
{
    int32 Cases=0,Compared=0,UvCompared=0,Settings=0,Pairs=0;
    for(const auto& Fixture:{TPair<FString,FString>(TEXT("GameMarchGaze284"),TEXT("[Card] March_7th_keiyuu")),
        TPair<FString,FString>(TEXT("GameAliceGaze284"),TEXT("Alice_keiyuu"))})
    {
        FString Text,Error;
        if(!TestTrue(TEXT("native gaze table"),FFileHelper::LoadFileToString(Text,
            *(FPaths::ProjectSavedDir()/TEXT("Tests/EyeGaze284")/(Fixture.Key+TEXT(".json.eye-gaze.tsv")))))) return false;
        FOkSession Session;
        if(!Session.Open(TEXT("C:/portable/KoikatuSunshine"),TEXT("C:/portable/KoikatuSunshine/UserData/chara/female")/(Fixture.Value+TEXT(".png")),0,Error)) {AddError(Error);return false;}
        TMap<FString,int32> Parts;
        for(int32 Part=0;Part<Session.PartCount();++Part) {OkMeshView View={};if(Session.PartAt(Part,View)&&View.name) Parts.Add(UTF8_TO_TCHAR(View.name),Part);}
        TArray<FString> Lines;Text.ParseIntoArrayLines(Lines);
        TMap<FString,FName> Names;
        TMap<FString,TArray<TPair<FName,FName>>> Patterns;
        TMap<FString,TArray<FVector2f>> RawUvs;
        for(const auto& Line:Lines)
        {
            TArray<FString> F;Line.ParseIntoArray(F,TEXT("\t"),false);
            if(F[0]==TEXT("setting")) {
                if(!TestTrue(TEXT("gaze runtime settings"),FMath::IsNearlyEqual(Session.FaceRuntimeSetting(FCString::Atoi(*F[1])),FCString::Atof(*F[2]),1e-6f))) return false;
                ++Settings;
            }
            if(F[0]==TEXT("material-setting")) {
                const int32* Part=Parts.Find(F[1]);if(!TestNotNull(TEXT("actual eye material renderer"),Part)) return false;
                if(!TestTrue(TEXT("source material settings"),FMath::IsNearlyEqual(Session.EyeMaterialSetting(*Part,FCString::Atoi(*F[2])),FCString::Atof(*F[3]),1e-6f))) return false;
                ++Settings;
            }
            if(F[0]==TEXT("raw-uv")) {
                auto& Uvs=RawUvs.FindOrAdd(F[1]);const int32 Index=FCString::Atoi(*F[2]);
                if(Uvs.Num()<=Index) Uvs.SetNum(Index+1);Uvs[Index]=FVector2f(FCString::Atof(*F[3]),FCString::Atof(*F[4]));
            }
            if(F[0]==TEXT("name")) Names.Add(F[1]+TEXT("|")+F[2],FName(*F[3]));
            if(F[0]!=TEXT("pattern")) continue;
            const int32* Part=Parts.Find(F[2]);if(!TestNotNull(TEXT("gaze FBS target"),Part)) return false;
            int32 Close=-2,Open=-2;const int32 Control=FCString::Atoi(*F[1]),Pattern=FCString::Atoi(*F[3]);
            if(!Session.FacePair(*Part,Control,Pattern,Close,Open)||Close!=FCString::Atoi(*F[4])||Open!=FCString::Atoi(*F[5])) {AddError(TEXT("gaze native pair mismatch"));return false;}
            auto NameAt=[&](int32 Index)->FName {OkMorphView M={};return Index>=0&&Session.MorphAt(*Part,Index,M)&&M.name?FName(UTF8_TO_TCHAR(M.name)):NAME_None;};
            auto& P=Patterns.FindOrAdd(F[2]+TEXT("|")+F[1]);if(P.Num()<=Pattern) P.SetNum(Pattern+1);P[Pattern]={NameAt(Close),NameAt(Open)};++Pairs;
        }
        TMap<FString,TMap<FName,float>> Expected;
        float EyeRate=0.f;int32 EyePattern=0,LocalCases=0;
        for(const auto& Line:Lines)
        {
            TArray<FString> F;Line.ParseIntoArray(F,TEXT("\t"),false);
            if(F[0]==TEXT("case")) {
                const float H=FCString::Atof(*F[2]),R=FCString::Atof(*F[3]),V=FCString::Atof(*F[4]);
                const float Blink=FCString::Atof(*F[5]),Fixed=FCString::Atof(*F[6]),Maximum=FCString::Atof(*F[8]);
                EyePattern=FCString::Atoi(*F[7]);
                const float Correct=OKEyeCorrectOpenMax(Maximum,H,V,Session.FaceRuntimeSetting(8),Session.FaceRuntimeSetting(9),Session.FaceRuntimeSetting(10));
                if(!TestTrue(TEXT("native sqrt gaze correction"),FMath::IsNearlyEqual(Correct,FCString::Atof(*F[9]),2e-6f))) return false;
                EyeRate=Fixed>=0.f?Fixed:FMath::Lerp(Session.FaceRuntimeSetting(4),Correct>=0.f?Correct:Maximum,Blink);
                if(!TestTrue(TEXT("source rate input"),Session.SetEyeGaze(H,R,V))) return false;
                Expected.Reset();++Cases;++LocalCases;
            }
            if(F[0]==TEXT("st")) {
                const int32* Part=Parts.Find(F[2]);if(!TestNotNull(TEXT("native iris renderer"),Part)) return false;
                const auto* Raw=RawUvs.Find(F[2]);if(!TestNotNull(TEXT("native untransformed UV"),Raw)) return false;
                OkMeshView View={};if(!Session.PartAt(*Part,View)||!View.uvs||View.n_verts!=Raw->Num()) {AddError(TEXT("iris UV count mismatch"));return false;}
                const float Sx=FCString::Atof(*F[3]),Sy=FCString::Atof(*F[4]),Ox=FCString::Atof(*F[5]),Oy=FCString::Atof(*F[6]);
                for(int32 Vertex=0;Vertex<Raw->Num();++Vertex) {
                    const FVector2f Wanted((*Raw)[Vertex].X*Sx+Ox,(*Raw)[Vertex].Y*Sy+Oy);
                    if(!FMath::IsNearlyEqual(View.uvs[Vertex*2],Wanted.X,1e-6f)||!FMath::IsNearlyEqual(View.uvs[Vertex*2+1],Wanted.Y,1e-6f)) {
                        AddError(FString::Printf(TEXT("native iris UV mismatch %s case=%s renderer=%s vertex=%d"),*Fixture.Key,*F[1],*F[2],Vertex));return false;
                    }
                    ++UvCompared;
                }
            }
            if(F[0]==TEXT("weight")) {
                if(!Expected.Contains(F[2])) {
                    auto& W=Expected.Add(F[2]);
                    for(int32 Control:{2,1,0}) if(const auto* P=Patterns.Find(F[2]+TEXT("|")+FString::FromInt(Control)))
                        OKFbsWriteController(W,*P,Control==0?8:Control==1?EyePattern:2,Control==1?EyeRate:.8f);
                }
                const FName* Name=Names.Find(F[2]+TEXT("|")+F[3]);if(!TestNotNull(TEXT("source gaze channel"),Name)) return false;
                const float Actual=FCString::Atof(*F[4]),Wanted=Expected[F[2]].FindRef(*Name);
                if(!FMath::IsNearlyEqual(Actual,Wanted,1e-5f)) {AddError(FString::Printf(TEXT("native gaze channel mismatch %s case=%s %s %s native=%g runtime=%g"),*Fixture.Key,*F[1],*F[2],*Name->ToString(),Actual,Wanted));return false;}
                ++Compared;
            }
        }
        if(!TestEqual(TEXT("full native rate grid"),LocalCases,200)||!TestEqual(TEXT("both eye renderers"),RawUvs.Num(),2)) return false;
    }
    UE_LOG(LogTemp,Display,TEXT("KK native eye gaze cases=%d settings=%d pairs=%d channel_comparisons=%d uv_comparisons=%d"),Cases,Settings,Pairs,Compared,UvCompared);
    return !HasAnyErrors();
}
#endif
