#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SWDevTestInputComponent.generated.h"
class UInputAction;
class UInputMappingContext;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class USWDevTestInputWidget;
class ABasePlayer;
UENUM()
enum class ESWDevTestAction : uint8 { KillSelf, KillBoth, SinkPlayerShip, EnterFinalEncounter };
UCLASS()
class CLASSFEATURE_API USWDevTestInputComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 static bool ValidateAssets(UInputMappingContext* Context, const TArray<TObjectPtr<UInputAction>>& InActions, FString& Error);
 USWDevTestInputComponent();
 void SetLocalInputDesired(bool bDesired);
 void RequestSessionEnabled(bool bEnabled);
 /** Console and key input share the same session, UI, generation and server policy. */
 void RequestSuicide();
 void BindInput(UInputComponent* Input);
 void RefreshLocalState();
 bool IsEffectiveEnabled() const { return bEffective; }
 virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
 UFUNCTION(Server, Reliable) void ServerSetSessionEnabled(bool bEnabled);
 UFUNCTION(Server, Reliable) void ServerExecuteTest(ESWDevTestAction Action, int32 ExpectedRestoreGeneration, uint64 RequestId, ABasePlayer* ExpectedLifeCharacter, int32 ExpectedWaitingGeneration);
 UFUNCTION(Client, Reliable) void ClientTestResult(uint64 RequestId, bool bAccepted, const FString& Message);
protected:
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
 friend class FPlayerDeathDevelopmentTest;
#endif
 bool MatchesCurrentLife(ABasePlayer* ExpectedLifeCharacter, int32 ExpectedWaitingGeneration) const;
 UFUNCTION() void OnRep_SessionEnabled();
 UFUNCTION() void OnRep_ServerRestoreGeneration();
 UFUNCTION() void OnMappingsRebuilt();
 void ExecuteLocal(ESWDevTestAction Action);
 void KillSelf(); void KillBoth(); void Sink(); void Final();
 bool LoadAssets();
 void RemoveLocal();
 void ResetWorld();
 UPROPERTY(ReplicatedUsing=OnRep_SessionEnabled) bool bServerSessionEnabled = false;
 UPROPERTY(ReplicatedUsing=OnRep_ServerRestoreGeneration) int32 ServerRestoreGeneration = 0;
 UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Mapping;
 UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> Actions;
 UPROPERTY(Transient) TObjectPtr<USWDevTestInputWidget> Widget;
 TWeakObjectPtr<UEnhancedInputComponent> BoundInput;
 TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> BoundSubsystem;
 TWeakObjectPtr<UWorld> InputWorld;
 TArray<uint32> BindingHandles;
 bool bLocalInputDesired = false, bEffective = false, bRefreshing = false, bAssetsValid = false;
 uint64 NextRequestId = 0, PendingRequestId = 0, LastRequestId = 0;
 double PendingAt = 0, LastRequestAt = -1;
 bool bLastAccepted = false;
 FString LastMessage;
};
