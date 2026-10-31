// The card's parts as instances of the shared parents (KKRuntimeSharedMaterial), in every build: a packaged game
// loads the cooked parents and their shader library; the editor and -game load the same assets uncooked (their shaders
// compile once per machine and stay in the DDC), so a new card compiles nothing. In the editor a part no parent covers
// keeps its own transient graph (ApplyPartMaterial); a packaged game refuses such a card.
// Both paths consume the complete native material metadata and textures.
#if !WITH_EDITOR
#include "ShaderCodeLibrary.h"
#endif
// OKPrepareCookedRole's progress across load batches: the textures and the part instances (a character's ~160 MIDs took
// ~40 ms, and with its textures up to ~200 ms, in one frame) are made a few milliseconds at a time.
struct FOKCookedRoleBuild {FString Root;bool HG=false;FKKRuntimeMaterialScene Scene;TArray<UTexture2D*> Textures;int32 NextTexture=0,NextPart=0,Uncovered=0;};
// 1: the actor's shared-parent instances are ready; 0: continue in a later batch (Budget seconds were spent; 0 = no
// limit); -1: failed (Error).
static int32 OKPrepareCookedRole(AKKPreviewActor* Actor,FString& Error,double Budget=0.){
    if(!Actor->CookedRoleBuild&&!Actor->CookedRoleMaterials.IsEmpty())return 1;
    const double Deadline=Budget>0.?FPlatformTime::Seconds()+Budget:DBL_MAX;
    auto Fail=[&](){Actor->CookedRoleBuild.Reset();Actor->CookedRoleMaterials.Reset();Actor->CookedRoleOutlines.Reset();return -1;};
    if(!Actor->CookedRoleBuild){
    auto Setup=[&]()->bool{
    {TRACE_CPUPROFILER_EVENT_SCOPE(KK_CookedRolePrepare);if(!Actor->Session.PrepareRuntimeMaterials(Error))return false;}
    FString Manifest;
    if(!FParse::Value(FCommandLine::Get(),TEXT("StreetSocialManifest="),Manifest))FParse::Value(FCommandLine::Get(),TEXT("CrowdRuntimeScene="),Manifest);
    FString Root;
    FParse::Value(FCommandLine::Get(),TEXT("RuntimeMaterialRoot="),Root);
    if(Root.IsEmpty()){
        FString Json;TSharedPtr<FJsonObject> Object;
        if(FFileHelper::LoadFileToString(Json,*Manifest)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Object)&&Object)Object->TryGetStringField(TEXT("material_root"),Root);
    }
#if WITH_EDITOR
    // The card reader in the editor / -game: the street's display root (the HG one is next to it, below).
    if(Root.IsEmpty())Root=TEXT("/Game/IndependentResources/RuntimeMaterials/StreetDisplay26");
#endif
    // The host's root for this actor (OK Studio items: the one with the depth-writing ForgeStudioOpaque parent).
    if(!Actor->SharedMaterialRootOverride.IsEmpty())Root=Actor->SharedMaterialRootOverride;
    if(Root.IsEmpty()){Error=TEXT("High model requires a cooked shared material root");return false;}
    // The cooked parents' shaders live in their own library (Tools/rebuild_street_social_patch.ps1 cooks it as
    // RuntimeMaterialBake), which the engine does not open by itself: RuntimeMaterialLibrary= or manifest material_library.
#if !WITH_EDITOR
    static TSet<FString> OpenedLibraries;
    FString Library;
    if(!FParse::Value(FCommandLine::Get(),TEXT("RuntimeMaterialLibrary="),Library)){
        FString Json;TSharedPtr<FJsonObject> Object;
        if(FFileHelper::LoadFileToString(Json,*Manifest)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Object)&&Object)Object->TryGetStringField(TEXT("material_library"),Library);
    }
    if(Library.IsEmpty())Library=TEXT("RuntimeMaterialBake");
    if(!OpenedLibraries.Contains(Library)){
        if(!FShaderCodeLibrary::OpenLibrary(Library,FPaths::ProjectContentDir()))UE_LOG(LogTemp,Warning,TEXT("High model shader library %s not found; cooked parents may lack shaders"),*Library);
        OpenedLibraries.Add(Library);
    }
#endif
    // Render style (ok.RenderStyle, OKHGShading): HG has its own cooked parents
    // next to the Koikatsu ones (HGMaterialRoot= or manifest hg_material_root).
    // Without them the Koikatsu root stays, so an old manifest still plays.
    const IConsoleVariable* StyleVar=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.RenderStyle"));
    bool HG=StyleVar&&StyleVar->GetInt()==1;
    if(HG){
        FString HGRoot;FParse::Value(FCommandLine::Get(),TEXT("HGMaterialRoot="),HGRoot);
        if(HGRoot.IsEmpty()){FString Json;TSharedPtr<FJsonObject> Object;if(FFileHelper::LoadFileToString(Json,*Manifest)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Object)&&Object)Object->TryGetStringField(TEXT("hg_material_root"),HGRoot);}
#if WITH_EDITOR
        if(HGRoot.IsEmpty())HGRoot=TEXT("/Game/IndependentResources/RuntimeMaterials/StreetHG05");
#endif
        if(HGRoot.IsEmpty()){UE_LOG(LogTemp,Warning,TEXT("HG render style: no hg_material_root, keeping %s"),*Root);HG=false;}else Root=HGRoot;
    }
    FKKRuntimeMaterialScene Scene;Scene.EnableShadows=(Actor->bCharacterDisplayShadows||Actor->bStudioItemShading)&&!HG;OKApplyCharacterMakerLighting(Scene);Scene.Globals=Actor->StudioShaderGlobals;
    auto Build=MakeShared<FOKCookedRoleBuild>();Build->Root=Root;Build->HG=HG;Build->Scene=Scene;Actor->CookedRoleBuild=Build;
    return true;
    };
    if(!Setup()){Actor->CookedRoleBuild.Reset();return -1;}
    }
    FOKCookedRoleBuild& Role=*Actor->CookedRoleBuild;
    const auto& Data=*Actor->Session.RuntimeMaterials;
    {TRACE_CPUPROFILER_EVENT_SCOPE(KK_CookedRoleTextures);
    while(Role.NextTexture<Data.Textures.Num()){
        FOkCardProfileScope Profile(GOkCardLoadProfile.bActive?&GOkCardLoadProfile.RoleTextureSeconds:nullptr);
        auto* Texture=OKCreateRuntimePreparedTexture(Data.Textures[Role.NextTexture++],Error);if(!Texture)return Fail();Role.Textures.Add(Texture);Actor->LoadedTextures.Add(Texture);
        if(FPlatformTime::Seconds()>=Deadline)return 0;
    }}
    if(Role.NextPart==0&&Actor->CookedRoleMaterials.IsEmpty()){
        Actor->FlatFaceMaterials.Reset();Actor->FlatFaceState=FKKFaceFlattenState();
        Actor->CookedRoleMaterials.SetNum(Actor->Session.PartCount());Actor->CookedRoleOutlines.SetNum(Actor->Session.PartCount());
    }
    TRACE_CPUPROFILER_EVENT_SCOPE(KK_CookedRoleParts);
    auto BuildPart=[&](const FKKRuntimeMaterialPart& SourcePart)->int32{
        auto& Textures=Role.Textures;auto& Scene=Role.Scene;const FString& Root=Role.Root;const bool HG=Role.HG;int32& Uncovered=Role.Uncovered;
        OkMeshView View{};if(!Actor->Session.PartAt(SourcePart.NativePart,View)||!View.render_visible||View.n_verts<=0)return 1;
        // OK Studio item state (AKKPreviewActor::ItemColorOverrides, ItemShadowColorOverride: sRGB, the values the game
        // writes to _Color.._Color4 and _ShadowColor) reaches the shared parent as the part's own material values.
        FKKRuntimeMaterialPart Part=SourcePart;
        if(Part.ItemDraw){
            static const TCHAR* ColorNames[]={TEXT("_Color"),TEXT("_Color2"),TEXT("_Color3"),TEXT("_Color4")};
            for(int32 Index=0;Index<4;++Index)
                if(Actor->ItemColorOverrides.IsValidIndex(Index)&&Actor->ItemColorOverrides[Index].A>=0.f){const FLinearColor& O=Actor->ItemColorOverrides[Index];Part.Overrides.Add(ColorNames[Index],FLinearColor(O.R,O.G,O.B,O.A));}
            if(Actor->ItemShadowColorOverride.A>=0.f){const FLinearColor& O=Actor->ItemShadowColorOverride;Part.Values.Add(TEXT("_ShadowColor"),FVector4f(O.R,O.G,O.B,1.f));Part.Overrides.Add(TEXT("_ShadowColor"),FLinearColor(O.R,O.G,O.B,1.f));}
            // A faded item blends: the blending parent (ForgeStudio rather than the depth-writing ForgeStudioOpaque); in the
            // editor its own graph draws it.
            Part.Values.Add(TEXT("_OKItemAlpha"),FVector4f(Actor->ItemAlpha,0.f,0.f,0.f));
#if WITH_EDITOR
            if(Actor->ItemAlpha<1.f){++Uncovered;return 1;}
#endif
        }
        const auto Recipe=OKRuntimeSharedMaterialRecipe(Part);const FString Name=OKRuntimeSharedMaterialParentName(Recipe);
        // OK_LOAD_PROFILE: the part's instances, and of that the shared parents loaded for it.
        FOkCardProfileScope Instance(GOkCardLoadProfile.bActive?&GOkCardLoadProfile.RoleInstanceSeconds:nullptr);
        auto LoadParent=[&](bool Outline){FOkCardProfileScope Load(GOkCardLoadProfile.bActive?&GOkCardLoadProfile.RoleParentSeconds:nullptr);return OKLoadRuntimeSharedMaterialParent(Root,Recipe,Outline);};
        auto FlatParent=[&](UMaterialInterface* Of){FOkCardProfileScope Load(GOkCardLoadProfile.bActive?&GOkCardLoadProfile.RoleParentSeconds:nullptr);return OKRuntimeFlatFaceParent(Of,true);};
        auto* Parent=Name.IsEmpty()?nullptr:LoadParent(false);
        if(!Parent&&Name.IsEmpty()){
#if WITH_EDITOR
            ++Uncovered;return 1; // no shared family: ApplyPartMaterial builds this part's own graph
#endif
        }
        // OK_SHARED_DUMP=<part name part>: every value the shared parent gets for that part (diagnostic).
        {static const FString Dump=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHARED_DUMP"));if(!Dump.IsEmpty()&&Part.Name.Contains(Dump)){FKKRuntimeMaterialBinding B;FString E;if(OKPrepareRuntimeSharedMaterialBinding(Part,Scene,B,E)){for(const auto& V:B.Vectors)UE_LOG(LogTemp,Display,TEXT("OK shared dump %s %s=%s"),*Part.Name,*V.Key.ToString(),*V.Value.ToString());for(const auto& V:B.Scalars)UE_LOG(LogTemp,Display,TEXT("OK shared dump %s %s=%g"),*Part.Name,*V.Key.ToString(),V.Value);for(const auto& T:B.Textures)UE_LOG(LogTemp,Display,TEXT("OK shared dump %s %s=texture %d source=%d avgBGRA=%s (%dx%d srgb=%d)"),*Part.Name,*T.Key.ToString(),T.Value,Data.Textures.IsValidIndex(T.Value)?Data.Textures[T.Value].SourceWidth:-1,Data.Textures.IsValidIndex(T.Value)&&!Data.Textures[T.Value].Mips.IsEmpty()&&Data.Textures[T.Value].Mips.Last().BGRA.Num()>=4?*FString::Printf(TEXT("%d,%d,%d,%d"),Data.Textures[T.Value].Mips.Last().BGRA[0],Data.Textures[T.Value].Mips.Last().BGRA[1],Data.Textures[T.Value].Mips.Last().BGRA[2],Data.Textures[T.Value].Mips.Last().BGRA[3]):TEXT("?"),Textures.IsValidIndex(T.Value)&&Textures[T.Value]?Textures[T.Value]->GetSizeX():0,Textures.IsValidIndex(T.Value)&&Textures[T.Value]?Textures[T.Value]->GetSizeY():0,Textures.IsValidIndex(T.Value)&&Textures[T.Value]?int32(Textures[T.Value]->SRGB):-1);}UE_LOG(LogTemp,Display,TEXT("OK shared dump %s recipe=%s outline=%d"),*Part.Name,*Name,Part.Outline?1:0);FKKRuntimeMaterialBinding OB;FString OE;if(Part.Outline&&OKPrepareRuntimeSharedMaterialBinding(Part,Scene,OB,OE,true)){for(const auto& V:OB.Scalars)UE_LOG(LogTemp,Display,TEXT("OK shared outline dump %s %s=%g"),*Part.Name,*V.Key.ToString(),V.Value);for(const auto& T:OB.Textures)UE_LOG(LogTemp,Display,TEXT("OK shared outline dump %s %s=texture %d"),*Part.Name,*T.Key.ToString(),T.Value);}else if(Part.Outline)UE_LOG(LogTemp,Display,TEXT("OK shared outline dump %s failed: %s"),*Part.Name,*OE);}}
        // The head (face, eyes, brows, nose, tears) pressed flat along the view, as in the street (ok.FaceFlatten).
        UMaterialInterface* Flat=Part.Role==TEXT("head")?FlatParent(Parent):nullptr;
        auto* Material=OKCreateRuntimeSharedMaterial(Part,Textures,Flat?Flat:Parent,Scene,Error);
        if(Material&&Flat)Actor->FlatFaceMaterials.Add(Material);
#if WITH_EDITOR
        if(!Material){UE_LOG(LogTemp,Warning,TEXT("Shared material for part %d (%s): %s; its own graph instead"),Part.NativePart,*Name,*Error);Error.Reset();++Uncovered;return 1;}
#endif
        if(!Material){Actor->CookedRoleMaterials.Reset();Actor->CookedRoleOutlines.Reset();return -1;}
        if(Actor->ToonRampTex){UTexture* Existing=nullptr;if(Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("_RampG")),Existing))Material->SetTextureParameterValue(TEXT("_RampG"),Actor->ToonRampTex);}
        // HG faces light from the character's front instead of their bumpy normals.
        if(HG&&Part.Role==TEXT("head")){float Face=0;if(Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("__OK_HG_Face")),Face))Material->SetScalarParameterValue(TEXT("__OK_HG_Face"),1);}
        // OK_SHARED_PARAM_DUMP=1 (debugging): an item instance's colour, shadow and light parameters.
        if(Part.ItemDraw&&FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHARED_PARAM_DUMP"))==TEXT("1")){FString Line;for(const TCHAR* N:{TEXT("_Color"),TEXT("_ShadowColor"),TEXT("_LightColor0"),TEXT("_WorldSpaceLightPos0"),TEXT("_ambientshadowG"),TEXT("_LineColorG")}){FLinearColor V;if(Material->GetVectorParameterValue(FMaterialParameterInfo(N),V))Line+=FString::Printf(TEXT(" %s=(%.3f,%.3f,%.3f,%.3f)"),N,V.R,V.G,V.B,V.A);}UE_LOG(LogTemp,Display,TEXT("OK shared item params part=%s parent=%s%s"),*Part.Name,*Name,*Line);}
        Actor->CookedRoleMaterials[Part.NativePart]=Material;
        if(Part.Outline&&OKRuntimeOutlineLayout(Recipe)){
            const FString OutlineName=Name+TEXT("_Outline");
            auto* OutlineParent=LoadParent(true);
            UMaterialInterface* OutlineFlat=Part.Role==TEXT("head")?FlatParent(OutlineParent):nullptr;
            auto* Outline=OKCreateRuntimeSharedMaterial(Part,Textures,OutlineFlat?OutlineFlat:OutlineParent,Scene,Error,true);
            if(Outline&&OutlineFlat)Actor->FlatFaceMaterials.Add(Outline);
#if WITH_EDITOR
            if(!Outline){UE_LOG(LogTemp,Warning,TEXT("Shared outline for part %d (%s): %s"),Part.NativePart,*OutlineName,*Error);Error.Reset();}
#else
            if(!Outline){Actor->CookedRoleMaterials.Reset();Actor->CookedRoleOutlines.Reset();return -1;}
#endif
            if(Outline&&Actor->ToonRampTex){UTexture* Existing=nullptr;if(Outline->GetTextureParameterValue(FMaterialParameterInfo(TEXT("_RampG")),Existing))Outline->SetTextureParameterValue(TEXT("_RampG"),Actor->ToonRampTex);}
            Actor->CookedRoleOutlines[Part.NativePart]=Outline;
        }
        return 1;
    };
    while(Role.NextPart<Data.Parts.Num()){
        if(BuildPart(Data.Parts[Role.NextPart++])<0)return Fail();
        if(Role.NextPart<Data.Parts.Num()&&FPlatformTime::Seconds()>=Deadline)return 0;
    }
    UE_LOG(LogTemp,Display,TEXT("High model cooked shared materials root=%s style=%s parts=%d own_graph=%d"),*Role.Root,Role.HG?TEXT("hg"):TEXT("koikatsu"),Actor->CookedRoleMaterials.Num(),Role.Uncovered);
    // A Studio item's instances are made and its textures uploaded: the decoded texture data is not needed again, and
    // a scene holds thousands of items (it stayed per actor: 28 -> 39 GB on a 1,900-item scene).
    if(Actor->bStudioItemShading)Actor->Session.RuntimeMaterials.Reset();
    Actor->CookedRoleBuild.Reset();
    return 1;
}
