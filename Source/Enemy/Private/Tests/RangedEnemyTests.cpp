#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "AI/EnemyAITypes.h"
#include "BaseEnemy.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "Decorator/BTD_CombatTargetState.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GAS/Ability/GA_RangedEnemyAttack.h"
#include "GASCombatLibrary.h"
#include "GASDamageInstantGameplayEffect.h"
#include "GameplayEffect.h"
#include "Item/Projectiles/PlayerArrowProjectile.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "RangedEnemy/RangedEnemy.h"
#include "RangedEnemy/RangedEnemyAIController.h"
#include "RangedEnemy/RangedEnemyProjectile.h"
#include "Ship.h"
#include "StatusEffectLibrary.h"
#include "Task/BTT_ClearFocus.h"
#include "Task/BTT_SetFocus.h"
#include "Task/BTT_SetMovementSpeed.h"

namespace RangedEnemyTests
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("RangedEnemyTestWorld"));
			if (World)
			{
				FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
				WorldContext.SetCurrentWorld(World);
			}
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}
	};

	template <typename ActorType>
	int32 CountActors(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ActorType> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRangedEnemyDefaultsTest,
	"ArtisticSW.Enemy.RangedEnemy.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRangedEnemyDefaultsTest::RunTest(const FString& Parameters)
{
	const ARangedEnemy* EnemyCDO = GetDefault<ARangedEnemy>();
	const ARangedEnemyAIController* ControllerCDO = GetDefault<ARangedEnemyAIController>();
	const UGA_RangedEnemyAttack* AbilityCDO = GetDefault<UGA_RangedEnemyAttack>();

	TestNotNull(TEXT("RangedEnemy CDO exists"), EnemyCDO);
	TestNotNull(TEXT("RangedEnemy AI Controller CDO exists"), ControllerCDO);
	TestTrue(TEXT("RangedEnemy AI Controller can tick to update focus rotation"),
		ControllerCDO && ControllerCDO->PrimaryActorTick.bCanEverTick);
	TestTrue(TEXT("RangedEnemy AI Controller starts with focus rotation ticking"),
		ControllerCDO && ControllerCDO->PrimaryActorTick.bStartWithTickEnabled);
	TestEqual(TEXT("RangedEnemy uses the dedicated AI controller"), EnemyCDO->AIControllerClass,
		TSubclassOf<AController>(ARangedEnemyAIController::StaticClass()));
	TestTrue(TEXT("Default projectile derives from RangedEnemyProjectile"),
		EnemyCDO->GetRangedProjectileClass()
		&& EnemyCDO->GetRangedProjectileClass()->IsChildOf(ARangedEnemyProjectile::StaticClass()));
	TestTrue(TEXT("Player and Enemy projectile entry points share AArrowProjectile"),
		APlayerArrowProjectile::StaticClass()->IsChildOf(AArrowProjectile::StaticClass())
		&& ARangedEnemyProjectile::StaticClass()->IsChildOf(AArrowProjectile::StaticClass()));
	TestFalse(TEXT("Faction-agnostic damage is the default gameplay policy"),
		GetDefault<AArrowProjectile>()->IsTeamDamageFilteringEnabled());
	TestTrue(TEXT("Ranged attack ability exposes the ranged attack asset tag"),
		AbilityCDO->GetAssetTags().HasTagExact(GameplayAbility_RangedAttack));
	TestTrue(TEXT("Ranged attack remains part of the common basic-attack ability family"),
		AbilityCDO->GetAssetTags().HasTagExact(GameplayAbility_BasicAttack));

	const UAIPerceptionComponent* PerceptionComponent = ControllerCDO
		? ControllerCDO->GetAIPerceptionComponent()
		: nullptr;
	if (TestNotNull(TEXT("Ranged controller owns an AI Perception component"), PerceptionComponent))
	{
		TestNotNull(TEXT("Sight sense is configured"), PerceptionComponent->GetSenseConfig<UAISenseConfig_Sight>());
		TestNotNull(TEXT("Hearing sense is configured"), PerceptionComponent->GetSenseConfig<UAISenseConfig_Hearing>());
		TestNotNull(TEXT("Damage sense is configured"), PerceptionComponent->GetSenseConfig<UAISenseConfig_Damage>());
	}

	TestEqual(TEXT("Passive enum value remains serialization-stable"), static_cast<uint8>(EEnemyAIState::Passive), uint8(0));
	TestEqual(TEXT("Investigating enum value remains serialization-stable"), static_cast<uint8>(EEnemyAIState::Investigating), uint8(10));
	TestEqual(TEXT("Combat enum value remains serialization-stable"), static_cast<uint8>(EEnemyAIState::Combat), uint8(20));
	TestEqual(TEXT("Frozen enum value remains serialization-stable"), static_cast<uint8>(EEnemyAIState::Frozen), uint8(30));
	TestEqual(TEXT("Dead enum value remains serialization-stable"), static_cast<uint8>(EEnemyAIState::Dead), uint8(250));

	const UClass* BlueprintAbilityClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Ability/Enemy/BPGA_RangedAttack.BPGA_RangedAttack_C"));
	if (TestNotNull(TEXT("BPGA_RangedAttack class can be loaded"), BlueprintAbilityClass))
	{
		const UGameplayAbility* BlueprintAbilityCDO = BlueprintAbilityClass->GetDefaultObject<UGameplayAbility>();
		TestTrue(TEXT("BPGA_RangedAttack retains the ranged attack asset tag"),
			BlueprintAbilityCDO
			&& BlueprintAbilityCDO->GetAssetTags().HasTagExact(GameplayAbility_RangedAttack));
	}

	FCollisionResponseTemplate ProjectileProfile;
	if (TestTrue(TEXT("Projectile collision profile is registered"),
		UCollisionProfile::Get()->GetProfileTemplate(TEXT("Projectile"), ProjectileProfile)))
	{
		TestEqual(TEXT("Projectile collision is query-only"),
			ProjectileProfile.CollisionEnabled, ECollisionEnabled::QueryOnly);
		TestEqual(TEXT("Projectile blocks Pawn for hit events"),
			ProjectileProfile.ResponseToChannels.GetResponse(ECC_Pawn), ECR_Block);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRangedEnemyCombatTreeContractTest,
	"ArtisticSW.Enemy.RangedEnemy.CombatTreeContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRangedEnemyCombatTreeContractTest::RunTest(const FString& Parameters)
{
	const UBehaviorTree* CombatTree = LoadObject<UBehaviorTree>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Enemy/AI/SubTree/BT_Subtree_RangedEnemy_Combat.BT_Subtree_RangedEnemy_Combat"));
	if (!TestNotNull(TEXT("Ranged combat behavior tree exists"), CombatTree))
	{
		return false;
	}

	const UBTComposite_Selector* RootSelector = Cast<UBTComposite_Selector>(CombatTree->RootNode);
	if (!TestNotNull(TEXT("Ranged combat tree is rooted in a selector"), RootSelector))
	{
		return false;
	}

	const UBTComposite_Sequence* AttackSequence = nullptr;
	const UBTComposite_Sequence* SearchSequence = nullptr;
	for (const FBTCompositeChild& Child : RootSelector->Children)
	{
		const UBTComposite_Sequence* Sequence = Cast<UBTComposite_Sequence>(Child.ChildComposite);
		for (const UBTDecorator* Decorator : Child.Decorators)
		{
			const UBTD_CombatTargetState* TargetDecorator = Cast<UBTD_CombatTargetState>(Decorator);
			if (!TargetDecorator)
			{
				continue;
			}

			TestEqual(TEXT("Combat target branches abort both directions"),
				TargetDecorator->GetFlowAbortMode(), EBTFlowAbortMode::Both);
			if (TargetDecorator->GetQuery() == ECombatTargetStateQuery::IsSet)
			{
				AttackSequence = Sequence;
			}
			else
			{
				SearchSequence = Sequence;
			}
		}
	}

	TestNotNull(TEXT("Attack branch requires a combat target"), AttackSequence);
	if (!TestNotNull(TEXT("Search branch requires no combat target"), SearchSequence))
	{
		return false;
	}

	bool bHasIdleSpeed = false;
	bool bHasClearFocus = false;
	bool bHasWait = false;
	bool bHasUnexpectedTargetMove = false;
	for (const FBTCompositeChild& Child : SearchSequence->Children)
	{
		const UBTTaskNode* Task = Child.ChildTask;
		if (const UBTT_SetMovementSpeed* MovementTask = Cast<UBTT_SetMovementSpeed>(Task))
		{
			bHasIdleSpeed = MovementTask->GetMovementMode() == EEnemyMovementSpeedMode::Idle;
		}
		bHasClearFocus |= Task && Task->IsA<UBTT_ClearFocus>();
		bHasWait |= Task && Task->IsA<UBTTask_Wait>();
		bHasUnexpectedTargetMove |= Task
			&& (Task->IsA<UBTT_SetFocus>() || Task->IsA<UBTTask_MoveTo>());
	}

	TestTrue(TEXT("Search branch stops movement"), bHasIdleSpeed);
	TestTrue(TEXT("Search branch clears gameplay focus"), bHasClearFocus);
	TestTrue(TEXT("Search branch waits for target reacquisition"), bHasWait);
	TestFalse(TEXT("Search branch does not focus or move toward the dead target"), bHasUnexpectedTargetMove);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRangedEnemyProjectileTeamFilterTest,
	"ArtisticSW.Enemy.RangedEnemy.ProjectileTeamFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRangedEnemyProjectileTeamFilterTest::RunTest(const FString& Parameters)
{
	RangedEnemyTests::FScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	ARangedEnemy* SourceEnemy = TestWorld.World->SpawnActor<ARangedEnemy>();
	ABaseEnemy* FriendlyEnemy = TestWorld.World->SpawnActor<ABaseEnemy>();
	AShip* PlayerTeamTarget = TestWorld.World->SpawnActor<AShip>();
	ARangedEnemyProjectile* Projectile = TestWorld.World->SpawnActor<ARangedEnemyProjectile>();
	if (!TestNotNull(TEXT("Source RangedEnemy is spawned"), SourceEnemy)
		|| !TestNotNull(TEXT("Friendly enemy is spawned"), FriendlyEnemy)
		|| !TestNotNull(TEXT("Player-team target is spawned"), PlayerTeamTarget)
		|| !TestNotNull(TEXT("Ranged projectile is spawned"), Projectile))
	{
		return false;
	}

	UAbilitySystemComponent* SourceASC = SourceEnemy->GetAbilitySystemComponent();
	UAbilitySystemComponent* FriendlyASC = FriendlyEnemy->GetAbilitySystemComponent();
	UAbilitySystemComponent* PlayerTargetASC = PlayerTeamTarget->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("Source ASC exists"), SourceASC)
		|| !TestNotNull(TEXT("Friendly ASC exists"), FriendlyASC)
		|| !TestNotNull(TEXT("Player target ASC exists"), PlayerTargetASC))
	{
		return false;
	}

	SourceASC->InitAbilityActorInfo(SourceEnemy, SourceEnemy);
	SourceASC->AddLooseGameplayTag(Team_Enemy);
	FriendlyASC->InitAbilityActorInfo(FriendlyEnemy, FriendlyEnemy);
	FriendlyASC->AddLooseGameplayTag(Team_Enemy);
	PlayerTargetASC->InitAbilityActorInfo(PlayerTeamTarget, PlayerTeamTarget);
	PlayerTargetASC->AddLooseGameplayTag(Team_Player);

	Projectile->SetOwner(SourceEnemy);
	Projectile->SetInstigator(SourceEnemy);
	Projectile->InitializeDamage(SourceASC, SourceEnemy, 1.0f);

	TestFalse(TEXT("Projectile always rejects its source actor"), Projectile->IsValidDamageTarget(SourceEnemy));
	TestTrue(TEXT("Default faction-agnostic mode accepts another enemy-team actor"),
		Projectile->IsValidDamageTarget(FriendlyEnemy));
	TestTrue(TEXT("Default faction-agnostic mode accepts a player-team actor"),
		Projectile->IsValidDamageTarget(PlayerTeamTarget));

	Projectile->SetTeamDamageFilteringEnabled(true);
	TestFalse(TEXT("Debug team filter rejects another enemy-team actor"),
		Projectile->IsValidDamageTarget(FriendlyEnemy));
	TestTrue(TEXT("Debug team filter still accepts an opposing player-team actor"),
		Projectile->IsValidDamageTarget(PlayerTeamTarget));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRangedEnemyAttackIntegrationTest,
	"ArtisticSW.Enemy.RangedEnemy.AttackIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRangedEnemyAttackIntegrationTest::RunTest(const FString& Parameters)
{
	RangedEnemyTests::FScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	ARangedEnemy* Enemy = TestWorld.World->SpawnActor<ARangedEnemy>(ARangedEnemy::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	ABasePlayer* Player = TestWorld.World->SpawnActor<ABasePlayer>(ABasePlayer::StaticClass(), FVector(600.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	AShip* HostShip = TestWorld.World->SpawnActor<AShip>();
	if (!TestNotNull(TEXT("RangedEnemy is spawned"), Enemy)
		|| !TestNotNull(TEXT("Player is spawned"), Player)
		|| !TestNotNull(TEXT("Host ship is spawned"), HostShip))
	{
		return false;
	}

	TestNull(TEXT("RangedEnemy starts without requiring a host ship"), Enemy->GetHostShip());
	TestTrue(TEXT("An enabled ABasePlayer is a valid combat target before PlayerState ASC initialization"),
		Enemy->IsValidCombatTarget(Player));

	Enemy->SetCombatTarget(Player);
	UAbilitySystemComponent* EnemyASC = Enemy->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("RangedEnemy ASC exists"), EnemyASC))
	{
		return false;
	}

	EnemyASC->InitAbilityActorInfo(Enemy, Enemy);
	EnemyASC->AddLooseGameplayTag(Team_Enemy);
	const FGameplayAbilitySpecHandle GrantedAttackHandle =
		EnemyASC->GiveAbility(FGameplayAbilitySpec(UGA_RangedEnemyAttack::StaticClass(), 1));

	FGameplayAbilitySpecHandle ResolvedAttackHandle;
	TestTrue(TEXT("RangedEnemy resolves one exact ranged attack ability"),
		Enemy->FindRangedAttackAbility(ResolvedAttackHandle));
	TestTrue(TEXT("Resolved ranged attack handle matches the granted ability"),
		ResolvedAttackHandle == GrantedAttackHandle);

	bool bObservedAbilityEnd = false;
	bool bObservedAbilityCancel = true;
	const FDelegateHandle AbilityEndedHandle = EnemyASC->OnAbilityEnded.AddLambda(
		[&](const FAbilityEndedData& EndedData)
		{
			if (EndedData.AbilitySpecHandle == ResolvedAttackHandle)
			{
				bObservedAbilityEnd = true;
				bObservedAbilityCancel = EndedData.bWasCancelled;
			}
		});

	const int32 ProjectilesBefore = RangedEnemyTests::CountActors<ARangedEnemyProjectile>(TestWorld.World);
	TestTrue(TEXT("Standalone RangedEnemy activates its exact server ranged attack without HostShip"),
		Enemy->TryStartRangedAttack(ResolvedAttackHandle));
	EnemyASC->OnAbilityEnded.Remove(AbilityEndedHandle);
	TestTrue(TEXT("Ranged attack broadcasts ability completion"), bObservedAbilityEnd);
	TestFalse(TEXT("Immediate ranged attack completion is not a cancellation"), bObservedAbilityCancel);
	const int32 ProjectilesAfter = RangedEnemyTests::CountActors<ARangedEnemyProjectile>(TestWorld.World);
	TestEqual(TEXT("An immediate-fire projectile is spawned when no montage is assigned"),
		ProjectilesAfter, ProjectilesBefore + 1);

	Player->SetActorEnableCollision(false);
	TestTrue(TEXT("A collision-disabled/helming ABasePlayer remains a combat target"), Enemy->IsValidCombatTarget(Player));

	Enemy->SetHostShip(HostShip);
	TestEqual(TEXT("An optional explicit host ship assignment is retained"), Enemy->GetHostShip(), HostShip);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStrengthProjectilePayloadTest,
	"ArtisticSW.Enemy.RangedEnemy.StrengthProjectilePayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrengthProjectilePayloadTest::RunTest(const FString& Parameters)
{
	RangedEnemyTests::FScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	ARangedEnemy* SourceEnemy = TestWorld.World->SpawnActor<ARangedEnemy>();
	ABaseEnemy* TargetEnemy = TestWorld.World->SpawnActor<ABaseEnemy>();
	AArrowProjectile* Projectile = TestWorld.World->SpawnActor<AArrowProjectile>();
	if (!TestNotNull(TEXT("Source enemy is spawned"), SourceEnemy)
		|| !TestNotNull(TEXT("Target enemy is spawned"), TargetEnemy)
		|| !TestNotNull(TEXT("Shared arrow is spawned"), Projectile))
	{
		return false;
	}

	UAbilitySystemComponent* SourceASC = SourceEnemy->GetAbilitySystemComponent();
	UAbilitySystemComponent* TargetASC = TargetEnemy->GetAbilitySystemComponent();
	SourceASC->InitAbilityActorInfo(SourceEnemy, SourceEnemy);
	TargetASC->InitAbilityActorInfo(TargetEnemy, TargetEnemy);
	UBaseAttributeSet* SourceAttributes = NewObject<UBaseAttributeSet>(SourceEnemy);
	SourceAttributes->InitStrength(10.0f);
	SourceASC->AddAttributeSetSubobject(SourceAttributes);
	UBaseAttributeSet* TargetAttributes = NewObject<UBaseAttributeSet>(TargetEnemy);
	TargetAttributes->InitMaxHealth(100.0f);
	TargetAttributes->InitHealth(100.0f);
	TargetASC->AddAttributeSetSubobject(TargetAttributes);

	FArrowStatusEffect FirstStatus;
	FirstStatus.StatusEffectClass = UGASDamageInstantGameplayEffect::StaticClass();
	FArrowStatusEffect SecondStatus;
	SecondStatus.StatusEffectClass = UGASDamageInstantGameplayEffect::StaticClass();
	Projectile->DamageData.StatusEffects = {FirstStatus, SecondStatus};

	FStrengthDamageRequest DamageRequest;
	DamageRequest.SourceASC = SourceASC;
	DamageRequest.DamageEffectClass = UGASDamageInstantGameplayEffect::StaticClass();
	DamageRequest.AttackCoefficient = 1.0f;
	DamageRequest.ChargeMultiplier = 1.0f;
	DamageRequest.InstigatorActor = SourceEnemy;
	DamageRequest.EffectCauser = Projectile;
	const FGameplayEffectSpecHandle DirectDamageSpec = UGASCombatLibrary::MakeStrengthDamageEffectSpec(DamageRequest);
	Projectile->InitializeStrengthDamage(SourceASC, SourceEnemy, DirectDamageSpec);

	TestEqual(TEXT("Projectile stores one direct-damage spec"), Projectile->DamageEffectSpecHandles.Num(), 1);
	TestEqual(TEXT("Projectile builds every configured status spec"), Projectile->StatusEffectSpecHandles.Num(), 2);
	for (const FGameplayEffectSpecHandle& StatusSpec : Projectile->StatusEffectSpecHandles)
	{
		TestTrue(TEXT("Status spec remains attributed to the firing source"),
			StatusSpec.IsValid()
			&& StatusSpec.Data.IsValid()
			&& StatusSpec.Data->GetContext().GetOriginalInstigator() == SourceEnemy);
	}

	// Reuse the instant meta-damage GE as an observable test status payload.
	Projectile->StatusEffectSpecHandles[0].Data->SetSetByCallerMagnitude(Data_Damage, 2.0f);
	Projectile->StatusEffectSpecHandles[1].Data->SetSetByCallerMagnitude(Data_Damage, 3.0f);
	const float HealthBefore = TargetASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute());
	Projectile->ApplyDamageToActor(TargetEnemy);
	TestEqual(TEXT("Direct damage is followed by both status payloads"),
		TargetASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()), HealthBefore - 15.0f);

	Projectile->ApplyDamageToActor(TargetEnemy);
	TestEqual(TEXT("A piercing projectile applies to the same target only once"),
		TargetASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()), HealthBefore - 15.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStatusEffectRefreshTest,
	"ArtisticSW.Enemy.RangedEnemy.StatusEffectRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStatusEffectRefreshTest::RunTest(const FString& Parameters)
{
	RangedEnemyTests::FScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	ABaseEnemy* SourceEnemy = TestWorld.World->SpawnActor<ABaseEnemy>();
	ABaseEnemy* TargetEnemy = TestWorld.World->SpawnActor<ABaseEnemy>();
	if (!TestNotNull(TEXT("Status source is spawned"), SourceEnemy)
		|| !TestNotNull(TEXT("Status target is spawned"), TargetEnemy))
	{
		return false;
	}

	UAbilitySystemComponent* SourceASC = SourceEnemy->GetAbilitySystemComponent();
	UAbilitySystemComponent* TargetASC = TargetEnemy->GetAbilitySystemComponent();
	SourceASC->InitAbilityActorInfo(SourceEnemy, SourceEnemy);
	TargetASC->InitAbilityActorInfo(TargetEnemy, TargetEnemy);
	UBaseAttributeSet* SourceAttributes = NewObject<UBaseAttributeSet>(SourceEnemy);
	SourceASC->AddAttributeSetSubobject(SourceAttributes);
	UBaseAttributeSet* TargetAttributes = NewObject<UBaseAttributeSet>(TargetEnemy);
	TargetAttributes->InitMaxHealth(100.0f);
	TargetAttributes->InitHealth(100.0f);
	TargetASC->AddAttributeSetSubobject(TargetAttributes);

	UClass* PoisonEffectClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/GameplayEffect/GE_Poison_Damage.GE_Poison_Damage_C"));
	if (!TestNotNull(TEXT("Poison status GE can be loaded"), PoisonEffectClass))
	{
		return false;
	}

	const UGameplayEffect* PoisonCDO = PoisonEffectClass->GetDefaultObject<UGameplayEffect>();
	if (!TestTrue(TEXT("Poison GE has a finite duration"),
		PoisonCDO && PoisonCDO->DurationPolicy == EGameplayEffectDurationType::HasDuration))
	{
		return false;
	}

	FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
	ContextHandle.AddInstigator(SourceEnemy, SourceEnemy);
	const FGameplayEffectSpecHandle PoisonSpec = SourceASC->MakeOutgoingSpec(PoisonEffectClass, 1.0f, ContextHandle);
	if (!TestTrue(TEXT("Poison status spec is valid"), PoisonSpec.IsValid() && PoisonSpec.Data.IsValid()))
	{
		return false;
	}

	const FActiveGameplayEffectHandle FirstHandle =
		UStatusEffectLibrary::ApplyDurationDamageEffectSpecToTarget(TargetASC, PoisonSpec, FGameplayTag());
	if (!TestTrue(TEXT("First poison application creates an active effect"), FirstHandle.IsValid()))
	{
		return false;
	}

	FGameplayEffectQuery PoisonQuery;
	PoisonQuery.EffectDefinition = PoisonEffectClass;
	TestEqual(TEXT("Only one poison timer is active after first hit"), TargetASC->GetActiveEffects(PoisonQuery).Num(), 1);

	const TArray<float> Durations = TargetASC->GetActiveEffectsDuration(PoisonQuery);
	if (!TestTrue(TEXT("Poison exposes a positive duration"), Durations.Num() == 1 && Durations[0] > KINDA_SMALL_NUMBER))
	{
		return false;
	}

	TargetASC->ModifyActiveEffectStartTime(FirstHandle, -Durations[0] * 0.5f);
	const TArray<float> AgedRemainingTimes = TargetASC->GetActiveEffectsTimeRemaining(PoisonQuery);
	if (!TestTrue(TEXT("Existing poison timer can be aged for refresh verification"), AgedRemainingTimes.Num() == 1))
	{
		return false;
	}

	const FActiveGameplayEffectHandle RefreshedHandle =
		UStatusEffectLibrary::ApplyDurationDamageEffectSpecToTarget(TargetASC, PoisonSpec, FGameplayTag());
	TestTrue(TEXT("Repeated poison hit returns a refreshed active handle"), RefreshedHandle.IsValid());
	TestEqual(TEXT("Repeated poison hit does not add another stack"), TargetASC->GetActiveEffects(PoisonQuery).Num(), 1);

	const TArray<float> RefreshedRemainingTimes = TargetASC->GetActiveEffectsTimeRemaining(PoisonQuery);
	TestTrue(TEXT("Repeated poison hit resets the status timer"),
		RefreshedRemainingTimes.Num() == 1
		&& RefreshedRemainingTimes[0] > AgedRemainingTimes[0] + Durations[0] * 0.25f);
	return true;
}

#endif
