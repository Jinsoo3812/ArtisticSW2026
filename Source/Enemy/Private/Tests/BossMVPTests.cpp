#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AI/EnemyBehaviorSet.h"
#include "AI/EnemyAITypes.h"
#include "BaseGameplayTags.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/Tasks/BTTask_RunBehaviorDynamic.h"
#include "BossAI/BossDeckPointSelector.h"
#include "BossAI/BossEncounterComponent.h"
#include "BossAI/EnemyItemBox.h"
#include "BossAI/ShipBossAIController.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/BaseHealthComponent.h"
#include "Decorator/BTD_CanActivateAbilityByTag.h"
#include "GAS/Ability/Boss/BossGameplayAbility.h"
#include "GAS/Ability/Boss/GA_BossBasicAttack.h"
#include "GAS/Ability/Boss/GA_BossDashSlash.h"
#include "GAS/Ability/Boss/GA_BossKnockback.h"
#include "GAS/Ability/Boss/GA_BossVanish.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayCue/SWGameplayCueNotify_BurstFeedback.h"
#include "Components/ChildActorComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Interactable/InteractableComponent.h"
#include "ShipAI/EnemyShip.h"
#include "Task/BTT_ActivateBossAbility.h"
#include "Task/BTT_SelectBossDestinationPoint.h"
#include "UObject/UnrealType.h"
#include "Weapon/WeaponDataAsset.h"

namespace BossMVPTests
{
	template <typename TNode>
	void CollectNodes(const UBTCompositeNode* Composite, TArray<const TNode*>& OutNodes)
	{
		if (!Composite)
		{
			return;
		}

		for (int32 ChildIndex = 0; ChildIndex < Composite->GetChildrenNum(); ++ChildIndex)
		{
			const UBTNode* ChildNode = Composite->GetChildNode(ChildIndex);
			if (const TNode* MatchingNode = Cast<TNode>(ChildNode))
			{
				OutNodes.Add(MatchingNode);
			}
			if (const UBTCompositeNode* ChildComposite = Cast<UBTCompositeNode>(ChildNode))
			{
				CollectNodes(ChildComposite, OutNodes);
			}
		}
	}

	const FBlackboardEntry* FindBlackboardKey(const UBlackboardData* Blackboard, const FName KeyName)
	{
		if (!Blackboard)
		{
			return nullptr;
		}
		const FBlackboard::FKey KeyId = Blackboard->GetKeyID(KeyName);
		return KeyId != FBlackboard::InvalidKey ? Blackboard->GetKey(KeyId) : nullptr;
	}

	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("BossMVPTestWorld"));
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBossMVPDefaultsTest,
	"ArtisticSW.Enemy.BossMVP.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBossMVPDefaultsTest::RunTest(const FString& Parameters)
{
	const AShipBossEnemy* BossCDO = GetDefault<AShipBossEnemy>();
	const UBossEncounterComponent* EncounterCDO = GetDefault<UBossEncounterComponent>();
	const AEnemyItemBox* ItemBoxCDO = GetDefault<AEnemyItemBox>();
	const UBTT_SelectBossDestinationPoint* SelectTaskCDO = GetDefault<UBTT_SelectBossDestinationPoint>();
	const UBTT_ActivateBossAbility* ActivateTaskCDO = GetDefault<UBTT_ActivateBossAbility>();
	const UBTD_CanActivateAbilityByTag* AbilityDecoratorCDO = GetDefault<UBTD_CanActivateAbilityByTag>();
	const AShipBossAIController* BossControllerCDO = GetDefault<AShipBossAIController>();

	if (TestNotNull(TEXT("Ship boss class exists"), BossCDO))
	{
		TestTrue(TEXT("Boss uses its strict BT-only controller"),
			BossCDO->AIControllerClass == AShipBossAIController::StaticClass());
		TestTrue(TEXT("Boss is server-replicated"), BossCDO->GetIsReplicated());
		TestEqual(TEXT("Boss does not walk point-by-point in the MVP"),
			BossCDO->GetCharacterMovement()->MaxWalkSpeed, 0.0f);
		TestTrue(TEXT("Boss confirmed damage emits the multiplayer hit feedback cue"),
			BossCDO->GetHealthComponent()->GetDamageGameplayCueTag() == GameplayCue_Boss_Hit);
		if (const USphereComponent* DashDamageVolume = BossCDO->GetDashDamageVolume())
		{
			TestTrue(TEXT("Dash damage volume is disabled outside DashSlash"),
				DashDamageVolume->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
			TestTrue(TEXT("Dash damage volume queries Pawn overlaps"),
				DashDamageVolume->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Overlap);
		}
		else
		{
			AddError(TEXT("Boss has no DashDamageVolume."));
		}
	}
	if (TestNotNull(TEXT("Encounter component exists"), EncounterCDO))
	{
		TestTrue(TEXT("Encounter state replicates through its component"), EncounterCDO->GetIsReplicated());
		TestEqual(TEXT("Encounter waits for first box interaction"),
			EncounterCDO->GetEncounterState(), EBossEncounterState::Waiting);
	}
	const AEnemyShip* EnemyShipCDO = GetDefault<AEnemyShip>();
	if (TestNotNull(TEXT("EnemyShip CDO exists"), EnemyShipCDO))
	{
		TestNotNull(TEXT("Every EnemyShip owns an optional boss encounter component"),
			EnemyShipCDO->GetBossEncounterComponent());
		TestFalse(TEXT("Existing EnemyShips do not enable boss encounters accidentally"),
			EnemyShipCDO->GetBossEncounterComponent()->IsEncounterEnabled());
	}
	if (TestNotNull(TEXT("Enemy item box exists"), ItemBoxCDO))
	{
		TestFalse(TEXT("Ship-mounted item box does not simulate buoyancy"),
			ItemBoxCDO->IsPhysicsAndBuoyancyEnabled());
	}
	TestNotNull(TEXT("Shared destination BT task exists"), SelectTaskCDO);
	TestNotNull(TEXT("Generic boss ability BT task exists"), ActivateTaskCDO);
	TestNotNull(TEXT("Ability readiness decorator exists"), AbilityDecoratorCDO);
	if (TestNotNull(TEXT("BT-only boss controller exists"), BossControllerCDO))
	{
		TestNull(TEXT("Legacy timer decision settings were removed from the boss controller"),
			FindFProperty<FProperty>(BossControllerCDO->GetClass(), TEXT("DecisionInterval")));
	}
	if (TestNotNull(TEXT("Generic boss ability BT task has deterministic spec selection"), ActivateTaskCDO))
	{
		TestTrue(TEXT("BT task prefers the spec granted by the equipped weapon"),
			ActivateTaskCDO->PrefersCurrentWeaponAbility());
	}
	TestTrue(TEXT("Boss attack cue tag is registered"), GameplayCue_Boss_Attack.GetTag().IsValid());
	TestTrue(TEXT("Boss hit cue tag is registered"), GameplayCue_Boss_Hit.GetTag().IsValid());
	TestTrue(TEXT("Confirmed impact cue root is registered"), GameplayCue_Impact.GetTag().IsValid());
	TestTrue(TEXT("Sword impact cue tag is registered"), GameplayCue_Impact_Weapon_Sword.GetTag().IsValid());
	TestTrue(TEXT("Dash montage start event tag is registered"), Event_Boss_Dash_Start.GetTag().IsValid());
	TestTrue(TEXT("Burst feedback cue is authored through a Blueprint subclass"),
		USWGameplayCueNotify_BurstFeedback::StaticClass()->HasAnyClassFlags(CLASS_Abstract));

	const UClass* AttackCueClass = LoadClass<USWGameplayCueNotify_BurstFeedback>(
		nullptr, TEXT("/Game/GameplayCues/Boss/GCN_Boss_Attack.GCN_Boss_Attack_C"));
	const UClass* HitCueClass = LoadClass<USWGameplayCueNotify_BurstFeedback>(
		nullptr, TEXT("/Game/GameplayCues/Boss/GCN_Boss_Hit.GCN_Boss_Hit_C"));
	const UClass* SwordImpactCueClass = LoadClass<USWGameplayCueNotify_BurstFeedback>(
		nullptr,
		TEXT("/Game/GameplayCues/Impact/Weapon/GCN_Impact_Weapon_Sword.GCN_Impact_Weapon_Sword_C"));
	if (TestNotNull(TEXT("Boss attack cue Blueprint loads"), AttackCueClass))
	{
		const USWGameplayCueNotify_BurstFeedback* AttackCue =
			Cast<USWGameplayCueNotify_BurstFeedback>(AttackCueClass->GetDefaultObject());
		if (TestNotNull(TEXT("Boss attack cue has the native feedback parent"), AttackCue))
		{
			TestEqual(TEXT("Attack feedback uses the weak camera shake scale"),
				AttackCue->GetCameraShakeScale(), 0.25f);
			TestTrue(TEXT("Attack feedback shakes nearby local players only"),
				AttackCue->GetCameraShakeRecipient()
				== ESWGameplayCueCameraShakeRecipient::AllLocalPlayersInRadius);
			TestNull(TEXT("Attack feedback does not spawn the damage VFX"),
				AttackCue->GetNiagaraSystem());
		}
	}
	if (TestNotNull(TEXT("Boss hit cue Blueprint loads"), HitCueClass))
	{
		const USWGameplayCueNotify_BurstFeedback* HitCue =
			Cast<USWGameplayCueNotify_BurstFeedback>(HitCueClass->GetDefaultObject());
		if (TestNotNull(TEXT("Boss hit cue has the native feedback parent"), HitCue))
		{
			TestEqual(TEXT("Hit feedback uses the strong camera shake scale"),
				HitCue->GetCameraShakeScale(), 1.0f);
			TestTrue(TEXT("Hit feedback shakes only the attacking local player"),
				HitCue->GetCameraShakeRecipient()
				== ESWGameplayCueCameraShakeRecipient::InstigatorLocalPlayer);
			TestNotNull(TEXT("Hit feedback owns a Niagara damage VFX"),
				HitCue->GetNiagaraSystem());
		}
	}
	if (TestNotNull(TEXT("Sword confirmed impact cue Blueprint loads"), SwordImpactCueClass))
	{
		const USWGameplayCueNotify_BurstFeedback* SwordImpactCue =
			Cast<USWGameplayCueNotify_BurstFeedback>(SwordImpactCueClass->GetDefaultObject());
		if (TestNotNull(TEXT("Sword impact cue has the native feedback parent"), SwordImpactCue))
		{
			TestTrue(TEXT("Sword impact shakes only the damaged local player"),
				SwordImpactCue->GetCameraShakeRecipient()
					== ESWGameplayCueCameraShakeRecipient::TargetLocalPlayer);
			TestNotNull(TEXT("Sword impact owns a Niagara effect"),
				SwordImpactCue->GetNiagaraSystem());
		}
	}

	const UWeaponDataAsset* WeaponData = LoadObject<UWeaponDataAsset>(
		nullptr, TEXT("/Game/GameplayAbilitySystem/Enemy/Weapon/DA_Weapon.DA_Weapon"));
	if (TestNotNull(TEXT("Enemy weapon data loads for confirmed impact feedback"), WeaponData))
	{
		const FWeaponDefinition* SwordDefinition =
			WeaponData->FindWeaponDefinitionByTag(Item_EnemyWeapon_Sword);
		TestTrue(TEXT("Sword stores its confirmed impact cue"),
			SwordDefinition
				&& SwordDefinition->CombatData.ImpactGameplayCueTag
					== GameplayCue_Impact_Weapon_Sword);
	}

	UClass* BossBlueprintClass = LoadClass<AShipBossEnemy>(
		nullptr, TEXT("/Game/GameplayAbilitySystem/Enemy/BP_Ship_BossEnemy.BP_Ship_BossEnemy_C"));
	const AShipBossEnemy* BossBlueprintCDO = BossBlueprintClass
		? Cast<AShipBossEnemy>(BossBlueprintClass->GetDefaultObject())
		: nullptr;
	if (TestNotNull(TEXT("Boss Blueprint class loads"), BossBlueprintCDO))
	{
		const UBehaviorTree* RootTree = BossBlueprintCDO->GetBehaviorTree();
		const UBlackboardData* BossBlackboard = LoadObject<UBlackboardData>(
			nullptr, TEXT("/Game/GameplayAbilitySystem/Enemy/AI/BB_RogueBoss.BB_RogueBoss"));
		const UBehaviorTree* CombatSubtree = LoadObject<UBehaviorTree>(
			nullptr,
			TEXT("/Game/GameplayAbilitySystem/Enemy/AI/SubTree/RogueBoss/BT_Subtree_RogueBoss_Combat.BT_Subtree_RogueBoss_Combat"));

		if (TestNotNull(TEXT("BP_Ship_BossEnemy references a root Behavior Tree"), RootTree))
		{
			TestTrue(TEXT("Boss root tree is BT_RogueBoss"),
				RootTree->GetPathName()
				== TEXT("/Game/GameplayAbilitySystem/Enemy/AI/BT_RogueBoss.BT_RogueBoss"));
			TestTrue(TEXT("BT_RogueBoss uses BB_RogueBoss"),
				RootTree->BlackboardAsset == BossBlackboard);
			TestNotNull(TEXT("BT_RogueBoss has a compiled runtime root node"), RootTree->RootNode.Get());

			TArray<const UBTTask_RunBehaviorDynamic*> DynamicRoutes;
			BossMVPTests::CollectNodes(RootTree->RootNode.Get(), DynamicRoutes);
			const FGameplayTag CombatRouteTag = FGameplayTag::RequestGameplayTag(
				TEXT("AI.Behavior.Combat"), false);
			const bool bHasCombatRoute = DynamicRoutes.ContainsByPredicate(
				[CombatRouteTag](const UBTTask_RunBehaviorDynamic* Route)
				{
					return Route && Route->HasMatchingTag(CombatRouteTag);
				});
			TestTrue(TEXT("BT_RogueBoss has an AI.Behavior.Combat dynamic route"), bHasCombatRoute);
		}

		if (TestNotNull(TEXT("BB_RogueBoss loads"), BossBlackboard))
		{
			const FBlackboardEntry* TargetKey = BossMVPTests::FindBlackboardKey(BossBlackboard, TEXT("TargetActor"));
			const FBlackboardEntry* DestinationKey = BossMVPTests::FindBlackboardKey(BossBlackboard, TEXT("DestinationPointId"));
			const FBlackboardEntry* StateKey = BossMVPTests::FindBlackboardKey(BossBlackboard, TEXT("State"));
			TestTrue(TEXT("TargetActor is an Object Blackboard key"),
				TargetKey && TargetKey->KeyType && TargetKey->KeyType->IsA<UBlackboardKeyType_Object>());
			TestTrue(TEXT("DestinationPointId is an Int Blackboard key"),
				DestinationKey && DestinationKey->KeyType && DestinationKey->KeyType->IsA<UBlackboardKeyType_Int>());
			const UBlackboardKeyType_Enum* StateEnum = StateKey
				? Cast<UBlackboardKeyType_Enum>(StateKey->KeyType)
				: nullptr;
			TestTrue(TEXT("State Blackboard key uses EEnemyAIState"),
				StateEnum && StateEnum->EnumType == StaticEnum<EEnemyAIState>());
		}

		if (TestNotNull(TEXT("Boss combat subtree loads"), CombatSubtree))
		{
			TestTrue(TEXT("Combat subtree uses the same BB_RogueBoss contract"),
				CombatSubtree->BlackboardAsset == BossBlackboard);
			TestNotNull(TEXT("Combat subtree has a compiled runtime root node"), CombatSubtree->RootNode.Get());

			TArray<const UBTT_ActivateBossAbility*> AbilityTasks;
			BossMVPTests::CollectNodes(CombatSubtree->RootNode.Get(), AbilityTasks);
			FGameplayTagContainer ConfiguredAbilityTags;
			for (const UBTT_ActivateBossAbility* AbilityTask : AbilityTasks)
			{
				if (AbilityTask && AbilityTask->GetAbilityAssetTag().IsValid())
				{
					ConfiguredAbilityTags.AddTag(AbilityTask->GetAbilityAssetTag());
				}
			}
			TestTrue(TEXT("Combat subtree activates BasicAttack"),
				ConfiguredAbilityTags.HasTagExact(GameplayAbility_BasicAttack));
			TestTrue(TEXT("Combat subtree activates Knockback"),
				ConfiguredAbilityTags.HasTagExact(GameplayAbility_Boss_Knockback));
			TestTrue(TEXT("Combat subtree activates DashSlash"),
				ConfiguredAbilityTags.HasTagExact(GameplayAbility_Boss_DashSlash));
			TestTrue(TEXT("Combat subtree activates Vanish"),
				ConfiguredAbilityTags.HasTagExact(GameplayAbility_Boss_Vanish));

			TArray<const UBTT_SelectBossDestinationPoint*> DestinationTasks;
			BossMVPTests::CollectNodes(CombatSubtree->RootNode.Get(), DestinationTasks);
			TestTrue(TEXT("Combat subtree contains BT-owned destination selection"),
				DestinationTasks.Num() > 0);
		}

		if (const UEnemyBehaviorSet* BehaviorSet = BossBlueprintCDO->GetBehaviorSet())
		{
			TestTrue(TEXT("DA_RogueBoss routes Combat to the authored boss subtree"),
				BehaviorSet->FindSubtree(EEnemyAIState::Combat) == CombatSubtree);
		}
		else
		{
			AddError(TEXT("BP_Ship_BossEnemy has no BehaviorSet for its dynamic Combat route."));
		}
	}

	const UGA_BossKnockback* Knockback = GetDefault<UGA_BossKnockback>();
	const UGA_BossVanish* Vanish = GetDefault<UGA_BossVanish>();
	const UGA_BossDashSlash* Dash = GetDefault<UGA_BossDashSlash>();
	const UGA_BossBasicAttack* Basic = GetDefault<UGA_BossBasicAttack>();
	const UBossAbilityCooldownEffect* CooldownEffect = GetDefault<UBossAbilityCooldownEffect>();

	if (TestNotNull(TEXT("Boss knockback ability exists"), Knockback))
	{
		TestTrue(TEXT("Knockback has its independent cooldown tag"),
			Knockback->GetCooldownTags()->HasTagExact(Cooldown_Boss_Knockback));
		TestTrue(TEXT("Knockback exposes its boss ability asset tag"),
			Knockback->GetAssetTags().HasTagExact(GameplayAbility_Boss_Knockback));
	}
	if (TestNotNull(TEXT("Boss vanish ability exists"), Vanish))
	{
		TestTrue(TEXT("Vanish has its independent cooldown tag"),
			Vanish->GetCooldownTags()->HasTagExact(Cooldown_Boss_Vanish));
		TestNull(TEXT("Vanish no longer owns destination selection settings"),
			FindFProperty<FProperty>(Vanish->GetClass(), TEXT("PointSelectionSettings")));
	}
	if (TestNotNull(TEXT("Boss dash ability exists"), Dash))
	{
		TestTrue(TEXT("Dash has its independent cooldown tag"),
			Dash->GetCooldownTags()->HasTagExact(Cooldown_Boss_DashSlash));
		TestNull(TEXT("Dash no longer owns destination selection settings"),
			FindFProperty<FProperty>(Dash->GetClass(), TEXT("PointSelectionSettings")));
		TestFalse(TEXT("Dash does not emit damage feedback before confirmed health loss"),
			Dash->GetStartupGameplayCueTag().IsValid());
		TestTrue(TEXT("Dash stamps its confirmed impact cue into the outgoing damage spec"),
			Dash->GetImpactGameplayCueTag() == GameplayCue_Impact_Boss_DashSlash);
		TestEqual(TEXT("Dash montage windup section contract"),
			Dash->GetWindupSectionName(), FName(TEXT("Windup")));
		TestEqual(TEXT("Dash montage slash section contract"),
			Dash->GetDashSlashSectionName(), FName(TEXT("DashSlash")));
		TestEqual(TEXT("Dash montage hold section contract"),
			Dash->GetDashHoldSectionName(), FName(TEXT("DashHold")));
		TestEqual(TEXT("Dash montage recovery section contract"),
			Dash->GetRecoverySectionName(), FName(TEXT("Recover")));
	}
	if (TestNotNull(TEXT("Boss basic attack specialization exists"), Basic))
	{
		TestTrue(TEXT("Basic attack remains independent from knockback cooldown"),
			Basic->GetCooldownTags()->HasTagExact(Cooldown_Enemy_BasicAttack));
	}
	if (TestNotNull(TEXT("Native boss cooldown effect exists"), CooldownEffect))
	{
		TestEqual(TEXT("Boss cooldown effect has duration policy"),
			CooldownEffect->DurationPolicy, EGameplayEffectDurationType::HasDuration);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBossBehaviorTreeStartTest,
	"ArtisticSW.Enemy.BossMVP.BehaviorTreeStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBossBehaviorTreeStartTest::RunTest(const FString& Parameters)
{
	BossMVPTests::FScopedTestWorld TestWorld;
	TestWorld.World->CreateAISystem();
	UClass* BossBlueprintClass = LoadClass<AShipBossEnemy>(
		nullptr, TEXT("/Game/GameplayAbilitySystem/Enemy/BP_Ship_BossEnemy.BP_Ship_BossEnemy_C"));
	AShipBossEnemy* Boss = BossBlueprintClass
		? TestWorld.World->SpawnActor<AShipBossEnemy>(BossBlueprintClass)
		: nullptr;
	if (!TestNotNull(TEXT("Boss Blueprint spawns for BT start validation"), Boss))
	{
		return false;
	}

	AShipBossAIController* Controller = TestWorld.World->SpawnActor<AShipBossAIController>();
	if (!TestNotNull(TEXT("BT-only boss controller spawns"), Controller))
	{
		return false;
	}
	Controller->Possess(Boss);

	const UBehaviorTreeComponent* Brain = Cast<UBehaviorTreeComponent>(Controller->GetBrainComponent());
	const UBlackboardComponent* Blackboard = Controller->GetBlackboardComponent();
	if (TestNotNull(TEXT("Possession creates a BehaviorTree brain"), Brain))
	{
		TestTrue(TEXT("Boss BehaviorTree brain is running after possession"), Brain->IsRunning());
	}
	if (TestNotNull(TEXT("Possession initializes the boss Blackboard"), Blackboard))
	{
		TestTrue(TEXT("Runtime Blackboard matches BT_RogueBoss Blackboard"),
			Blackboard->GetBlackboardAsset() == Boss->GetBehaviorTree()->BlackboardAsset);
		TestEqual(TEXT("Runtime DestinationPointId starts invalid"),
			Blackboard->GetValueAsInt(TEXT("DestinationPointId")), INDEX_NONE);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBossEncounterConfiguredChildActorBoxTest,
	"ArtisticSW.Enemy.BossMVP.ConfiguredChildActorBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBossEncounterConfiguredChildActorBoxTest::RunTest(const FString& Parameters)
{
	BossMVPTests::FScopedTestWorld TestWorld;
	AEnemyShip* Ship = TestWorld.World->SpawnActor<AEnemyShip>();
	if (!TestNotNull(TEXT("Enemy ship is spawned"), Ship))
	{
		return false;
	}

	UChildActorComponent* BoxComponent = NewObject<UChildActorComponent>(
		Ship, TEXT("BossEncounterBoxChildActor"));
	BoxComponent->SetChildActorClass(AEnemyItemBox::StaticClass());
	Ship->AddInstanceComponent(BoxComponent);
	BoxComponent->OnComponentCreated();
	BoxComponent->SetupAttachment(Ship->GetShipDeckMesh());
	BoxComponent->RegisterComponent();
	if (!BoxComponent->GetChildActor())
	{
		BoxComponent->CreateChildActor();
	}
	AEnemyItemBox* ItemBox = Cast<AEnemyItemBox>(BoxComponent->GetChildActor());
	if (!TestNotNull(TEXT("EnemyItemBox child actor is created from the ship Blueprint-style component"), ItemBox))
	{
		return false;
	}

	UBossEncounterComponent* Encounter = Ship->GetBossEncounterComponent();
	if (!TestNotNull(TEXT("Boss encounter component exists"), Encounter))
	{
		return false;
	}
	FStructProperty* BoxReferenceProperty = FindFProperty<FStructProperty>(
		Encounter->GetClass(), TEXT("EnemyItemBoxComponent"));
	if (!TestNotNull(TEXT("Encounter exposes an editor-configurable box component reference"),
		BoxReferenceProperty))
	{
		return false;
	}
	FComponentReference* BoxReference = BoxReferenceProperty->ContainerPtrToValuePtr<FComponentReference>(Encounter);
	BoxReference->OverrideComponent = BoxComponent;
	Encounter->ConfigureEncounter(nullptr, AShipBossEnemy::StaticClass());
	TestEqual(TEXT("Encounter resolves the EnemyItemBox child actor selected by component reference"),
		Encounter->GetEnemyItemBox(), static_cast<AStorageChest*>(ItemBox));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBossPointMathTest,
	"ArtisticSW.Enemy.BossMVP.PointMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBossPointMathTest::RunTest(const FString& Parameters)
{
	const FVector TargetLocation = FVector::ZeroVector;
	const FVector TargetForward = FVector::ForwardVector;
	const FVector DeckUp = FVector::UpVector;

	TestTrue(TEXT("Point in rear half-plane is accepted"),
		UBossDeckPointSelector::IsPointBehindTarget(
			TargetLocation, TargetForward, FVector(-300.0f, 50.0f, 0.0f), DeckUp, 0.0f));
	TestFalse(TEXT("Point in front of player is rejected"),
		UBossDeckPointSelector::IsPointBehindTarget(
			TargetLocation, TargetForward, FVector(300.0f, 0.0f, 0.0f), DeckUp, 0.0f));
	TestTrue(TEXT("Dash segment crossing the target is accepted"),
		UBossDeckPointSelector::DoesSegmentPassTarget(
			FVector(400.0f, 0.0f, 0.0f), FVector(-400.0f, 0.0f, 0.0f), TargetLocation, 120.0f));
	TestFalse(TEXT("Dash segment missing the target corridor is rejected"),
		UBossDeckPointSelector::DoesSegmentPassTarget(
			FVector(400.0f, 300.0f, 0.0f), FVector(-400.0f, 300.0f, 0.0f), TargetLocation, 120.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBossEncounterSingleTriggerTest,
	"ArtisticSW.Enemy.BossMVP.EncounterSingleTrigger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBossEncounterSingleTriggerTest::RunTest(const FString& Parameters)
{
	BossMVPTests::FScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient boss test world is created"), TestWorld.World))
	{
		return false;
	}

	AEnemyShip* Ship = TestWorld.World->SpawnActor<AEnemyShip>();
	AEnemyItemBox* ItemBox = TestWorld.World->SpawnActor<AEnemyItemBox>();
	AActor* Interactor = TestWorld.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Enemy ship is spawned"), Ship)
		|| !TestNotNull(TEXT("Enemy item box is spawned"), ItemBox)
		|| !TestNotNull(TEXT("Interactor is spawned"), Interactor))
	{
		return false;
	}

	UBossEncounterComponent* Encounter = Ship->GetBossEncounterComponent();
	if (!TestNotNull(TEXT("Ship encounter component is available"), Encounter))
	{
		return false;
	}
	Encounter->ConfigureEncounter(ItemBox, nullptr, -1);
	TestTrue(TEXT("Configured encounter locks the item box before combat"), ItemBox->IsLocked());

	ItemBox->GetInteractableComponent()->Interact(Interactor);
	TestEqual(TEXT("Invalid authoring fails atomically after the first interaction"),
		Encounter->GetEncounterState(), EBossEncounterState::Failed);
	TestNull(TEXT("Failed encounter does not leave a partial boss"), Encounter->GetSpawnedBoss());

	ItemBox->GetInteractableComponent()->Interact(Interactor);
	TestEqual(TEXT("A second interaction cannot restart a terminal encounter"),
		Encounter->GetEncounterState(), EBossEncounterState::Failed);
	TestTrue(TEXT("Failed encounter keeps the collectible locked"), ItemBox->IsLocked());
	return true;
}

#endif
