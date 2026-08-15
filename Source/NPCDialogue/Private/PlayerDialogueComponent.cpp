#include "PlayerDialogueComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "BaseGameplayTags.h"
#include "DialogueInventoryProvider.h"
#include "NPCDialogueData.h"
#include "NPCDialogueSourceComponent.h"
#include "StoryFacadeSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UI/NPCDialogueWidget.h"

UPlayerDialogueComponent::UPlayerDialogueComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPlayerDialogueComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		TryBindInteractionEvent();
		if (!BoundAbilitySystem.IsValid() && GetWorld())
		{
			GetWorld()->GetTimerManager().SetTimer(
				BindRetryTimerHandle, this, &UPlayerDialogueComponent::TryBindInteractionEvent, 0.2f, true);
		}
	}
}

void UPlayerDialogueComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(BindRetryTimerHandle);
	}
	if (BoundAbilitySystem.IsValid())
	{
		BoundAbilitySystem->GenericGameplayEventCallbacks.FindOrAdd(Interaction_Dialogue).RemoveAll(this);
	}
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		EndServerDialogue();
	}
	CloseClientPresentation();
	Super::EndPlay(EndPlayReason);
}

void UPlayerDialogueComponent::AdvanceDialogue()
{
	if (bClientDialogueActive)
	{
		ServerAdvanceDialogue(ClientSessionId);
	}
}

void UPlayerDialogueComponent::CancelDialogue()
{
	if (bClientDialogueActive)
	{
		ServerCancelDialogue(ClientSessionId);
	}
}

void UPlayerDialogueComponent::SelectReply(FName ReplyId)
{
	if (bClientDialogueActive && !ReplyId.IsNone())
	{
		ServerSelectReply(ClientSessionId, ReplyId);
	}
}

void UPlayerDialogueComponent::TryBindInteractionEvent()
{
	if (BoundAbilitySystem.IsValid())
	{
		return;
	}
	if (UAbilitySystemComponent* ASC = ResolveAbilitySystem())
	{
		BoundAbilitySystem = ASC;
		ASC->GenericGameplayEventCallbacks.FindOrAdd(Interaction_Dialogue).AddUObject(
			this, &UPlayerDialogueComponent::HandleDialogueInteractionEvent);
		if (GetWorld())
		{
			GetWorld()->GetTimerManager().ClearTimer(BindRetryTimerHandle);
		}
	}
}

void UPlayerDialogueComponent::HandleDialogueInteractionEvent(const FGameplayEventData* Payload)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Payload || !Payload->Target)
	{
		ClientDialogueFailed(ENPCDialogueFailureReason::InvalidTarget);
		return;
	}
	if (ServerDialogueSource.IsValid())
	{
		ClientDialogueFailed(ENPCDialogueFailureReason::AlreadyInDialogue);
		return;
	}

	AActor* TargetActor = const_cast<AActor*>(Cast<AActor>(Payload->Target));
	UNPCDialogueSourceComponent* Source = TargetActor
		? TargetActor->FindComponentByClass<UNPCDialogueSourceComponent>()
		: nullptr;
	if (!Source)
	{
		ClientDialogueFailed(ENPCDialogueFailureReason::InvalidTarget);
		return;
	}
	BeginServerDialogue(Source);
}

void UPlayerDialogueComponent::BeginServerDialogue(UNPCDialogueSourceComponent* Source)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Source || !Source->GetOwner())
	{
		ClientDialogueFailed(ENPCDialogueFailureReason::InvalidTarget);
		return;
	}
	// Until a real WBP is assigned there is no way to advance or cancel the dialogue.
	// Reject safely instead of leaving the local player in a camera/input lock.
	if (!DialogueWidgetClass)
	{
		ClientDialogueFailed(ENPCDialogueFailureReason::MissingDialogueWidget);
		return;
	}
	if (Owner->GetDistanceTo(Source->GetOwner()) > Source->GetMaxDialogueDistance())
	{
		ClientDialogueFailed(ENPCDialogueFailureReason::OutOfRange);
		return;
	}
	if (!Source->TryReserve(Owner))
	{
		ClientDialogueFailed(ENPCDialogueFailureReason::Busy);
		return;
	}

	UStoryFacadeSubsystem* Story = GetWorld() && GetWorld()->GetGameInstance()
		? GetWorld()->GetGameInstance()->GetSubsystem<UStoryFacadeSubsystem>()
		: nullptr;
	IDialogueInventoryProvider* Inventory = FindInventoryProvider();
	const FNPCDialogueRule* Rule = Source->ResolveBestRule(Story, Inventory);
	if (!Rule)
	{
		Source->Release(Owner);
		ClientDialogueFailed(ENPCDialogueFailureReason::NoMatchingDialogue);
		return;
	}

	ServerDialogueSource = Source;
	ServerRuleId = Rule->RuleId;
	ServerLineIndex = 0;
	++ServerSessionId;
	SetServerDialogueStateTag(true);
	if (ACharacter* Character = Cast<ACharacter>(Owner))
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
	}
	ClientOpenDialogue(ServerSessionId, MakeView(Source->GetOwner(), *Rule, ServerLineIndex));
}

void UPlayerDialogueComponent::EndServerDialogue()
{
	if (UNPCDialogueSourceComponent* Source = ServerDialogueSource.Get())
	{
		Source->Release(GetOwner());
	}
	SetServerDialogueStateTag(false);
	ServerDialogueSource.Reset();
	ServerRuleId = NAME_None;
	ServerLineIndex = INDEX_NONE;
}

bool UPlayerDialogueComponent::ValidateServerSession(
	const FNPCDialogueRule*& OutRule,
	IDialogueInventoryProvider*& OutInventory,
	ENPCDialogueFailureReason& OutFailure) const
{
	OutRule = nullptr;
	OutInventory = FindInventoryProvider();
	OutFailure = ENPCDialogueFailureReason::RequirementsChanged;

	UNPCDialogueSourceComponent* Source = ServerDialogueSource.Get();
	AActor* Owner = GetOwner();
	if (!Source || !Source->GetOwner() || !Owner || !Source->IsReservedBy(Owner))
	{
		OutFailure = ENPCDialogueFailureReason::InvalidTarget;
		return false;
	}
	if (Owner->GetDistanceTo(Source->GetOwner()) > Source->GetMaxDialogueDistance())
	{
		OutFailure = ENPCDialogueFailureReason::OutOfRange;
		return false;
	}
	const UNPCDialogueData* Data = Source->GetDialogueData();
	OutRule = Data ? Data->FindRule(ServerRuleId) : nullptr;
	UStoryFacadeSubsystem* Story = GetWorld() && GetWorld()->GetGameInstance()
		? GetWorld()->GetGameInstance()->GetSubsystem<UStoryFacadeSubsystem>()
		: nullptr;
	return OutRule && Source->IsRuleAvailable(*OutRule, Story, OutInventory);
}

bool UPlayerDialogueComponent::CommitServerOutcome(
	const FNPCDialogueRule& Rule,
	IDialogueInventoryProvider* Inventory)
{
	UStoryFacadeSubsystem* Story = GetWorld() && GetWorld()->GetGameInstance()
		? GetWorld()->GetGameInstance()->GetSubsystem<UStoryFacadeSubsystem>()
		: nullptr;
	if (!Story)
	{
		return false;
	}
	if (Rule.bCompleteStoryNode && !Story->CanCompleteStoryNode(Rule.StoryNodeToComplete))
	{
		return false;
	}

	const bool bHasItemTransaction = !Rule.ConsumedItems.IsEmpty() || !Rule.RewardItems.IsEmpty();
	if (bHasItemTransaction)
	{
		if (!Inventory
			|| !Inventory->CanApplyDialogueItemTransaction(Rule.ConsumedItems, Rule.RewardItems)
			|| !Inventory->ApplyDialogueItemTransaction(Rule.ConsumedItems, Rule.RewardItems))
		{
			return false;
		}
	}

	if (Rule.bCompleteStoryNode && !Story->CompleteStoryNode(Rule.StoryNodeToComplete))
	{
		// Server execution is serialized; this is only a defensive rollback for an unexpected Story rejection.
		if (bHasItemTransaction && Inventory)
		{
			Inventory->ApplyDialogueItemTransaction(Rule.RewardItems, Rule.ConsumedItems);
		}
		return false;
	}
	return true;
}

FNPCDialogueView UPlayerDialogueComponent::MakeView(
	AActor* NPC,
	const FNPCDialogueRule& Rule,
	int32 LineIndex) const
{
	FNPCDialogueView View;
	View.NPC = NPC;
	if (const UNPCDialogueSourceComponent* Source = ServerDialogueSource.Get())
	{
		if (const UNPCDialogueData* Data = Source->GetDialogueData())
		{
			View.NPCDisplayName = Data->DisplayName;
		}
	}
	View.LineIndex = LineIndex;
	View.TotalLines = Rule.Lines.Num();
	View.bIsLastLine = LineIndex == Rule.Lines.Num() - 1;
	if (Rule.Lines.IsValidIndex(LineIndex))
	{
		View.CurrentLine = Rule.Lines[LineIndex];
		View.Replies = View.CurrentLine.Replies;
	}
	return View;
}

IDialogueInventoryProvider* UPlayerDialogueComponent::FindInventoryProvider() const
{
	if (AActor* Owner = GetOwner())
	{
		TArray<UActorComponent*> Components;
		Owner->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (Component && Component->GetClass()->ImplementsInterface(UDialogueInventoryProvider::StaticClass()))
			{
				return Cast<IDialogueInventoryProvider>(Component);
			}
		}
	}
	return nullptr;
}

UAbilitySystemComponent* UPlayerDialogueComponent::ResolveAbilitySystem() const
{
	return GetOwner()
		? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner())
		: nullptr;
}

void UPlayerDialogueComponent::SetServerDialogueStateTag(bool bEnabled)
{
	if (UAbilitySystemComponent* ASC = ResolveAbilitySystem())
	{
		if (bEnabled)
		{
			ASC->AddLooseGameplayTag(State_Dialogue);
		}
		else
		{
			ASC->RemoveLooseGameplayTag(State_Dialogue);
		}
	}
}

void UPlayerDialogueComponent::ServerAdvanceDialogue_Implementation(int32 ExpectedSessionId)
{
	if (ExpectedSessionId != ServerSessionId)
	{
		return;
	}
	const FNPCDialogueRule* Rule = nullptr;
	IDialogueInventoryProvider* Inventory = nullptr;
	ENPCDialogueFailureReason Failure = ENPCDialogueFailureReason::RequirementsChanged;
	if (!ValidateServerSession(Rule, Inventory, Failure))
	{
		ClientDialogueFailed(Failure);
		ClientCloseDialogue(ServerSessionId);
		EndServerDialogue();
		return;
	}
	if (Rule->Lines.IsValidIndex(ServerLineIndex)
		&& !Rule->Lines[ServerLineIndex].Replies.IsEmpty())
	{
		return;
	}

	if (Rule->Lines.IsValidIndex(ServerLineIndex + 1))
	{
		++ServerLineIndex;
		ClientUpdateDialogue(ServerSessionId, MakeView(ServerDialogueSource->GetOwner(), *Rule, ServerLineIndex));
		return;
	}

	if (!CommitServerOutcome(*Rule, Inventory))
	{
		ClientDialogueFailed(Rule->ConsumedItems.IsEmpty() && Rule->RewardItems.IsEmpty()
			? ENPCDialogueFailureReason::StoryCommitFailed
			: ENPCDialogueFailureReason::InventoryTransactionFailed);
		return;
	}
	ClientCloseDialogue(ServerSessionId);
	EndServerDialogue();
}

void UPlayerDialogueComponent::ServerSelectReply_Implementation(
	int32 ExpectedSessionId,
	FName ReplyId)
{
	if (ExpectedSessionId != ServerSessionId || ReplyId.IsNone())
	{
		return;
	}

	const FNPCDialogueRule* Rule = nullptr;
	IDialogueInventoryProvider* Inventory = nullptr;
	ENPCDialogueFailureReason Failure = ENPCDialogueFailureReason::RequirementsChanged;
	if (!ValidateServerSession(Rule, Inventory, Failure))
	{
		ClientDialogueFailed(Failure);
		ClientCloseDialogue(ServerSessionId);
		EndServerDialogue();
		return;
	}
	if (!Rule || !Rule->Lines.IsValidIndex(ServerLineIndex))
	{
		return;
	}

	const FNPCDialogueReply* Reply = Rule->Lines[ServerLineIndex].Replies.FindByPredicate(
		[ReplyId](const FNPCDialogueReply& Candidate)
		{
			return Candidate.ReplyId == ReplyId;
		});
	if (!Reply)
	{
		return;
	}

	if (!Reply->NextLineId.IsNone())
	{
		const int32 NextLineIndex = Rule->Lines.IndexOfByPredicate(
			[Reply](const FNPCDialogueLine& Line)
			{
				return Line.LineId == Reply->NextLineId;
			});
		if (NextLineIndex == INDEX_NONE)
		{
			ClientDialogueFailed(ENPCDialogueFailureReason::RequirementsChanged);
			ClientCloseDialogue(ServerSessionId);
			EndServerDialogue();
			return;
		}
		ServerLineIndex = NextLineIndex;
		ClientUpdateDialogue(
			ServerSessionId,
			MakeView(ServerDialogueSource->GetOwner(), *Rule, ServerLineIndex));
		return;
	}

	if (!CommitServerOutcome(*Rule, Inventory))
	{
		ClientDialogueFailed(Rule->ConsumedItems.IsEmpty() && Rule->RewardItems.IsEmpty()
			? ENPCDialogueFailureReason::StoryCommitFailed
			: ENPCDialogueFailureReason::InventoryTransactionFailed);
		return;
	}
	ClientCloseDialogue(ServerSessionId);
	EndServerDialogue();
}

void UPlayerDialogueComponent::ServerCancelDialogue_Implementation(int32 ExpectedSessionId)
{
	if (ExpectedSessionId == ServerSessionId && ServerDialogueSource.IsValid())
	{
		ClientCloseDialogue(ServerSessionId);
		EndServerDialogue();
	}
}

void UPlayerDialogueComponent::ClientOpenDialogue_Implementation(
	int32 SessionId,
	const FNPCDialogueView& View)
{
	ClientSessionId = SessionId;
	CurrentView = View;
	bClientDialogueActive = true;
	OpenClientPresentation();
	OnDialogueOpened.Broadcast();
}

void UPlayerDialogueComponent::ClientUpdateDialogue_Implementation(
	int32 SessionId,
	const FNPCDialogueView& View)
{
	if (bClientDialogueActive && SessionId == ClientSessionId)
	{
		CurrentView = View;
		if (ActiveDialogueWidget)
		{
			ActiveDialogueWidget->ApplyDialogueView(CurrentView);
		}
		OnDialogueLineChanged.Broadcast();
	}
}

void UPlayerDialogueComponent::ClientCloseDialogue_Implementation(int32 SessionId)
{
	if (SessionId == ClientSessionId)
	{
		CloseClientPresentation();
		OnDialogueClosed.Broadcast();
	}
}

void UPlayerDialogueComponent::ClientDialogueFailed_Implementation(ENPCDialogueFailureReason Reason)
{
	OnDialogueFailed.Broadcast(Reason);
}

void UPlayerDialogueComponent::OpenClientPresentation()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
	StartClientCamera();
	// Release any movement key that was held as dialogue opened. SetIgnoreMoveInput
	// blocks movement application, but does not clear custom locomotion input state.
	PC->FlushPressedKeys();
	PC->SetIgnoreMoveInput(true);
	PC->SetIgnoreLookInput(true);

	if (DialogueWidgetClass)
	{
		ActiveDialogueWidget = CreateWidget<UNPCDialogueWidget>(PC, DialogueWidgetClass);
		if (ActiveDialogueWidget)
		{
			ActiveDialogueWidget->InitializeDialogue(this, CurrentView);
			ActiveDialogueWidget->AddToViewport();
			// Dialogue owns keyboard input. GameAndUI lets an unhandled Escape fall
			// through to the PIE viewport after a mouse click, which stops PIE.
			FInputModeUIOnly InputMode;
			InputMode.SetWidgetToFocus(ActiveDialogueWidget->TakeWidget());
			PC->SetInputMode(InputMode);
			PC->bShowMouseCursor = bShowMouseCursorDuringDialogue;
			ActiveDialogueWidget->SetKeyboardFocus();
		}
	}
}

void UPlayerDialogueComponent::CloseClientPresentation()
{
	if (!bClientDialogueActive && !ActiveDialogueWidget && !DialogueCameraActor)
	{
		return;
	}
	bClientDialogueActive = false;
	if (ActiveDialogueWidget)
	{
		ActiveDialogueWidget->RemoveFromParent();
		ActiveDialogueWidget = nullptr;
	}
	StopClientCamera();

	APawn* Pawn = Cast<APawn>(GetOwner());
	if (APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr)
	{
		PC->FlushPressedKeys();
		PC->SetIgnoreMoveInput(false);
		PC->SetIgnoreLookInput(false);
		FInputModeGameOnly InputMode;
		PC->SetInputMode(InputMode);
		PC->bShowMouseCursor = false;
	}
	CurrentView = FNPCDialogueView();
}

void UPlayerDialogueComponent::StartClientCamera()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	AActor* NPC = CurrentView.NPC;
	UNPCDialogueSourceComponent* Source = NPC
		? NPC->FindComponentByClass<UNPCDialogueSourceComponent>()
		: nullptr;
	const UNPCDialogueData* Data = Source ? Source->GetDialogueData() : nullptr;
	if (!PC || !Source || !Data || !GetWorld())
	{
		return;
	}

	PreviousViewTarget = PC->GetViewTarget();
	DialogueCameraActor = GetWorld()->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), Source->GetDialogueCameraTransform());
	if (!DialogueCameraActor)
	{
		return;
	}
	const FVector CameraLocation = DialogueCameraActor->GetActorLocation();
	DialogueCameraActor->SetActorRotation((NPC->GetActorLocation() + Data->CameraLookAtOffset - CameraLocation).Rotation());
	DialogueCameraActor->GetCameraComponent()->SetFieldOfView(Data->CameraFieldOfView);
	PC->SetViewTargetWithBlend(DialogueCameraActor, Data->CameraBlendInTime, VTBlend_Cubic);
}

void UPlayerDialogueComponent::StopClientCamera()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	float BlendOutTime = 0.25f;
	if (CurrentView.NPC)
	{
		if (const UNPCDialogueSourceComponent* Source =
			CurrentView.NPC->FindComponentByClass<UNPCDialogueSourceComponent>())
		{
			if (const UNPCDialogueData* Data = Source->GetDialogueData())
			{
				BlendOutTime = Data->CameraBlendOutTime;
			}
		}
	}
	if (PC)
	{
		AActor* RestoreTarget = PreviousViewTarget.IsValid() ? PreviousViewTarget.Get() : GetOwner();
		PC->SetViewTargetWithBlend(RestoreTarget, BlendOutTime, VTBlend_Cubic);
	}
	if (DialogueCameraActor)
	{
		DialogueCameraActor->SetLifeSpan(FMath::Max(BlendOutTime, 0.01f) + 0.05f);
		DialogueCameraActor = nullptr;
	}
	PreviousViewTarget.Reset();
}
