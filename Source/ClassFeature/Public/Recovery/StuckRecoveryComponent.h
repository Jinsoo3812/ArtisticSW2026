#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Recovery/StuckRecoveryTypes.h"
#include "StuckRecoveryComponent.generated.h"

class ACharacter;
class UAnimInstance;
class UGameplayAbility;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FStuckRecoveryStateChanged, EStuckRecoveryState, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FStuckRecoveryResult, bool, bSucceeded);

/** Server owns detection, hold timing and relocation; clients consume replicated state. */
UCLASS(ClassGroup=(Movement), meta=(BlueprintSpawnableComponent))
class CLASSFEATURE_API UStuckRecoveryComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UStuckRecoveryComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Each caller retains its own token; ending one source cannot end another. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Stuck Recovery")
	FGuid BeginMonitoring(UGameplayAbility* SourceAbility = nullptr);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Stuck Recovery")
	void EndMonitoring(FGuid SourceId);
	/** Call from the same validated movement-input path used by the character. */
	void RecordMovementInput(FVector2D Input);
	UFUNCTION(BlueprintCallable, Category="Stuck Recovery")
	void SetRecoveryInputHeld(bool bHeld);
	/** Cutscenes and intentional custom movement locks can explicitly suppress detection. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Stuck Recovery")
	void SetDetectionSuspended(bool bSuspended);
	UFUNCTION(BlueprintPure, Category="Stuck Recovery")
	EStuckRecoveryState GetRecoveryState() const { return State; }
	UFUNCTION(BlueprintPure, Category="Stuck Recovery")
	bool IsEscapeAvailable() const { return State == EStuckRecoveryState::ConfirmedStuck; }
	UFUNCTION(BlueprintPure, Category="Stuck Recovery")
	float GetHoldProgress() const;
	UFUNCTION(BlueprintPure, Category="Stuck Recovery")
	float GetHoldDuration() const { return FMath::Max(0.1f, Escape.HoldDuration); }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stuck Recovery")
	bool bEnableStuckDetection = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stuck Recovery|Detection")
	FStuckDetectionSettings Detection;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stuck Recovery|Escape")
	FStuckEscapeSettings Escape;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stuck Recovery|Detection")
	FGameplayTagContainer SuppressionTags;
	UPROPERTY(BlueprintAssignable, Category="Stuck Recovery")
	FStuckRecoveryStateChanged OnStateChanged;
	UPROPERTY(BlueprintAssignable, Category="Stuck Recovery")
	FStuckRecoveryResult OnRecoveryResult;
private:
	UFUNCTION(Server, Reliable)
	void ServerSetRecoveryInputHeld(bool bHeld);
	UFUNCTION(Client, Reliable)
	void ClientRecoveryResult(bool bSucceeded);
	UFUNCTION()
	void OnRep_State();
	UFUNCTION()
	void HandleMovementModeChanged(ACharacter* Character, EMovementMode PreviousMode, uint8 PreviousCustomMode);
	void Evaluate(float Elapsed);
	bool CanMonitor() const;
	bool HasAbnormalCollision() const;
	void ChangeState(EStuckRecoveryState NewState);
	void ResetHold();
	void ResetDetection();
	void ExecuteRecovery();
	void ObserveMontage();
	void CaptureLocationSample();

	UPROPERTY(ReplicatedUsing=OnRep_State)
	EStuckRecoveryState State = EStuckRecoveryState::Idle;
	UPROPERTY(Replicated)
	float ServerHoldElapsed = 0.f;
	TWeakObjectPtr<ACharacter> Character;
	TMap<FGuid, TWeakObjectPtr<UGameplayAbility>> MonitoringSources;
	FVector2D MovementInput = FVector2D::ZeroVector;
	FVector PreviousLocation = FVector::ZeroVector;
	/** Remove movement imparted by a moving ship/deck from the displacement sample. */
	TWeakObjectPtr<UPrimitiveComponent> PreviousMovementBase;
	FVector PreviousBaseLocalLocation = FVector::ZeroVector;
	float SampleElapsed = 0.f;
	float InactivityElapsed = 0.f;
	float BlockedInputElapsed = 0.f;
	float SuspectedElapsed = 0.f;
	float MonitoringUntil = 0.f;
	float CooldownUntil = 0.f;
	float NextFallbackProbeTime = 0.f;
	bool bDetectionSuspended = false;
	bool bMontageWasPlaying = false;
	bool bServerInputHeld = false;
	bool bLocalInputHeld = false;
	bool bHoldConsumed = false;
};
