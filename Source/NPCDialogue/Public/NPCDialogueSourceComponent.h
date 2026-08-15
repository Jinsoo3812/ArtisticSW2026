#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCDialogueTypes.h"
#include "NPCDialogueSourceComponent.generated.h"

class IDialogueInventoryProvider;
class UNPCDialogueData;
class UStoryFacadeSubsystem;

/** Server-authoritative dialogue source and exclusive per-NPC reservation. */
UCLASS(ClassGroup = (NPC), meta = (BlueprintSpawnableComponent))
class NPCDIALOGUE_API UNPCDialogueSourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCDialogueSourceComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "NPC|Dialogue")
	UNPCDialogueData* GetDialogueData() const { return DialogueData; }

	UFUNCTION(BlueprintCallable, Category = "NPC|Dialogue")
	void SetDialogueData(UNPCDialogueData* NewDialogueData) { DialogueData = NewDialogueData; }

	UFUNCTION(BlueprintPure, Category = "NPC|Dialogue")
	float GetMaxDialogueDistance() const { return MaxDialogueDistance; }

	bool TryReserve(AActor* Interactor);
	void Release(AActor* Interactor);
	bool IsReservedBy(AActor* Interactor) const;

	const FNPCDialogueRule* ResolveBestRule(
		const UStoryFacadeSubsystem* Story,
		const IDialogueInventoryProvider* Inventory) const;

	bool IsRuleAvailable(
		const FNPCDialogueRule& Rule,
		const UStoryFacadeSubsystem* Story,
		const IDialogueInventoryProvider* Inventory) const;

	FTransform GetDialogueCameraTransform() const;

protected:
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "NPC|Dialogue")
	TObjectPtr<UNPCDialogueData> DialogueData = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Dialogue", meta = (ClampMin = "1.0"))
	float MaxDialogueDistance = 250.0f;

	/** Safety release for disconnects or UI failures. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Dialogue", meta = (ClampMin = "5.0"))
	float ReservationTimeoutSeconds = 120.0f;

private:
	void HandleReservationTimeout();

	TWeakObjectPtr<AActor> ActiveInteractor;
	FTimerHandle ReservationTimerHandle;
};
