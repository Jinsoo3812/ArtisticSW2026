#include "Development/TestInput/SWDevTestInputComponent.h"
#include "Development/TestInput/SWDevTestInputWidget.h"
#include "BasePlayerController.h"
#include "BasePlayer.h"
#include "MultiGameMode.h"
#include "Room/SWRoomProgressSubsystem.h"
#include "Room/ClassFeatureRoomProgressSubsystem.h"
#include "Room/SWRoomReadyState.h"
#include "Components/BaseHealthComponent.h"
#include "BaseAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Ship.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "HAL/IConsoleManager.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "String/LexFromString.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWDevTestInput, Log, All);
namespace SWDevTestInput
{
constexpr bool Allowed()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
 return true;
#else
 return false;
#endif
}
const TCHAR* Names[] = { TEXT("KillSelf"), TEXT("KillBoth"), TEXT("SinkPlayerShip"), TEXT("EnterFinalEncounter"), TEXT("ModifierCtrl"), TEXT("ModifierAlt") };
USWRoomProgressSubsystem* Room(UWorld* World) { return World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<USWRoomProgressSubsystem>() : nullptr; }
ASWRoomReadyState* Ready(UWorld* World) { for (TActorIterator<ASWRoomReadyState> It(World); It; ++It) return *It; return nullptr; }
bool SafeWorld(UWorld* World)
{
 USWRoomProgressSubsystem* State = Room(World);
 AMultiGameMode* Mode = World ? World->GetAuthGameMode<AMultiGameMode>() : nullptr;
 ASWRoomReadyState* Barrier = World ? Ready(World) : nullptr;
 UClassFeatureRoomProgressSubsystem* Progress = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UClassFeatureRoomProgressSubsystem>() : nullptr;
 return State && Mode && Progress && State->IsHostedRoom() && State->GetActiveRoom()
  && Mode->GetSessionLifePhase() == ESWSessionLifePhase::Playing && Barrier && Barrier->bWorldReady
  && Barrier->RestoreGeneration == State->GetRestoreGeneration() && !Mode->IsLevelRestartRequested()
  && !State->IsNewRoomPending() && !State->IsReturnTravelPending() && !State->IsFinalDepartureTravelPending()
  && !State->IsGameOverTravelPending() && !State->IsGameOverRetryTravelPending() && !Progress->IsDevelopmentTransitionBusy();
}
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
void Console(const TArray<FString>& Args, UWorld* World, bool bSession)
{
 if (Args.Num()!=1 || (Args[0]!=TEXT("0") && Args[0]!=TEXT("1")) || !World || !World->IsGameWorld())
 { UE_LOG(LogSWDevTestInput, Warning, TEXT("Invalid arguments/world")); return; }
 ABasePlayerController* Target = nullptr; int32 Count = 0;
 for (FConstPlayerControllerIterator It=World->GetPlayerControllerIterator(); It; ++It)
  if (ABasePlayerController* PC=Cast<ABasePlayerController>(It->Get()); PC && PC->IsLocalPlayerController()) { Target=PC; ++Count; }
 if (Count!=1) { UE_LOG(LogSWDevTestInput, Warning, TEXT("Requires exactly one local controller Count=%d"), Count); return; }
 if (bSession) Target->DevTestInput->RequestSessionEnabled(Args[0]==TEXT("1"));
 else Target->DevTestInput->SetLocalInputDesired(Args[0]==TEXT("1"));
}
FAutoConsoleCommandWithWorldAndArgs Session(TEXT("SW.DevTest.Session"), TEXT("Host server test permission 0|1"), FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World){ Console(Args,World,true); }));
FAutoConsoleCommandWithWorldAndArgs Input(TEXT("SW.DevTest.Input"), TEXT("Local test input 0|1"), FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World){ Console(Args,World,false); }));
FAutoConsoleCommandWithWorldAndArgs Voyage(TEXT("SW.DevTest.Voyage"), TEXT("Return | Hold <phase> <seconds 1..60> | Release | FailPre | FailPost | FailSave | Clear | FixtureSeed | FixtureObserve"),
 FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
 {
  if (!World || !World->IsGameWorld() || Args.IsEmpty()) return;
  const bool bHold = Args[0] == TEXT("Hold");
  float Seconds = 0.f;
  if ((bHold && (Args.Num() != 3 || !LexTryParseString(Seconds, *Args[2]) || !FMath::IsFinite(Seconds) || Seconds < 1.f || Seconds > 60.f))
   || (!bHold && (Args.Num() != 1 || (Args[0] != TEXT("Return") && Args[0] != TEXT("Release") && Args[0] != TEXT("FailPre")
    && Args[0] != TEXT("FailPost") && Args[0] != TEXT("FailSave") && Args[0] != TEXT("Clear")
    && Args[0] != TEXT("FixtureSeed") && Args[0] != TEXT("FixtureObserve")))))
  { UE_LOG(LogSWDevTestInput, Warning, TEXT("Invalid SW.DevTest.Voyage arguments")); return; }
  ABasePlayerController* Target = nullptr;
  int32 Count = 0;
  for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
   if (ABasePlayerController* PC = Cast<ABasePlayerController>(It->Get()); PC && PC->IsLocalPlayerController()) { Target = PC; ++Count; }
  if (Count != 1 || !Target->DevTestInput) { UE_LOG(LogSWDevTestInput, Warning, TEXT("Voyage probe requires exactly one local controller")); return; }
  Target->DevTestInput->RequestVoyageProbe(Args[0], bHold ? Args[1] : FString(), Seconds);
 }));
#endif
}
USWDevTestInputComponent::USWDevTestInputComponent()
{
 SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.TickInterval=0.25f;
}
void USWDevTestInputComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
 Super::GetLifetimeReplicatedProps(OutLifetimeProps);
 DOREPLIFETIME_CONDITION(USWDevTestInputComponent,bServerSessionEnabled,COND_OwnerOnly);
 DOREPLIFETIME_CONDITION(USWDevTestInputComponent,ServerRestoreGeneration,COND_OwnerOnly);
}
void USWDevTestInputComponent::BeginPlay() { Super::BeginPlay(); InputWorld=GetWorld(); RefreshLocalState(); }
void USWDevTestInputComponent::RemoveLocal()
{
 if (BoundSubsystem.IsValid()) { if (Mapping) BoundSubsystem->RemoveMappingContext(Mapping); BoundSubsystem->ControlMappingsRebuiltDelegate.RemoveDynamic(this,&USWDevTestInputComponent::OnMappingsRebuilt); }
 BoundSubsystem.Reset();
 if (Widget) { Widget->RemoveFromParent(); Widget=nullptr; } bEffective=false;
}
void USWDevTestInputComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 RemoveLocal(); if (BoundInput.IsValid()) for (uint32 Handle:BindingHandles) BoundInput->RemoveBindingByHandle(Handle);
 BindingHandles.Reset(); BoundInput.Reset(); Super::EndPlay(Reason);
}
void USWDevTestInputComponent::ResetWorld()
{
 if (InputWorld.Get()==GetWorld()) return;
 RemoveLocal(); bLocalInputDesired=false; PendingRequestId=NextRequestId=LastRequestId=0; LastRequestAt=-1;
 bServerSessionEnabled=false; ServerRestoreGeneration=0; InputWorld=GetWorld();
}
void USWDevTestInputComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
 Super::TickComponent(DeltaTime,TickType,TickFunction); if (!SWDevTestInput::Allowed()) return; ResetWorld();
 if (GetOwner()->HasAuthority()) if (USWRoomProgressSubsystem* State=SWDevTestInput::Room(GetWorld()))
 { bServerSessionEnabled=State->IsDevelopmentTestSessionEnabled(GetWorld()); ServerRestoreGeneration=State->GetRestoreGeneration(); }
 if (PendingRequestId && FPlatformTime::Seconds()-PendingAt>=5) PendingRequestId=0;
 RefreshLocalState();
}
bool USWDevTestInputComponent::ValidateAssets(UInputMappingContext* Context,const TArray<TObjectPtr<UInputAction>>& InActions,FString& Error)
{
 Error=TEXT("Invalid development input asset contract");
 if (!Context || Context->GetClass()!=UInputMappingContext::StaticClass() || Context->GetPathName()!=TEXT("/Game/Developer/Testing/Input/IMC_DevTest.IMC_DevTest") || InActions.Num()!=6 || Context->GetMappings().Num()!=8) return false;
 for (int32 Index=0;Index<6;++Index)
 {
  UInputAction* Action=InActions[Index];
  const FString ObjectName=FString(TEXT("IA_DevTest_"))+SWDevTestInput::Names[Index];
  if (!Action || Action->GetPathName()!=TEXT("/Game/Developer/Testing/Input/Actions/")+ObjectName+TEXT(".")+ObjectName) return false;
  if (!Action || Action->GetClass()!=UInputAction::StaticClass() || Action->GetName()!=FString(TEXT("IA_DevTest_"))+SWDevTestInput::Names[Index] || Action->ValueType!=EInputActionValueType::Boolean
   || Action->bTriggerWhenPaused || Action->bConsumesActionAndAxisMappings || Action->bConsumeInput!=(Index<4)
   || Action->AccumulationBehavior!=EInputActionAccumulationBehavior::TakeHighestAbsoluteValue || !Action->Triggers.IsEmpty() || !Action->Modifiers.IsEmpty()) return false;
  int32 Count=0; TSet<FKey> SeenKeys;
  for (const FEnhancedActionKeyMapping& Map:Context->GetMappings()) if (Map.Action==Action)
  {
   ++Count; if (!Map.Key.IsValid() || !Map.Modifiers.IsEmpty() || SeenKeys.Contains(Map.Key)) return false; SeenKeys.Add(Map.Key);
   if (Index>=4)
   {
    const bool bKey=Index==4 ? (Map.Key==EKeys::LeftControl || Map.Key==EKeys::RightControl) : (Map.Key==EKeys::LeftAlt || Map.Key==EKeys::RightAlt);
    if (!bKey || Map.Triggers.Num()!=1 || !Map.Triggers[0] || Map.Triggers[0]->GetClass()!=UInputTriggerDown::StaticClass() || Map.Triggers[0]->ActuationThreshold!=0.5f || Map.Triggers[0]->GetOuter()!=Context) return false;
   }
   else
   {
    int32 Pressed=0,Ctrl=0,Alt=0;
    for (UInputTrigger* Trigger:Map.Triggers)
    {
     if (!Trigger || Trigger->GetOuter()!=Context || Trigger->ActuationThreshold!=0.5f) return false;
     if (Trigger->GetClass()==UInputTriggerPressed::StaticClass()) ++Pressed;
     else if (Trigger->GetClass()==UInputTriggerChordAction::StaticClass())
     { UInputTriggerChordAction* Chord=CastChecked<UInputTriggerChordAction>(Trigger); Ctrl+=Chord->ChordAction==InActions[4]; Alt+=Chord->ChordAction==InActions[5]; }
     else return false;
    }
    if (Map.Triggers.Num()!=3 || Pressed!=1 || Ctrl!=1 || Alt!=1) return false;
   }
  }
  if (Count!=(Index<4 ? 1:2)) return false;
 }
 Error.Reset(); return true;
}
bool USWDevTestInputComponent::LoadAssets()
{
 if (bAssetsValid) return true;
 Mapping=LoadObject<UInputMappingContext>(nullptr,TEXT("/Game/Developer/Testing/Input/IMC_DevTest.IMC_DevTest"));
 Actions.Reset();
 for (const TCHAR* Name:SWDevTestInput::Names)
 {
  FString ObjectName=FString(TEXT("IA_DevTest_"))+Name;
  Actions.Add(LoadObject<UInputAction>(nullptr,*(TEXT("/Game/Developer/Testing/Input/Actions/")+ObjectName+TEXT(".")+ObjectName)));
 }
 FString Error; bAssetsValid=ValidateAssets(Mapping,Actions,Error);
 if (!bAssetsValid) UE_LOG(LogSWDevTestInput,Error,TEXT("%s Root=/Game/Developer/Testing/Input"),*Error);
 return bAssetsValid;
}
void USWDevTestInputComponent::BindInput(UInputComponent* Input)
{
 if (!SWDevTestInput::Allowed()) return; ResetWorld();
 UEnhancedInputComponent* Enhanced=Cast<UEnhancedInputComponent>(Input);
 if (BoundInput.Get()==Enhanced && BindingHandles.Num()==4) return;
 if (BoundInput.IsValid()) for (uint32 Handle:BindingHandles) BoundInput->RemoveBindingByHandle(Handle);
 BindingHandles.Reset(); BoundInput=Enhanced;
 if (!Enhanced || !bAssetsValid) return;
 BindingHandles.Add(Enhanced->BindAction(Actions[0],ETriggerEvent::Triggered,this,&USWDevTestInputComponent::KillSelf).GetHandle());
 BindingHandles.Add(Enhanced->BindAction(Actions[1],ETriggerEvent::Triggered,this,&USWDevTestInputComponent::KillBoth).GetHandle());
 BindingHandles.Add(Enhanced->BindAction(Actions[2],ETriggerEvent::Triggered,this,&USWDevTestInputComponent::Sink).GetHandle());
 BindingHandles.Add(Enhanced->BindAction(Actions[3],ETriggerEvent::Triggered,this,&USWDevTestInputComponent::Final).GetHandle());
}
void USWDevTestInputComponent::RefreshLocalState()
{
 if (!SWDevTestInput::Allowed() || bRefreshing) return; TGuardValue<bool> Guard(bRefreshing,true); ResetWorld();
 ABasePlayerController* PC=Cast<ABasePlayerController>(GetOwner()); if (!PC || !PC->IsLocalPlayerController()) return;
 if (bLocalInputDesired) { LoadAssets(); BindInput(PC->InputComponent); }
 UEnhancedInputLocalPlayerSubsystem* Subsystem=PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
 ASWRoomReadyState* Ready=SWDevTestInput::Ready(GetWorld());
 bEffective=bLocalInputDesired && bServerSessionEnabled && bAssetsValid && Subsystem && BindingHandles.Num()==4 && Ready && Ready->RestoreGeneration==ServerRestoreGeneration;
 if (!bEffective) { RemoveLocal(); return; }
 if (BoundSubsystem.Get()!=Subsystem) { RemoveLocal(); BoundSubsystem=Subsystem; Subsystem->ControlMappingsRebuiltDelegate.AddUniqueDynamic(this,&USWDevTestInputComponent::OnMappingsRebuilt); bEffective=true; }
 if (!Subsystem->HasMappingContext(Mapping)) { FModifyContextOptions Options; Options.bIgnoreAllPressedKeysUntilRelease=true; Options.bNotifyUserSettings=false; Subsystem->AddMappingContext(Mapping,100,Options); }
 if (!Widget)
 {
  Widget=CreateWidget<USWDevTestInputWidget>(PC); Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
  FString Guide=TEXT("개발 테스트 입력 ON | Ctrl+Alt+"); const TCHAR* Labels[]={TEXT(" 자신 사망 / "),TEXT(" 두 명 사망 / "),TEXT(" 배 침몰 / "),TEXT(" 최종 출항(자동 저장)")};
  for (int32 Index=0;Index<4;++Index) for (const FEnhancedActionKeyMapping& Map:Mapping->GetMappings()) if (Map.Action==Actions[Index]) Guide+=Map.Key.GetDisplayName().ToString()+Labels[Index];
  if (Widget)
  {
   int32 Width=0,Height=0; PC->GetViewportSize(Width,Height);
   Widget->SetGuide(Guide); Widget->AddToPlayerScreen(-50); Widget->SetAlignmentInViewport(FVector2D(1,0)); Widget->SetPositionInViewport(FVector2D(Width-16,16)); Widget->SetDesiredSizeInViewport(FVector2D(720,60));
  }
 }
 if (Widget) Widget->SetVisibility(PC->IsDevelopmentTestInputBlockedByUI() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
}
void USWDevTestInputComponent::SetLocalInputDesired(bool bDesired)
{
 if (!SWDevTestInput::Allowed()) return; ResetWorld(); bLocalInputDesired=bDesired; RefreshLocalState();
 if (bDesired && !bServerSessionEnabled) if (APlayerController* PC=Cast<APlayerController>(GetOwner())) PC->ClientMessage(TEXT("서버 테스트 허용이 꺼져 있습니다"));
}
void USWDevTestInputComponent::RequestSessionEnabled(bool bEnabled) { if (SWDevTestInput::Allowed()) ServerSetSessionEnabled(bEnabled); }
void USWDevTestInputComponent::RequestVoyageProbe(const FString& Command, const FString& Phase, float Seconds)
{
 if (!SWDevTestInput::Allowed()) return;
 ResetWorld();
 ABasePlayerController* PC = Cast<ABasePlayerController>(GetOwner());
 ASWRoomReadyState* Barrier = PC ? SWDevTestInput::Ready(GetWorld()) : nullptr;
 if (!PC || !PC->IsLocalPlayerController() || !Barrier || PendingRequestId || NextRequestId == MAX_uint64) return;
 PendingRequestId = ++NextRequestId; PendingAt = FPlatformTime::Seconds();
 ServerVoyageProbe(Command, Phase, Seconds, Barrier->VoyageState.AttemptId, Barrier->VoyageState.Generation, PendingRequestId);
}
void USWDevTestInputComponent::ServerVoyageProbe_Implementation(const FString& Command, const FString& Phase, float Seconds, int64 ExpectedAttempt, int32 ExpectedGeneration, uint64 RequestId)
{
 if (!SWDevTestInput::Allowed()) { ClientTestResult(RequestId, false, TEXT("개발 빌드에서만 사용 가능합니다")); return; }
 ResetWorld();
 ABasePlayerController* PC = Cast<ABasePlayerController>(GetOwner());
 if (!PC || !PC->HasAuthority()) return;
 if (RequestId && RequestId == LastRequestId) { ClientTestResult(RequestId, bLastAccepted, LastMessage); return; }
 if (!RequestId || RequestId < LastRequestId || Command.Len() > 16 || Phase.Len() > 16)
 { ClientTestResult(RequestId, false, TEXT("잘못된 항해 시험 요청")); return; }
 const double Now = FPlatformTime::Seconds();
 const bool bRateLimited = LastRequestAt >= 0 && Now - LastRequestAt < 0.25; LastRequestAt = Now;
 FString Message;
 UClassFeatureRoomProgressSubsystem* Progress = GetWorld()->GetGameInstance()->GetSubsystem<UClassFeatureRoomProgressSubsystem>();
 const bool bAccepted = !bRateLimited && Progress && Progress->ExecuteDevelopmentVoyageProbe(PC, Command, Phase, Seconds, ExpectedAttempt, ExpectedGeneration, Message);
 if (Message.IsEmpty()) Message = bAccepted ? TEXT("항해 시험 명령 적용") : TEXT("항해 시험 요청 거부");
 LastRequestId = RequestId; bLastAccepted = bAccepted; LastMessage = Message;
 UE_LOG(LogSWDevTestInput, Display, TEXT("VoyageProbe=%s Request=%llu Attempt=%lld Generation=%d Accepted=%d Reason=%s"),
  *Command, RequestId, ExpectedAttempt, ExpectedGeneration, bAccepted, *Message);
 ClientTestResult(RequestId, bAccepted, Message);
}
void USWDevTestInputComponent::ServerSetSessionEnabled_Implementation(bool bEnabled)
{
 if (!SWDevTestInput::Allowed()) return;
 ABasePlayerController* PC=Cast<ABasePlayerController>(GetOwner()); AMultiGameMode* Mode=GetWorld()->GetAuthGameMode<AMultiGameMode>(); USWRoomProgressSubsystem* State=SWDevTestInput::Room(GetWorld());
 if (!PC || !PC->HasAuthority() || !Mode || !State || !Mode->IsRoomHostController(PC) || (bEnabled && !SWDevTestInput::SafeWorld(GetWorld()))) { ClientTestResult(0,false,TEXT("호스트 권한 또는 준비 상태를 확인하세요")); return; }
 if (!bEnabled)
  if (UClassFeatureRoomProgressSubsystem* Progress = GetWorld()->GetGameInstance()->GetSubsystem<UClassFeatureRoomProgressSubsystem>())
   if (ASWRoomReadyState* Barrier = SWDevTestInput::Ready(GetWorld()))
   { FString Error; Progress->ExecuteDevelopmentVoyageProbe(PC, TEXT("Clear"), FString(), 0.f, Barrier->VoyageState.AttemptId, Barrier->VoyageState.Generation, Error); }
 State->SetDevelopmentTestSessionEnabled(GetWorld(),bEnabled);
 for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It) if (ABasePlayerController* Target=Cast<ABasePlayerController>(It->Get()))
 { Target->DevTestInput->bServerSessionEnabled=bEnabled; Target->DevTestInput->ServerRestoreGeneration=State->GetRestoreGeneration(); Target->DevTestInput->RefreshLocalState(); Target->ForceNetUpdate(); }
}
void USWDevTestInputComponent::OnRep_SessionEnabled(){ RefreshLocalState(); }
void USWDevTestInputComponent::OnRep_ServerRestoreGeneration(){ RefreshLocalState(); }
void USWDevTestInputComponent::OnMappingsRebuilt(){ RefreshLocalState(); }
void USWDevTestInputComponent::KillSelf(){ ExecuteLocal(ESWDevTestAction::KillSelf); }
void USWDevTestInputComponent::KillBoth(){ ExecuteLocal(ESWDevTestAction::KillBoth); }
void USWDevTestInputComponent::Sink(){ ExecuteLocal(ESWDevTestAction::SinkPlayerShip); }
void USWDevTestInputComponent::Final(){ ExecuteLocal(ESWDevTestAction::EnterFinalEncounter); }
void USWDevTestInputComponent::ExecuteLocal(ESWDevTestAction Action)
{
 ABasePlayerController* PC=Cast<ABasePlayerController>(GetOwner());
 if (!SWDevTestInput::Allowed() || !bEffective || !PC || PC->IsDevelopmentTestInputBlockedByUI() || PendingRequestId) return;
 PendingRequestId=++NextRequestId; PendingAt=FPlatformTime::Seconds(); ServerExecuteTest(Action,ServerRestoreGeneration,PendingRequestId);
}
void USWDevTestInputComponent::ClientTestResult_Implementation(uint64 RequestId,bool bAccepted,const FString& Message)
{
 if (RequestId==PendingRequestId) PendingRequestId=0;
 if (APlayerController* PC=Cast<APlayerController>(GetOwner())) PC->ClientMessage(Message);
 if (Widget) Widget->SetResult(Message);
}
void USWDevTestInputComponent::ServerExecuteTest_Implementation(ESWDevTestAction Action,int32 ExpectedRestoreGeneration,uint64 RequestId)
{
 if (!SWDevTestInput::Allowed()) { ClientTestResult(RequestId,false,TEXT("개발 빌드에서만 사용 가능합니다")); return; }
 ResetWorld(); ABasePlayerController* PC=Cast<ABasePlayerController>(GetOwner());
 if (!PC || !PC->HasAuthority()) return;
 if (RequestId && RequestId==LastRequestId) { ClientTestResult(RequestId,bLastAccepted,LastMessage); return; }
 auto Reply=[&](bool bAccepted,const FString& Message)
 {
  if (RequestId>LastRequestId) { LastRequestId=RequestId; bLastAccepted=bAccepted; LastMessage=Message; }
  AMultiGameMode* Mode=GetWorld()->GetAuthGameMode<AMultiGameMode>();
  UE_LOG(LogSWDevTestInput,Display,TEXT("Action=%d Slot=%d Request=%llu Generation=%d Accepted=%d Reason=%s"),int32(Action),Mode ? Mode->GetPlayerIndex(PC):-1,RequestId,ExpectedRestoreGeneration,bAccepted,*Message);
  ClientTestResult(RequestId,bAccepted,Message);
 };
 if (!RequestId || RequestId<LastRequestId || uint8(Action)>3) { Reply(false,TEXT("잘못된 요청")); return; }
 const double Now=FPlatformTime::Seconds(); const bool bRate=LastRequestAt>=0 && Now-LastRequestAt<0.25; LastRequestAt=Now;
 if (bRate) { Reply(false,TEXT("요청 간격이 너무 짧습니다")); return; }
 USWRoomProgressSubsystem* State=SWDevTestInput::Room(GetWorld());
 if (!State || !State->IsDevelopmentTestSessionEnabled(GetWorld())) { Reply(false,TEXT("서버 테스트 허용이 꺼져 있습니다")); return; }
 if (ExpectedRestoreGeneration!=State->GetRestoreGeneration() || !SWDevTestInput::SafeWorld(GetWorld())) { Reply(false,TEXT("현재 전환 중이거나 준비되지 않았습니다")); return; }
 AMultiGameMode* Mode=GetWorld()->GetAuthGameMode<AMultiGameMode>();
 if (Action!=ESWDevTestAction::KillSelf && !Mode->IsRoomHostController(PC)) { Reply(false,TEXT("호스트만 실행할 수 있습니다")); return; }
 if (PC->IsDevelopmentTestInputBlockedByServerUI()) { Reply(false,TEXT("상자/시설/대화 중입니다")); return; }
 if (Action==ESWDevTestAction::EnterFinalEncounter)
 {
  FString Error; const bool bAccepted=GetWorld()->GetGameInstance()->GetSubsystem<UClassFeatureRoomProgressSubsystem>()->TryDevelopmentFinalDeparture(GetWorld(),PC,Error);
  Reply(bAccepted,bAccepted ? TEXT("최종 출항 테스트 실행: 자동 저장 포함"):Error); return;
 }
 if (Action==ESWDevTestAction::SinkPlayerShip)
 {
  AShip* Ship=Cast<AShip>(Mode->GetPlayerRespawnShip());
  UAbilitySystemComponent* ASC=Ship ? Ship->GetAbilitySystemComponent():nullptr;
  if (!Ship || Ship->ActorHasTag(TEXT("Enemy")) || !Ship->ActorHasTag(TEXT("Player")) || Ship->IsSinking() || !ASC || ASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute())<=0) { Reply(false,TEXT("플레이어 배가 없거나 침몰 중입니다")); return; }
  ASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(),0); Reply(true,TEXT("배 침몰 테스트 실행")); return;
 }
 TArray<ABasePlayerController*> Targets;
 if (Action==ESWDevTestAction::KillSelf) Targets.Add(PC);
 else
 {
  TSet<int32> Slots;
  for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It) if (ABasePlayerController* Target=Cast<ABasePlayerController>(It->Get()))
  { const int32 Slot=Mode->GetPlayerIndex(Target); if (Slot<0 || Slot>1 || Slots.Contains(Slot)) { Reply(false,TEXT("두 플레이어가 접속해야 합니다")); return; } Slots.Add(Slot); Targets.Add(Target); }
  if (Mode->GetConnectedPlayerCount()!=2 || Targets.Num()!=2) { Reply(false,TEXT("두 플레이어가 접속해야 합니다")); return; }
 }
 TArray<ABasePlayerController*> Alive;
 for (ABasePlayerController* Target:Targets)
 {
  if (!Target->IsLifeCharacterAlive()) continue;
  ABasePlayer* Character=Target->GetLifeCharacter();
  if (!Character || !Character->GetHealthComponent() || Character->GetHealthComponent()->GetDeathState()!=EBaseDeathState::NotDead || Character->GetHealthComponent()->GetHealth()<=0 || !Character->GetAbilitySystemComponent()) { Reply(false,TEXT("사망 대상 상태 오류")); return; }
  Alive.Add(Target);
 }
 if (Alive.IsEmpty()) { Reply(false,TEXT("이미 사망 또는 부활 대기 중입니다")); return; }
 for (ABasePlayerController* Target:Alive) if (!Target->CleanupLifeInteraction()) { Reply(false,TEXT("조작 상태 정리 실패")); return; }
 for (ABasePlayerController* Target:Alive) Target->GetLifeCharacter()->GetAbilitySystemComponent()->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(),0);
 Reply(true,TEXT("정상 사망 테스트 실행"));
}
