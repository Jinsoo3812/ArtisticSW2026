#include "RangedEnemy/RangedEnemy.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Components/BaseHealthComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "RangedEnemy/RangedEnemyAIController.h"
#include "Ship.h"
#include "TimerManager.h"
#include "Weapon/BaseWeaponComponent.h"
#include "Weapon/EnemyBow.h"
#include "Weapon/WeaponDataAsset.h"

ARangedEnemy::ARangedEnemy()
{
	AIControllerClass = ARangedEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = true;
	// The ranged Combat subtree already starts with EQS movement. Equipping at
	// spawn guarantees that the bow-granted GA is ready before the first query
	// finishes, without adding an asset-only setup dependency to the BT.
	bEquipWeaponOnSpawn = true;
	DefaultWeaponTag = Item_EnemyWeapon_Bow;

	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->bOrientRotationToMovement = false;
		MovementComponent->bUseControllerDesiredRotation = false;
		MovementComponent->MaxWalkSpeed = 0.0f;
	}

	FireEventTag = Event_Montage_FireArrow;
}

void ARangedEnemy::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && bAutoResolveHostShip)
	{
		HostShipResolveAttemptCount = 0;
		RetryResolveHostShip();
	}
}

void ARangedEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(HostShipResolveTimerHandle);
	UnbindHostShipLifecycle();
	ClearCombatTarget();
	Super::EndPlay(EndPlayReason);
}

void ARangedEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARangedEnemy, HostShip);
}

void ARangedEnemy::SetHostShip(AShip* NewHostShip)
{
	if (!HasAuthority() || HostShip == NewHostShip)
	{
		return;
	}

	UnbindHostShipLifecycle();
	HostShip = NewHostShip;
	BindHostShipLifecycle();
	ForceNetUpdate();

	if (HostShip)
	{
		GetWorldTimerManager().ClearTimer(HostShipResolveTimerHandle);
	}
}

bool ARangedEnemy::ResolveHostShip()
{
	if (IsValid(HostShip))
	{
		BindHostShipLifecycle();
		GetWorldTimerManager().ClearTimer(HostShipResolveTimerHandle);
		return true;
	}

	if (!HasAuthority())
	{
		return false;
	}

	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		if (UPrimitiveComponent* MovementBase = MovementComponent->GetMovementBase())
		{
			if (AShip* BasedShip = FindShipInActorHierarchy(MovementBase->GetOwner()))
			{
				SetHostShip(BasedShip);
				return true;
			}
		}
	}

	if (AShip* AttachedShip = FindShipInActorHierarchy(GetAttachParentActor()))
	{
		SetHostShip(AttachedShip);
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RangedEnemyResolveHostShip), false, this);
	const FVector Start = GetActorLocation() + FVector(0.0f, 0.0f, 50.0f);
	const FVector End = Start - FVector(0.0f, 0.0f, 500.0f);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldDynamic, QueryParams))
	{
		AActor* HitOwner = Hit.GetComponent() ? Hit.GetComponent()->GetOwner() : Hit.GetActor();
		if (AShip* TracedShip = FindShipInActorHierarchy(HitOwner))
		{
			SetHostShip(TracedShip);
			return true;
		}
	}

	return false;
}

void ARangedEnemy::SetCombatTarget(AActor* NewTarget)
{
	FString ValidationReason;
	const bool bAccepted = EvaluateCombatTarget(NewTarget, ValidationReason);
	CombatTarget = bAccepted ? NewTarget : nullptr;
}

void ARangedEnemy::ClearCombatTarget()
{
	CombatTarget = nullptr;
}

bool ARangedEnemy::IsValidCombatTarget(const AActor* Candidate) const
{
	FString UnusedReason;
	return EvaluateCombatTarget(Candidate, UnusedReason);
}

bool ARangedEnemy::CanEngageActor_Implementation(AActor* Candidate) const
{
	return IsValidCombatTarget(Candidate);
}

bool ARangedEnemy::EvaluateCombatTarget(const AActor* Candidate, FString& OutReason) const
{
	if (!IsValid(Candidate))
	{
		OutReason = TEXT("InvalidActor");
		return false;
	}

	const ABasePlayer* Player = Cast<ABasePlayer>(Candidate);
	if (!Player)
	{
		OutReason = TEXT("NotBasePlayer");
		return false;
	}

	if (Player->IsActorBeingDestroyed())
	{
		OutReason = TEXT("ActorBeingDestroyed");
		return false;
	}

	if (const UBaseHealthComponent* TargetHealthComponent = Player->FindComponentByClass<UBaseHealthComponent>())
	{
		if (TargetHealthComponent->IsDead())
		{
			OutReason = TEXT("TargetDead");
			return false;
		}
	}

	const UAbilitySystemComponent* TargetASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<ABasePlayer*>(Player));
	// A concrete ABasePlayer is sufficient for targeting. Collision can be disabled
	// while boarding a Ship/Cannon, and Team.Player may arrive after initial possession;
	// both remain diagnostic values instead of silently blocking the attack loop.
	if (TargetASC && TargetASC->HasMatchingGameplayTag(Team_Enemy))
	{
		OutReason = TEXT("TargetHasTeamEnemy");
		return false;
	}

	OutReason = TEXT("ValidBasePlayer");
	return true;
}

bool ARangedEnemy::HasLineOfSightTo(const AActor* Candidate) const
{
	return TraceLineOfSight(Candidate);
}

bool ARangedEnemy::TraceLineOfSight(const AActor* Candidate, FHitResult* OutHit) const
{
	if (!IsValidCombatTarget(Candidate) || !GetWorld())
	{
		if (OutHit)
		{
			*OutHit = FHitResult();
		}
		return false;
	}

	FTransform ArrowSpawnTransform;
	if (!GetRangedAttackOrigin(ArrowSpawnTransform))
	{
		if (OutHit)
		{
			*OutHit = FHitResult();
		}
		return false;
	}

	const FVector Start = ArrowSpawnTransform.GetLocation();
	const FVector End = GetRangedAimLocation(Candidate);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RangedEnemyAttackLOS), true, this);
	QueryParams.AddIgnoredActor(this);
	if (HostShip)
	{
		QueryParams.AddIgnoredActor(HostShip);
	}

	FHitResult Hit;
	const bool bBlockingHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams);
	const bool bVisible = !bBlockingHit || Hit.GetActor() == Candidate;
	if (OutHit)
	{
		*OutHit = Hit;
	}

	if (bDrawAttackLineOfSight)
	{
		DrawDebugLine(GetWorld(), Start, End, bVisible ? FColor::Green : FColor::Red, false, 0.25f, 0, 2.0f);
	}

	return bVisible;
}

bool ARangedEnemy::CanAttackCurrentTarget(bool bRequireLineOfSight) const
{
	return CanAttackTarget(CombatTarget, bRequireLineOfSight);
}

bool ARangedEnemy::CanAttackTarget(const AActor* Candidate, bool bRequireLineOfSight) const
{
	FString UnusedReason;
	return EvaluateAttackTarget(Candidate, bRequireLineOfSight, UnusedReason);
}

bool ARangedEnemy::FindRangedAttackAbility(FGameplayAbilitySpecHandle& OutAbilityHandle) const
{
	OutAbilityHandle = FGameplayAbilitySpecHandle();

	const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC)
	{
		return false;
	}

	FGameplayTagContainer AbilityTags;
	AbilityTags.AddTag(GameplayAbility_RangedAttack);

	TArray<FGameplayAbilitySpec*> MatchingAbilities;
	ASC->GetActivatableGameplayAbilitySpecsByAllMatchingTags(AbilityTags, MatchingAbilities, false);
	for (const FGameplayAbilitySpec* AbilitySpec : MatchingAbilities)
	{
		if (AbilitySpec && AbilitySpec->Ability
			&& AbilitySpec->Ability->GetAssetTags().HasTagExact(GameplayAbility_RangedAttack))
		{
			OutAbilityHandle = AbilitySpec->Handle;
			return true;
		}
	}

	return false;
}

bool ARangedEnemy::TryStartRangedAttack(FGameplayAbilitySpecHandle AbilityHandle)
{
	if (!HasAuthority() || !AbilityHandle.IsValid())
	{
		return false;
	}

	FString AttackReason;
	if (!EvaluateAttackTarget(CombatTarget, true, AttackReason))
	{
		return false;
	}

	UWorld* World = GetWorld();
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!World)
	{
		return false;
	}
	if (!ASC)
	{
		return false;
	}
	if (ASC->HasMatchingGameplayTag(State_Attacking))
	{
		return false;
	}

	const FGameplayAbilitySpec* AbilitySpec = ASC->FindAbilitySpecFromHandle(AbilityHandle);
	if (!AbilitySpec || !AbilitySpec->Ability
		|| !AbilitySpec->Ability->GetAssetTags().HasTagExact(GameplayAbility_RangedAttack))
	{
		return false;
	}

	const double CurrentTime = World->GetTimeSeconds();
	if (CurrentTime < NextAttackTime)
	{
		return false;
	}

	const bool bActivated = ASC->TryActivateAbility(AbilityHandle, false);
	if (bActivated)
	{
		NextAttackTime = CurrentTime + FMath::Max(0.0f, AttackCooldown);
	}
	return bActivated;
}

bool ARangedEnemy::TryStartRangedAttack()
{
	FGameplayAbilitySpecHandle AbilityHandle;
	return FindRangedAttackAbility(AbilityHandle) && TryStartRangedAttack(AbilityHandle);
}

float ARangedEnemy::GetRemainingAttackCooldown() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}

	const double RemainingTime =
		NextAttackTime - World->GetTimeSeconds();

	return static_cast<float>(FMath::Max(0.0, RemainingTime));
}

bool ARangedEnemy::EvaluateAttackTarget(const AActor* Candidate, bool bRequireLineOfSight, FString& OutReason) const
{
	if (!EvaluateCombatTarget(Candidate, OutReason))
	{
		return false;
	}

	const float Distance = FVector::Distance(GetActorLocation(), Candidate->GetActorLocation());
	if (Distance < MinAttackRange)
	{
		OutReason = TEXT("BelowMinAttackRange");
		return false;
	}
	if (Distance > MaxAttackRange)
	{
		OutReason = TEXT("AboveMaxAttackRange");
		return false;
	}

	if (bRequireLineOfSight && !TraceLineOfSight(Candidate))
	{
		OutReason = TEXT("LineOfSightBlocked");
		return false;
	}

	OutReason = TEXT("Ready");
	return true;
}

AEnemyBow* ARangedEnemy::GetEquippedBow() const
{
	const UBaseWeaponComponent* EquippedWeaponComponent = GetWeaponComponent();
	return EquippedWeaponComponent && EquippedWeaponComponent->IsWeaponEquipped()
		? Cast<AEnemyBow>(EquippedWeaponComponent->GetCurrentWeapon())
		: nullptr;
}

bool ARangedEnemy::GetRangedAttackOrigin(FTransform& OutSpawnTransform) const
{
	const AEnemyBow* Bow = GetEquippedBow();
	return Bow && Bow->GetArrowSpawnTransform(OutSpawnTransform);
}

FVector ARangedEnemy::GetRangedAimLocation(const AActor* TargetActor) const
{
	return TargetActor
		? TargetActor->GetActorLocation() + FVector(0.0f, 0.0f, TargetAimHeightOffset)
		: FVector::ZeroVector;
}

UAnimMontage* ARangedEnemy::GetRangedAttackMontage() const
{
	const UBaseWeaponComponent* EquippedWeaponComponent = GetWeaponComponent();
	const FWeaponDefinition* WeaponDefinition = EquippedWeaponComponent
		? EquippedWeaponComponent->GetCurrentWeaponDefinition()
		: nullptr;
	if (WeaponDefinition && WeaponDefinition->CombatData.AttackMontage)
	{
		return WeaponDefinition->CombatData.AttackMontage;
	}

	return AttackMontage;
}

float ARangedEnemy::GetRangedAttackMontagePlayRate() const
{
	const UBaseWeaponComponent* EquippedWeaponComponent = GetWeaponComponent();
	const FWeaponDefinition* WeaponDefinition = EquippedWeaponComponent
		? EquippedWeaponComponent->GetCurrentWeaponDefinition()
		: nullptr;
	return WeaponDefinition
		? FMath::Max(0.001f, WeaponDefinition->CombatData.AttackMontagePlayRate)
		: 1.0f;
}

void ARangedEnemy::OnRep_HostShip()
{
	BindHostShipLifecycle();
}

void ARangedEnemy::OnHostShipDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor != HostShip)
	{
		return;
	}

	UnbindHostShipLifecycle();
	HostShip = nullptr;
	if (bDestroyWithHostShip && HasAuthority())
	{
		Destroy();
		return;
	}
}

void ARangedEnemy::BindHostShipLifecycle()
{
	if (HostShip)
	{
		HostShip->OnDestroyed.AddUniqueDynamic(this, &ARangedEnemy::OnHostShipDestroyed);
	}
}

void ARangedEnemy::UnbindHostShipLifecycle()
{
	if (HostShip)
	{
		HostShip->OnDestroyed.RemoveDynamic(this, &ARangedEnemy::OnHostShipDestroyed);
	}
}

void ARangedEnemy::RetryResolveHostShip()
{
	GetWorldTimerManager().ClearTimer(HostShipResolveTimerHandle);
	++HostShipResolveAttemptCount;
	if (ResolveHostShip())
	{
		return;
	}

	if (HostShipResolveAttemptCount >= FMath::Max(1, MaxHostShipResolveAttempts))
	{
		return;
	}

	GetWorldTimerManager().SetTimer(
		HostShipResolveTimerHandle,
		this,
		&ARangedEnemy::RetryResolveHostShip,
		FMath::Max(0.05f, HostShipResolveRetryInterval),
		false);
}

AShip* ARangedEnemy::FindShipInActorHierarchy(AActor* Actor) const
{
	TSet<const AActor*> VisitedActors;
	for (AActor* Current = Actor; Current && !VisitedActors.Contains(Current); Current = Current->GetAttachParentActor())
	{
		VisitedActors.Add(Current);
		if (AShip* Ship = Cast<AShip>(Current))
		{
			return Ship;
		}
	}
	return nullptr;
}
