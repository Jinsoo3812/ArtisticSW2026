#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Projectiles/GravityVortexProjectile.h"
#include "Ship.h"
#include "Skills/GravityVortexField.h"
#include "Skills/VortexAimLine.h"
#include "Skills/Abilities/GA_GravityVortexThrow.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGravityVortexHoldInputTest,
	"ArtisticSW.GravityVortex.HoldInputLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGravityVortexHoldInputTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("Skill-bearing DefaultIMC priority is raised above the legacy ItemIMC"),
		ABasePlayer::ResolveDefaultMappingPriority(1, 1, true) > 1);
	TestEqual(
		TEXT("DefaultIMC priority is unchanged when no skill input is assigned"),
		ABasePlayer::ResolveDefaultMappingPriority(1, 1, false),
		1);

	const UGA_GravityVortexThrow* AbilityDefaults = GetDefault<UGA_GravityVortexThrow>();
	TestEqual(
		TEXT("Gravity Vortex uses one persistent ability instance per player"),
		AbilityDefaults->GetInstancingPolicy(),
		EGameplayAbilityInstancingPolicy::InstancedPerActor);
	TestEqual(
		TEXT("Gravity Vortex is locally predicted and confirmed by the server"),
		AbilityDefaults->GetNetExecutionPolicy(),
		EGameplayAbilityNetExecutionPolicy::LocalPredicted);
	TestFalse(
		TEXT("A hand-bone fallback is configured when an authored socket cannot be found"),
		AbilityDefaults->FallbackSpawnBoneName.IsNone());
	TestTrue(
		TEXT("A Blueprint VFX trajectory is updated independently from debug drawing"),
		AbilityDefaults->bUpdateAimTrajectoryVisual);

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GravityVortexQuickSlotWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	auto CleanupWorld = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	ABasePlayerState* PlayerState = World->SpawnActor<ABasePlayerState>();
	APlayerController* PlayerController = World->SpawnActor<APlayerController>();
	ABasePlayer* Player = World->SpawnActor<ABasePlayer>();
	if (!TestNotNull(TEXT("PlayerState is spawned"), PlayerState)
		|| !TestNotNull(TEXT("PlayerController is spawned"), PlayerController)
		|| !TestNotNull(TEXT("Player is spawned"), Player))
	{
		CleanupWorld();
		return false;
	}

	Player->bBypassSkillRequirementsForTesting = true;
	Player->SetPlayerState(PlayerState);
	PlayerController->Possess(Player);
	UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("Player ASC is initialized"), ASC))
	{
		CleanupWorld();
		return false;
	}

	Player->OnGravityVortexSkillPressed();
	TestTrue(
		TEXT("Pressing and holding the skill key enters Gravity Vortex aiming mode"),
		ASC->HasMatchingGameplayTag(GameplayAbility_Skill_GravityVortex));

	Player->OnMouseInputPressed(Key_Default_Mouse_RightClick);
	TestFalse(
		TEXT("Right click cancels Gravity Vortex aiming mode"),
		ASC->HasMatchingGameplayTag(GameplayAbility_Skill_GravityVortex));

	Player->OnGravityVortexSkillPressed();
	TestTrue(
		TEXT("The skill can enter aiming mode again"),
		ASC->HasMatchingGameplayTag(GameplayAbility_Skill_GravityVortex));

	Player->OnGravityVortexSkillReleased();
	TestFalse(
		TEXT("Releasing the held skill key cancels Gravity Vortex aiming mode"),
		ASC->HasMatchingGameplayTag(GameplayAbility_Skill_GravityVortex));

	CleanupWorld();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGravityVortexAssetWiringTest,
	"ArtisticSW.GravityVortex.AssetWiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGravityVortexAssetWiringTest::RunTest(const FString& Parameters)
{
	UClass* PlayerClass = LoadClass<ABasePlayer>(
		nullptr, TEXT("/Game/Blueprints/Player/BP_Player.BP_Player_C"));
	if (!TestNotNull(TEXT("BP_Player class loads"), PlayerClass))
	{
		return false;
	}

	const ABasePlayer* PlayerDefaults = Cast<ABasePlayer>(PlayerClass->GetDefaultObject());
	const USkeletalMeshComponent* PlayerMesh = PlayerDefaults ? PlayerDefaults->GetMesh() : nullptr;
	if (!TestNotNull(TEXT("BP_Player has a skeletal mesh component"), PlayerMesh))
	{
		return false;
	}
	AddInfo(FString::Printf(
		TEXT("BP_Player skeletal mesh: %s"),
		*GetPathNameSafe(PlayerMesh->GetSkeletalMeshAsset())));
	TestTrue(
		TEXT("BP_Player mesh exposes the authored HandGrip_R socket"),
		PlayerMesh->DoesSocketExist(TEXT("HandGrip_R")));
	if (const USkeletalMeshSocket* HandGripSocket = PlayerMesh->GetSocketByName(TEXT("HandGrip_R")))
	{
		AddInfo(FString::Printf(
			TEXT("HandGrip_R parent=%s relativeLocation=%s relativeRotation=%s relativeScale=%s"),
			*HandGripSocket->BoneName.ToString(),
			*HandGripSocket->RelativeLocation.ToCompactString(),
			*HandGripSocket->RelativeRotation.ToCompactString(),
			*HandGripSocket->RelativeScale.ToCompactString()));
	}
	TestTrue(
		TEXT("BP_Player mesh exposes the hand_r fallback bone"),
		PlayerMesh->DoesSocketExist(TEXT("hand_r")));

	UClass* AbilityClass = LoadClass<UGA_GravityVortexThrow>(
		nullptr, TEXT("/Game/New/Skill/Vortex/GA_VortexField.GA_VortexField_C"));
	if (!TestNotNull(TEXT("GA_VortexField class loads"), AbilityClass))
	{
		return false;
	}
	const UGA_GravityVortexThrow* AbilityDefaults =
		Cast<UGA_GravityVortexThrow>(AbilityClass->GetDefaultObject());
	AddInfo(FString::Printf(
		TEXT("GA socket=%s projectile=%s aimLine=%s debug=%s visual=%s"),
		AbilityDefaults ? *AbilityDefaults->SpawnSocketName.ToString() : TEXT("None"),
		AbilityDefaults ? *GetPathNameSafe(AbilityDefaults->ProjectileClass.Get()) : TEXT("None"),
		AbilityDefaults ? *GetPathNameSafe(AbilityDefaults->AimLineClass.Get()) : TEXT("None"),
		AbilityDefaults && AbilityDefaults->bDrawAimTrajectory ? TEXT("true") : TEXT("false"),
		AbilityDefaults && AbilityDefaults->bUpdateAimTrajectoryVisual ? TEXT("true") : TEXT("false")));
	TestEqual(
		TEXT("BP_Player grants the configured GA_VortexField Blueprint, not the native fallback"),
		PlayerDefaults->GravityVortexAbilityClass.Get(),
		AbilityClass);
	TestEqual(
		TEXT("GA_VortexField resolves the authored right-hand socket"),
		AbilityDefaults->SpawnSocketName,
		FName(TEXT("HandGrip_R")));
	TestFalse(
		TEXT("GA_VortexField debug trajectory is disabled"),
		AbilityDefaults->bDrawAimTrajectory);
	TestTrue(
		TEXT("GA_VortexField real trajectory visualization is enabled"),
		AbilityDefaults->bUpdateAimTrajectoryVisual);
	TestTrue(
		TEXT("GA_VortexField aim line refreshes at least at approximately 60 Hz"),
		AbilityDefaults->TrajectoryRefreshInterval <= 0.017f);
	TestTrue(
		TEXT("GA_VortexField prediction sampling is capped for visual stability"),
		AbilityDefaults->TrajectorySimulationFrequency <= 10.0f);
	TestNotNull(TEXT("GA_VortexField has a projectile class"), AbilityDefaults->ProjectileClass.Get());
	if (TestNotNull(TEXT("GA_VortexField has an aim-line class"), AbilityDefaults->AimLineClass.Get()))
	{
		const AVortexAimLine* AimLineDefaults =
			AbilityDefaults->AimLineClass->GetDefaultObject<AVortexAimLine>();
		if (TestNotNull(TEXT("Configured aim-line CDO derives from AVortexAimLine"), AimLineDefaults))
		{
			TestNotNull(
				TEXT("Configured aim-line has SM_VortexAimLine assigned"),
				AimLineDefaults->AimLineMesh.Get());
			TestNotNull(
				TEXT("Configured aim-line has M_VortexAimLine assigned"),
				AimLineDefaults->AimLineMaterial.Get());
			TestFalse(
				TEXT("Configured aim-line disables unstable CurveClamped tangents"),
				AimLineDefaults->bSmoothTrajectory);
			TestEqual(
				TEXT("Configured aim-line caps rendered spline components"),
				AimLineDefaults->MaxSegments,
				20);
		}
	}

	const AGravityVortexProjectile* ProjectileDefaults =
		AbilityDefaults && AbilityDefaults->ProjectileClass
			? AbilityDefaults->ProjectileClass->GetDefaultObject<AGravityVortexProjectile>()
			: nullptr;
	if (!TestNotNull(TEXT("Configured projectile CDO loads"), ProjectileDefaults))
	{
		return false;
	}
	AddInfo(FString::Printf(
		TEXT("Projectile=%s field=%s"),
		*GetPathNameSafe(ProjectileDefaults->GetClass()),
		*GetPathNameSafe(ProjectileDefaults->FieldClass.Get())));
	TestNotNull(TEXT("Projectile has a vortex field class"), ProjectileDefaults->FieldClass.Get());

	const AGravityVortexField* FieldDefaults = ProjectileDefaults->FieldClass
		? ProjectileDefaults->FieldClass->GetDefaultObject<AGravityVortexField>()
		: nullptr;
	if (!TestNotNull(TEXT("Configured field CDO loads"), FieldDefaults))
	{
		return false;
	}
	AddInfo(FString::Printf(
		TEXT("Field=%s replicates=%s tick=%s debug=%s radius=%.1f acceleration=%.1f duration=%.2f"),
		*GetPathNameSafe(FieldDefaults->GetClass()),
		FieldDefaults->GetIsReplicated() ? TEXT("true") : TEXT("false"),
		FieldDefaults->PrimaryActorTick.bCanEverTick ? TEXT("true") : TEXT("false"),
		FieldDefaults->bDrawDebug ? TEXT("true") : TEXT("false"),
		FieldDefaults->PullRadius,
		FieldDefaults->PullAcceleration,
		FieldDefaults->Duration));
	TestTrue(TEXT("Field replication is enabled"), FieldDefaults->GetIsReplicated());
	TestTrue(TEXT("Field ticking is enabled"), FieldDefaults->PrimaryActorTick.bCanEverTick);
	TestTrue(TEXT("Field debug visualization is enabled"), FieldDefaults->bDrawDebug);
	TestTrue(TEXT("Field pull radius is positive"), FieldDefaults->PullRadius > 0.0f);
	TestTrue(TEXT("Field acceleration is positive"), FieldDefaults->PullAcceleration > 0.0f);
	TestTrue(TEXT("Field duration is positive"), FieldDefaults->Duration > 0.0f);

	UStaticMesh* AimLineMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/New/Skill/Vortex/SM_VortexAimLine.SM_VortexAimLine"));
	if (TestNotNull(TEXT("SM_VortexAimLine loads"), AimLineMesh))
	{
		const FBoxSphereBounds MeshBounds = AimLineMesh->GetBounds();
		const FVector Size = MeshBounds.BoxExtent * 2.0f;
		const TCHAR* LongestAxis = Size.X >= Size.Y && Size.X >= Size.Z
			? TEXT("X")
			: (Size.Y >= Size.X && Size.Y >= Size.Z ? TEXT("Y") : TEXT("Z"));
		AddInfo(FString::Printf(
			TEXT("SM_VortexAimLine origin=%s size=%s longestAxis=%s verticesLOD0=%d"),
			*MeshBounds.Origin.ToCompactString(),
			*Size.ToCompactString(),
			LongestAxis,
			AimLineMesh->GetNumVertices(0)));
		TestTrue(
			TEXT("SM_VortexAimLine has a clearly identifiable forward axis"),
			FMath::Max3(Size.X, Size.Y, Size.Z)
				> FMath::Min3(Size.X, Size.Y, Size.Z) * 2.0f);
	}

	UClass* EnemyShipClass = LoadClass<AShip>(
		nullptr, TEXT("/Game/New/Enemy_Ship/BP_EnemyShip.BP_EnemyShip_C"));
	if (!TestNotNull(TEXT("BP_EnemyShip class loads"), EnemyShipClass))
	{
		return false;
	}
	const AShip* EnemyShipDefaults = Cast<AShip>(EnemyShipClass->GetDefaultObject());
	TestTrue(
		TEXT("BP_EnemyShip is eligible for enemy-only field effects"),
		EnemyShipDefaults && EnemyShipDefaults->IsEnemyShipForEffects());

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GravityVortexFieldWorld"));
	if (!TestNotNull(TEXT("Transient vortex field world is created"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	auto CleanupWorld = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	const FVector PlayerSpawnLocation(3000.0f, 4000.0f, 500.0f);
	ABasePlayer* BlueprintPlayer = World->SpawnActor<ABasePlayer>(
		PlayerClass, PlayerSpawnLocation, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("BP_Player spawns for socket transform validation"), BlueprintPlayer))
	{
		CleanupWorld();
		return false;
	}
	const FVector HandGripWorldLocation =
		BlueprintPlayer->GetMesh()->GetSocketLocation(TEXT("HandGrip_R"));
	const FVector HeadWorldLocation =
		BlueprintPlayer->GetMesh()->GetSocketLocation(TEXT("head"));
	AddInfo(FString::Printf(
		TEXT("Spawned BP_Player actor=%s handGrip=%s head=%s handToHeadDistance=%.1f"),
		*BlueprintPlayer->GetActorLocation().ToCompactString(),
		*HandGripWorldLocation.ToCompactString(),
		*HeadWorldLocation.ToCompactString(),
		FVector::Distance(HandGripWorldLocation, HeadWorldLocation)));
	TestTrue(
		TEXT("The resolved HandGrip_R world transform is spatially separate from the face/head"),
		FVector::Distance(HandGripWorldLocation, HeadWorldLocation) > 20.0f);
	FVector ResolvedLaunchLocation = FVector::ZeroVector;
	const USkeletalMeshComponent* ResolvedLaunchMesh = nullptr;
	FName ResolvedLaunchName = NAME_None;
	TestTrue(
		TEXT("Gravity Vortex launch resolver finds the configured socket"),
		UGA_GravityVortexThrow::ResolveSpawnSocket(
			BlueprintPlayer,
			AbilityDefaults->SpawnSocketName,
			AbilityDefaults->FallbackSpawnBoneName,
			ResolvedLaunchLocation,
			ResolvedLaunchMesh,
			ResolvedLaunchName));
	TestEqual(
		TEXT("Gravity Vortex launch resolver keeps HandGrip_R instead of falling back"),
		ResolvedLaunchName,
		FName(TEXT("HandGrip_R")));
	TestTrue(
		TEXT("Gravity Vortex launch starts exactly at the HandGrip_R world transform"),
		ResolvedLaunchLocation.Equals(HandGripWorldLocation, 0.1f));
	TestTrue(
		TEXT("Gravity Vortex launch resolver uses the visible player mesh"),
		ResolvedLaunchMesh == BlueprintPlayer->GetMesh());

	if (AimLineMesh && AbilityDefaults->AimLineClass)
	{
		AVortexAimLine* AimLine = World->SpawnActor<AVortexAimLine>(
			AbilityDefaults->AimLineClass);
		if (TestNotNull(TEXT("Configured BP_VortexAimLine actor spawns"), AimLine))
		{
			AimLine->SetTrajectory({
				FVector(0.0f, 0.0f, 100.0f),
				FVector(500.0f, 0.0f, 300.0f),
				FVector(1000.0f, 0.0f, 100.0f) });
			TArray<USplineMeshComponent*> SplineMeshes;
			AimLine->GetComponents<USplineMeshComponent>(SplineMeshes);
			TestEqual(
				TEXT("Three trajectory points create two visible spline-mesh segments"),
				SplineMeshes.Num(),
				2);
			for (USplineMeshComponent* SplineMesh : SplineMeshes)
			{
				TestTrue(
					TEXT("Configured spline-mesh segment is visible"),
					SplineMesh && SplineMesh->IsVisible() && !SplineMesh->bHiddenInGame);
				TestTrue(
					TEXT("Configured spline-mesh segment uses SM_VortexAimLine"),
					SplineMesh && SplineMesh->GetStaticMesh() == AimLineMesh);
				TestTrue(
					TEXT("Configured spline-mesh segment uses M_VortexAimLine"),
					SplineMesh
						&& SplineMesh->GetMaterial(0) == AimLine->AimLineMaterial.Get());
			}

			TArray<FVector> DenseTrajectory;
			for (int32 PointIndex = 0; PointIndex < 62; ++PointIndex)
			{
				const float Time = static_cast<float>(PointIndex) / 20.0f;
				DenseTrajectory.Add(FVector(
					Time * 1000.0f,
					0.0f,
					100.0f + Time * 600.0f - 490.0f * Time * Time));
			}
			AimLine->SetTrajectory(DenseTrajectory);
			SplineMeshes.Reset();
			AimLine->GetComponents<USplineMeshComponent>(SplineMeshes);
			TestEqual(
				TEXT("Dense predicted trajectory is uniformly capped to twenty components"),
				SplineMeshes.Num(),
				20);
			TestTrue(
				TEXT("Resampling preserves the exact socket/start point"),
				AimLine->TrajectorySpline->GetLocationAtSplinePoint(
					0, ESplineCoordinateSpace::World).Equals(DenseTrajectory[0], 0.1f));
			TestTrue(
				TEXT("Resampling preserves the complete trajectory endpoint"),
				AimLine->TrajectorySpline->GetLocationAtSplinePoint(
					AimLine->TrajectorySpline->GetNumberOfSplinePoints() - 1,
					ESplineCoordinateSpace::World).Equals(DenseTrajectory.Last(), 0.1f));
		}
	}

	const FVector FieldSpawnLocation(10000.0f, 20000.0f, 125.0f);
	AShip* EnemyShip = World->SpawnActor<AShip>(
		EnemyShipClass, FieldSpawnLocation + FVector(1000.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	AGravityVortexField* Field = World->SpawnActor<AGravityVortexField>(
		ProjectileDefaults->FieldClass, FieldSpawnLocation, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Configured BP_VortexField spawns"), Field)
		|| !TestNotNull(TEXT("BP_EnemyShip spawns"), EnemyShip))
	{
		CleanupWorld();
		return false;
	}
	TestTrue(
		TEXT("BP_VortexField preserves the projectile water-impact spawn location"),
		Field->GetActorLocation().Equals(FieldSpawnLocation, 0.1f));
	Field->DispatchBeginPlay();
	TestTrue(
		TEXT("BP_VortexField registers an in-range BP_EnemyShip acceleration source"),
		EnemyShip->GetExternalAccelerationSourceCount() > 0);
	CleanupWorld();
	return true;
}

#endif
