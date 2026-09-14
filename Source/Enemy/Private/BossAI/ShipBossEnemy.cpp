#include "BossAI/ShipBossEnemy.h"
#include "Components/StatusComponent.h"
#include "Components/CombatHurtboxComponent.h"
#include "GAS/Ability/Boss/BossStunEffects.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "BrainComponent.h"
#include "AI/BaseAIController.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossAIController.h"
#include "Components/BaseHealthComponent.h"
#include "GAS/Ability/Boss/GA_BossDashSlash.h"
#include "GAS/Ability/Boss/GA_BossBasicAttack.h"
#include "GAS/Ability/Boss/GA_BossKnockback.h"
#include "GAS/Ability/Boss/GA_BossVanish.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "ShipAI/EnemyShip.h"
#include "Weapon/BaseWeaponComponent.h"

AShipBossEnemy::AShipBossEnemy()
{
	CombatHurtboxComponent->Mode = ECombatHurtboxMode::AnimatedPhysicsAsset;
	HeadHitStunEffect = UBossHeadHitStunEffect::StaticClass();
	HealthThresholdStunEffect = UBossHealthThresholdStunEffect::StaticClass();
	// Boss damage feedback is intentionally stronger and must not leak into the
	// regular enemy defaults inherited by melee and ranged archetypes.
	GetHealthComponent()->SetDamageGameplayCueTag(GameplayCue_Boss_Hit);

	DashDamageVolume = CreateDefaultSubobject<USphereComponent>(TEXT("DashDamageVolume"));
	DashDamageVolume->SetupAttachment(GetRootComponent());
	DashDamageVolume->InitSphereRadius(120.0f);
	DashDamageVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DashDamageVolume->SetCollisionObjectType(ECC_WorldDynamic);
	DashDamageVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	DashDamageVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	DashDamageVolume->SetGenerateOverlapEvents(true);
	DashDamageVolume->SetCanEverAffectNavigation(false);

	AIControllerClass = AShipBossAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = true;
	bAlwaysRelevant = true;
	bDestroyAfterDeathFinished = true;
	bEquipWeaponOnSpawn = true;
	DefaultWeaponTag = Item_EnemyWeapon_Sword;
	StartingAbilities.Add(UGA_BossBasicAttack::StaticClass());
	StartingAbilities.Add(UGA_BossKnockback::StaticClass());
	StartingAbilities.Add(UGA_BossVanish::StaticClass());
	StartingAbilities.Add(UGA_BossVanishV2::StaticClass());
	StartingAbilities.Add(UGA_BossDashSlash::StaticClass());

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = 0.0f;
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = true;
		// ShipDeckMesh is a collision/query child of the physics-driven ship root.
		// Resolve based movement through that attachment root so the boss inherits
		// the ship's full wave-driven transform without changing the ship itself.
		Movement->bBaseOnAttachmentRoot = true;
	}
}

void AShipBossEnemy::BeginPlay()
{
	InitialCapsuleCollision = GetCapsuleComponent()
		? GetCapsuleComponent()->GetCollisionEnabled()
		: ECollisionEnabled::QueryAndPhysics;
	Super::BeginPlay();

	BindHostShip();
	GetHealthComponent()->OnConfirmedDamage.AddUObject(this, &AShipBossEnemy::HandleConfirmedDamage);
	GetHealthComponent()->OnHealthChanged.AddUniqueDynamic(this, &AShipBossEnemy::HandleStunHealthChanged);
	ApplyHiddenPresentation();
	if (HasAuthority())
	{
		TransitionBossAIState(FGameplayTag(), AI_State_Boss_Intro);
	}
}

void AShipBossEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetHealthComponent()->OnConfirmedDamage.RemoveAll(this);
	GetHealthComponent()->OnHealthChanged.RemoveDynamic(this, &AShipBossEnemy::HandleStunHealthChanged);
	ReleaseSummonedDeckEnemies();
	if (HasAuthority() && HostShip)
	{
		HostShip->ReleaseAllDeckPointsFor(this);
	}
	DestinationReservation.Reset();
	UnbindHostShip();
	Super::EndPlay(EndPlayReason);
}

bool AShipBossEnemy::HasBossBasicAttackStartingAbility() const
{
	return StartingAbilities.Contains(UGA_BossBasicAttack::StaticClass());
}

void AShipBossEnemy::HandleConfirmedDamage(float Damage, const FGameplayEffectContextHandle& Context, bool bPeriodic)
{
	const FHitResult* Hit = Context.GetHitResult();
	if (HasAuthority() && Damage > 0.f && !bPeriodic && Hit && StunHeadBones.Contains(Hit->BoneName)
		&& GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Attacking))
		StatusComponent->ApplyStatus(HeadHitStunEffect, GetAbilitySystemComponent(), Context);
}

void AShipBossEnemy::HandleStunHealthChanged(UBaseHealthComponent* Health, float OldHealth, float NewHealth, AActor* InstigatorActor)
{
	if (!HasAuthority() || bStunHealthThresholdConsumed || StunHealthThreshold <= 0.f) return;
	const float Threshold = Health->GetMaxHealth() * StunHealthThreshold;
	if (OldHealth > Threshold && NewHealth <= Threshold)
	{
		bStunHealthThresholdConsumed = true;
		if (NewHealth > 0.f) StatusComponent->ApplyStatus(HealthThresholdStunEffect, GetAbilitySystemComponent(), {});
	}
}

void AShipBossEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShipBossEnemy, HostShip);
	DOREPLIFETIME(AShipBossEnemy, CurrentPointId);
	DOREPLIFETIME(AShipBossEnemy, PreviousPointId);
	DOREPLIFETIME(AShipBossEnemy, DestinationPointId);
	DOREPLIFETIME(AShipBossEnemy, bBossHidden);
}

bool AShipBossEnemy::InitializeBoss(AEnemyShip* InHostShip, int32 InitialPointId, AActor* InitialTarget)
{
	if (!HasAuthority() || !IsValid(InHostShip)
		|| (InitialTarget && !CanEngageActor(InitialTarget)))
	{
		return false;
	}
	if (!InHostShip->TryOccupyDeckPoint(InitialPointId, this))
	{
		return false;
	}

	UnbindHostShip();
	HostShip = InHostShip;
	CurrentPointId = InitialPointId;
	PreviousPointId = INDEX_NONE;
	DestinationPointId = INDEX_NONE;
	BindHostShip();
	SetBossCombatTarget(InitialTarget);

	if (UStaticMeshComponent* DeckMesh = HostShip->GetShipDeckMesh())
	{
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->SetBase(DeckMesh);
		}
	}
	TransitionBossAIState(AI_State_Boss_Intro, AI_State_Boss_Combat);
	ForceNetUpdate();
	return true;
}

void AShipBossEnemy::SetBossCombatTarget(AActor* NewTarget)
{
	if (!HasAuthority())
	{
		return;
	}

	BossCombatTarget = CanEngageActor(NewTarget) ? NewTarget : nullptr;
	if (ABaseAIController* BossController = Cast<ABaseAIController>(GetController()))
	{
		if (BossCombatTarget)
		{
			BossController->SetCombatTarget(BossCombatTarget);
		}
		else
		{
			BossController->ClearCombatTarget(false);
		}
	}
}

AActor* AShipBossEnemy::GetBossCombatTarget() const
{
	if (const ABaseAIController* BossController = Cast<ABaseAIController>(GetController()))
	{
		if (AActor* ControllerTarget = BossController->GetCombatTarget())
		{
			return ControllerTarget;
		}
	}
	return IsValid(BossCombatTarget) ? BossCombatTarget.Get() : nullptr;
}

void AShipBossEnemy::MarkDestinationReached()
{
	if (!HasAuthority() || DestinationPointId == INDEX_NONE)
	{
		return;
	}
	if (!HostShip || !HostShip->CommitDeckPointReservation(DestinationReservation, this))
	{
		OnDeckMoveFailed();
		return;
	}
	DestinationReservation.Reset();
	HostShip->ReleaseDeckPointOccupancy(CurrentPointId, this);
	PreviousPointId = CurrentPointId;
	CurrentPointId = DestinationPointId;
	DestinationPointId = INDEX_NONE;
	ForceNetUpdate();
}

void AShipBossEnemy::SetDestinationPointId(int32 NewPointId)
{
	TrySetDestinationPointId(NewPointId);
}

bool AShipBossEnemy::TrySetDestinationPointId(int32 NewPointId)
{
	if (!HasAuthority() || !HostShip)
	{
		return false;
	}
	if (NewPointId == DestinationPointId && DestinationReservation.IsValid())
	{
		return true;
	}
	HostShip->ReleaseDeckPointReservation(DestinationReservation);
	DestinationPointId = INDEX_NONE;
	if (NewPointId == INDEX_NONE)
	{
		ForceNetUpdate();
		return true;
	}
	if (NewPointId == CurrentPointId
		|| !HostShip->TryReserveDeckPoint(NewPointId, this, DestinationReservation))
	{
		ForceNetUpdate();
		return false;
	}
	DestinationPointId = NewPointId;
	ForceNetUpdate();
	return true;
}

void AShipBossEnemy::OnDeckMoveFailed()
{
	if (!HasAuthority())
	{
		return;
	}

	if (HostShip)
	{
		HostShip->ReleaseDeckPointReservation(DestinationReservation);
	}
	else
	{
		DestinationReservation.Reset();
	}
	DestinationPointId = INDEX_NONE;
	ForceNetUpdate();
}

bool AShipBossEnemy::CanMoveOnDeck() const
{
	return HasAuthority() && !bDeathHandled && !bBossHidden && IsValid(HostShip);
}

bool AShipBossEnemy::CanSummonDeckEnemy() const
{
	if (!HasAuthority() || bDeathHandled || !IsValid(HostShip) || !CanEngageActor(GetBossCombatTarget()))
	{
		return false;
	}
	if (const UWorld* World = GetWorld(); !World || World->GetTimeSeconds() < NextSummonAllowedTime)
	{
		return false;
	}

	int32 ActiveCount = 0;
	for (const TWeakObjectPtr<ADeckEnemy>& EnemyPtr : SummonedDeckEnemies)
	{
		const ADeckEnemy* Enemy = EnemyPtr.Get();
		if (Enemy && Enemy->IsPoolActive()
			&& Enemy->GetHealthComponent() && !Enemy->GetHealthComponent()->IsDead())
		{
			++ActiveCount;
		}
	}
	return ActiveCount < FMath::Max(1, MaxSummonedDeckEnemies);
}

bool AShipBossEnemy::TrySummonDeckEnemy(ADeckEnemy*& OutEnemy)
{
	OutEnemy = nullptr;
	if (!CanSummonDeckEnemy())
	{
		return false;
	}

	AActor* Target = GetBossCombatTarget();
	UWorld* World = GetWorld();
	NextSummonAllowedTime = World->GetTimeSeconds() + FMath::Max(0.0f, SummonCooldown);
	SummonedDeckEnemies.RemoveAll([](const TWeakObjectPtr<ADeckEnemy>& EnemyPtr)
	{
		const ADeckEnemy* Enemy = EnemyPtr.Get();
		return !Enemy || !Enemy->IsPoolActive()
			|| (Enemy->GetHealthComponent() && Enemy->GetHealthComponent()->IsDead());
	});

	FDeckEnemySpawnRequest Request;
	Request.Requester = this;
	Request.Target = Target;
	Request.ExcludedPointId = CurrentPointId;
	Request.MinimumDistanceFromRequester = MinimumSummonDistanceFromBoss;
	Request.MinimumDistanceFromTarget = MinimumSummonDistanceFromTarget;
	HostShip->GetConnectedDeckWaypointIds(CurrentPointId, Request.PreferredPointIds);

	FDeckPointReservation Reservation;
	if (!HostShip->TryReserveDeckEnemySpawnPoint(Request, Reservation)
		|| !HostShip->ActivateDeckEnemyAtReservation(Reservation, Target, OutEnemy))
	{
		HostShip->ReleaseDeckPointReservation(Reservation);
		return false;
	}
	SummonedDeckEnemies.Add(OutEnemy);
	return true;
}

bool AShipBossEnemy::ResolvePointTransform(int32 PointId, FTransform& OutTransform) const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
	if (!HostShip || !HostShip->ResolveDeckCharacterTransform(PointId, HalfHeight, OutTransform))
	{
		OutTransform = FTransform::Identity;
		return false;
	}
	return true;
}

bool AShipBossEnemy::TransitionBossAIState(FGameplayTag ExpectedState, FGameplayTag NewState)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!HasAuthority() || !ASC || !IsExclusiveBossAIState(NewState))
	{
		return false;
	}
	if (ExpectedState.IsValid() && !ASC->HasMatchingGameplayTag(ExpectedState))
	{
		return false;
	}

	ASC->RemoveLooseGameplayTag(AI_State_Boss_Intro);
	ASC->RemoveLooseGameplayTag(AI_State_Boss_Combat);
	ASC->RemoveLooseGameplayTag(AI_State_Boss_Dead);
	ASC->AddLooseGameplayTag(NewState);
	return true;
}

void AShipBossEnemy::SetBossHidden(bool bInHidden)
{
	// Late Vanish callbacks cannot hide a dead boss after the montage starts.
	if (bDeathHandled)
	{
		bInHidden = false;
	}
	if (!HasAuthority() || bBossHidden == bInHidden)
	{
		return;
	}
	bBossHidden = bInHidden;
	ApplyHiddenPresentation();
	ForceNetUpdate();
}

bool AShipBossEnemy::BeginHiddenRelocation()
{
	if (!HasAuthority() || bDeathHandled || bHiddenRelocationActive || !IsValid(HostShip))
	{
		return false;
	}

	// Visibility is removed before any movement state can change. The ability
	// keeps this state for a separate net-update interval before teleporting.
	SetBossHidden(true);
	if (AAIController* BossAIController = Cast<AAIController>(GetController()))
	{
		BossAIController->StopMovement();
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	bHiddenRelocationActive = true;
	ForceNetUpdate();
	return true;
}

bool AShipBossEnemy::RelocateWhileHidden(const FTransform& DestinationTransform)
{
	if (!HasAuthority() || bDeathHandled || !bHiddenRelocationActive || !bBossHidden || !IsValid(HostShip))
	{
		return false;
	}

	SetActorLocationAndRotation(
		DestinationTransform.GetLocation(),
		DestinationTransform.GetRotation(),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		if (UStaticMeshComponent* DeckMesh = HostShip->GetShipDeckMesh())
		{
			Movement->SetBase(DeckMesh);
		}
		Movement->StopMovementImmediately();
	}

	// Movement is sent while bBossHidden is still true. The ability waits for a
	// second update interval before revealing the destination.
	ForceNetUpdate();
	return true;
}

void AShipBossEnemy::FinishHiddenRelocation()
{
	if (!HasAuthority())
	{
		return;
	}

	if (bDeathHandled)
	{
		ApplyDeathMovementState();
	}
	else if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_Walking);
		if (UStaticMeshComponent* DeckMesh = HostShip ? HostShip->GetShipDeckMesh() : nullptr)
		{
			Movement->SetBase(DeckMesh);
		}
	}

	bHiddenRelocationActive = false;
	SetBossHidden(false);
	ForceNetUpdate();
}

void AShipBossEnemy::HandleDeath_Implementation()
{
	// Capture before ability/BT cleanup. A dash may be between authored points;
	// neither its destination nor its last occupied point is the death location.
	const FTransform DeathWorldTransform = GetActorTransform();
	if (HasAuthority())
	{
		if (AAIController* BossController = Cast<AAIController>(GetController()))
		{
			BossController->StopMovement();
			BossController->ClearFocus(EAIFocusPriority::Gameplay);
			if (UBrainComponent* Brain = BossController->GetBrainComponent())
			{
				Brain->StopLogic(TEXT("Boss died"));
			}
		}
		if (HostShip)
		{
			HostShip->ReleaseAllDeckPointsFor(this);
		}
		DestinationReservation.Reset();
		ReleaseSummonedDeckEnemies();
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
		{
			ASC->CancelAllAbilities();
		}
		if (WeaponComponent)
		{
			WeaponComponent->DestroyCurrentWeapon();
		}
		if (bHiddenRelocationActive)
		{
			FinishHiddenRelocation();
		}
		else
		{
			SetBossHidden(false);
		}
		TransitionBossAIState(FGameplayTag(), AI_State_Boss_Dead);
		BossCombatTarget = nullptr;
		AnchorDeathToDeck(DeathWorldTransform);
	}
	ApplyDeathMovementState();
	ApplyHiddenPresentation();
	Super::HandleDeath_Implementation();
}

void AShipBossEnemy::ApplyDeathMovementState()
{
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
		Movement->SetBase(nullptr);
		// Attachment owns the entire transform now. Do not let simulated-proxy
		// smoothing, based movement or root motion move the corpse independently.
		Movement->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
		Movement->SetComponentTickEnabled(false);
	}
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (DashDamageVolume)
	{
		DashDamageVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// Clear any outstanding client smoothing offset, then let the AnimBP
		// continue evaluating the death montage (including offscreen/server).
		CharacterMesh->SetRelativeLocationAndRotation(GetBaseTranslationOffset(), GetBaseRotationOffset());
		CharacterMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		if (UAnimInstance* AnimInstance = CharacterMesh->GetAnimInstance())
		{
			AnimInstance->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
		}
	}
}

void AShipBossEnemy::AnchorDeathToDeck(const FTransform& DeathWorldTransform)
{
	ApplyDeathMovementState();
	UStaticMeshComponent* DeckMesh = IsValid(HostShip) ? HostShip->GetShipDeckMesh() : nullptr;
	if (IsValid(DeckMesh))
	{
		const FTransform DeathLocalTransform = DeathWorldTransform.GetRelativeTransform(DeckMesh->GetComponentTransform());
		if (AttachToComponent(DeckMesh, FAttachmentTransformRules::KeepWorldTransform))
		{
			GetRootComponent()->SetRelativeTransform(DeathLocalTransform);
		}
	}
	// Native AttachmentReplication sends the parent AND relative transform.
	// Clients must never capture a second anchor from their delayed world pose.
	ForceNetUpdate();
}

void AShipBossEnemy::HandleDeathFinishedPresentation()
{
	// The authored montage disables auto blend-out and holds its final pose.
	// Keep it evaluating locally: DeathFinished can arrive before a client's
	// montage reaches the end. BaseEnemy still owns the existing corpse lifespan.
	ApplyDeathMovementState();
}

void AShipBossEnemy::ApplyLocalDeathRagdoll()
{
	// Preserve the boss-only animation policy for legacy Blueprint callers too.
}

void AShipBossEnemy::ReleaseSummonedDeckEnemies()
{
	if (HasAuthority())
	{
		for (const TWeakObjectPtr<ADeckEnemy>& EnemyPtr : SummonedDeckEnemies)
		{
			if (ADeckEnemy* Enemy = EnemyPtr.Get(); Enemy && Enemy->IsPoolActive())
			{
				Enemy->DeactivateToPool();
			}
		}
	}
	SummonedDeckEnemies.Reset();
}

void AShipBossEnemy::OnRep_HostShip()
{
	BindHostShip();
	if (bDeathHandled || (GetHealthComponent() && GetHealthComponent()->IsDead()))
	{
		ApplyDeathMovementState();
		return;
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement();
		Movement && HostShip && HostShip->GetShipDeckMesh())
	{
		Movement->SetBase(HostShip->GetShipDeckMesh());
	}
}

void AShipBossEnemy::OnRep_BossHidden()
{
	ApplyHiddenPresentation();
}

void AShipBossEnemy::HandleHostShipDestroyed(AActor* DestroyedActor)
{
	if (HasAuthority() && DestroyedActor == HostShip && !IsActorBeingDestroyed())
	{
		DestinationReservation.Reset();
		Destroy();
	}
}

void AShipBossEnemy::BindHostShip()
{
	if (HostShip)
	{
		HostShip->OnDestroyed.AddUniqueDynamic(this, &AShipBossEnemy::HandleHostShipDestroyed);
	}
}

void AShipBossEnemy::UnbindHostShip()
{
	if (HostShip)
	{
		HostShip->OnDestroyed.RemoveDynamic(this, &AShipBossEnemy::HandleHostShipDestroyed);
	}
}

void AShipBossEnemy::ApplyHiddenPresentation()
{
	// Death replication can arrive before the final Vanish visibility update.
	const bool bIsDead = bDeathHandled || (GetHealthComponent() && GetHealthComponent()->IsDead());
	const bool bShouldHide = bBossHidden && !bIsDead;
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (bShouldHide)
	{
		// Hide first so neither collision removal nor later movement correction is visible.
		SetActorHiddenInGame(true);
		if (Capsule)
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
	else if (Capsule)
	{
		// Restore collision before visibility. The server has already restored the
		// movement base and Walking mode during FinishHiddenRelocation.
		Capsule->SetCollisionEnabled(bIsDead
			? ECollisionEnabled::NoCollision : InitialCapsuleCollision);
	}

	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	for (AActor* AttachedActor : AttachedActors)
	{
		if (AttachedActor)
		{
			AttachedActor->SetActorHiddenInGame(bShouldHide);
		}
	}

	if (!bShouldHide)
	{
		SetActorHiddenInGame(false);
	}
}

bool AShipBossEnemy::IsExclusiveBossAIState(FGameplayTag StateTag) const
{
	return StateTag == AI_State_Boss_Intro
		|| StateTag == AI_State_Boss_Combat
		|| StateTag == AI_State_Boss_Dead;
}
