// The card's parts as instances of the shared parents (KKRuntimeSharedMaterial), in every build: a packaged game
// loads the cooked parents and their shader library; the editor and -game load the same assets uncooked (their shaders
// compile once per machine and stay in the DDC), so a new card compiles nothing. In the editor a part no parent covers
// keeps its own transient graph (ApplyPartMaterial); a packaged game refuses such a card.
// Both paths consume the complete native material metadata and textures.
#if !WITH_EDITOR
#include "ShaderCodeLibrary.h"
#endif
static bool OKPrepareCookedRole(AKKPreviewActor* Actor,FString& Error){
    if(!Actor->CookedRoleMaterials.IsEmpty())return true;
    if(!Actor->Session.PrepareRuntimeMaterials(Error))return false;
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
    const auto& Data=*Actor->Session.RuntimeMaterials;
    FKKRuntimeMaterialScene Scene;Scene.EnableShadows=(Actor->bCharacterDisplayShadows||Actor->bStudioItemShading)&&!HG;OKApplyCharacterMakerLighting(Scene);Scene.Globals=Actor->StudioShaderGlobals;
    TArray<UTexture2D*> Textures;for(const auto& Prepared:Data.Textures){auto* Texture=OKCreateRuntimePreparedTexture(Prepared,Error);if(!Texture)return false;Textures.Add(Texture);Actor->LoadedTextures.Add(Texture);}
    Actor->CookedRoleMaterials.SetNum(Actor->Session.PartCount());Actor->CookedRoleOutlines.SetNum(Actor->Session.PartCount());
    int32 Uncovered=0;
    for(const auto& SourcePart:Data.Parts){
        OkMeshView View{};if(!Actor->Session.PartAt(SourcePart.NativePart,View)||!View.render_visible||View.n_verts<=0)continue;
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
            if(Actor->ItemAlpha<1.f){++Uncovered;continue;}
#endif
        }
        const auto Recipe=OKRuntimeSharedMaterialRecipe(Part);const FString Name=OKRuntimeSharedMaterialParentName(Recipe);
        auto* Parent=Name.IsEmpty()?nullptr:OKLoadRuntimeSharedMaterialParent(Root,Recipe);
        if(!Parent&&Name.IsEmpty()){
#if WITH_EDITOR
            ++Uncovered;continue; // no shared family: ApplyPartMaterial builds this part's own graph
#endif
        }
        // OK_SHARED_DUMP=<part name part>: every value the shared parent gets for that part (diagnostic).
        {static const FString Dump=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHARED_DUMP"));if(!Dump.IsEmpty()&&Part.Name.Contains(Dump)){FKKRuntimeMaterialBinding B;FString E;if(OKPrepareRuntimeSharedMaterialBinding(Part,Scene,B,E)){for(const auto& V:B.Vectors)UE_LOG(LogTemp,Display,TEXT("OK shared dump %s %s=%s"),*Part.Name,*V.Key.ToString(),*V.Value.ToString());for(const auto& V:B.Scalars)UE_LOG(LogTemp,Display,TEXT("OK shared dump %s %s=%g"),*Part.Name,*V.Key.ToString(),V.Value);for(const auto& T:B.Textures)UE_LOG(LogTemp,Display,TEXT("OK shared dump %s %s=texture %d source=%d avgBGRA=%s (%dx%d srgb=%d)"),*Part.Name,*T.Key.ToString(),T.Value,Data.Textures.IsValidIndex(T.Value)?Data.Textures[T.Value].SourceWidth:-1,Data.Textures.IsValidIndex(T.Value)&&!Data.Textures[T.Value].Mips.IsEmpty()&&Data.Textures[T.Value].Mips.Last().BGRA.Num()>=4?*FString::Printf(TEXT("%d,%d,%d,%d"),Data.Textures[T.Value].Mips.Last().BGRA[0],Data.Textures[T.Value].Mips.Last().BGRA[1],Data.Textures[T.Value].Mips.Last().BGRA[2],Data.Textures[T.Value].Mips.Last().BGRA[3]):TEXT("?"),Textures.IsValidIndex(T.Value)&&Textures[T.Value]?Textures[T.Value]->GetSizeX():0,Textures.IsValidIndex(T.Value)&&Textures[T.Value]?Textures[T.Value]->GetSizeY():0,Textures.IsValidIndex(T.Value)&&Textures[T.Value]?int32(Textures[T.Value]->SRGB):-1);}UE_LOG(LogTemp,Display,TEXT("OK shared dump %s recipe=%s outline=%d"),*Part.Name,*Name,Part.Outline?1:0);FKKRuntimeMaterialBinding OB;FString OE;if(Part.Outline&&OKPrepareRuntimeSharedMaterialBinding(Part,Scene,OB,OE,true)){for(const auto& V:OB.Scalars)UE_LOG(LogTemp,Display,TEXT("OK shared outline dump %s %s=%g"),*Part.Name,*V.Key.ToString(),V.Value);for(const auto& T:OB.Textures)UE_LOG(LogTemp,Display,TEXT("OK shared outline dump %s %s=texture %d"),*Part.Name,*T.Key.ToString(),T.Value);}else if(Part.Outline)UE_LOG(LogTemp,Display,TEXT("OK shared outline dump %s failed: %s"),*Part.Name,*OE);}}
        auto* Material=OKCreateRuntimeSharedMaterial(Part,Textures,Parent,Scene,Error);
#if WITH_EDITOR
        if(!Material){UE_LOG(LogTemp,Warning,TEXT("Shared material for part %d (%s): %s; its own graph instead"),Part.NativePart,*Name,*Error);Error.Reset();++Uncovered;continue;}
#endif
        if(!Material){Actor->CookedRoleMaterials.Reset();Actor->CookedRoleOutlines.Reset();return false;}
        if(Actor->ToonRampTex){UTexture* Existing=nullptr;if(Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("_RampG")),Existing))Material->SetTextureParameterValue(TEXT("_RampG"),Actor->ToonRampTex);}
        // HG faces light from the character's front instead of their bumpy normals.
        if(HG&&Part.Role==TEXT("head")){float Face=0;if(Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("__OK_HG_Face")),Face))Material->SetScalarParameterValue(TEXT("__OK_HG_Face"),1);}
        // OK_SHARED_PARAM_DUMP=1 (debugging): an item instance's colour, shadow and light parameters.
        if(Part.ItemDraw&&FPlatformMisc::GetEnvironmentVariable(TEXT("OK_SHARED_PARAM_DUMP"))==TEXT("1")){FString Line;for(const TCHAR* N:{TEXT("_Color"),TEXT("_ShadowColor"),TEXT("_LightColor0"),TEXT("_WorldSpaceLightPos0"),TEXT("_ambientshadowG"),TEXT("_LineColorG")}){FLinearColor V;if(Material->GetVectorParameterValue(FMaterialParameterInfo(N),V))Line+=FString::Printf(TEXT(" %s=(%.3f,%.3f,%.3f,%.3f)"),N,V.R,V.G,V.B,V.A);}UE_LOG(LogTemp,Display,TEXT("OK shared item params part=%s parent=%s%s"),*Part.Name,*Name,*Line);}
        Actor->CookedRoleMaterials[Part.NativePart]=Material;
        if(Part.Outline&&OKRuntimeOutlineLayout(Recipe)){
            const FString OutlineName=Name+TEXT("_Outline");
            auto* OutlineParent=OKLoadRuntimeSharedMaterialParent(Root,Recipe,true);
            auto* Outline=OKCreateRuntimeSharedMaterial(Part,Textures,OutlineParent,Scene,Error,true);
#if WITH_EDITOR
            if(!Outline){UE_LOG(LogTemp,Warning,TEXT("Shared outline for part %d (%s): %s"),Part.NativePart,*OutlineName,*Error);Error.Reset();}
#else
            if(!Outline){Actor->CookedRoleMaterials.Reset();Actor->CookedRoleOutlines.Reset();return false;}
#endif
            if(Outline&&Actor->ToonRampTex){UTexture* Existing=nullptr;if(Outline->GetTextureParameterValue(FMaterialParameterInfo(TEXT("_RampG")),Existing))Outline->SetTextureParameterValue(TEXT("_RampG"),Actor->ToonRampTex);}
            Actor->CookedRoleOutlines[Part.NativePart]=Outline;
        }
    }
    UE_LOG(LogTemp,Display,TEXT("High model cooked shared materials root=%s style=%s parts=%d own_graph=%d"),*Root,HG?TEXT("hg"):TEXT("koikatsu"),Actor->CookedRoleMaterials.Num(),Uncovered);
    // A Studio item's instances are made and its textures uploaded: the decoded texture data is not needed again, and
    // a scene holds thousands of items (it stayed per actor: 28 -> 39 GB on a 1,900-item scene).
    if(Actor->bStudioItemShading)Actor->Session.RuntimeMaterials.Reset();
    return true;
}
