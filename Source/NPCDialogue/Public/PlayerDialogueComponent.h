#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCDialogueTypes.h"
#include "PlayerDialogueComponent.generated.h"

class ACameraActor;
class IDialogueInventoryProvider;
class UAbilitySystemComponent;
class UNPCDialogueSourceComponent;
class UUserWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNPCDialogueEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FNPCDialogueFailedEvent,
	ENPCDialogueFailureReason,
	Reason);

/**
 * Player-owned network boundary for NPC dialogue.
 * UI is optional: a future WBP can be assigned to DialogueWidgetClass, bind these events,
 * read CurrentView, and call AdvanceDialogue/CancelDialogue.
 */
UCLASS(ClassGroup = (NPC), meta = (BlueprintSpawnableComponent))
class NPCDIALOGUE_API UPlayerDialogueComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerDialogueComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "NPC|Dialogue")
	void AdvanceDialogue();

	UFUNCTION(BlueprintCallable, Category = "NPC|Dialogue")
	void CancelDialogue();

	UFUNCTION(BlueprintPure, Category = "NPC|Dialogue")
	bool IsDialogueActive() const { return bClientDialogueActive; }

	UFUNCTION(BlueprintPure, Category = "NPC|Dialogue")
	const FNPCDialogueView& GetCurrentDialogueView() const { return CurrentView; }

	UPROPERTY(BlueprintAssignable, Category = "NPC|Dialogue|UI")
	FNPCDialogueEvent OnDialogueOpened;

	UPROPERTY(BlueprintAssignable, Category = "NPC|Dialogue|UI")
	FNPCDialogueEvent OnDialogueLineChanged;

	UPROPERTY(BlueprintAssignable, Category = "NPC|Dialogue|UI")
	FNPCDialogueEvent OnDialogueClosed;

	UPROPERTY(BlueprintAssignable, Category = "NPC|Dialogue|UI")
	FNPCDialogueFailedEvent OnDialogueFailed;

protected:
	/** Optional. Assign the future WBP here; the system already works without one. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Dialogue|UI")
	TSubclassOf<UUserWidget> DialogueWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Dialogue|UI")
	bool bShowMouseCursorDuringDialogue = true;

private:
	void TryBindInteractionEvent();
	void HandleDialogueInteractionEvent(const struct FGameplayEventData* Payload);
	void BeginServerDialogue(UNPCDialogueSourceComponent* Source);
	void EndServerDialogue();
	bool ValidateServerSession(
		const FNPCDialogueRule*& OutRule,
		IDialogueInventoryProvider*& OutInventory,
		ENPCDialogueFailureReason& OutFailure) const;
	bool CommitServerOutcome(const FNPCDialogueRule& Rule, IDialogueInventoryProvider* Inventory);
	FNPCDialogueView MakeView(AActor* NPC, const FNPCDialogueRule& Rule, int32 LineIndex) const;
	IDialogueInventoryProvider* FindInventoryProvider() const;
	UAbilitySystemComponent* ResolveAbilitySystem() const;
	void SetServerDialogueStateTag(bool bEnabled);

	UFUNCTION(Server, Reliable)
	void ServerAdvanceDialogue(int32 ExpectedSessionId);

	UFUNCTION(Server, Reliable)
	void ServerCancelDialogue(int32 ExpectedSessionId);

	UFUNCTION(Client, Reliable)
	void ClientOpenDialogue(int32 SessionId, const FNPCDialogueView& View);

	UFUNCTION(Client, Reliable)
	void ClientUpdateDialogue(int32 SessionId, const FNPCDialogueView& View);

	UFUNCTION(Client, Reliable)
	void ClientCloseDialogue(int32 SessionId);

	UFUNCTION(Client, Reliable)
	void ClientDialogueFailed(ENPCDialogueFailureReason Reason);

	void OpenClientPresentation();
	void CloseClientPresentation();
	void StartClientCamera();
	void StopClientCamera();

	TWeakObjectPtr<UAbilitySystemComponent> BoundAbilitySystem;
	FTimerHandle BindRetryTimerHandle;

	TWeakObjectPtr<UNPCDialogueSourceComponent> ServerDialogueSource;
	FName ServerRuleId = NAME_None;
	int32 ServerLineIndex = INDEX_NONE;
	int32 ServerSessionId = 0;

	UPROPERTY(Transient)
	FNPCDialogueView CurrentView;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> ActiveDialogueWidget = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> DialogueCameraActor = nullptr;

	TWeakObjectPtr<AActor> PreviousViewTarget;
	int32 ClientSessionId = 0;
	bool bClientDialogueActive = false;
};
