#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "BaseEnemy.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GAS/Ability/GA_RangedEnemyAttack.h"
#include "RangedEnemy/RangedEnemy.h"
#include "RangedEnemy/RangedEnemyAIController.h"
#include "RangedEnemy/RangedEnemyProjectile.h"
#include "Ship.h"

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
	const UGA_RangedEnemyAttack* AbilityCDO = GetDefault<UGA_RangedEnemyAttack>();

	TestNotNull(TEXT("RangedEnemy CDO exists"), EnemyCDO);
	TestEqual(TEXT("RangedEnemy uses the dedicated AI controller"), EnemyCDO->AIControllerClass,
		TSubclassOf<AController>(ARangedEnemyAIController::StaticClass()));
	TestTrue(TEXT("Default projectile derives from RangedEnemyProjectile"),
		EnemyCDO->GetRangedProjectileClass()
		&& EnemyCDO->GetRangedProjectileClass()->IsChildOf(ARangedEnemyProjectile::StaticClass()));
	TestTrue(TEXT("Ranged attack ability exposes the ranged attack asset tag"),
		AbilityCDO->GetAssetTags().HasTagExact(GameplayAbility_RangedAttack));
	TestTrue(TEXT("Ranged attack remains compatible with the existing basic-attack task tag"),
		AbilityCDO->GetAssetTags().HasTagExact(GameplayAbility_BasicAttack));

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

	TestFalse(TEXT("Projectile rejects its source enemy"), Projectile->IsValidDamageTarget(SourceEnemy));
	TestFalse(TEXT("Projectile rejects another enemy-team actor"), Projectile->IsValidDamageTarget(FriendlyEnemy));
	TestTrue(TEXT("Projectile accepts an actor carrying Team.Player"), Projectile->IsValidDamageTarget(PlayerTeamTarget));
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
	EnemyASC->GiveAbility(FGameplayAbilitySpec(UGA_RangedEnemyAttack::StaticClass(), 1));

	const int32 ProjectilesBefore = RangedEnemyTests::CountActors<ARangedEnemyProjectile>(TestWorld.World);
	TestTrue(TEXT("Standalone RangedEnemy activates its server ranged attack without HostShip"), Enemy->TryStartRangedAttack());
	const int32 ProjectilesAfter = RangedEnemyTests::CountActors<ARangedEnemyProjectile>(TestWorld.World);
	TestEqual(TEXT("An immediate-fire projectile is spawned when no montage is assigned"),
		ProjectilesAfter, ProjectilesBefore + 1);

	Player->SetActorEnableCollision(false);
	TestTrue(TEXT("A collision-disabled/helming ABasePlayer remains a combat target"), Enemy->IsValidCombatTarget(Player));

	Enemy->SetHostShip(HostShip);
	TestEqual(TEXT("An optional explicit host ship assignment is retained"), Enemy->GetHostShip(), HostShip);
	return true;
}

#endif
