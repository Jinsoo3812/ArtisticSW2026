#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "AI/EnemyAITypes.h"
#include "BaseEnemy.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "BehaviorTree/Tasks/BTTask_RunEQSQuery.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "Decorator/BTD_CanRangedAttack.h"
#include "Decorator/BTD_CombatTargetState.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "DataProviders/AIDataProvider_QueryParams.h"
#include "EQS/EnvQueryContext_EnemyCombatTarget.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_Donut.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_OnCircle.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Distance.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Pathfinding.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Trace.h"
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
#include "Task/BTT_RangedAttack.h"
#include "Task/BTT_RetreatToWeaponRange.h"
#include "Task/BTT_SetFocus.h"
#include "Task/BTT_SetMovementSpeed.h"
#include "Weapon/BaseWeaponComponent.h"
#include "Weapon/EnemyBow.h"
#include "Weapon/WeaponDataAsset.h"

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
	FRangedEnemyEQSAssetContractTest,
	"ArtisticSW.Enemy.RangedEnemy.EQSAssetContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRangedEnemyEQSAssetContractTest::RunTest(const FString& Parameters)
{
	const UEnvQuery* Query = LoadObject<UEnvQuery>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Enemy/AI/EQS/EQS_RangedEnemy_CombatPosition.EQS_RangedEnemy_CombatPosition"));
	if (!TestNotNull(TEXT("Ranged combat EQS asset exists"), Query))
	{
		return false;
	}

	const TArray<UEnvQueryOption*>& Options = Query->GetOptions();
	if (!TestEqual(TEXT("Query has one reusable option"), Options.Num(), 1)
		|| !TestNotNull(TEXT("Query option exists"), Options[0]))
	{
		return false;
	}

	const UEnvQueryOption* Option = Options[0];
	const UEnvQueryGenerator_Donut* DonutGenerator = Cast<UEnvQueryGenerator_Donut>(Option->Generator);
	const UEnvQueryGenerator_OnCircle* CircleGenerator = Cast<UEnvQueryGenerator_OnCircle>(Option->Generator);
	if (!TestTrue(TEXT("Query uses a target-centered ring generator"),
		DonutGenerator || CircleGenerator))
	{
		return false;
	}
	if (DonutGenerator)
	{
		TestEqual(TEXT("Donut is centered on the controller-owned combat target"),
			DonutGenerator->Center, TSubclassOf<UEnvQueryContext>(UEnvQueryContext_EnemyCombatTarget::StaticClass()));
		TestTrue(TEXT("Donut keeps a positive combat band"),
			DonutGenerator->InnerRadius.DefaultValue > 0.0f
			&& DonutGenerator->OuterRadius.DefaultValue > DonutGenerator->InnerRadius.DefaultValue);
	}
	else if (CircleGenerator)
	{
		TestNotNull(TEXT("Circle has an explicit center context"), CircleGenerator->CircleCenter.Get());
		TestTrue(TEXT("Circle has a positive combat radius"), CircleGenerator->CircleRadius.DefaultValue > 0.0f);
		TestTrue(TEXT("Circle generates enough movement candidates"),
			CircleGenerator->PointOnCircleSpacingMethod == EPointOnCircleSpacingMethod::BySpaceBetween
			|| CircleGenerator->NumberOfPoints.DefaultValue >= 8);
	}

	const UEnvQueryTest_Distance* TargetDistance = nullptr;
	const UEnvQueryTest_Distance* QuerierDistance = nullptr;
	const UEnvQueryTest_Pathfinding* PathExist = nullptr;
	const UEnvQueryTest_Trace* LineOfSight = nullptr;
	for (const UEnvQueryTest* Test : Option->Tests)
	{
		if (const UEnvQueryTest_Distance* Distance = Cast<UEnvQueryTest_Distance>(Test))
		{
			if (Distance->DistanceTo == UEnvQueryContext_EnemyCombatTarget::StaticClass())
			{
				TargetDistance = Distance;
			}
			else if (Distance->DistanceTo == UEnvQueryContext_Querier::StaticClass())
			{
				QuerierDistance = Distance;
			}
		}
		PathExist = PathExist ? PathExist : Cast<UEnvQueryTest_Pathfinding>(Test);
		LineOfSight = LineOfSight ? LineOfSight : Cast<UEnvQueryTest_Trace>(Test);
	}

	if (!TestNotNull(TEXT("Query enforces a reposition step from the querier"), QuerierDistance)
		|| !TestNotNull(TEXT("Query filters unreachable paths"), PathExist)
		|| !TestNotNull(TEXT("Query predicts line of sight"), LineOfSight))
	{
		return false;
	}

	if (TargetDistance)
	{
		TestTrue(TEXT("Optional target-distance test keeps a positive combat band"),
			TargetDistance->FloatValueMin.DefaultValue >= 0.0f
			&& TargetDistance->FloatValueMax.DefaultValue > TargetDistance->FloatValueMin.DefaultValue);
	}
	TestTrue(TEXT("Reposition keeps a positive minimum step"),
		QuerierDistance->FloatValueMin.DefaultValue > 0.0f);
	TestEqual(TEXT("Reposition score prefers the nearest valid next step"),
		QuerierDistance->ScoringEquation.GetValue(), EEnvTestScoreEquation::InverseLinear);
	TestEqual(TEXT("Pathfinding runs from the querier"), PathExist->PathFromContext.DefaultValue, true);
	TestNotNull(TEXT("LOS trace has an explicit context"), LineOfSight->Context.Get());
	TestEqual(TEXT("LOS trace expects no blocking hit"), LineOfSight->BoolValue.DefaultValue, false);
	TestEqual(TEXT("LOS trace starts at the candidate item"), LineOfSight->TraceFromContext.DefaultValue, false);
	TestNull(TEXT("Combat target context safely rejects a missing query owner"),
		UEnvQueryContext_EnemyCombatTarget::ResolveCombatTarget(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRangedEnemyDefaultsTest,
	"ArtisticSW.Enemy.RangedEnemy.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRangedEnemyDefaultsTest::RunTest(const FString& Parameters)
{
	const ARangedEnemy* EnemyCDO = GetDefault<ARangedEnemy>();
	const AEnemyBow* BowCDO = GetDefault<AEnemyBow>();
	const ARangedEnemyAIController* ControllerCDO = GetDefault<ARangedEnemyAIController>();
	const UGA_RangedEnemyAttack* AbilityCDO = GetDefault<UGA_RangedEnemyAttack>();
	const UBTT_RetreatToWeaponRange* RetreatTaskCDO = GetDefault<UBTT_RetreatToWeaponRange>();

	TestNotNull(TEXT("RangedEnemy CDO exists"), EnemyCDO);
	TestNotNull(TEXT("EnemyBow CDO exists"), BowCDO);
	TestNotNull(TEXT("RangedEnemy AI Controller CDO exists"), ControllerCDO);
	TestTrue(TEXT("RangedEnemy AI Controller can tick to update focus rotation"),
		ControllerCDO && ControllerCDO->PrimaryActorTick.bCanEverTick);
	TestTrue(TEXT("RangedEnemy AI Controller starts with focus rotation ticking"),
		ControllerCDO && ControllerCDO->PrimaryActorTick.bStartWithTickEnabled);
	TestEqual(TEXT("RangedEnemy uses the dedicated AI controller"), EnemyCDO->AIControllerClass,
		TSubclassOf<AController>(ARangedEnemyAIController::StaticClass()));
	TestTrue(TEXT("RangedEnemy defaults to the Enemy Bow loadout"),
		EnemyCDO->GetDefaultWeaponTag() == Item_EnemyWeapon_Bow);
	TestTrue(TEXT("RangedEnemy equips its GA-granting bow on spawn"), EnemyCDO->ShouldEquipWeaponOnSpawn());
	TestEqual(TEXT("Enemy Bow uses the required Arrow_socket contract"),
		BowCDO->GetArrowSocketName(), FName(TEXT("Arrow_socket")));
	TestTrue(TEXT("Enemy Bow default projectile derives from RangedEnemyProjectile"),
		BowCDO->GetProjectileClass()
		&& BowCDO->GetProjectileClass()->IsChildOf(ARangedEnemyProjectile::StaticClass()));
	TestTrue(TEXT("Player and Enemy projectile entry points share AArrowProjectile"),
		APlayerArrowProjectile::StaticClass()->IsChildOf(AArrowProjectile::StaticClass())
		&& ARangedEnemyProjectile::StaticClass()->IsChildOf(AArrowProjectile::StaticClass()));
	TestFalse(TEXT("Faction-agnostic damage is the default gameplay policy"),
		GetDefault<AArrowProjectile>()->IsTeamDamageFilteringEnabled());
	TestTrue(TEXT("Ranged attack ability exposes the ranged attack asset tag"),
		AbilityCDO->GetAssetTags().HasTagExact(GameplayAbility_RangedAttack));
	TestTrue(TEXT("Ranged attack remains part of the common basic-attack ability family"),
		AbilityCDO->GetAssetTags().HasTagExact(GameplayAbility_BasicAttack));
	if (TestNotNull(TEXT("Weapon-range retreat task exists"), RetreatTaskCDO))
	{
		TestEqual(TEXT("Retreat task reads TargetActor"),
			RetreatTaskCDO->GetSelectedBlackboardKey(), FName(TEXT("TargetActor")));
		TestTrue(TEXT("Retreat task keeps an inset inside weapon range"),
			RetreatTaskCDO->GetRangeInset() > 0.0f);
		TestTrue(TEXT("Retreat task periodically replans around a moving target"),
			RetreatTaskCDO->GetRepathInterval() > 0.0f);
		TestEqual(TEXT("A normal ranged weapon resolves an inside-boundary retreat distance"),
			UBTT_RetreatToWeaponRange::ResolveDesiredRange(1000.0f, 75.0f, 300.0f),
			925.0f);
		TestEqual(TEXT("Minimum retreat distance never exceeds a short weapon range"),
			UBTT_RetreatToWeaponRange::ResolveDesiredRange(250.0f, 75.0f, 300.0f),
			250.0f);
		TestTrue(TEXT("Retreat direction points away from the target"),
			UBTT_RetreatToWeaponRange::ResolvePlanarAwayDirection(
				FVector(100.0f, 0.0f, 0.0f),
				FVector::ZeroVector,
				FVector::ForwardVector,
				FVector::ForwardVector).Equals(FVector::ForwardVector));
		TestTrue(TEXT("Overlapping actors use deterministic target-backward fallback"),
			UBTT_RetreatToWeaponRange::ResolvePlanarAwayDirection(
				FVector::ZeroVector,
				FVector::ZeroVector,
				FVector::ForwardVector,
				FVector::RightVector).Equals(FVector::BackwardVector));
	}
	TestEqual(TEXT("An unequipped CDO retains the legacy maximum-range fallback"),
		EnemyCDO->GetEffectiveAttackRange(), EnemyCDO->GetFallbackMaxAttackRange());

	const UClass* RangedEnemyBlueprintClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Enemy/BP_RangedEnemy.BP_RangedEnemy_C"));
	const ARangedEnemy* RangedEnemyBlueprintCDO = RangedEnemyBlueprintClass
		? RangedEnemyBlueprintClass->GetDefaultObject<ARangedEnemy>()
		: nullptr;
	if (TestNotNull(TEXT("BP_RangedEnemy uses the native ranged-enemy contract"), RangedEnemyBlueprintCDO))
	{
		TestTrue(TEXT("BP_RangedEnemy resolves the Enemy Bow loadout"),
			RangedEnemyBlueprintCDO->GetDefaultWeaponTag() == Item_EnemyWeapon_Bow);
		TestTrue(TEXT("BP_RangedEnemy equips the bow on spawn"),
			RangedEnemyBlueprintCDO->ShouldEquipWeaponOnSpawn());
	}

	const UWeaponDataAsset* WeaponRegistry = LoadObject<UWeaponDataAsset>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Enemy/Weapon/DA_Weapon.DA_Weapon"));
	if (TestNotNull(TEXT("Enemy weapon registry exists"), WeaponRegistry))
	{
		const FWeaponDefinition* BowDefinition =
			WeaponRegistry->FindWeaponDefinitionByTag(Item_EnemyWeapon_Bow);
		if (TestNotNull(TEXT("Enemy weapon registry contains the bow definition"), BowDefinition))
		{
			TestTrue(TEXT("Bow weapon range can contain the default retreat destination"),
				RetreatTaskCDO
				&& BowDefinition->CombatData.AttackRange > RetreatTaskCDO->GetMinimumDesiredRange());
			TestTrue(TEXT("Bow definition spawns an EnemyBow actor"),
				BowDefinition->WeaponActorClass
				&& BowDefinition->WeaponActorClass->IsChildOf(AEnemyBow::StaticClass()));
			bool bGrantsRangedAttack = false;
			for (const FGrantedWeaponAbility& GrantedAbility : BowDefinition->AbilityData.GrantedAbilities)
			{
				bGrantsRangedAttack |= GrantedAbility.AbilityClass
					&& GrantedAbility.AbilityClass->IsChildOf(UGA_RangedEnemyAttack::StaticClass());
			}
			TestTrue(TEXT("Equipping the bow grants the ranged attack GA"), bGrantsRangedAttack);
		}
	}

	const UAIPerceptionComponent* PerceptionComponent = ControllerCDO
		? ControllerCDO->GetAIPerceptionComponent()
		: nullptr;
	if (TestNotNull(TEXT("Ranged controller owns an AI Perception component"), PerceptionComponent))
	{
		TestFalse(TEXT("Inherited AI Perception component is read-only in Blueprint defaults"),
			PerceptionComponent->IsEditableWhenInherited());
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
	FRangedEnemyEQSPreviewTargetCacheTest,
	"ArtisticSW.Enemy.RangedEnemy.EQSPreviewTargetCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRangedEnemyEQSPreviewTargetCacheTest::RunTest(const FString& Parameters)
{
	RangedEnemyTests::FScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	ARangedEnemyAIController* Controller = TestWorld.World->SpawnActor<ARangedEnemyAIController>();
	AActor* PreviewTarget = TestWorld.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Ranged AI controller is spawned"), Controller)
		|| !TestNotNull(TEXT("EQS preview target is spawned"), PreviewTarget))
	{
		return false;
	}

	TestNull(TEXT("Controller starts without a cached target"), Controller->GetCombatTarget());
	Controller->SetEQSPreviewTarget(PreviewTarget);
	TestEqual(TEXT("EQS preview setter supplies the shared combat-target context"),
		Controller->GetCombatTarget(), PreviewTarget);

	Controller->SetEQSPreviewTarget(nullptr);
	TestNull(TEXT("A null EQS preview target clears the cache"), Controller->GetCombatTarget());

	Controller->SetEQSPreviewTarget(PreviewTarget);
	Controller->ClearCombatTarget(false);
	TestNull(TEXT("Runtime combat clearing also clears the EQS target cache"), Controller->GetCombatTarget());
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
		TEXT("/Game/GameplayAbilitySystem/Enemy/AI/SubTree/Ranged/BT_Subtree_RangedEnemy_Combat.BT_Subtree_RangedEnemy_Combat"));
	if (!TestNotNull(TEXT("Ranged combat behavior tree exists"), CombatTree))
	{
		return false;
	}

	const UBlackboardData* BlackboardAsset = CombatTree->BlackboardAsset;
	if (TestNotNull(TEXT("Ranged combat tree has a Blackboard asset"), BlackboardAsset))
	{
		const FBlackboard::FKey PointOfInterestKey = BlackboardAsset->GetKeyID(TEXT("PointOfInterest"));
		TestTrue(TEXT("PointOfInterest Blackboard key exists"),
			PointOfInterestKey != FBlackboard::InvalidKey);
		if (PointOfInterestKey != FBlackboard::InvalidKey)
		{
			TestEqual(TEXT("PointOfInterest stores EQS point results as a Vector"),
				BlackboardAsset->GetKeyType(PointOfInterestKey),
				TSubclassOf<UBlackboardKeyType>(UBlackboardKeyType_Vector::StaticClass()));
		}
	}

	const UBTComposite_Selector* RootSelector = Cast<UBTComposite_Selector>(CombatTree->RootNode);
	if (!TestNotNull(TEXT("Ranged combat tree is rooted in a selector"), RootSelector))
	{
		return false;
	}

	const UBTComposite_Sequence* AttackSequence = nullptr;
	const UBTComposite_Sequence* RepositionSequence = nullptr;
	const UBTComposite_Sequence* TrackTargetSequence = nullptr;
	int32 AttackBranchIndex = INDEX_NONE;
	int32 RepositionBranchIndex = INDEX_NONE;
	bool bAttackBranchHasCanAttack = false;
	bool bRepositionRequiresTarget = false;
	for (int32 BranchIndex = 0; BranchIndex < RootSelector->Children.Num(); ++BranchIndex)
	{
		const FBTCompositeChild& Branch = RootSelector->Children[BranchIndex];
		const UBTComposite_Sequence* Sequence = Cast<UBTComposite_Sequence>(Branch.ChildComposite);
		if (!Sequence)
		{
			continue;
		}

		bool bContainsAttack = false;
		bool bContainsEQS = false;
		bool bContainsWait = false;
		for (const FBTCompositeChild& Child : Sequence->Children)
		{
			bContainsAttack |= Child.ChildTask && Child.ChildTask->IsA<UBTT_RangedAttack>();
			bContainsEQS |= Child.ChildTask && Child.ChildTask->IsA<UBTTask_RunEQSQuery>();
			bContainsWait |= Child.ChildTask && Child.ChildTask->IsA<UBTTask_Wait>();
		}

		if (bContainsAttack)
		{
			AttackSequence = Sequence;
			AttackBranchIndex = BranchIndex;
			for (const UBTDecorator* Decorator : Branch.Decorators)
			{
				bAttackBranchHasCanAttack |= Decorator && Decorator->IsA<UBTD_CanRangedAttack>();
			}
		}
		else if (bContainsEQS)
		{
			RepositionSequence = Sequence;
			RepositionBranchIndex = BranchIndex;
			for (const UBTDecorator* Decorator : Branch.Decorators)
			{
				const UBTD_CombatTargetState* TargetState = Cast<UBTD_CombatTargetState>(Decorator);
				bRepositionRequiresTarget |= TargetState
					&& TargetState->GetQuery() == ECombatTargetStateQuery::IsSet;
			}
		}
		else if (bContainsWait)
		{
			TrackTargetSequence = Sequence;
		}
	}

	if (!TestNotNull(TEXT("Selector has a ranged attack branch"), AttackSequence)
		|| !TestNotNull(TEXT("Selector has an EQS reposition branch"), RepositionSequence))
	{
		return false;
	}
	TestTrue(TEXT("Attack branch is gated by Can Ranged Attack"), bAttackBranchHasCanAttack);
	TestTrue(TEXT("Reposition branch requires a live combat target"), bRepositionRequiresTarget);
	TestTrue(TEXT("Selector attacks first and falls through to EQS repositioning when it cannot fire"),
		AttackBranchIndex >= 0 && RepositionBranchIndex > AttackBranchIndex);

	const UBTTask_RunEQSQuery* RunEQS = nullptr;
	const UBTTask_MoveTo* CombatMove = nullptr;
	const UBTT_RangedAttack* RangedAttack = nullptr;
	int32 RunEQSIndex = INDEX_NONE;
	int32 StrafeSpeedIndex = INDEX_NONE;
	int32 MoveIndex = INDEX_NONE;
	for (int32 ChildIndex = 0; ChildIndex < RepositionSequence->Children.Num(); ++ChildIndex)
	{
		const UBTTaskNode* Task = RepositionSequence->Children[ChildIndex].ChildTask;
		if (const UBTTask_RunEQSQuery* EQSTask = Cast<UBTTask_RunEQSQuery>(Task))
		{
			RunEQS = EQSTask;
			RunEQSIndex = ChildIndex;
		}
		else if (const UBTTask_MoveTo* MoveTask = Cast<UBTTask_MoveTo>(Task))
		{
			CombatMove = MoveTask;
			MoveIndex = ChildIndex;
		}
		else if (const UBTT_SetMovementSpeed* MovementTask = Cast<UBTT_SetMovementSpeed>(Task))
		{
			if (MovementTask->GetMovementMode() == EEnemyMovementSpeedMode::Strafe)
			{
				StrafeSpeedIndex = ChildIndex;
			}
		}
	}

	int32 IdleSpeedIndex = INDEX_NONE;
	int32 AttackIndex = INDEX_NONE;
	for (int32 ChildIndex = 0; ChildIndex < AttackSequence->Children.Num(); ++ChildIndex)
	{
		const UBTTaskNode* Task = AttackSequence->Children[ChildIndex].ChildTask;
		if (const UBTT_SetMovementSpeed* MovementTask = Cast<UBTT_SetMovementSpeed>(Task))
		{
			if (MovementTask->GetMovementMode() == EEnemyMovementSpeedMode::Idle)
			{
				IdleSpeedIndex = ChildIndex;
			}
		}
		else if (const UBTT_RangedAttack* AttackTask = Cast<UBTT_RangedAttack>(Task))
		{
			RangedAttack = AttackTask;
			AttackIndex = ChildIndex;
		}
	}

	if (TestNotNull(TEXT("Attack branch runs the combat-position EQS"), RunEQS))
	{
		TestEqual(TEXT("EQS result is written to the shared PointOfInterest key"),
			RunEQS->GetSelectedBlackboardKey(), FName(TEXT("PointOfInterest")));
		TestEqual(TEXT("Attack branch uses the reusable ranged combat query"),
			RunEQS->EQSRequest.QueryTemplate.Get(),
			LoadObject<UEnvQuery>(nullptr,
				TEXT("/Game/GameplayAbilitySystem/Enemy/AI/EQS/EQS_RangedEnemy_CombatPosition.EQS_RangedEnemy_CombatPosition")));
		TestEqual(TEXT("Combat-position selection uses a varied top-score band"),
			RunEQS->EQSRequest.RunMode.GetValue(), EEnvQueryRunMode::RandomBest25Pct);
		TestTrue(TEXT("A failed query clears stale PointOfInterest data"), RunEQS->bUpdateBBOnFail);
	}
	if (TestNotNull(TEXT("Attack branch moves to the EQS result"), CombatMove))
	{
		TestEqual(TEXT("Combat movement reads the shared PointOfInterest key"),
			CombatMove->GetSelectedBlackboardKey(), FName(TEXT("PointOfInterest")));
	}
	TestNotNull(TEXT("Attack branch still executes the ranged attack task"), RangedAttack);
	TestTrue(TEXT("Strafe speed is enabled after the query and before movement"),
		RunEQSIndex != INDEX_NONE && StrafeSpeedIndex > RunEQSIndex && MoveIndex > StrafeSpeedIndex);
	TestTrue(TEXT("Attack branch stops movement before firing"),
		IdleSpeedIndex != INDEX_NONE && AttackIndex > IdleSpeedIndex);

	if (TrackTargetSequence)
	{
		bool bHasMovementSpeed = false;
		bool bHasSetFocus = false;
		bool bHasClearFocus = false;
		bool bHasWait = false;
		for (const FBTCompositeChild& Child : TrackTargetSequence->Children)
		{
			const UBTTaskNode* Task = Child.ChildTask;
			bHasMovementSpeed |= Task && Task->IsA<UBTT_SetMovementSpeed>();
			bHasSetFocus |= Task && Task->IsA<UBTT_SetFocus>();
			bHasClearFocus |= Task && Task->IsA<UBTT_ClearFocus>();
			bHasWait |= Task && Task->IsA<UBTTask_Wait>();
		}

		TestTrue(TEXT("Tracking fallback controls movement speed"), bHasMovementSpeed);
		TestTrue(TEXT("Tracking fallback faces the combat target"), bHasSetFocus);
		TestTrue(TEXT("Tracking fallback waits before reevaluating the selector"), bHasWait);
		TestTrue(TEXT("Tracking fallback releases gameplay focus on completion"), bHasClearFocus);
	}
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
	UBaseWeaponComponent* WeaponComponent = Enemy->GetWeaponComponent();
	if (!TestNotNull(TEXT("RangedEnemy ASC exists"), EnemyASC))
	{
		return false;
	}
	if (!TestNotNull(TEXT("RangedEnemy WeaponComponent exists"), WeaponComponent))
	{
		return false;
	}

	EnemyASC->InitAbilityActorInfo(Enemy, Enemy);
	EnemyASC->AddLooseGameplayTag(Team_Enemy);

	UWeaponDataAsset* TestWeaponRegistry = NewObject<UWeaponDataAsset>(Enemy);
	FWeaponDefinition BowDefinition;
	BowDefinition.WeaponTag = Item_EnemyWeapon_Bow;
	BowDefinition.WeaponActorClass = AEnemyBow::StaticClass();
	BowDefinition.CombatData.AttackRange = 1000.0f;
	FGrantedWeaponAbility& GrantedAbility = BowDefinition.AbilityData.GrantedAbilities.AddDefaulted_GetRef();
	GrantedAbility.AbilityClass = UGA_RangedEnemyAttack::StaticClass();
	TestWeaponRegistry->WeaponDefinitions.Add(BowDefinition);
	WeaponComponent->WeaponRegistry = TestWeaponRegistry;
	WeaponComponent->InitializeHolsteredLoadout(Item_EnemyWeapon_Bow);
	WeaponComponent->EquipCurrentWeapon();

	AEnemyBow* EquippedBow = Enemy->GetEquippedBow();
	if (!TestNotNull(TEXT("Combat loadout equips an Enemy Bow"), EquippedBow))
	{
		return false;
	}
	TestEqual(TEXT("RangedEnemy attack validation resolves the equipped bow range"),
		Enemy->GetEffectiveAttackRange(), 1000.0f);
	Player->SetActorLocation(FVector(1100.0f, 0.0f, 0.0f));
	TestFalse(TEXT("Equipped bow range rejects a target beyond its weapon definition"),
		Enemy->CanAttackTarget(Player, false));
	Player->SetActorLocation(FVector(600.0f, 0.0f, 0.0f));
	TestTrue(TEXT("Equipped bow range accepts a target inside its weapon definition"),
		Enemy->CanAttackTarget(Player, false));

	TestEqual(TEXT("Equipping grants exactly one weapon ability"),
		WeaponComponent->GrantedAbilityHandles.Num(), 1);
	WeaponComponent->DeactivateForOwnerDeath();
	TestEqual(TEXT("Death deactivation enters the non-gameplay corpse state"),
		WeaponComponent->GetWeaponLifecycleState(), EEnemyWeaponLifecycleState::DeathInactive);
	TestFalse(TEXT("Death deactivation keeps the weapon visible with the corpse"),
		EquippedBow->IsHidden());
	TestEqual(TEXT("Death deactivation removes weapon-granted abilities"),
		WeaponComponent->GrantedAbilityHandles.Num(), 0);

	WeaponComponent->RestoreFromOwnerPool();
	TestEqual(TEXT("Lifecycle restore returns the weapon to active state"),
		WeaponComponent->GetWeaponLifecycleState(), EEnemyWeaponLifecycleState::Active);
	TestEqual(TEXT("Lifecycle restore regrants one weapon ability"),
		WeaponComponent->GrantedAbilityHandles.Num(), 1);

	WeaponComponent->SuspendForOwnerPool();
	TestFalse(TEXT("Pool suspension disables weapon presentation"),
		WeaponComponent->IsPoolPresentationActive());
	TestEqual(TEXT("Pool suspension enters the pooled lifecycle state"),
		WeaponComponent->GetWeaponLifecycleState(), EEnemyWeaponLifecycleState::Pooled);
	TestTrue(TEXT("Pool suspension hides the separate weapon actor"),
		EquippedBow->IsHidden());
	TestFalse(TEXT("Pool suspension stops weapon hit scanning"),
		EquippedBow->IsHitScanActive());
	TestEqual(TEXT("Pool suspension removes weapon-granted abilities"),
		WeaponComponent->GrantedAbilityHandles.Num(), 0);

	WeaponComponent->RestoreFromOwnerPool();
	TestTrue(TEXT("Pool restore enables weapon presentation"),
		WeaponComponent->IsPoolPresentationActive());
	TestFalse(TEXT("Pool restore unhides the existing weapon actor"),
		EquippedBow->IsHidden());
	TestEqual(TEXT("Pool restore regrants the weapon ability once"),
		WeaponComponent->GrantedAbilityHandles.Num(), 1);

	UStaticMesh* TestBowMesh = NewObject<UStaticMesh>(EquippedBow);
	UStaticMeshSocket* ArrowSocket = NewObject<UStaticMeshSocket>(TestBowMesh);
	ArrowSocket->SocketName = TEXT("Arrow_socket");
	ArrowSocket->RelativeLocation = FVector(75.0f, 10.0f, 25.0f);
	TestBowMesh->Sockets.Add(ArrowSocket);
	EquippedBow->GetWeaponMesh()->SetStaticMesh(TestBowMesh);

	FTransform ExpectedArrowSpawnTransform;
	if (!TestTrue(TEXT("Equipped bow resolves Arrow_socket"),
		EquippedBow->GetArrowSpawnTransform(ExpectedArrowSpawnTransform)))
	{
		return false;
	}

	FGameplayAbilitySpecHandle ResolvedAttackHandle;
	TestTrue(TEXT("RangedEnemy resolves one exact ranged attack ability"),
		Enemy->FindRangedAttackAbility(ResolvedAttackHandle));
	TestTrue(TEXT("Resolved ranged attack handle matches the granted ability"),
		ResolvedAttackHandle.IsValid());

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
	for (TActorIterator<ARangedEnemyProjectile> It(TestWorld.World); It; ++It)
	{
		TestTrue(TEXT("GA spawns the arrow at the equipped bow's Arrow_socket"),
			It->GetActorLocation().Equals(ExpectedArrowSpawnTransform.GetLocation(), 0.1f));
		break;
	}

	Player->SetActorEnableCollision(false);
	TestTrue(TEXT("A collision-disabled/helming ABasePlayer remains a combat target"), Enemy->IsValidCombatTarget(Player));

	Enemy->SetHostShip(HostShip);
	TestEqual(TEXT("An optional explicit host ship assignment is retained"), Enemy->GetHostShip(), HostShip);

	Enemy->Destroy();
	TestTrue(TEXT("The enemy enters destruction through its normal owner lifetime path"),
		Enemy->IsActorBeingDestroyed());
	TestNull(TEXT("Owner destruction clears the replicated weapon reference"),
		WeaponComponent->GetCurrentWeapon());
	TestTrue(TEXT("Owner destruction also destroys the separate weapon actor"),
		EquippedBow->IsActorBeingDestroyed());
	TestEqual(TEXT("Owner destruction resets the equip state"),
		WeaponComponent->WeaponState, EEnemyWeaponState::None);
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
	FHitResult ProjectileHit;
	ProjectileHit.ImpactPoint = TargetEnemy->GetActorLocation();
	ProjectileHit.TraceStart = Projectile->GetActorLocation();
	ProjectileHit.TraceEnd = TargetEnemy->GetActorLocation();
	Projectile->ApplyDamageToActor(TargetEnemy, ProjectileHit);
	TestEqual(TEXT("Direct damage is followed by both status payloads"),
		TargetASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()), HealthBefore - 15.0f);

	Projectile->ApplyDamageToActor(TargetEnemy, ProjectileHit);
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
