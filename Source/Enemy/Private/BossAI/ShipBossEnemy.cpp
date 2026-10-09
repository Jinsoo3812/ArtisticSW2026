#include "BossAI/ShipBossEnemy.h"
#include "Room/SWRoomSnapshotComponent.h"
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
#include "DeckAI/DeckEnemySpawnerComponent.h"
#include "DeckAI/DeckWalkRouteComponent.h"
#include "DeckAI/DeckEnemyCharacterMovementComponent.h"
#include "DeckAI/DeckWalkAreaComponent.h"
#include "DeckAI/DeckCombatTargetResolverComponent.h"
#include "Components/StaticMeshComponent.h"
#include "BaseAttributeSet.h"
#include "DeckAI/DeckWaypointComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "ShipAI/EnemyShip.h"
#include "Weapon/BaseWeaponComponent.h"

namespace
{
	UPrimitiveComponent* ResolveBossDeckBase(const AShipBossEnemy& Boss, AEnemyShip* Ship)
	{
		if (!Ship) return nullptr;
		const UDeckWalkAreaComponent* Area = Ship->GetDeckWalkAreaComponent();
		return Area && Area->IsReady() ? Area->GetMovementBase(Boss) : nullptr;
	}
}

AShipBossEnemy::AShipBossEnemy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UDeckEnemyCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	DeckWalkRouteComponent = CreateDefaultSubobject<UDeckWalkRouteComponent>(TEXT("DeckWalkRouteComponent"));
	DeckTargetResolver = CreateDefaultSubobject<UDeckCombatTargetResolverComponent>(TEXT("DeckTargetResolver"));
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
	if (HasAuthority() && !EncounterBalanceRow.IsNull())
	{
		const auto* Row = EncounterBalanceRow.GetRow<FEnemyEncounterBalanceRow>(TEXT("Boss encounter balance"));
		bool bValid = Row && Row->SummonCount > 0 && Row->SummonAliveLimit > 0
			&& FMath::IsFinite(Row->StrongAttackDamage) && Row->StrongAttackDamage > 0.f
			&& FMath::IsFinite(Row->MajorAttackDamage) && Row->MajorAttackDamage > 0.f
			&& FMath::IsFinite(Row->MajorAttackTelegraphSeconds) && Row->MajorAttackTelegraphSeconds >= 0.f
			&& SummonedEnemyClass && !Row->SummonStats.IsNull();
		if (Row)
		{
			float Previous = 1.f;
			for (float Fraction : Row->SummonHealthFractions)
			{
				bValid &= FMath::IsFinite(Fraction) && Fraction > 0.f && Fraction < Previous;
				Previous = Fraction;
			}
		}
		if (!bValid)
		{
			UE_LOG(LogTemp, Error, TEXT("[EnemyBalance] Invalid boss encounter row or missing summon class: %s"), *GetName());
			Destroy();
			return;
		}
		EncounterBalance = *Row;
		bUseEncounterBalance = true;
		MaxSummonedDeckEnemies = Row->SummonAliveLimit;
		GetHealthComponent()->OnHealthChanged.AddUniqueDynamic(this, &AShipBossEnemy::HandleBalanceHealthChanged);
	}

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
	if (DeckTargetResolver) DeckTargetResolver->Reset();
	if (DeckWalkRouteComponent) DeckWalkRouteComponent->ResetNavigationState();
	GetHealthComponent()->OnConfirmedDamage.RemoveAll(this);
	GetHealthComponent()->OnHealthChanged.RemoveDynamic(this, &AShipBossEnemy::HandleStunHealthChanged);
	ReleaseSummonedDeckEnemies();
	if (HasAuthority() && HostShip)
	{
		HostShip->ReleaseAllDeckPointsFor(this);
	}
	ClearDestination();
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
	DOREPLIFETIME(AShipBossEnemy, InitialSpawnPointId);
	DOREPLIFETIME(AShipBossEnemy, DestinationLocation);
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

	ClearDestination();
	if (DeckWalkRouteComponent) DeckWalkRouteComponent->ResetNavigationState();
	UnbindHostShip();
	HostShip = InHostShip;
	DeckTargetResolver->Reset();
	InitialSpawnPointId = InitialPointId;
	DestinationLocation = FDeckWalkLocation();
	PreviousLocation = FDeckWalkLocation();
	BindHostShip();
	SetBossCombatTarget(InitialTarget);

	if (UStaticMeshComponent* DeckMesh = HostShip->GetShipDeckMesh())
	{
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->SetBase(ResolveBossDeckBase(*this, HostShip));
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
	DeckTargetResolver->Reset();
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

bool AShipBossEnemy::HasDestination() const
{
	const UDeckWalkAreaComponent* Area = HostShip ? HostShip->GetDeckWalkAreaComponent() : nullptr;
	return Area && Area->IsLocationValid(DestinationLocation);
}

void AShipBossEnemy::MarkDestinationReached()
{
	if (!HasAuthority() || !HasDestination()) { OnDeckMoveFailed(); return; }
	ClearDestination();
}

bool AShipBossEnemy::TrySetDestinationLocation(const FDeckWalkLocation& Location, bool bWalking)
{
	if (!HasAuthority() || !HostShip) return false;
	UDeckWalkAreaComponent* Area = HostShip->GetDeckWalkAreaComponent();
	if (!Area || !Area->TryClaimLocation(Location, *this)) return false;
	if (bWalking && (!DeckWalkRouteComponent || !DeckWalkRouteComponent->SetLocationGoal(Location)))
	{
		Area->RestoreLocationClaim(DestinationLocation, *this);
		return false;
	}
	if (!bWalking && DeckWalkRouteComponent) DeckWalkRouteComponent->ClearGoal();
	if (!bWalking) WalkingTarget.Reset();
	Area->ResolveActorOnDeck(*this, PreviousLocation);
	HostShip->ReleaseDeckPointOccupancy(InitialSpawnPointId, this);
	DestinationLocation = Location;
	ForceNetUpdate();
	return true;
}

void AShipBossEnemy::ClearDestination()
{
	if (!HasAuthority()) return;
	WalkingTarget.Reset();
	if (DeckWalkRouteComponent) DeckWalkRouteComponent->ClearGoal();
	if (HostShip && HostShip->GetDeckWalkAreaComponent()) HostShip->GetDeckWalkAreaComponent()->ReleaseLocationClaim(this);
	DestinationLocation = FDeckWalkLocation();
	ForceNetUpdate();
}

void AShipBossEnemy::TrackWalkingTarget(AActor* Target, const FBossDestinationSelectionSettings& Settings)
{
	if (!HasAuthority()) return;
	WalkingTarget = Target; WalkingSettings = Settings;
	FDeckTargetAnchor Anchor;
	if (DeckTargetResolver->Resolve(Target, Anchor))
	{ PlannedWalkingCenter = Anchor.LocalCenter; PlannedWalkingSurface = Anchor.SurfaceId; }
	NextWalkingReplanTime = GetWorld()->GetTimeSeconds() + 0.35;
}

void AShipBossEnemy::ReplanWalkingTarget()
{
	if (!WalkingTarget.IsValid() || !HasDestination() || !DeckWalkRouteComponent->HasGoal()
		|| WalkingTarget.Get() != GetBossCombatTarget() || GetWorld()->GetTimeSeconds() < NextWalkingReplanTime
		|| (GetAbilitySystemComponent() && GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Boss_Busy))) return;
	NextWalkingReplanTime = GetWorld()->GetTimeSeconds() + 0.35;
	FDeckTargetAnchor Anchor;
	if (!DeckTargetResolver->Resolve(WalkingTarget.Get(), Anchor)) return;
	if (Anchor.SurfaceId == PlannedWalkingSurface && FVector::Dist2D(Anchor.LocalCenter, PlannedWalkingCenter) < 100.f) return;
	FDeckWalkLocation NewGoal;
	if (UBossDeckPointSelector::SelectDestinationLocation(HostShip, this, WalkingTarget.Get(), EBossDestinationPurpose::Walk,
		EBossDestinationRelation::Any, WalkingSettings, NewGoal) && TrySetDestinationLocation(NewGoal, true))
	{ PlannedWalkingCenter = Anchor.LocalCenter; PlannedWalkingSurface = Anchor.SurfaceId; }
}

void AShipBossEnemy::OnDeckMoveFailed() { ClearDestination(); }

bool AShipBossEnemy::CanMoveOnDeck() const
{
	return HasAuthority() && !bDeathHandled && !bBossHidden && IsValid(HostShip);
}

bool AShipBossEnemy::CanSummonDeckEnemy() const
{
	if (bUseEncounterBalance && PendingBalanceSummons <= 0) return false;
	if (!HasAuthority() || bDeathHandled || !IsValid(HostShip) || HostShip->IsCrewDefeated() || !CanEngageActor(GetBossCombatTarget()))
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

bool AShipBossEnemy::SummonOneDeckEnemy(ADeckEnemy*& OutEnemy)
{
	OutEnemy = nullptr;
	if (!CanSummonDeckEnemy())
	{
		return false;
	}

	AActor* Target = GetBossCombatTarget();
	UWorld* World = GetWorld();
	if (!bUseEncounterBalance)
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
	Request.ExcludedPointId = InitialSpawnPointId;
	Request.MinimumDistanceFromRequester = MinimumSummonDistanceFromBoss;
	Request.MinimumDistanceFromTarget = MinimumSummonDistanceFromTarget;

	FDeckPointReservation Reservation;
	if (!HostShip->TryReserveDeckEnemySpawnPoint(Request, Reservation)
		|| !HostShip->GetDeckEnemySpawnerComponent()->ActivateEnemyAtReservation(Reservation, Target, OutEnemy,
			SummonedEnemyClass, bUseEncounterBalance ? EncounterBalance.SummonStats : FDataTableRowHandle()))
	{
		HostShip->ReleaseDeckPointReservation(Reservation);
		return false;
	}
	SummonedDeckEnemies.Add(OutEnemy);
	return true;
}

void AShipBossEnemy::HandleBalanceHealthChanged(UBaseHealthComponent* Health, float OldHealth, float NewHealth, AActor*)
{
	if (!HasAuthority() || !bUseEncounterBalance || NewHealth <= 0.f) return;
	for (int32 Index = 0; Index < EncounterBalance.SummonHealthFractions.Num(); ++Index)
	{
		const float Threshold = Health->GetMaxHealth() * EncounterBalance.SummonHealthFractions[Index];
		if (!ConsumedSummonThresholds.Contains(Index) && OldHealth > Threshold && NewHealth <= Threshold)
		{
			ConsumedSummonThresholds.Add(Index);
			int32 Alive = 0;
			for (const auto& Ptr : SummonedDeckEnemies)
			{
				const ADeckEnemy* Enemy = Ptr.Get();
				if (Enemy && Enemy->IsPoolActive() && Enemy->GetHealthComponent() && !Enemy->GetHealthComponent()->IsDead()) ++Alive;
			}
			PendingBalanceSummons = FMath::Clamp(PendingBalanceSummons + EncounterBalance.SummonCount,
				0, FMath::Max(0, MaxSummonedDeckEnemies - Alive));
			UE_LOG(LogTemp, Log, TEXT("[EnemyBalance] Boss=%s Threshold=%.2f PendingSummons=%d"),
				*GetName(), EncounterBalance.SummonHealthFractions[Index], PendingBalanceSummons);
		}
	}
}

bool AShipBossEnemy::TrySummonDeckEnemy(ADeckEnemy*& OutEnemy)
{
	if (!bUseEncounterBalance) return SummonOneDeckEnemy(OutEnemy);
	OutEnemy = nullptr;
	if (!CanSummonDeckEnemy()) return false;
	const int32 Requested = PendingBalanceSummons;
	for (int32 Index = 0; Index < Requested; ++Index)
	{
		ADeckEnemy* Enemy = nullptr;
		if (SummonOneDeckEnemy(Enemy)) OutEnemy = Enemy;
	}
	// An event is attempted once; unavailable pool slots/points do not refill later.
	PendingBalanceSummons = 0;
	return OutEnemy != nullptr;
}

float AShipBossEnemy::GetBalancedBossAttackCoefficient(float Fallback, bool bMajorAttack) const
{
	if (!bUseEncounterBalance || !GetAbilitySystemComponent()) return Fallback;
	const float BaseStrength = GetAbilitySystemComponent()->GetNumericAttributeBase(UBaseAttributeSet::GetStrengthAttribute());
	const float TargetDamage = bMajorAttack ? EncounterBalance.MajorAttackDamage : EncounterBalance.StrongAttackDamage;
	return BaseStrength > 0.f ? TargetDamage / BaseStrength : Fallback;
}

bool AShipBossEnemy::ResolveDestinationTransform(FTransform& OutTransform) const
{
	const UDeckWalkAreaComponent* Area = HostShip ? HostShip->GetDeckWalkAreaComponent() : nullptr;
	return Area && Area->IsLocationAvailable(DestinationLocation, *this)
		&& Area->ResolveLocationTransform(DestinationLocation, *this, OutTransform);
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
			Movement->SetBase(ResolveBossDeckBase(*this, HostShip));
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
			Movement->SetBase(ResolveBossDeckBase(*this, HostShip));
		}
	}

	bHiddenRelocationActive = false;
	SetBossHidden(false);
	ForceNetUpdate();
}

void AShipBossEnemy::HandleDeath_Implementation()
{
	if (DeckTargetResolver) DeckTargetResolver->Reset();
	if (DeckWalkRouteComponent) DeckWalkRouteComponent->ResetNavigationState();
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
		ClearDestination();
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

}

void AShipBossEnemy::OnRep_BossHidden()
{
	ApplyHiddenPresentation();
}

void AShipBossEnemy::HandleHostShipDestroyed(AActor* DestroyedActor)
{
	if (DeckTargetResolver) DeckTargetResolver->Reset();
	if (HasAuthority() && DestroyedActor == HostShip && !IsActorBeingDestroyed())
	{
		ClearDestination();
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

void AShipBossEnemy::CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const
{
	ABaseEnemy::CaptureRoomDomains(OutParts, OutIssues);
	FSWRoomShipBossState State;
	if (HostShip)
		if (const USWRoomSnapshotComponent* Id = HostShip->FindComponentByClass<USWRoomSnapshotComponent>())
			State.HostShipId = Id->StableId;
	State.InitialSpawnPointId = InitialSpawnPointId;
	const UDeckWalkAreaComponent* Area = HostShip ? HostShip->GetDeckWalkAreaComponent() : nullptr;
	if (Area && Area->IsLocationValid(PreviousLocation))
	{
		State.PreviousSurfaceId = PreviousLocation.SurfaceId;
		State.PreviousLocalFloor = PreviousLocation.LocalFloor;
	}
	if (HasDestination())
	{
		State.DestinationSurfaceId = DestinationLocation.SurfaceId;
		State.DestinationLocalFloor = DestinationLocation.LocalFloor;
		State.bWalkingToDestination = DeckWalkRouteComponent && DeckWalkRouteComponent->HasGoal();
	}
	State.bStunHealthThresholdConsumed = bStunHealthThresholdConsumed;
	State.PendingBalanceSummons = PendingBalanceSummons;
	for (int32 Threshold : ConsumedSummonThresholds) State.ConsumedSummonThresholds.Add(Threshold);
	State.ConsumedSummonThresholds.Sort();
	State.SummonCooldownRemaining = GetWorld() ? FMath::Max(0.0, NextSummonAllowedTime - GetWorld()->GetTimeSeconds()) : 0.f;
	if (const UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		if (ASC->HasMatchingGameplayTag(AI_State_Boss_Dead)) State.BossAIState = AI_State_Boss_Dead;
		else if (ASC->HasMatchingGameplayTag(AI_State_Boss_Combat)) State.BossAIState = AI_State_Boss_Combat;
		else if (ASC->HasMatchingGameplayTag(AI_State_Boss_Intro)) State.BossAIState = AI_State_Boss_Intro;
	}
	for (const TWeakObjectPtr<ADeckEnemy>& Enemy : SummonedDeckEnemies)
		if (Enemy.IsValid())
			if (const USWRoomSnapshotComponent* Id = Enemy->FindComponentByClass<USWRoomSnapshotComponent>(); Id && Id->StableId.IsValid())
				State.SummonedEnemyIds.AddUnique(Id->StableId);
	State.SummonedEnemyIds.Sort();
	FSWRoomDomainPart& Part = OutParts.AddDefaulted_GetRef();
	Part.Domain = ESWRoomDomain::Boss;
	Part.Version = 2;
	if (!FSWRoomStructCodec::Write(State, Part.Bytes))
	{
		OutParts.Pop();
		FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("Boss");
		Issue.FieldKey = TEXT("ShipBossState");
		Issue.Reason = TEXT("Ship boss state serialization failed");
	}
}

bool AShipBossEnemy::RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError)
{
	if (Part.Domain == ESWRoomDomain::Enemy) return ABaseEnemy::RestoreRoomDomain(Part, OutError);
	if (Part.Domain != ESWRoomDomain::Boss || Part.Version != 2)
	{
		OutError = TEXT("Unsupported ship boss state version; recapture the checkpoint with DeckWalk state");
		return false;
	}
	FSWRoomShipBossState State;
	if (!FSWRoomStructCodec::Read(Part.Bytes, State)
		|| State.InitialSpawnPointId < INDEX_NONE || State.PreviousLocalFloor.ContainsNaN()
		|| State.DestinationLocalFloor.ContainsNaN()
		|| (State.bWalkingToDestination && State.DestinationSurfaceId.IsNone())
		|| State.PendingBalanceSummons < 0 || !FMath::IsFinite(State.SummonCooldownRemaining)
		|| State.SummonCooldownRemaining < 0.f)
	{
		OutError = TEXT("Invalid ship boss state");
		return false;
	}
	ClearDestination();
	InitialSpawnPointId = State.InitialSpawnPointId;
	if (DeckWalkRouteComponent) DeckWalkRouteComponent->ResetNavigationState();
	PreviousLocation = FDeckWalkLocation();
	bStunHealthThresholdConsumed = State.bStunHealthThresholdConsumed;
	PendingBalanceSummons = State.PendingBalanceSummons;
	ConsumedSummonThresholds.Reset();
	for (int32 Threshold : State.ConsumedSummonThresholds) ConsumedSummonThresholds.Add(Threshold);
	bHiddenRelocationActive = false;
	bBossHidden = false;
	ApplyHiddenPresentation();
	PendingRoomState = MoveTemp(State);
	bHasPendingRoomState = true;
	return true;
}

bool AShipBossEnemy::FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError)
{
	if (!ABaseEnemy::FinalizeRoomRestore(RegisteredActors, OutError)) return false;
	if (!bHasPendingRoomState) return true;
	bHasPendingRoomState = false;
	UnbindHostShip();
	HostShip = nullptr;
	if (AActor* const* Found = RegisteredActors.Find(PendingRoomState.HostShipId))
	{
		HostShip = Cast<AEnemyShip>(*Found);
	}
	if (PendingRoomState.HostShipId.IsValid() && !HostShip)
	{
		OutError = TEXT("Ship boss host ship missing");
		return false;
	}
	OnRep_HostShip();
	UDeckWalkAreaComponent* Area = HostShip ? HostShip->GetDeckWalkAreaComponent() : nullptr;
	if (!PendingRoomState.DestinationSurfaceId.IsNone())
	{
		FDeckWalkLocation RestoredDestination;
		if (!Area || !Area->ResolveLocalFloor(PendingRoomState.DestinationLocalFloor,
			PendingRoomState.DestinationSurfaceId, RestoredDestination)
			|| !TrySetDestinationLocation(RestoredDestination, PendingRoomState.bWalkingToDestination))
		{
			OutError = TEXT("Ship boss DeckWalk destination cannot be restored");
			return false;
		}
	}
	// TrySetDestinationLocation samples the current floor; restore the saved history instead.
	PreviousLocation = FDeckWalkLocation();
	if (!PendingRoomState.PreviousSurfaceId.IsNone()
		&& (!Area || !Area->ResolveLocalFloor(PendingRoomState.PreviousLocalFloor,
			PendingRoomState.PreviousSurfaceId, PreviousLocation)))
	{
		OutError = TEXT("Ship boss previous DeckWalk location cannot be restored");
		return false;
	}
	NextSummonAllowedTime = GetWorld()->GetTimeSeconds() + PendingRoomState.SummonCooldownRemaining;
	SummonedDeckEnemies.Reset();
	for (const FGuid& Id : PendingRoomState.SummonedEnemyIds)
		if (AActor* const* Found = RegisteredActors.Find(Id))
			if (ADeckEnemy* Enemy = Cast<ADeckEnemy>(*Found)) SummonedDeckEnemies.Add(Enemy);
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->RemoveLooseGameplayTag(AI_State_Boss_Intro);
		ASC->RemoveLooseGameplayTag(AI_State_Boss_Combat);
		ASC->RemoveLooseGameplayTag(AI_State_Boss_Dead);
		if (PendingRoomState.BossAIState.IsValid()) ASC->AddLooseGameplayTag(PendingRoomState.BossAIState);
	}
	return true;
}
