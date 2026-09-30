#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Development/TestInput/SWDevTestInputComponent.h"
#include "EnhancedPlayerInput.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystemInterface.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/StrongObjectPtr.h"

namespace SWDevTestInputTests
{
const TCHAR* Names[]={TEXT("KillSelf"),TEXT("KillBoth"),TEXT("SinkPlayerShip"),TEXT("EnterFinalEncounter"),TEXT("ModifierCtrl"),TEXT("ModifierAlt")};
struct FInputHarness : IEnhancedInputSubsystemInterface
{
 UEnhancedPlayerInput* PlayerInput=nullptr;
 TMap<TObjectPtr<const UInputAction>,FInjectedInput> Injected;
 virtual UEnhancedPlayerInput* GetPlayerInput() const override { return PlayerInput; }
 virtual TMap<TObjectPtr<const UInputAction>,FInjectedInput>& GetContinuouslyInjectedInputs() override { return Injected; }
 void RebuildNow() { FModifyContextOptions Options; Options.bForceImmediately=true; Options.bIgnoreAllPressedKeysUntilRelease=false; Options.bNotifyUserSettings=false; RequestRebuildControlMappings(Options); }
};
struct FFixture
{
 TStrongObjectPtr<UWorld> World;
 TStrongObjectPtr<UInputMappingContext> Saved;
 TArray<TStrongObjectPtr<UInputAction>> StrongActions;
 TArray<TObjectPtr<UInputAction>> Actions;
 APlayerController* Controller=nullptr;
 UEnhancedInputComponent* Input=nullptr;
 FInputHarness Harness;
 int32 Counts[4]={0,0,0,0};
 // Input processing needs a persistent level and controller, not gameplay world subsystems.
 FFixture() : World(UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,false,ERHIFeatureLevel::Num,nullptr,true)), Saved(LoadObject<UInputMappingContext>(nullptr,TEXT("/Game/Developer/Testing/Input/IMC_DevTest.IMC_DevTest")))
 {
  Controller=World->SpawnActor<APlayerController>();
  Harness.PlayerInput=NewObject<UEnhancedPlayerInput>(Controller); Controller->PlayerInput=Harness.PlayerInput;
  Input=NewObject<UEnhancedInputComponent>(Controller);
  Controller->AddInstanceComponent(Input);
  for (int32 Index=0;Index<6;++Index)
  {
   FString Name=FString(TEXT("IA_DevTest_"))+Names[Index];
   UInputAction* Action=LoadObject<UInputAction>(nullptr,*(TEXT("/Game/Developer/Testing/Input/Actions/")+Name+TEXT(".")+Name));
   StrongActions.Emplace(Action); Actions.Add(Action);
   if (Index<4 && Action) Input->BindActionValueLambda(Action,ETriggerEvent::Triggered,[this,Index](const FInputActionValue&){ ++Counts[Index]; });
  }
 }
 ~FFixture() { Controller->PlayerInput=nullptr; World->DestroyWorld(false); }
 void Frame() { TArray<UInputComponent*> Stack{Input}; Harness.PlayerInput->ProcessInputStack(Stack,1.f/60.f,false); }
 void Key(FKey Key,EInputEvent Event) { Harness.PlayerInput->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,Key,Event,Event==IE_Released ? 0.f:1.f,false,FPlatformTime::Cycles64())); }
 void SetContext(UInputMappingContext* Context,bool bOn,int32 Priority=100)
 {
  FModifyContextOptions Options; Options.bForceImmediately=true; Options.bIgnoreAllPressedKeysUntilRelease=false; Options.bNotifyUserSettings=false;
  if (bOn) Harness.AddMappingContext(Context,Priority,Options); else Harness.RemoveMappingContext(Context,Options);
  Harness.RebuildNow(); Frame();
 }
 void Release()
 {
  for (FKey Key:{EKeys::LeftControl,EKeys::RightControl,EKeys::LeftAlt,EKeys::RightAlt,EKeys::F6,EKeys::F7,EKeys::F8,EKeys::F9,EKeys::F10}) this->Key(Key,IE_Released);
  Frame(); Frame();
 }
 void Chord(FKey Main,FKey Ctrl=EKeys::LeftControl,FKey Alt=EKeys::LeftAlt)
 { Release(); Key(Ctrl,IE_Pressed); Key(Alt,IE_Pressed); Frame(); Key(Main,IE_Pressed); Frame(); Release(); }
 bool Valid(FAutomationTestBase& Test)
 { FString Error; return Test.TestTrue(TEXT("Saved asset contract"),USWDevTestInputComponent::ValidateAssets(Saved.Get(),Actions,Error)); }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWDevAssets,"ArtisticSW.Development.TestInput.Assets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSWDevAssets::RunTest(const FString& Parameters)
{
 SWDevTestInputTests::FFixture Fixture; return Fixture.Valid(*this);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWDevChords,"ArtisticSW.Development.TestInput.KeyChords",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSWDevChords::RunTest(const FString& Parameters)
{
 using namespace SWDevTestInputTests;
 FFixture F; if (!F.Valid(*this)) return false;
 FKey Keys[]={EKeys::F6,EKeys::F7,EKeys::F8,EKeys::F9};
 for (int32 I=0;I<4;++I) F.Chord(Keys[I]);
 for (int32 I=0;I<4;++I) TestEqual(TEXT("K01 no IMC"),F.Counts[I],0);
 F.SetContext(F.Saved.Get(),true);
 for (int32 I=0;I<4;++I)
 {
  F.Release(); F.Key(Keys[I],IE_Pressed); F.Frame(); F.Release(); TestEqual(TEXT("K02 F only"),F.Counts[I],0);
  for (FKey Modifier:{EKeys::LeftControl,EKeys::LeftAlt}) { F.Key(Modifier,IE_Pressed); F.Frame(); F.Key(Keys[I],IE_Pressed); F.Frame(); F.Release(); TestEqual(TEXT("K03 one modifier"),F.Counts[I],0); }
  F.Chord(Keys[I]); TestEqual(TEXT("K04 left chord"),F.Counts[I],1);
  F.Chord(Keys[I],EKeys::RightControl,EKeys::RightAlt); TestEqual(TEXT("K05 right chord"),F.Counts[I],2);
  F.Chord(Keys[I],EKeys::LeftControl,EKeys::RightAlt); F.Chord(Keys[I],EKeys::RightControl,EKeys::LeftAlt); TestEqual(TEXT("K05 mixed"),F.Counts[I],4);
 }
 F.Key(EKeys::LeftControl,IE_Pressed); F.Key(EKeys::RightControl,IE_Pressed); F.Frame(); F.Key(EKeys::LeftControl,IE_Released); F.Key(EKeys::LeftAlt,IE_Pressed); F.Frame(); F.Key(EKeys::F6,IE_Pressed); F.Frame(); TestEqual(TEXT("K06 remaining ctrl"),F.Counts[0],5); F.Release();
 F.Key(EKeys::LeftControl,IE_Pressed); F.Key(EKeys::LeftAlt,IE_Pressed); F.Frame(); F.Key(EKeys::F6,IE_Pressed); F.Frame();
 for (int32 I=0;I<120;++I) F.Frame(); F.Key(EKeys::F6,IE_Repeat); F.Frame(); TestEqual(TEXT("K07 held/repeat"),F.Counts[0],6);
 F.Key(EKeys::F6,IE_Released); F.Frame(); F.Key(EKeys::F6,IE_Pressed); F.Frame(); TestEqual(TEXT("K07 repress"),F.Counts[0],7); F.Release();
 F.Key(EKeys::F6,IE_Pressed); F.Frame(); F.Key(EKeys::LeftControl,IE_Pressed); F.Key(EKeys::LeftAlt,IE_Pressed); F.Frame(); TestEqual(TEXT("K08 main first"),F.Counts[0],7);
 F.Key(EKeys::F6,IE_Released); F.Frame(); F.Key(EKeys::F6,IE_Pressed); F.Frame(); TestEqual(TEXT("K08 repress"),F.Counts[0],8); F.Release();
 F.SetContext(F.Saved.Get(),false); F.Chord(EKeys::F6); TestEqual(TEXT("K09 removed"),F.Counts[0],8);
 F.SetContext(F.Saved.Get(),true); F.SetContext(F.Saved.Get(),true); int32 Priority=0; TestTrue(TEXT("K10 single context"),F.Harness.HasMappingContext(F.Saved.Get(),Priority)); TestEqual(TEXT("K10 priority"),Priority,100); F.Chord(EKeys::F6); TestEqual(TEXT("K10 callback once"),F.Counts[0],9);
 F.SetContext(F.Saved.Get(),false);
 TStrongObjectPtr<UInputMappingContext> Clone(DuplicateObject<UInputMappingContext>(F.Saved.Get(),GetTransientPackage()));
 Clone->UnmapKey(F.Actions[0],EKeys::F6);
 FEnhancedActionKeyMapping& Map=Clone->MapKey(F.Actions[0],EKeys::F10);
 Map.Triggers.Add(NewObject<UInputTriggerPressed>(Clone.Get()));
 for (int32 I=4;I<6;++I) { auto* Chord=NewObject<UInputTriggerChordAction>(Clone.Get()); Chord->ChordAction=F.Actions[I]; Map.Triggers.Add(Chord); }
 F.SetContext(Clone.Get(),true); F.Chord(EKeys::F6); TestEqual(TEXT("K11 old key"),F.Counts[0],9); F.Chord(EKeys::F10); TestEqual(TEXT("K11 new key"),F.Counts[0],10);
 bool bSourceF6=false; for (const auto& Source:F.Saved->GetMappings()) if (Source.Action==F.Actions[0]) bSourceF6=Source.Key==EKeys::F6; TestTrue(TEXT("K11 source unchanged"),bSourceF6);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSWDevIsolation,"ArtisticSW.Development.TestInput.ContextIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSWDevIsolation::RunTest(const FString& Parameters)
{
 using namespace SWDevTestInputTests;
 FFixture F; if (!F.Valid(*this)) return false;
 TStrongObjectPtr<UInputMappingContext> Lower(NewObject<UInputMappingContext>());
 TArray<TStrongObjectPtr<UInputAction>> LowerActions; int32 Counts[3]={0,0,0}; FKey Keys[]={EKeys::F6,EKeys::LeftControl,EKeys::LeftAlt};
 for (int32 I=0;I<3;++I)
 {
  auto* Action=NewObject<UInputAction>(); LowerActions.Emplace(Action); Action->ValueType=EInputActionValueType::Boolean; Action->bConsumeInput=false;
  Lower->MapKey(Action,Keys[I]).Triggers.Add(NewObject<UInputTriggerDown>(Lower.Get()));
  F.Input->BindActionValueLambda(Action,ETriggerEvent::Triggered,[&Counts,I](const FInputActionValue&){ ++Counts[I]; });
 }
 F.SetContext(Lower.Get(),true,0); F.SetContext(F.Saved.Get(),true);
 F.Release(); F.Key(EKeys::F6,IE_Pressed); F.Frame(); TestTrue(TEXT("K12 F alone reaches lower"),Counts[0]>0); F.Release();
 F.Key(EKeys::LeftControl,IE_Pressed); F.Key(EKeys::LeftAlt,IE_Pressed); F.Frame(); int32 Before=Counts[0]; int32 Ctrl=Counts[1],Alt=Counts[2];
 F.Key(EKeys::F6,IE_Pressed); F.Frame(); TestEqual(TEXT("K12 actual chord"),F.Counts[0],1); TestEqual(TEXT("K12 lower F blocked"),Counts[0],Before); TestTrue(TEXT("K12 modifier lower preserved"),Counts[1]>Ctrl && Counts[2]>Alt); F.Release();
 F.SetContext(F.Saved.Get(),false); int32 Priority=-1; TestTrue(TEXT("K13 lower retained"),F.Harness.HasMappingContext(Lower.Get(),Priority)); TestEqual(TEXT("K13 lower priority"),Priority,0);
 Before=Counts[0]; F.Chord(EKeys::F6); TestTrue(TEXT("K13 lower continues"),Counts[0]>Before); TestEqual(TEXT("K13 test removed"),F.Counts[0],1);
 return true;
}
#endif
