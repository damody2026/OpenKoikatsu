#include "OKCharacterMCPRegistry.h"
#include "OKCharacterController.h"
#include "OKShapeSlider.h"
#include "OKShapeDefaults.h"
#include "OKBoneControls.h"
#include "OKPropertyInput.h"
#include "KKPreviewActor.h"
#include "ok_extract.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

namespace OKCharacterMCPRegistry
{
using FObject=TSharedPtr<FJsonObject>;
static const TCHAR* AxisName(int32 Axis)
{
 static const TCHAR* Names[]={TEXT("scale.x"),TEXT("scale.y"),TEXT("scale.z"),TEXT("length"),TEXT("position.x"),TEXT("position.y"),TEXT("position.z"),TEXT("rotation.x"),TEXT("rotation.y"),TEXT("rotation.z")};
 return Axis<0?TEXT("scale.uniform"):Names[Axis];
}
static FObject Number(const FString& Key,const FString& Label,double Value,double Min,double Max,double Default)
{
 auto P=MakeShared<FJsonObject>();P->SetStringField(TEXT("key"),Key);P->SetStringField(TEXT("label"),Label.IsEmpty()?Key:Label);P->SetStringField(TEXT("kind"),TEXT("number"));
 P->SetNumberField(TEXT("value"),Value);P->SetNumberField(TEXT("min"),Min);P->SetNumberField(TEXT("max"),Max);P->SetNumberField(TEXT("default"),Default);P->SetBoolField(TEXT("supported"),true);P->SetBoolField(TEXT("requires_apply"),false);return P;
}
static FString Label(UWidget* W)
{
 if(auto* C=Cast<UCheckBox>(W))if(auto* T=Cast<UTextBlock>(C->GetContent()))return T->GetText().ToString();
 if(auto* Parent=W->GetParent())if(Parent->GetChildrenCount()>0)if(auto* Box=Cast<USizeBox>(Parent->GetChildAt(0)))if(auto* T=Cast<UTextBlock>(Box->GetContent()))return T->GetText().ToString();
 return W->GetToolTipText().ToString();
}
static FObject Mods(UOKCharacterController* G)
{
 FObject O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(G->EditorModsJson()),O);return O;
}
static FObject BoneData(FObject M,const FString& Name,int32 Outfit)
{
 const FRows* Bones=nullptr;if(!M||!M->TryGetArrayField(TEXT("abm"),Bones))return nullptr;
 for(auto B:*Bones){auto O=B->AsObject();if(O->GetStringField(TEXT("name"))!=Name||O->GetIntegerField(TEXT("location"))>=10)continue;
  const auto& C=O->GetArrayField(TEXT("coord"));const int32 Index=C.Num()==1?0:Outfit;return C.IsValidIndex(Index)?C[Index]->AsObject():nullptr;}
 return nullptr;
}
static double BoneValue(FObject Data,int32 Axis)
{
 if(!Data)return Axis>=4?0:1;if(Axis==3)return Data->GetNumberField(TEXT("length"));
 const auto& V=Data->GetArrayField(Axis>=7?TEXT("rot"):Axis>=4?TEXT("pos"):TEXT("scale"));return V[Axis<0?0:Axis>=4?(Axis-4)%3:Axis]->AsNumber();
}
FRows Parameters(UOKCharacterController* G)
{
 FRows Rows;auto Add=[&](FObject P){Rows.Add(MakeShared<FJsonValueObject>(P));};
 for(bool Body:{true,false}){const auto& Values=Body?G->Selected.Body.ShapeValueBody:G->Selected.Face.ShapeValueFace;
  for(int32 I=0;I<(Body?44:52);++I){FString L;for(auto S:G->ShapeSliders)if(S&&S->bBody==Body&&S->Index==I){L=Label(S);break;}
   auto P=Number(FString::Printf(TEXT("shape.%s.%d"),Body?TEXT("body"):TEXT("face"),I),L,Values.IsValidIndex(I)?Values[I]:.5,-1,2,OKShapeDefaults::Value(G->Selected.Sex,Body,I));P->SetStringField(TEXT("category"),Body?TEXT("body"):TEXT("face"));Add(P);}
 }
 const auto M=Mods(G);TSet<FString> Available;
 if(G->PreviewActor&&G->PreviewActor->Session.IsOpen())for(int32 I=0;I<G->PreviewActor->Session.SkelBoneCount();++I){OkBoneView V{};if(G->PreviewActor->Session.SkelBoneAt(I,V)&&V.name)Available.Add(UTF8_TO_TCHAR(V.name));}
 TSet<FString> Seen;
 for(const auto& Def:OKBoneControls)for(const FString Name:{FString(Def.Bone),FString(Def.Right)}){
  if(Name.IsEmpty())continue;const FString Key=TEXT("bone.")+Name+TEXT(".")+AxisName(Def.Axis);if(Seen.Contains(Key))continue;Seen.Add(Key);
  auto P=Number(Key,Def.Label,BoneValue(BoneData(M,Name,G->OutfitIndex),Def.Axis),Def.Min,Def.Max,Def.Axis>=4?0:1);
  P->SetStringField(TEXT("category"),Def.Category);P->SetStringField(TEXT("bone"),Name);P->SetNumberField(TEXT("axis"),Def.Axis);P->SetBoolField(TEXT("coordinate_specific"),FString(Def.Category)==TEXT("BodySkirtScl"));
  P->SetBoolField(TEXT("supported"),M.IsValid()&&Available.Contains(Name));P->SetStringField(TEXT("unit"),Def.Axis>=7?TEXT("degrees"):Def.Axis>=4?TEXT("source_units"):TEXT("multiplier"));
  if(!Available.Contains(Name))P->SetStringField(TEXT("unsupported_reason"),TEXT("Bone absent from current character skeleton"));Add(P);
 }
 for(auto S:G->BoneSliders)if(S&&(!S->BodyParameter.IsEmpty()||!S->PregnancyParameter.IsEmpty())){
  double V=0;FString Key;
  if(!S->BodyParameter.IsEmpty()){Key=TEXT("physics.")+S->BodyParameter;V=S->BodyParameter==TEXT("bustSoftness")?G->Selected.Body.BustSoftness:G->Selected.Body.BustWeight;}
  else{Key=TEXT("pregnancy.")+S->PregnancyParameter;const FObject* Preg=nullptr;if(M&&M->TryGetObjectField(TEXT("pregnancy"),Preg))(*Preg)->TryGetNumberField(S->PregnancyParameter,V);}
  auto P=Number(Key,Label(S),V,S->GetMinValue(),S->GetMaxValue(),S->BodyParameter.IsEmpty()?0:.5);P->SetBoolField(TEXT("supported"),!S->BodyParameter.IsEmpty()||M.IsValid());Add(P);
 }
 for(auto P:G->EditorProperties){auto Copy=MakeShared<FJsonObject>(*P);const FString Key=P->GetStringField(TEXT("key"));
  if(auto* Pending=G->PendingProperties.Find(Key))Copy->SetField(TEXT("value"),*Pending);
  const FString Kind=P->GetStringField(TEXT("kind"));if(Kind!=TEXT("bool")&&Kind!=TEXT("parent")){Copy->SetNumberField(TEXT("min"),Kind==TEXT("color")?0:-1000000);Copy->SetNumberField(TEXT("max"),Kind==TEXT("color")?1:1000000);}
  Copy->SetBoolField(TEXT("supported"),true);Copy->SetBoolField(TEXT("requires_apply"),true);Copy->SetStringField(TEXT("category"),TEXT("properties"));Add(Copy);
 }
 return Rows;
}
bool Validate(const FObject& P,const TSharedPtr<FJsonValue>& V,FString& Error)
{
 bool Supported=true;P->TryGetBoolField(TEXT("supported"),Supported);if(!Supported){Error=TEXT("Parameter unsupported by current model");return false;}
 auto Base=P->TryGetField(TEXT("value"));if(!V||!Base||V->Type!=Base->Type){Error=TEXT("Wrong value type");return false;}
 if(V->Type==EJson::Number){double Min=-1000000,Max=1000000;P->TryGetNumberField(TEXT("min"),Min);P->TryGetNumberField(TEXT("max"),Max);const double N=V->AsNumber();if(!FMath::IsFinite(N)||N<Min||N>Max){Error=TEXT("Value outside allowed range");return false;}}
 if(V->Type==EJson::String){if(V->AsString().Len()>4096){Error=TEXT("String too long");return false;}const FRows* Choices=nullptr;if(P->TryGetArrayField(TEXT("choices"),Choices)&&!Choices->ContainsByPredicate([&](auto C){return C->AsString()==V->AsString();})){Error=TEXT("Value absent from choices");return false;}}
 return true;
}
static FObject NewBoneData()
{
 auto O=MakeShared<FJsonObject>();FRows Scale,Zero;for(int32 I=0;I<3;++I){Scale.Add(MakeShared<FJsonValueNumber>(1));Zero.Add(MakeShared<FJsonValueNumber>(0));}
 O->SetArrayField(TEXT("scale"),Scale);O->SetArrayField(TEXT("pos"),Zero);O->SetArrayField(TEXT("rot"),Zero);O->SetNumberField(TEXT("length"),1);return O;
}
bool SetParameters(UOKCharacterController* G,const FRows& Edits,FString& Error)
{
 if(Edits.IsEmpty()||Edits.Num()>256){Error=TEXT("edits must contain 1..256 entries");return false;}
 TMap<FString,FObject> Catalog;for(auto P:Parameters(G))Catalog.Add(P->AsObject()->GetStringField(TEXT("key")),P->AsObject());TSet<FString> Seen;
 for(auto E:Edits){const FObject* O=nullptr;if(!E->TryGetObject(O)||!O||!O->IsValid()){Error=TEXT("Invalid edit object");return false;}FString Key;(*O)->TryGetStringField(TEXT("key"),Key);auto* P=Catalog.Find(Key);
  if(!P||Seen.Contains(Key)){Error=TEXT("Unknown or duplicate key: ")+Key;return false;}Seen.Add(Key);if(!Validate(*P,(*O)->TryGetField(TEXT("value")),Error)){Error+=TEXT(": ")+Key;return false;}
 }
 // Stage all mod changes into a private decoded document. Commit once, before
 // touching body/face or pending property values, so encoding failures are atomic.
 auto M=Mods(G);bool ChangedMods=false;
 for(auto E:Edits){auto O=E->AsObject();const FString Key=O->GetStringField(TEXT("key"));auto P=Catalog[Key];const auto V=O->TryGetField(TEXT("value"));
  if(Key.StartsWith(TEXT("bone."))){if(!M){Error=TEXT("Cannot decode bone modifiers");return false;}auto Bones=M->GetArrayField(TEXT("abm"));const FString Name=P->GetStringField(TEXT("bone"));const int32 Axis=P->GetIntegerField(TEXT("axis"));FObject Bone;
   for(auto B:Bones)if(B->AsObject()->GetStringField(TEXT("name"))==Name&&B->AsObject()->GetIntegerField(TEXT("location"))<10){Bone=B->AsObject();break;}
   if(!Bone){Bone=MakeShared<FJsonObject>();Bone->SetStringField(TEXT("name"),Name);Bone->SetNumberField(TEXT("location"),1);Bone->SetArrayField(TEXT("coord"),{MakeShared<FJsonValueObject>(NewBoneData())});Bones.Add(MakeShared<FJsonValueObject>(Bone));}
   auto C=Bone->GetArrayField(TEXT("coord"));if(P->GetBoolField(TEXT("coordinate_specific"))&&C.Num()==1){const FString Initial=[](auto Data){FString T;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&T));return T;}(C[0]->AsObject().ToSharedRef());while(C.Num()<FMath::Max(7,G->OutfitIndex+1)){FObject Copy;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Initial),Copy);C.Add(MakeShared<FJsonValueObject>(Copy));}}
   const int32 Index=C.Num()==1?0:G->OutfitIndex;while(C.Num()<=Index)C.Add(MakeShared<FJsonValueObject>(NewBoneData()));auto D=C[Index]->AsObject();
   if(Axis==3)D->SetField(TEXT("length"),V);else{const TCHAR* Field=Axis>=7?TEXT("rot"):Axis>=4?TEXT("pos"):TEXT("scale");auto A=D->GetArrayField(Field);if(Axis<0)for(auto& Item:A)Item=V;else A[Axis>=4?(Axis-4)%3:Axis]=V;D->SetArrayField(Field,A);}
   Bone->SetArrayField(TEXT("coord"),C);M->SetArrayField(TEXT("abm"),Bones);ChangedMods=true;
  }else if(Key.StartsWith(TEXT("pregnancy."))){if(!M){Error=TEXT("Cannot decode pregnancy modifiers");return false;}const FObject* Preg=nullptr;if(!M->TryGetObjectField(TEXT("pregnancy"),Preg)){Error=TEXT("Pregnancy modifier unavailable");return false;}(*Preg)->SetField(Key.Mid(10),V);ChangedMods=true;}
 }
 if(ChangedMods&&!G->CommitEditorMods(M)){Error=TEXT("Modifier encoding failed; batch was not applied");return false;}
 for(auto E:Edits){auto O=E->AsObject();const FString Key=O->GetStringField(TEXT("key"));const auto V=O->TryGetField(TEXT("value"));
  if(Key.StartsWith(TEXT("shape."))){TArray<FString> Parts;Key.ParseIntoArray(Parts,TEXT("."));G->OnShapeChanged(Parts[1]==TEXT("body"),FCString::Atoi(*Parts[2]),V->AsNumber());}
  else if(Key.StartsWith(TEXT("physics.")))G->OnBodyPhysicsChanged(Key.Mid(8),V->AsNumber());
  else if(!Key.StartsWith(TEXT("bone."))&&!Key.StartsWith(TEXT("pregnancy.")))G->SetEditorProperty(Key,V);
 }
 G->ApplyPendingEditorMods();G->SyncShapeSliders();G->RebuildPropertyControls();return true;
}
FRows Controls(UOKCharacterController* G,TMap<FString,UWidget*>* Widgets)
{
 TMap<UWidget*,FString> Names;
 // Controller UPROPERTY names give stable identities independent of widget order.
 for(TFieldIterator<FProperty> It(G->GetClass());It;++It){FProperty* P=*It;
  if(auto* O=CastField<FObjectPropertyBase>(P)){if(auto* W=Cast<UWidget>(O->GetObjectPropertyValue_InContainer(G)))Names.Add(W,TEXT("control.")+P->GetName());}
  else if(auto* A=CastField<FArrayProperty>(P))if(auto* Inner=CastField<FObjectPropertyBase>(A->Inner)){FScriptArrayHelper H(A,A->ContainerPtrToValuePtr<void>(G));for(int32 I=0;I<H.Num();++I)if(auto* W=Cast<UWidget>(Inner->GetObjectPropertyValue(H.GetRawPtr(I))))Names.Add(W,FString::Printf(TEXT("control.%s.%d"),*P->GetName(),I));}
 }
 TArray<UWidget*> All;G->WidgetTree->GetAllWidgets(All);for(auto E:Names)All.AddUnique(E.Key);
 FRows Rows;TSet<FString> Used;
 for(UWidget* W:All){if(!IsValid(W)||W->GetTypedOuter<UOKCharacterController>()!=G)continue;
  // Detailed property rows and bone UI are represented by semantic parameters.
  if(Cast<UOKPropertyInput>(W)||Cast<UOKPropertyParent>(W)||Cast<UOKPropertyToggle>(W)||Cast<UOKPropertyParentChoice>(W))continue;
  if(auto* S=Cast<UOKShapeSlider>(W))if(S->AnimationParameter.IsEmpty())continue;
  if(G->BoneSliders.ContainsByPredicate([W](auto S){return S&&S->ModSide==W;}))continue;
  FString Key=Names.FindRef(W);if(auto* S=Cast<UOKShapeSlider>(W))Key=TEXT("animation.")+S->AnimationParameter;
  bool Stable=!Key.IsEmpty();if(Key.IsEmpty())Key=TEXT("widget.")+W->GetName();
  FObject P;bool Bound=false;
  if(auto* S=Cast<USlider>(W)){P=Number(Key,Label(W),S->GetValue(),S->GetMinValue(),S->GetMaxValue(),S->GetValue());P->RemoveField(TEXT("default"));Bound=S->OnValueChanged.IsBound();}
  else if(auto* C=Cast<UComboBoxString>(W)){P=MakeShared<FJsonObject>();P->SetStringField(TEXT("value"),C->GetSelectedOption());P->SetStringField(TEXT("kind"),TEXT("choice"));FRows Choices;for(int32 I=0;I<C->GetOptionCount();++I)Choices.Add(MakeShared<FJsonValueString>(C->GetOptionAtIndex(I)));P->SetArrayField(TEXT("choices"),Choices);Bound=C->OnSelectionChanged.IsBound();}
  else if(auto* Check=Cast<UCheckBox>(W)){P=MakeShared<FJsonObject>();P->SetBoolField(TEXT("value"),Check->IsChecked());P->SetStringField(TEXT("kind"),TEXT("bool"));Bound=Check->OnCheckStateChanged.IsBound();}
  else if(auto* T=Cast<UEditableTextBox>(W)){P=MakeShared<FJsonObject>();P->SetStringField(TEXT("value"),T->GetText().ToString());P->SetStringField(TEXT("kind"),TEXT("text"));Bound=true;}
  else if(auto* Spin=Cast<USpinBox>(W)){P=Number(Key,Label(W),Spin->GetValue(),Spin->GetMinValue(),Spin->GetMaxValue(),Spin->GetValue());P->RemoveField(TEXT("default"));Bound=Spin->OnValueChanged.IsBound()||Spin->OnValueCommitted.IsBound();}
  if(!P||Used.Contains(Key))continue;Used.Add(Key);P->SetStringField(TEXT("key"),Key);P->SetStringField(TEXT("label"),Label(W).IsEmpty()?Key:Label(W));P->SetStringField(TEXT("class"),W->GetClass()->GetName());P->SetBoolField(TEXT("stable_key"),Stable);P->SetBoolField(TEXT("supported"),Bound&&W->GetIsEnabled());P->SetBoolField(TEXT("visible"),W->IsVisible());P->SetStringField(TEXT("persistence"),TEXT("editor_callback"));Rows.Add(MakeShared<FJsonValueObject>(P));if(Widgets)Widgets->Add(Key,W);
 }
 Rows.Sort([](auto A,auto B){return A->AsObject()->GetStringField(TEXT("key"))<B->AsObject()->GetStringField(TEXT("key"));});return Rows;
}
bool SetControl(UOKCharacterController* G,const FString& Key,TSharedPtr<FJsonValue> Value,FString& Error)
{
 TMap<FString,UWidget*> Widgets;const auto Rows=Controls(G,&Widgets);FObject P;for(auto R:Rows)if(R->AsObject()->GetStringField(TEXT("key"))==Key){P=R->AsObject();break;}
 if(!P){Error=TEXT("Unknown editor control");return false;}if(!Validate(P,Value,Error))return false;UWidget* W=Widgets[Key];
 if(auto* S=Cast<USlider>(W)){S->SetValue(Value->AsNumber());S->OnValueChanged.Broadcast(Value->AsNumber());}
 else if(auto* C=Cast<UComboBoxString>(W)){auto Changed=C->OnSelectionChanged;C->OnSelectionChanged.Clear();C->SetSelectedOption(Value->AsString());C->OnSelectionChanged=Changed;C->OnSelectionChanged.Broadcast(Value->AsString(),ESelectInfo::OnMouseClick);}
 else if(auto* Check=Cast<UCheckBox>(W)){Check->SetIsChecked(Value->AsBool());Check->OnCheckStateChanged.Broadcast(Value->AsBool());}
 else if(auto* T=Cast<UEditableTextBox>(W)){auto Changed=T->OnTextChanged;T->OnTextChanged.Clear();T->SetText(FText::FromString(Value->AsString()));T->OnTextChanged=Changed;T->OnTextChanged.Broadcast(T->GetText());T->OnTextCommitted.Broadcast(T->GetText(),ETextCommit::OnEnter);}
 else if(auto* Spin=Cast<USpinBox>(W)){Spin->SetValue(Value->AsNumber());Spin->OnValueChanged.Broadcast(Value->AsNumber());Spin->OnValueCommitted.Broadcast(Value->AsNumber(),ETextCommit::OnEnter);}
 G->ApplyPendingEditorMods();return true;
}
}
