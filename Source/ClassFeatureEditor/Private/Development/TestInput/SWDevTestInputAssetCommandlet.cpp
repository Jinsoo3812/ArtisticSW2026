#include "Development/TestInput/SWDevTestInputAssetCommandlet.h"
#include "Development/TestInput/SWDevTestInputComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"
#include "InputCoreTypes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"

namespace SWDevTestInput
{
const TCHAR* AssetNames[]={TEXT("IA_DevTest_KillSelf"),TEXT("IA_DevTest_KillBoth"),TEXT("IA_DevTest_SinkPlayerShip"),TEXT("IA_DevTest_EnterFinalEncounter"),TEXT("IA_DevTest_ModifierCtrl"),TEXT("IA_DevTest_ModifierAlt"),TEXT("IMC_DevTest")};
FString Package(int32 Index) { return FString(TEXT("/Game/Developer/Testing/Input/"))+(Index<6 ? TEXT("Actions/"):TEXT(""))+AssetNames[Index]; }
}
USWDevTestInputAssetCommandlet::USWDevTestInputAssetCommandlet() { IsClient=false; IsServer=false; IsEditor=true; LogToConsole=true; }
int32 USWDevTestInputAssetCommandlet::Main(const FString& Params)
{
 const bool bVerify=FParse::Param(*Params,TEXT("VerifyOnly")), bPreserve=FParse::Param(*Params,TEXT("PreserveMainKeys"));
 TArray<TObjectPtr<UObject>> Objects; TArray<TObjectPtr<UInputAction>> Actions;
 for (int32 Index=0;Index<7;++Index)
 {
  const FString Path=SWDevTestInput::Package(Index)+TEXT(".")+SWDevTestInput::AssetNames[Index];
  UObject* Object=LoadObject<UObject>(nullptr,*Path);
  if ((Object && Object->GetClass()!=(Index<6 ? UInputAction::StaticClass():UInputMappingContext::StaticClass())) || (bVerify && !Object)) { UE_LOG(LogTemp,Error,TEXT("Preflight failed %s"),*Path); return 1; }
  Objects.Add(Object);
 }
 FKey Keys[]={EKeys::F6,EKeys::F7,EKeys::F8,EKeys::F9};
 if (bPreserve)
 {
  UInputMappingContext* Existing=Cast<UInputMappingContext>(Objects[6]); if (!Existing) return 1;
  for (int32 Index=0;Index<4;++Index)
  { int32 Count=0; for (const auto& Map:Existing->GetMappings()) if (Map.Action==Objects[Index]) { Keys[Index]=Map.Key; ++Count; } if (Count!=1 || !Keys[Index].IsValid()) return 1; }
 }
 for (int32 Index=0;Index<7;++Index) if (!Objects[Index])
 {
  UPackage* Package=CreatePackage(*SWDevTestInput::Package(Index));
  Objects[Index]=NewObject<UObject>(Package,Index<6 ? UInputAction::StaticClass():UInputMappingContext::StaticClass(),SWDevTestInput::AssetNames[Index],RF_Public|RF_Standalone);
  FAssetRegistryModule::AssetCreated(Objects[Index]); Objects[Index]->MarkPackageDirty();
 }
 for (int32 Index=0;Index<6;++Index) Actions.Add(CastChecked<UInputAction>(Objects[Index]));
 UInputMappingContext* Context=CastChecked<UInputMappingContext>(Objects[6]);
 FString Error; const bool bValid=USWDevTestInputComponent::ValidateAssets(Context,Actions,Error);
 bool bKeysMatch=true; for (int32 Index=0;Index<4;++Index) for (const auto& Map:Context->GetMappings()) if (Map.Action==Actions[Index]) { UE_LOG(LogTemp,Display,TEXT("MainKey %s=%s"),SWDevTestInput::AssetNames[Index],*Map.Key.ToString()); bKeysMatch &= Map.Key==Keys[Index]; }
 if (bVerify) { UE_LOG(LogTemp,Display,TEXT("VerifyOnly Valid=%d DefaultKeys=%d Assets=7"),bValid,bKeysMatch); return bValid ? 0:1; }
 UE_LOG(LogTemp,Display,TEXT("Main key policy=%s"),bPreserve ? TEXT("PreserveMainKeys"):TEXT("Reset F6-F9"));
 int32 Changed=0;
 if (!bValid || !bKeysMatch)
 {
  for (int32 Index=0;Index<6;++Index)
  {
   UInputAction* Action=Actions[Index];
   const bool bActionChanged=Action->ValueType!=EInputActionValueType::Boolean || Action->bTriggerWhenPaused || Action->bConsumesActionAndAxisMappings || Action->bConsumeInput!=(Index<4)
    || Action->AccumulationBehavior!=EInputActionAccumulationBehavior::TakeHighestAbsoluteValue || !Action->Modifiers.IsEmpty() || !Action->Triggers.IsEmpty();
   Action->ValueType=EInputActionValueType::Boolean; Action->bTriggerWhenPaused=false; Action->bConsumesActionAndAxisMappings=false; Action->bConsumeInput=Index<4;
   Action->AccumulationBehavior=EInputActionAccumulationBehavior::TakeHighestAbsoluteValue; Action->Modifiers.Reset(); Action->Triggers.Reset(); if (bActionChanged) Action->MarkPackageDirty();
  }
  Context->UnmapAll();
  for (int32 Index=0;Index<8;++Index)
  {
   const int32 ActionIndex=Index<4 ? Index : (Index<6 ? 4:5);
   const FKey Key=Index<4 ? Keys[Index] : (Index==4 ? EKeys::LeftControl:Index==5 ? EKeys::RightControl:Index==6 ? EKeys::LeftAlt:EKeys::RightAlt);
   FEnhancedActionKeyMapping& Map=Context->MapKey(Actions[ActionIndex],Key);
   UInputTrigger* Trigger=Index<4 ? static_cast<UInputTrigger*>(NewObject<UInputTriggerPressed>(Context)) : NewObject<UInputTriggerDown>(Context);
   Trigger->ActuationThreshold=0.5f; Map.Triggers.Add(Trigger);
   if (Index<4) for (int32 Modifier=4;Modifier<6;++Modifier)
   { UInputTriggerChordAction* Chord=NewObject<UInputTriggerChordAction>(Context); Chord->ActuationThreshold=0.5f; Chord->ChordAction=Actions[Modifier]; Map.Triggers.Add(Chord); }
  }
  Context->MarkPackageDirty();
 }
 if (!USWDevTestInputComponent::ValidateAssets(Context,Actions,Error)) { UE_LOG(LogTemp,Error,TEXT("%s"),*Error); return 1; }
 for (int32 Index=0;Index<7;++Index) if (Objects[Index]->GetOutermost()->IsDirty())
 {
  const FString Filename=FPackageName::LongPackageNameToFilename(SWDevTestInput::Package(Index),FPackageName::GetAssetPackageExtension());
  IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
  FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
  if (!UPackage::SavePackage(Objects[Index]->GetOutermost(),Objects[Index],*Filename,Args)) { UE_LOG(LogTemp,Error,TEXT("Partial save failed %s Saved=%d"),*Filename,Changed); return 1; }
  ++Changed; UE_LOG(LogTemp,Display,TEXT("Saved %s"),*Filename);
 }
 UE_LOG(LogTemp,Display,TEXT("Assets=7 Changed=%d"),Changed); return 0;
}
