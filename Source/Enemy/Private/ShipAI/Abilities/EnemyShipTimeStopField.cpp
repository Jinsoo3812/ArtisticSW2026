#include "ShipAI/Abilities/EnemyShipTimeStopField.h"

#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Cannon.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Ship.h"
#include "TimerManager.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "MultiGameMode.h"

AEnemyShipTimeStopField::AEnemyShipTimeStopField()
{
	CreateDefaultSubobject<USWRoomSnapshotComponent>(TEXT("RoomSnapshot"));
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetActorEnableCollision(false);
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void AEnemyShipTimeStopField::InitializeTimeStop(float InRadius, float InDurationSeconds)
{
	if (!HasAuthority())
	{
		return;
	}
	EffectRadius = FMath::Max(1.0f, InRadius);
	EffectDurationSeconds = FMath::Max(0.05f, InDurationSeconds);
	FreezeSourceId = FGuid::NewGuid();
	GatherAffectedTargets();
	ApplyAllTargets();
	GetWorldTimerManager().SetTimer(
		ExpirationTimerHandle, this, &AEnemyShipTimeStopField::FinishTimeStop,
		EffectDurationSeconds, false);
	ForceNetUpdate();
}

void AEnemyShipTimeStopField::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bReleased || (HasAuthority() && GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot()))
	{
		return;
	}

#if !UE_SERVER
	if (bDrawDebugSphere && GetWorld() && GetNetMode() != NM_DedicatedServer)
	{
		DrawDebugSphere(
			GetWorld(), GetActorLocation(), EffectRadius, 48, DebugSphereColor,
			false, 0.0f, 0, 3.0f);
	}
#endif

	ApplyAllTargets();
}

void AEnemyShipTimeStopField::CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const
{
	FSWRoomTimeStopFieldState State;
	State.FreezeSourceId = FreezeSourceId;
	State.EffectRadius = EffectRadius;
	State.EffectDurationSeconds = EffectDurationSeconds;
	State.ExpirationRemaining = GetWorldTimerManager().IsTimerActive(ExpirationTimerHandle)
		? FMath::Max(0.f, GetWorldTimerManager().GetTimerRemaining(ExpirationTimerHandle)) : 0.f;
	State.bReleased = bReleased;
	const AMultiGameMode* Mode = GetWorld() ? Cast<AMultiGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
	for (const FEnemyShipTimeStopTarget& Target : AffectedTargets)
	{
		if (!IsValid(Target.Actor)) continue;
		FSWRoomTimeStopTargetState Saved;
		Saved.Anchor = Target.Anchor;
		Saved.TargetType = static_cast<uint8>(Target.Type);
		if (Target.Type == EEnemyShipTimeStopTargetType::PlayerCharacter)
		{
			const ABasePlayer* Player = Cast<ABasePlayer>(Target.Actor);
			Saved.PlayerIndex = Mode && Player ? Mode->GetPlayerIndex(Player->GetController()) : INDEX_NONE;
		}
		else if (const USWRoomSnapshotComponent* Id = Target.Actor->FindComponentByClass<USWRoomSnapshotComponent>())
			Saved.ActorId = Id->StableId;
		if ((Target.Type == EEnemyShipTimeStopTargetType::PlayerCharacter && Saved.PlayerIndex == INDEX_NONE)
			|| (Target.Type == EEnemyShipTimeStopTargetType::PlayerShip && !Saved.ActorId.IsValid()))
		{
			FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Domain = TEXT("Area");
			Issue.FieldKey = FName(*(TEXT("TimeStopTarget:") + Target.Actor->GetPathName()));
			Issue.Reason = TEXT("Time-stop target lacks stable room identity");
			continue;
		}
		State.Targets.Add(MoveTemp(Saved));
	}
	FSWRoomDomainPart& Part = OutParts.AddDefaulted_GetRef();
	Part.Domain = ESWRoomDomain::Area;
	Part.Version = 1;
	if (!FSWRoomStructCodec::Write(State, Part.Bytes))
	{
		OutParts.Pop();
		FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("Area");
		Issue.FieldKey = TEXT("TimeStopState");
		Issue.Reason = TEXT("Time-stop field serialization failed");
	}
}

bool AEnemyShipTimeStopField::RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError)
{
	FSWRoomTimeStopFieldState State;
	if (Part.Domain != ESWRoomDomain::Area || Part.Version != 1 || !FSWRoomStructCodec::Read(Part.Bytes, State)
		|| !State.FreezeSourceId.IsValid() || !FMath::IsFinite(State.EffectRadius) || State.EffectRadius <= 0.f
		|| !FMath::IsFinite(State.EffectDurationSeconds) || State.EffectDurationSeconds <= 0.f
		|| !FMath::IsFinite(State.ExpirationRemaining) || State.ExpirationRemaining < 0.f)
	{
		OutError = TEXT("Invalid time-stop field state");
		return false;
	}
	for (const FSWRoomTimeStopTargetState& Target : State.Targets)
		if (Target.Anchor.ContainsNaN() || Target.TargetType > static_cast<uint8>(EEnemyShipTimeStopTargetType::PlayerCharacter))
		{
			OutError = TEXT("Invalid time-stop target");
			return false;
		}
	GetWorldTimerManager().ClearTimer(ExpirationTimerHandle);
	ReleaseAllTargets();
	AffectedTargets.Reset();
	FreezeSourceId = State.FreezeSourceId;
	EffectRadius = State.EffectRadius;
	EffectDurationSeconds = State.EffectDurationSeconds;
	bReleased = State.bReleased;
	PendingRoomState = MoveTemp(State);
	bHasPendingRoomState = true;
	return true;
}

bool AEnemyShipTimeStopField::FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError)
{
	if (!bHasPendingRoomState) return true;
	bHasPendingRoomState = false;
	const AMultiGameMode* Mode = Cast<AMultiGameMode>(GetWorld()->GetAuthGameMode());
	for (const FSWRoomTimeStopTargetState& Saved : PendingRoomState.Targets)
	{
		AActor* Actor = nullptr;
		if (Saved.TargetType == static_cast<uint8>(EEnemyShipTimeStopTargetType::PlayerShip))
		{
			if (AActor* const* Found = RegisteredActors.Find(Saved.ActorId)) Actor = *Found;
		}
		else if (Mode)
		{
			for (TActorIterator<ABasePlayer> It(GetWorld()); It; ++It)
				if (Mode->GetPlayerIndex(It->GetController()) == Saved.PlayerIndex) { Actor = *It; break; }
		}
		if (!Actor)
		{
			UE_LOG(LogTemp, Warning, TEXT("Time-stop restore omitted missing target Id=%s Player=%d"),
				*Saved.ActorId.ToString(), Saved.PlayerIndex);
			continue;
		}
		FEnemyShipTimeStopTarget& Target = AffectedTargets.AddDefaulted_GetRef();
		Target.Actor = Actor;
		Target.Anchor = Saved.Anchor;
		Target.Type = static_cast<EEnemyShipTimeStopTargetType>(Saved.TargetType);
	}
	if (!bReleased)
	{
		ApplyAllTargets();
		if (PendingRoomState.ExpirationRemaining <= 0.f)
			ExpirationTimerHandle = GetWorldTimerManager().SetTimerForNextTick(this, &AEnemyShipTimeStopField::FinishTimeStop);
		else
			GetWorldTimerManager().SetTimer(ExpirationTimerHandle, this, &AEnemyShipTimeStopField::FinishTimeStop,
				PendingRoomState.ExpirationRemaining, false);
	}
	ForceNetUpdate();
	return true;
}

void AEnemyShipTimeStopField::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ExpirationTimerHandle);
	ReleaseAllTargets();
	Super::EndPlay(EndPlayReason);
}

void AEnemyShipTimeStopField::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEnemyShipTimeStopField, AffectedTargets);
	DOREPLIFETIME(AEnemyShipTimeStopField, EffectRadius);
	DOREPLIFETIME(AEnemyShipTimeStopField, EffectDurationSeconds);
	DOREPLIFETIME(AEnemyShipTimeStopField, FreezeSourceId);
}

void AEnemyShipTimeStopField::OnRep_AffectedTargets()
{
	ApplyAllTargets();
}

void AEnemyShipTimeStopField::GatherAffectedTargets()
{
	AffectedTargets.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<FString> ShipNames;
	TArray<FString> PlayerNames;
	const float RadiusSquared = FMath::Square(EffectRadius);
	for (TActorIterator<AShip> It(World); It; ++It)
	{
		AShip* Ship = *It;
		if (!IsValid(Ship) || Ship->IsEnemyShipForEffects()
			|| !Ship->ActorHasTag(TEXT("Player")) || Ship->ActorHasTag(TEXT("Enemy"))
			|| FVector::DistSquared(Ship->GetActorLocation(), GetActorLocation()) > RadiusSquared)
		{
			continue;
		}
		FEnemyShipTimeStopTarget& Entry = AffectedTargets.AddDefaulted_GetRef();
		Entry.Actor = Ship;
		Entry.Anchor = Ship->BuoyancyRoot
			? Ship->BuoyancyRoot->GetComponentTransform()
			: Ship->GetActorTransform();
		Entry.Type = EEnemyShipTimeStopTargetType::PlayerShip;
		ShipNames.Add(Ship->GetName());
	}

	for (TActorIterator<ABasePlayer> It(World); It; ++It)
	{
		ABasePlayer* Player = *It;
		if (!IsValid(Player)
			|| FVector::DistSquared(Player->GetActorLocation(), GetActorLocation()) > RadiusSquared)
		{
			continue;
		}
		FEnemyShipTimeStopTarget& Entry = AffectedTargets.AddDefaulted_GetRef();
		Entry.Actor = Player;
		Entry.Anchor = Player->GetActorTransform();
		Entry.Type = EEnemyShipTimeStopTargetType::PlayerCharacter;
		PlayerNames.Add(Player->GetName());
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[EnemyShipTimeStop] Hit confirmed. Center=%s Radius=%.1f Ships=[%s] Players=[%s]"),
		*GetActorLocation().ToCompactString(), EffectRadius,
		*FString::Join(ShipNames, TEXT(", ")),
		*FString::Join(PlayerNames, TEXT(", ")));
}

void AEnemyShipTimeStopField::ApplyAllTargets()
{
	for (const FEnemyShipTimeStopTarget& Target : AffectedTargets)
	{
		if (!IsValid(Target.Actor))
		{
			continue;
		}
		if (Target.Type == EEnemyShipTimeStopTargetType::PlayerShip)
		{
			ApplyShipTarget(Target);
		}
		else
		{
			ApplyPlayerTarget(Target);
		}
	}
}

void AEnemyShipTimeStopField::ApplyShipTarget(const FEnemyShipTimeStopTarget& Target)
{
	AShip* Ship = Cast<AShip>(Target.Actor);
	if (!Ship || !Ship->BuoyancyRoot)
	{
		return;
	}

	if (FreezeSourceId.IsValid())
	{
		Ship->AddPropulsionSuppression(FreezeSourceId);
	}
	if (!TaggedShips.Contains(Ship))
	{
		if (UAbilitySystemComponent* ASC = Ship->GetAbilitySystemComponent())
		{
			ASC->AddLooseGameplayTag(State_Debuff_TimeStopped);
			TaggedShips.Add(Ship);
		}
	}

	if (!ShipConstraints.Contains(Ship))
	{
		if (UPhysicsConstraintComponent* Constraint = CreateWorldLock(Ship, Target.Anchor))
		{
			ShipConstraints.Add(Ship, Constraint);
		}
	}

	Ship->BuoyancyRoot->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Ship->BuoyancyRoot->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
}

void AEnemyShipTimeStopField::ApplyPlayerTarget(const FEnemyShipTimeStopTarget& Target)
{
	ABasePlayer* Player = Cast<ABasePlayer>(Target.Actor);
	if (!Player)
	{
		return;
	}

	const bool bNewRuntime = !PlayerRuntimeStates.Contains(Player);
	FPlayerRuntimeState& Runtime = PlayerRuntimeStates.FindOrAdd(Player);
	Runtime.Player = Player;
	UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	if (bNewRuntime)
	{
		Runtime.bCapturedBaseline = !ASC
			|| !ASC->HasMatchingGameplayTag(State_Debuff_TimeStopped);

		// Input suppression can prevent Enhanced Input's Completed event from
		// reaching the character. Clear the locomotion caches explicitly so a
		// held movement or sprint input cannot leave the frozen player in a
		// moving animation pose while their CharacterMovement is disabled.
		Player->ConsumeMovementInputVector();
		Player->StopMoveInput();
		Player->StopSprint();
	}
	if (!Runtime.bMovementSuppressed)
	{
		if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
		{
			Runtime.SavedMovementMode = static_cast<uint8>(Movement->MovementMode);
			Runtime.SavedCustomMovementMode = Movement->CustomMovementMode;
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
			Runtime.bMovementSuppressed = true;
		}
	}

	if (ASC)
	{
		if (!Runtime.bTagApplied)
		{
			ASC->AddLooseGameplayTag(State_Debuff_TimeStopped);
			Runtime.bTagApplied = true;
		}
		if (HasAuthority() && !Runtime.bAbilitiesCancelled)
		{
			ASC->CancelAllAbilities();
			Runtime.bAbilitiesCancelled = true;
		}
	}

	if (!Runtime.bInputSuppressed)
	{
		if (APlayerController* PC = FindControllerForPlayer(Player))
		{
			Runtime.Controller = PC;
			Runtime.ControlledPawn = PC->GetPawn();
			PC->SetIgnoreMoveInput(true);
			PC->SetIgnoreLookInput(true);
			if (APawn* ControlledPawn = Runtime.ControlledPawn.Get())
			{
				ControlledPawn->DisableInput(PC);
			}
			Runtime.bInputSuppressed = true;
		}
	}

	const AShip* ControlledShip = Runtime.Controller.IsValid()
		? Cast<AShip>(Runtime.Controller->GetPawn())
		: nullptr;
	const bool bIsAttachedHelmRider = ControlledShip
		&& ControlledShip->GetRidingPlayer() == Player;
	if (!bIsAttachedHelmRider)
	{
		Player->SetActorLocationAndRotation(
			Target.Anchor.GetLocation(), Target.Anchor.Rotator(), false, nullptr,
			ETeleportType::TeleportPhysics);
	}
}

void AEnemyShipTimeStopField::FinishTimeStop()
{
	ReleaseAllTargets();
	Destroy();
}

void AEnemyShipTimeStopField::TransferPlayerBaseline(
	ABasePlayer* Player,
	const FPlayerRuntimeState& Runtime)
{
	if (!Player || !Runtime.bCapturedBaseline || !GetWorld())
	{
		return;
	}
	for (TActorIterator<AEnemyShipTimeStopField> It(GetWorld()); It; ++It)
	{
		AEnemyShipTimeStopField* OtherField = *It;
		if (!OtherField || OtherField == this || OtherField->bReleased)
		{
			continue;
		}
		if (FPlayerRuntimeState* OtherRuntime = OtherField->PlayerRuntimeStates.Find(Player))
		{
			OtherRuntime->SavedMovementMode = Runtime.SavedMovementMode;
			OtherRuntime->SavedCustomMovementMode = Runtime.SavedCustomMovementMode;
			OtherRuntime->bCapturedBaseline = true;
			return;
		}
	}
}

void AEnemyShipTimeStopField::ReleaseAllTargets()
{
	if (bReleased)
	{
		return;
	}
	bReleased = true;

	for (const FEnemyShipTimeStopTarget& Target : AffectedTargets)
	{
		if (AShip* Ship = Cast<AShip>(Target.Actor))
		{
			if (FreezeSourceId.IsValid())
			{
				Ship->RemovePropulsionSuppression(FreezeSourceId);
			}
			if (UAbilitySystemComponent* ASC = Ship->GetAbilitySystemComponent())
			{
				ASC->RemoveLooseGameplayTag(State_Debuff_TimeStopped);
			}
			if (Ship->BuoyancyRoot)
			{
				Ship->BuoyancyRoot->SetPhysicsLinearVelocity(FVector::ZeroVector);
				Ship->BuoyancyRoot->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			}
		}
	}

	for (TPair<TWeakObjectPtr<AShip>, TObjectPtr<UPhysicsConstraintComponent>>& Pair : ShipConstraints)
	{
		if (Pair.Value)
		{
			Pair.Value->BreakConstraint();
			Pair.Value->DestroyComponent();
		}
	}
	ShipConstraints.Reset();
	TaggedShips.Reset();

	for (TPair<TWeakObjectPtr<ABasePlayer>, FPlayerRuntimeState>& Pair : PlayerRuntimeStates)
	{
		FPlayerRuntimeState& Runtime = Pair.Value;
		ABasePlayer* Player = Runtime.Player.Get();
		bool bStillTimeStopped = false;
		if (Player)
		{
			if (Runtime.bTagApplied)
			{
				if (UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent())
				{
					ASC->RemoveLooseGameplayTag(State_Debuff_TimeStopped);
					bStillTimeStopped = ASC->HasMatchingGameplayTag(State_Debuff_TimeStopped);
				}
			}
			if (bStillTimeStopped)
			{
				TransferPlayerBaseline(Player, Runtime);
			}
			else if (Runtime.bMovementSuppressed && Runtime.bCapturedBaseline)
			{
				if (UCharacterMovementComponent* Movement = Player->GetCharacterMovement())
				{
					Movement->SetMovementMode(
						static_cast<EMovementMode>(Runtime.SavedMovementMode),
						Runtime.SavedCustomMovementMode);
				}
			}
		}
		if (Runtime.bInputSuppressed)
		{
			if (APlayerController* PC = Runtime.Controller.Get())
			{
				if (!bStillTimeStopped)
				{
					if (APawn* ControlledPawn = Runtime.ControlledPawn.Get())
					{
						ControlledPawn->EnableInput(PC);
					}
					PC->SetIgnoreMoveInput(false);
					PC->SetIgnoreLookInput(false);
				}
			}
		}
	}
	PlayerRuntimeStates.Reset();
}

APlayerController* AEnemyShipTimeStopField::FindControllerForPlayer(const ABasePlayer* Player) const
{
	if (!Player || !GetWorld())
	{
		return nullptr;
	}
	if (APlayerController* DirectController = Cast<APlayerController>(Player->GetController()))
	{
		return DirectController;
	}

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (const AShip* Ship = Cast<AShip>(Pawn); Ship && Ship->GetRidingPlayer() == Player)
		{
			return PC;
		}
		if (const ACannon* Cannon = Cast<ACannon>(Pawn); Cannon && Cannon->GetRidingPlayer() == Player)
		{
			return PC;
		}
	}
	return nullptr;
}

UPhysicsConstraintComponent* AEnemyShipTimeStopField::CreateWorldLock(
	AShip* Ship,
	const FTransform& Anchor)
{
	if (!Ship || !Ship->BuoyancyRoot)
	{
		return nullptr;
	}

	Ship->BuoyancyRoot->SetWorldTransform(
		Anchor, false, nullptr, ETeleportType::TeleportPhysics);
	Ship->BuoyancyRoot->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Ship->BuoyancyRoot->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);

	UPhysicsConstraintComponent* Constraint = NewObject<UPhysicsConstraintComponent>(this);
	if (!Constraint)
	{
		return nullptr;
	}
	AddInstanceComponent(Constraint);
	Constraint->SetWorldTransform(Anchor);
	Constraint->SetDisableCollision(true);
	Constraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
	Constraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
	Constraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
	Constraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
	Constraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
	Constraint->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Locked, 0.0f);
	Constraint->RegisterComponent();
	Constraint->SetConstrainedComponents(Ship->BuoyancyRoot, NAME_None, nullptr, NAME_None);
	return Constraint;
}
