// Fill out your copyright notice in the Description page of Project Settings.


#include "Ship.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/StaticMeshComponent.h"
#include "InteractableComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystemComponent.h"
#include "HAL/IConsoleManager.h"
#include "BaseAttributeSet.h"
#include "ShipAttributeSet.h"
#include "BaseGameplayTags.h"
#include "Skills/SkillUseProvider.h"
#include "ShipPhysicsAsync.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PBDRigidsSolver.h"
#include "BuoyancyComponent.h"
#include "Buoyancy/SWBuoyancyComponent.h"
#include "Water/SWRippleStateSubsystem.h"
#include "WaterBodyActor.h"
#include "EngineUtils.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Physics/NetworkPhysicsComponent.h"
#include "GerstnerWaterWaves.h"
#include "WaterWaves.h"
#include "Chaos/PhysicsObject.h"
#include "Interfaces/IPhysicsComponent.h"
#include "GameFramework/GameStateBase.h"
#include "DrawDebugHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "CollisionChannels.h"
#include "Bombardment.h"
#include "Cannon.h"
#include "Cannonball.h"
#include "LandscapeProxy.h"
#include "WaterSurfaceQueryLibrary.h"
#include "Upgrade/ShipUpgradeComponent.h"

namespace
{
	TAutoConsoleVariable<int32> CVarShowShipNetworkBuoyancyDebug(
		TEXT("p.ShowShipNetworkBuoyancyDebug"),
		0,
		TEXT("Draw local and replicated ship buoyancy pontoons. 0=off, 1=on."),
		ECVF_Cheat);

	bool EvaluateGameThreadWaveOffset(
		UWorld* World,
		const FVector& Position,
		double ServerTime,
		float& OutHeight)
	{
		if (!World)
		{
			return false;
		}

		for (TActorIterator<AWaterBody> It(World); It; ++It)
		{
			if (UWaterWavesBase* Waves = It->GetWaterWaves())
			{
				FVector Normal = FVector::UpVector;
				// The Ship PT evaluator operates on the unattenuated deep-water wave
				// offset, so use a large water depth for an apples-to-apples check.
				OutHeight = Waves->GetWaveHeightAtPosition(
					Position,
					100000.0f,
					static_cast<float>(ServerTime),
					Normal);
				return true;
			}
		}

		return false;
	}
}


// Sets default values
AShip::AShip()
{
	BombardmentSkillHotkey = EKeys::Eight;

 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Buoyancy Root
	BuoyancyRoot = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BuoyancyRoot"));
	RootComponent = BuoyancyRoot;
	BuoyancyRoot->SetSimulatePhysics(true);
	BuoyancyRoot->SetCollisionProfileName(TEXT("PlayerShip"));
	BuoyancyRoot->SetGenerateOverlapEvents(false);
	BuoyancyRoot->SetLinearDamping(0.8f);
	BuoyancyRoot->SetAngularDamping(3.0f);

	// Split presentation and gameplay queries away from the Chaos body. Existing
	// Blueprint assets keep assigning their mesh to BuoyancyRoot; OnConstruction
	// mirrors that asset until dedicated meshes are authored.
	ShipVisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShipVisualMesh"));
	ShipVisualMesh->SetupAttachment(BuoyancyRoot);
	ShipVisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShipVisualMesh->SetGenerateOverlapEvents(false);

	ShipDamageMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShipDamageMesh"));
	ShipDamageMesh->SetupAttachment(BuoyancyRoot);
	ShipDamageMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ShipDamageMesh->SetCollisionObjectType(ECC_ShipDamage);
	ShipDamageMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	ShipDamageMesh->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Block);
	ShipDamageMesh->SetGenerateOverlapEvents(false);
	ShipDamageMesh->SetVisibility(false, false);
	ShipDamageMesh->SetHiddenInGame(true, false);
	ShipDamageMesh->SetCastShadow(false);

	ShipDeckMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShipDeckMesh"));
	ShipDeckMesh->SetupAttachment(BuoyancyRoot);
	ShipDeckMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ShipDeckMesh->SetCollisionObjectType(ECC_WorldDynamic);
	ShipDeckMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	ShipDeckMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	ShipDeckMesh->SetGenerateOverlapEvents(false);
	ShipDeckMesh->SetVisibility(false, false);
	ShipDeckMesh->SetHiddenInGame(true, false);
	ShipDeckMesh->SetCastShadow(false);

	SWBuoyancyComponent = CreateDefaultSubobject<USWBuoyancyComponent>(TEXT("SWBuoyancyComponent"));
	SWBuoyancyComponent->ExecutionMode = ESWBuoyancyExecutionMode::ExternalNetworkPhysics;
	SWBuoyancyComponent->bImportLegacyWaterBuoyancy = false;
	SWBuoyancyComponent->Pontoons.Reset();
	const auto AddShipPontoon = [this](const TCHAR* Name, const FVector& RelativeLocation)
	{
		FSWBuoyancyPontoon& Pontoon = SWBuoyancyComponent->Pontoons.AddDefaulted_GetRef();
		Pontoon.Name = FName(Name);
		Pontoon.RelativeLocation = RelativeLocation;
		Pontoon.Radius = 300.0f;
		Pontoon.ForceScale = 1.0f;
	};
	// Defaults migrated from BP_TestShip_SingleMesh's legacy Water Buoyancy.
	AddShipPontoon(TEXT("FrontPort"), FVector(1100.0f, 300.0f, 250.0f));
	AddShipPontoon(TEXT("FrontStarboard"), FVector(1100.0f, -300.0f, 250.0f));
	AddShipPontoon(TEXT("RearPort"), FVector(-1100.0f, 300.0f, 250.0f));
	AddShipPontoon(TEXT("RearStarboard"), FVector(-1100.0f, -300.0f, 250.0f));
	SWBuoyancyComponent->ForceSettings.BuoyancyCoefficient = 0.04f;
	SWBuoyancyComponent->ForceSettings.BuoyancyDamp = 1000.0f;
	SWBuoyancyComponent->ForceSettings.BuoyancyDamp2 = 1.0f;
	SWBuoyancyComponent->ForceSettings.MaxBuoyantForce = 5000000.0f;

	Tags.AddUnique(TEXT("Player"));

	// Camera Boom
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(BuoyancyRoot);
	CameraBoom->TargetArmLength = 800.0f;
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bInheritPitch = true;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bInheritRoll = false;

	// Follow Camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Interactable Component
	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
	InteractableComponent->SetupAttachment(BuoyancyRoot);
	// Set default collision preset for interactables
	InteractableComponent->SetCollisionProfileName(TEXT("Interactable"));

	// Port Sea Boarding Component
	PortSeaBoardingInteractable = CreateDefaultSubobject<UInteractableComponent>(TEXT("PortSeaBoardingInteractable"));
	PortSeaBoardingInteractable->SetupAttachment(BuoyancyRoot);
	PortSeaBoardingInteractable->SetCollisionProfileName(TEXT("Interactable"));

	PortSeaBoardingDestination = CreateDefaultSubobject<USceneComponent>(TEXT("PortSeaBoardingDestination"));
	PortSeaBoardingDestination->SetupAttachment(BuoyancyRoot);

	// Starboard Sea Boarding Component
	StarboardSeaBoardingInteractable = CreateDefaultSubobject<UInteractableComponent>(TEXT("StarboardSeaBoardingInteractable"));
	StarboardSeaBoardingInteractable->SetupAttachment(BuoyancyRoot);
	StarboardSeaBoardingInteractable->SetCollisionProfileName(TEXT("Interactable"));

	StarboardSeaBoardingDestination = CreateDefaultSubobject<USceneComponent>(TEXT("StarboardSeaBoardingDestination"));
	StarboardSeaBoardingDestination->SetupAttachment(BuoyancyRoot);

	// Ability System Component
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// Attribute Set
	AttributeSet = CreateDefaultSubobject<UShipAttributeSet>(TEXT("AttributeSet"));

	// Network Physics Component
	if (UPhysicsSettings::Get()->PhysicsPrediction.bEnablePhysicsPrediction)
	{
		static const FName NetworkPhysicsComponentName(TEXT("NetworkPhysicsComponent"));
		NetworkPhysicsComponent = CreateDefaultSubobject<UNetworkPhysicsComponent>(NetworkPhysicsComponentName);
		NetworkPhysicsComponent->SetNetAddressable();
		NetworkPhysicsComponent->SetIsReplicated(true);
		SetPhysicsReplicationMode(EPhysicsReplicationMode::Resimulation);
	}

	bReplicates = true;
	SetReplicateMovement(true); // standard movement replication 활성화 (Iris 비활성화 상태 하의 Simulated Proxy 롤백 채널 확보)
	bAlwaysRelevant = true;
}

// Called when the game starts or when spawned
void AShip::BeginPlay()
{
	Super::BeginPlay();
	bBuoyancyQueryDiagnostics = FParse::Param(
		FCommandLine::Get(), TEXT("BuoyancyQueryDiagnostics"));

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
	}

	if (HasAuthority() && AttributeSet)
	{
		InitializeDefaultAttributes();
	}

	SynchronizeSplitShipMeshes();

	// The legacy Water plugin component may have enabled root overlap during its
	// initialization. It is no longer a settings or force source, so disable it
	// before applying the final split collision policy.
	if (UBuoyancyComponent* LegacyBuoyancy = FindComponentByClass<UBuoyancyComponent>())
	{
		LegacyBuoyancy->SetAutoActivate(false);
		LegacyBuoyancy->SetComponentTickEnabled(false);
		LegacyBuoyancy->Deactivate();
	}

	ConfigureSplitShipCollision();
	if (BuoyancyRoot)
	{
		BuoyancyRoot->SetSimulatePhysics(true);
	}

	bool bPredictionEnabled = UPhysicsSettings::Get()->PhysicsPrediction.bEnablePhysicsPrediction;
	/* Network Physics initialization diagnostic log disabled after validation.
	UE_LOG(LogTemp, Warning, TEXT("[GT] AShip::BeginPlay - PhysicsPrediction Enabled Flag: %s | NetworkPhysicsComponent: %s"), 
		bPredictionEnabled ? TEXT("True") : TEXT("False"), 
		NetworkPhysicsComponent ? TEXT("Valid") : TEXT("Null"));
	*/

	if (bPredictionEnabled && NetworkPhysicsComponent)
	{
		if (UWorld* World = GetWorld())
		{
			if (FPhysScene* PhysScene = World->GetPhysicsScene())
			{
				if (Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver())
				{
					ShipPhysicsAsync = Solver->CreateAndRegisterSimCallbackObject_External<FShipPhysicsAsync>();
					if (ShipPhysicsAsync)
					{
						if (BuoyancyRoot)
						{
							ShipPhysicsAsync->SetPhysicsObject(BuoyancyRoot->GetPhysicsObjectByName(NAME_None));
						}
						NetworkPhysicsComponent->CreateDataHistory(ShipPhysicsAsync);
						NetworkPhysicsComponent->SetCompareStateToTriggerRewind(true, true);
						/* Network Physics initialization diagnostic log disabled after validation.
						UE_LOG(LogTemp, Warning, TEXT("[GT] AShip::BeginPlay - SUCCESSFULLY registered ShipPhysicsAsync and bound to NetworkPhysicsComponent! (Simulated Proxy Rollback Enabled)"));
						*/
					}
					else
					{
						/* Network Physics initialization diagnostic log disabled after validation.
						UE_LOG(LogTemp, Error, TEXT("[GT] AShip::BeginPlay - FAILED to create/register SimCallbackObject FShipPhysicsAsync!"));
						*/
					}
				}
			}
		}
	}
	else
	{
		/* Network Physics initialization diagnostic log disabled after validation.
		UE_LOG(LogTemp, Error, TEXT("[GT] AShip::BeginPlay - CRITICAL: Skipping Network Physics registration! (Prediction flag disabled or Component null)"));
		*/
	}

	ReplicatedState.Location = GetActorLocation();
	ReplicatedState.Rotation = GetActorRotation();

	// 좌현 바다 승선 상호작용 바인딩
	if (PortSeaBoardingInteractable)
	{
		PortSeaBoardingInteractable->InitializeInteractable(
			FText::FromString(TEXT("배")),
			FText::FromString(TEXT("승선하기"))
		);
		PortSeaBoardingInteractable->OnInteracted.AddUniqueDynamic(this, &AShip::HandlePortSeaBoarding);
	}

	// 우현 바다 승선 상호작용 바인딩
	if (StarboardSeaBoardingInteractable)
	{
		StarboardSeaBoardingInteractable->InitializeInteractable(
			FText::FromString(TEXT("배")),
			FText::FromString(TEXT("승선하기"))
		);
		StarboardSeaBoardingInteractable->OnInteracted.AddUniqueDynamic(this, &AShip::HandleStarboardSeaBoarding);
	}
}

void AShip::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SynchronizeSplitShipMeshes();
	ConfigureSplitShipCollision();
}

void AShip::SynchronizeSplitShipMeshes()
{
	if (!bMirrorPhysicsRootMeshToSplitMeshes || !BuoyancyRoot)
	{
		return;
	}

	UStaticMesh* SourceMesh = BuoyancyRoot->GetStaticMesh();
	if (!SourceMesh)
	{
		return;
	}

	const auto MirrorMeshAndMaterials = [this, SourceMesh](UStaticMeshComponent* Target)
	{
		if (!Target)
		{
			return;
		}

		Target->SetStaticMesh(SourceMesh);
		for (int32 MaterialIndex = 0; MaterialIndex < BuoyancyRoot->GetNumMaterials(); ++MaterialIndex)
		{
			Target->SetMaterial(MaterialIndex, BuoyancyRoot->GetMaterial(MaterialIndex));
		}
	};

	MirrorMeshAndMaterials(ShipVisualMesh);
	MirrorMeshAndMaterials(ShipDamageMesh);
	MirrorMeshAndMaterials(ShipDeckMesh);

	// Only the visual copy renders. The original component remains the serialized
	// Chaos body and supplies collision geometry, mass, and inertia.
	BuoyancyRoot->SetVisibility(false, false);
	BuoyancyRoot->SetHiddenInGame(true, false);
	if (ShipVisualMesh)
	{
		ShipVisualMesh->SetVisibility(true, false);
		ShipVisualMesh->SetHiddenInGame(false, false);
	}
}

void AShip::ConfigureSplitShipCollision()
{
	const bool bEnemyShip = ActorHasTag(TEXT("Enemy"));
	if (BuoyancyRoot)
	{
		BuoyancyRoot->SetCollisionProfileName(bEnemyShip ? TEXT("EnemyShip") : TEXT("PlayerShip"));
		BuoyancyRoot->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		BuoyancyRoot->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
		BuoyancyRoot->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Ignore);
		BuoyancyRoot->SetGenerateOverlapEvents(false);
	}

	if (ShipVisualMesh)
	{
		ShipVisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ShipVisualMesh->SetGenerateOverlapEvents(false);
	}

	if (ShipDamageMesh)
	{
		ShipDamageMesh->SetCollisionObjectType(ECC_ShipDamage);
		ShipDamageMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		ShipDamageMesh->SetGenerateOverlapEvents(false);
		// Damage is server authoritative. The hull never generates persistent
		// overlap pairs; the opposing projectile sweeps against this query body.
		if (HasAuthority())
		{
			ShipDamageMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			ShipDamageMesh->SetCollisionResponseToChannel(
				bEnemyShip ? ECC_GameTraceChannel2 : ECC_GameTraceChannel3,
				ECR_Block);
		}
		else
		{
			ShipDamageMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	if (ShipDeckMesh)
	{
		ShipDeckMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		ShipDeckMesh->SetCollisionObjectType(ECC_WorldDynamic);
		ShipDeckMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		ShipDeckMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		ShipDeckMesh->SetGenerateOverlapEvents(false);
	}
}

void AShip::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		CancelBombardmentAbilityAuthoritative();
	}
	EndLocalBombardmentTargeting();

	if (ShipPhysicsAsync)
	{
		if (UWorld* World = GetWorld())
		{
			if (FPhysScene* PhysScene = World->GetPhysicsScene())
			{
				if (Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver())
				{
					Solver->UnregisterAndFreeSimCallbackObject_External(ShipPhysicsAsync);
				}
			}
		}
		ShipPhysicsAsync = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

// Called every frame
void AShip::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (IsLocallyControlled() && bBombardmentTargeting)
	{
		UpdateLocalBombardmentPreview();
	}

	if (bBuoyancyQueryDiagnostics && ShipPhysicsAsync)
	{
		bool bHasLatestSample = false;
		bool bLatestWasResimming = false;
		FVector LatestPosition = FVector::ZeroVector;
		double LatestServerTime = 0.0;
		float LatestPTHeight = 0.0f;
		while (auto Output = ShipPhysicsAsync->PopOutputData_External())
		{
			if (Output->bWaveSampleValid)
			{
				bHasLatestSample = true;
				bLatestWasResimming = Output->bWasResimming;
				LatestPosition = Output->WaveSamplePosition;
				LatestServerTime = Output->WaveSampleServerTime;
				LatestPTHeight = Output->PTWaveHeight;
			}
		}

		if (bHasLatestSample && LatestServerTime >= NextBuoyancyQueryDiagnosticTime)
		{
			NextBuoyancyQueryDiagnosticTime = LatestServerTime + 1.0;
			float GTHeight = 0.0f;
			if (EvaluateGameThreadWaveOffset(GetWorld(), LatestPosition, LatestServerTime, GTHeight))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[BUOYANCY-GTPT] Role=%s Resim=%s Position=%s ServerTime=%.6f GT=%.6f PT=%.6f AbsDiff=%.9f"),
					HasAuthority() ? TEXT("Authority") : TEXT("Client"),
					bLatestWasResimming ? TEXT("true") : TEXT("false"),
					*LatestPosition.ToString(),
					LatestServerTime,
					GTHeight,
					LatestPTHeight,
					FMath::Abs(GTHeight - LatestPTHeight));
			}
		}
	}

	// Establish one authoritative, immutable mapping between the Network Physics
	// server-frame timeline and server world time. Replication makes the same
	// origin available to late-joining clients.
	if (HasAuthority() && ServerPhysicsTimeOrigin < 0.0)
	{
		if (UWorld* World = GetWorld())
		{
			if (FPhysScene* PhysScene = World->GetPhysicsScene())
			{
				if (Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver())
				{
					const int32 UpcomingServerFrame = UE::NetworkPhysicsUtils::GetUpcomingServerFrame_External(World);
					const float SolverStepSeconds = Solver->GetAsyncDeltaTime();
					if (UpcomingServerFrame != INDEX_NONE && SolverStepSeconds > UE_SMALL_NUMBER)
					{
						const double ServerWorldTime = World->GetGameState()
							? World->GetGameState()->GetServerWorldTimeSeconds()
							: World->GetTimeSeconds();
						ServerPhysicsStepSeconds = SolverStepSeconds;
						ServerPhysicsTimeOrigin = ServerWorldTime
							- static_cast<double>(UpcomingServerFrame) * static_cast<double>(SolverStepSeconds);
						ForceNetUpdate();

						/* Network Physics clock diagnostic log disabled after validation.
						UE_LOG(LogTemp, Warning, TEXT("[NETPHYS-CLOCK] Authority initialized Origin=%.9f Step=%.9f UpcomingServerFrame=%d ServerWorldTime=%.9f"),
							ServerPhysicsTimeOrigin,
							ServerPhysicsStepSeconds,
							UpcomingServerFrame,
							ServerWorldTime);
						*/
					}
				}
			}
		}
	}

#if !UE_SERVER
	if (!IsRunningDedicatedServer())
	{
		if (SWBuoyancyComponent && CVarShowShipNetworkBuoyancyDebug.GetValueOnGameThread() > 0)
		{
			FVector ShipLocation = GetActorLocation();
			FRotator ShipRotation = GetActorRotation();

			// 1. 클라이언트 로컬 물리 위치 기준 폰툰 (연두색 - Green)
			for (const FSWBuoyancyPontoon& Pontoon : SWBuoyancyComponent->GetPontoons())
			{
				FVector PontoonLocalWorldPos = ShipLocation + ShipRotation.RotateVector(Pontoon.RelativeLocation);
				DrawDebugSphere(GetWorld(), PontoonLocalWorldPos, Pontoon.Radius, 8, FColor::Green, false, 0.0f, 0, 1.5f);
			}

			// 2. 서버 공인 복제 위치 기준 폰툰 (빨간색 - Red)
			if (!HasAuthority())
			{
				FVector RepLocation = ReplicatedState.Location;
				FRotator RepRotation = ReplicatedState.Rotation;
				for (const FSWBuoyancyPontoon& Pontoon : SWBuoyancyComponent->GetPontoons())
				{
					FVector PontoonRepWorldPos = RepLocation + RepRotation.RotateVector(Pontoon.RelativeLocation);
					// 로컬 물리 구체와 구분되도록 크기를 살짝 줄여 드로우
					DrawDebugSphere(GetWorld(), PontoonRepWorldPos, Pontoon.Radius * 0.9f, 8, FColor::Red, false, 0.0f, 0, 1.5f);
				}
			}
		}
	}
#endif

	if (ShipPhysicsAsync)
	{
		// 1. 조작 입력 데이터 마샬링 (Autonomous Proxy 및 Local Controller 전용)
		CurrentExternalAcceleration = FVector::ZeroVector;
		for (const TPair<FGuid, FVector>& SourcePair : ExternalAccelerationSources)
		{
			if (!SourcePair.Value.ContainsNaN())
			{
				CurrentExternalAcceleration += SourcePair.Value;
			}
		}
		CurrentExternalAcceleration.Z = 0.0f;
		CurrentExternalAcceleration = CurrentExternalAcceleration.GetClampedToMaxSize(FMath::Max(0.0f, MaxExternalAcceleration));

		if (IsLocallyControlled())
		{
			if (FAsyncInputShip* AsyncInput = ShipPhysicsAsync->GetProducerInputData_External())
			{
				AsyncInput->MovementInput = CurrentMoveInput;
				AsyncInput->SteeringInput = CurrentTurnInput;
				AsyncInput->bHasLocalController = true; // 로컬 컨트롤러 조종 여부 릴레이
			}
		}

		// 2. 물리 틱용 기초 구조 정보 및 파도 파라미터 마샬링 (모든 권한/제어 상태에서 강제 전송 및 물리 캐시 다이렉트 주입)
		{
			// 로컬에서 임시 파도/폰툰 취합
			// This mapping becomes valid only after PlayerController's Network Physics
			// timestamp handshake. Before then, GetUpcomingServerFrame_External still
			// produces a local-frame placeholder and cannot be used as readiness proof.
			bool bNetworkPhysicsTickOffsetAssigned = HasAuthority();
			int32 NetworkPhysicsTickOffset = 0;
			if (!HasAuthority())
			{
				if (const UWorld* World = GetWorld())
				{
					if (const APlayerController* PlayerController = World->GetFirstPlayerController())
					{
						// A simulated-proxy Ship has no controller of its own, so
						// UNetworkPhysicsComponent::IsNetworkPhysicsTickOffsetAssigned()
						// always returns false for it. UE's async Network Physics path
						// also reads the world's first PlayerController for sim proxies.
						if (PlayerController->GetNetworkPhysicsTickOffsetAssigned())
						{
							NetworkPhysicsTickOffset = PlayerController->GetNetworkPhysicsTickOffset();
							bNetworkPhysicsTickOffsetAssigned = true;
						}
					}
				}
			}

			TArray<FVector> TempPontoons;
			TArray<float> TempPontoonRadii;
			TArray<float> TempPontoonForceScales;
			if (SWBuoyancyComponent)
			{
				for (const FSWBuoyancyPontoon& Pontoon : SWBuoyancyComponent->GetPontoons())
				{
					TempPontoons.Add(Pontoon.RelativeLocation);
					TempPontoonRadii.Add(Pontoon.Radius);
					TempPontoonForceScales.Add(Pontoon.ForceScale);
				}
			}

			TArray<FGerstnerWave> TempWaves;
			for (TActorIterator<AWaterBody> It(GetWorld()); It; ++It)
			{
				if (AWaterBody* WaterBody = *It)
				{
					UWaterWavesBase* WaterWaves = WaterBody->GetWaterWaves();
					if (WaterWaves)
					{
						if (WaterWaves->GetClass()->GetName() == TEXT("SWRippleWaterWaves"))
						{
							if (FProperty* Prop = WaterWaves->GetClass()->FindPropertyByName(TEXT("BaseWavesAsset")))
							{
								if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
								{
									UObject* AssetObj = ObjProp->GetObjectPropertyValue_InContainer(WaterWaves);
									if (AssetObj)
									{
										if (UWaterWavesAsset* WavesAsset = Cast<UWaterWavesAsset>(AssetObj))
										{
											if (UWaterWaves* InnerWaves = WavesAsset->GetWaterWaves())
											{
												if (UGerstnerWaterWaves* GerstnerAsset = Cast<UGerstnerWaterWaves>(InnerWaves))
												{
													TempWaves = GerstnerAsset->GetGerstnerWaves();
													break;
												}
											}
										}
									}
								}
							}
						}
						else if (UGerstnerWaterWaves* GerstnerAsset = Cast<UGerstnerWaterWaves>(WaterWaves))
						{
							TempWaves = GerstnerAsset->GetGerstnerWaves();
							break;
						}
					}
				}
			}

			float Gravity = GetWorld()->GetGravityZ();
			float LateralDrag = LateralDragCoefficient;
			float ForwardForceValue = ForwardForce;
			float TurnTorqueValue = TurnTorque;
			float ForwardPropulsionMultiplier = AttributeSet ? AttributeSet->GetForwardPropulsionMultiplier() : 1.0f;
			float TurnTorqueMultiplier = AttributeSet ? AttributeSet->GetTurnTorqueMultiplier() : 1.0f;
			float BuoyancyRadius = 150.f;
			float BuoyancyForceMultiplier = 1.3f;
			float WaterDamping = 3.0f;
			float WaterDamping2 = 0.1f;
			float MaxBuoyantForce = 5000000.0f;

			if (SWBuoyancyComponent)
			{
				const TArray<FSWBuoyancyPontoon>& Pontoons = SWBuoyancyComponent->GetPontoons();
				const FSWBuoyancyForceSettings& Settings = SWBuoyancyComponent->GetForceSettings();
				if (Pontoons.Num() > 0)
				{
					BuoyancyRadius = Pontoons[0].Radius;
				}
				BuoyancyForceMultiplier = Settings.BuoyancyCoefficient;
				WaterDamping = Settings.BuoyancyDamp;
				WaterDamping2 = Settings.BuoyancyDamp2;
				MaxBuoyantForce = Settings.MaxBuoyantForce;
			}

			TArray<FSWRippleEvent> TempRippleEvents;
			if (USWRippleStateSubsystem* RippleState = GetWorld()->GetSubsystem<USWRippleStateSubsystem>())
			{
				RippleState->GetEventsSnapshot(TempRippleEvents);
			}

			// A. 비동기 인풋 버퍼(GetProducerInputData_External)가 유효하다면 인풋 히스토리에 적재
			if (FAsyncInputShip* AsyncInput = ShipPhysicsAsync->GetProducerInputData_External())
			{
				AsyncInput->ExternalAcceleration = CurrentExternalAcceleration;
				AsyncInput->bApplyAuthoritativeExternalAcceleration = HasAuthority();
				AsyncInput->bQueryDiagnostics = bBuoyancyQueryDiagnostics;
				AsyncInput->PontoonOffsets = TempPontoons;
				AsyncInput->PontoonRadii = TempPontoonRadii;
				AsyncInput->PontoonForceScales = TempPontoonForceScales;
				AsyncInput->GerstnerWaves = TempWaves;
				AsyncInput->RippleEvents = MoveTemp(TempRippleEvents);
				AsyncInput->GravityZ = Gravity;
				AsyncInput->LateralDrag = LateralDrag;
				AsyncInput->ForwardForceValue = ForwardForceValue;
				AsyncInput->TurnTorqueValue = TurnTorqueValue;
				AsyncInput->ForwardPropulsionMultiplier = ForwardPropulsionMultiplier;
				AsyncInput->TurnTorqueMultiplier = TurnTorqueMultiplier;
				AsyncInput->BuoyancyRadius = BuoyancyRadius;
				AsyncInput->BuoyancyForceMultiplier = BuoyancyForceMultiplier;
				AsyncInput->WaterDamping = WaterDamping;
				AsyncInput->WaterDamping2 = WaterDamping2;
				AsyncInput->MaxBuoyantForce = MaxBuoyantForce;
				// Keep the custom payload aligned with the project's 5 cm Network
				// Physics threshold; 30 cm is visibly separated at pontoon scale.
				AsyncInput->ResimLocationThreshold = FMath::Clamp(ResimLocationThreshold, 0.1f, 5.0f);
				AsyncInput->ResimRotationThreshold = ResimRotationThreshold;
				AsyncInput->ServerPhysicsTimeOrigin = ServerPhysicsTimeOrigin;
				AsyncInput->ServerPhysicsStepSeconds = ServerPhysicsStepSeconds;
				AsyncInput->NetworkPhysicsTickOffset = NetworkPhysicsTickOffset;
				AsyncInput->bNetworkPhysicsTickOffsetAssigned = bNetworkPhysicsTickOffsetAssigned;
			}
		}
	}

	if (HasAuthority())
	{
		ReplicatedState.Location = GetActorLocation();
		ReplicatedState.Rotation = GetActorRotation();
	}
	else
	{
		// 매 틱 SimProxy 배에 대해 CompareState 플래그를 강제로 세팅 (BeginPlay 1회로 부족할 수 있음)
		if (NetworkPhysicsComponent && GetLocalRole() == ROLE_SimulatedProxy)
		{
			NetworkPhysicsComponent->SetCompareStateToTriggerRewind(true, true);
		}

		/* Network Physics client synchronization diagnostic logs disabled after validation.
		// 클라이언트 전용 GT 위치 오차 실측 디버그 로그 (1초 주기 호출)
		if (UWorld* World = GetWorld())
		{
			static float LogTimer = 0.0f;
			LogTimer += DeltaTime;
			if (LogTimer >= 1.0f)
			{
				LogTimer = 0.0f;
				float Dist = FVector::Dist(GetActorLocation(), ReplicatedState.Location);
				UE_LOG(LogTemp, Warning, TEXT("[SHIP-SYNC] LocDiff: %.2f cm | ActorLoc: %s | RepLoc: %s"), 
					Dist, *GetActorLocation().ToString(), *ReplicatedState.Location.ToString());

				if (BuoyancyRoot)
				{
					if (FBodyInstance* BI = BuoyancyRoot->GetBodyInstance())
					{
						FTransform GS_PhysTransform = BI->GetUnrealWorldTransform();
						UE_LOG(LogTemp, Warning, TEXT("[GS-BODY-TRANS] BodyZ: %.3f | ActorZ: %.3f | RepZ: %.3f"),
							GS_PhysTransform.GetLocation().Z, GetActorLocation().Z, ReplicatedState.Location.Z);
					}
				}

				// 소유권 및 네트워크 역할 실시간 실측 로그 추가
				AActor* ShipOwner = GetOwner();
				ENetRole LocalRole = GetLocalRole();
				ENetRole MyRemoteRole = GetRemoteRole();
				FString LocalRoleStr = UEnum::GetValueAsString(LocalRole);
				FString RemoteRoleStr = UEnum::GetValueAsString(MyRemoteRole);

				UE_LOG(LogTemp, Warning, TEXT("[GS-OWNER-DIAG] Owner: %s | LocalRole: %s | RemoteRole: %s | ReplicateMovement: %s"),
					ShipOwner ? *ShipOwner->GetName() : TEXT("None"),
					*LocalRoleStr,
					*RemoteRoleStr,
					IsReplicatingMovement() ? TEXT("TRUE") : TEXT("FALSE"));

				// CVar 값 직접 조회 로그 — ini 세팅이 실제로 적용되었는지 검증
				IConsoleVariable* CVarCompare = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.Resim.CompareStateToTriggerRewind"));
				IConsoleVariable* CVarSimProxy = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.Resim.CompareStateToTriggerRewind.IncludeSimProxies"));
				UE_LOG(LogTemp, Warning, TEXT("[GS-CVAR-DIAG] CompareState CVar: %s | IncludeSimProxies CVar: %s"),
					CVarCompare ? (CVarCompare->GetBool() ? TEXT("TRUE") : TEXT("FALSE")) : TEXT("NOT FOUND"),
					CVarSimProxy ? (CVarSimProxy->GetBool() ? TEXT("TRUE") : TEXT("FALSE")) : TEXT("NOT FOUND"));
			}
		}
		*/
	}
}

// Called to bind functionality to input
void AShip::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	CachedPlayerController = Cast<APlayerController>(GetController());
	CurrentMoveInput = 0.0f;
	CurrentTurnInput = 0.0f;

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Ship movement (W/S)
		if (ShipMoveAction)
		{
			EnhancedInput->BindAction(ShipMoveAction, ETriggerEvent::Triggered, this, &AShip::ShipMove);
			EnhancedInput->BindAction(ShipMoveAction, ETriggerEvent::Completed, this, &AShip::StopShipMove);
			EnhancedInput->BindAction(ShipMoveAction, ETriggerEvent::Canceled, this, &AShip::StopShipMove);
		}

		// Ship turning (A/D)
		if (ShipTurnAction)
		{
			EnhancedInput->BindAction(ShipTurnAction, ETriggerEvent::Triggered, this, &AShip::ShipTurn);
			EnhancedInput->BindAction(ShipTurnAction, ETriggerEvent::Completed, this, &AShip::StopShipTurn);
			EnhancedInput->BindAction(ShipTurnAction, ETriggerEvent::Canceled, this, &AShip::StopShipTurn);
		}

		// Ship camera look (Mouse)
		if (ShipLookAction)
		{
			EnhancedInput->BindAction(ShipLookAction, ETriggerEvent::Triggered, this, &AShip::ShipLook);
		}

		// Toggle fixed camera (C key)
		if (ShipToggleCameraAction)
		{
			EnhancedInput->BindAction(ShipToggleCameraAction, ETriggerEvent::Started, this, &AShip::ToggleFixedCamera);
		}

		// Disembark (F key)
		if (ShipDisembarkAction)
		{
			EnhancedInput->BindAction(ShipDisembarkAction, ETriggerEvent::Started, this, &AShip::OnDisembarkAction);
		}

		if (ShipZoomAction)
		{
			EnhancedInput->BindAction(ShipZoomAction, ETriggerEvent::Triggered, this, &AShip::ShipZoom);
		}
		if (ShipBombardmentConfirmAction)
		{
			EnhancedInput->BindAction(ShipBombardmentConfirmAction, ETriggerEvent::Started, this, &AShip::HandleBombardmentConfirm);
		}
		if (ShipBombardmentCancelAction)
		{
			EnhancedInput->BindAction(ShipBombardmentCancelAction, ETriggerEvent::Started, this, &AShip::HandleBombardmentCancel);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("AShip::SetupPlayerInputComponent - Failed to find Enhanced Input Component."));
	}

	if (BombardmentSkillHotkey.IsValid())
	{
		PlayerInputComponent->BindKey(
			BombardmentSkillHotkey, IE_Pressed, this, &AShip::HandleBombardmentToggle);
	}

	RestoreRememberedFollowCameraState(CachedPlayerController);
}

void AShip::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	CurrentMoveInput = 0.0f;
	CurrentTurnInput = 0.0f;

	if (NetworkPhysicsComponent)
	{
		// 서버 측에서는 로컬 입력 릴레이를 해제 (RPC 수신 및 로컬 예측은 클라이언트 책임)
		NetworkPhysicsComponent->SetIsRelayingLocalInputs(false);
	}

	if (const APlayerController* PlayerController = Cast<APlayerController>(NewController))
	{
		APlayerState* InPlayerState = PlayerController->PlayerState;
		const bool bFirstApplicationForPlayer = AppliedUpgradePlayerState != InPlayerState;
		if (ApplyPlayerUpgrades(InPlayerState, bFirstApplicationForPlayer))
		{
			AppliedUpgradePlayerState = InPlayerState;
		}
	}
}

void AShip::UnPossessed()
{
	ResetToFollowCamera();
	RememberFollowCameraState(Cast<APlayerController>(GetController()));

	if (HasAuthority())
	{
		CancelBombardmentAbilityAuthoritative();
	}
	EndLocalBombardmentTargeting();
	CurrentMoveInput = 0.0f;
	CurrentTurnInput = 0.0f;

	if (NetworkPhysicsComponent)
	{
		NetworkPhysicsComponent->SetIsRelayingLocalInputs(false);
	}

	Super::UnPossessed();
}

void AShip::SetAIControlInput(float MoveInput, float TurnInput)
{
	if (!HasAuthority())
	{
		return;
	}

	if (IsPropulsionSuppressed())
	{
		CurrentMoveInput = 0.0f;
		CurrentTurnInput = 0.0f;
		return;
	}

	CurrentMoveInput = FMath::Clamp(MoveInput, -1.0f, 1.0f);
	CurrentTurnInput = FMath::Clamp(TurnInput, -1.0f, 1.0f);
}

void AShip::SetExternalAccelerationSource(const FGuid& SourceId, const FVector& WorldAcceleration)
{
	if (!HasAuthority() || !SourceId.IsValid())
	{
		return;
	}

	FVector SafeAcceleration = WorldAcceleration.ContainsNaN() ? FVector::ZeroVector : WorldAcceleration;
	SafeAcceleration.Z = 0.0f;
	ExternalAccelerationSources.FindOrAdd(SourceId) =
		SafeAcceleration.GetClampedToMaxSize(FMath::Max(0.0f, MaxExternalAcceleration));
}

void AShip::RemoveExternalAccelerationSource(const FGuid& SourceId)
{
	if (HasAuthority())
	{
		ExternalAccelerationSources.Remove(SourceId);
	}
}

void AShip::AddPropulsionSuppression(const FGuid& SourceId)
{
	if (!HasAuthority() || !SourceId.IsValid())
	{
		return;
	}

	PropulsionSuppressionSources.Add(SourceId);
	CurrentMoveInput = 0.0f;
	CurrentTurnInput = 0.0f;
}

void AShip::RemovePropulsionSuppression(const FGuid& SourceId)
{
	if (HasAuthority())
	{
		PropulsionSuppressionSources.Remove(SourceId);
	}
}


void AShip::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShip, RidingPlayer);
	DOREPLIFETIME(AShip, bBombardmentTargeting);
	DOREPLIFETIME(AShip, ActiveBombardmentClass);
	DOREPLIFETIME(AShip, ReplicatedState);
	DOREPLIFETIME(AShip, ServerPhysicsTimeOrigin);
	DOREPLIFETIME(AShip, ServerPhysicsStepSeconds);
}

void AShip::Board(APawn* PlayerPawn)
{
	UE_LOG(LogTemp, Log, TEXT("AShip::Board - [SERVER] Entered. PlayerPawn: %s, HasAuthority: %s, RidingPlayer: %s"),
		PlayerPawn ? *PlayerPawn->GetName() : TEXT("None"),
		HasAuthority() ? TEXT("YES") : TEXT("NO"),
		RidingPlayer ? *RidingPlayer->GetName() : TEXT("None"));

	if (!HasAuthority()) return;
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("AShip::Board - [SERVER] Failed: PlayerPawn is null!"));
		return;
	}
	if (RidingPlayer)
	{
		UE_LOG(LogTemp, Warning, TEXT("AShip::Board - [SERVER] Failed: Ship is already being ridden by %s!"), *RidingPlayer->GetName());
		return;
	}

	APlayerController* PC = Cast<APlayerController>(PlayerPawn->GetController());
	if (!PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("AShip::Board - [SERVER] Failed: PlayerPawn has no PlayerController! Pawn: %s, Controller: %s"),
			*PlayerPawn->GetName(),
			PlayerPawn->GetController() ? *PlayerPawn->GetController()->GetName() : TEXT("None"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("AShip: [SERVER] Board initiated by player pawn %s. Ship location: %s, Player location: %s"), *PlayerPawn->GetName(), *GetActorLocation().ToString(), *PlayerPawn->GetActorLocation().ToString());

	RidingPlayer = PlayerPawn;

	// Disable player collision
	RidingPlayer->SetActorEnableCollision(false);
	RidingPlayer->SetActorHiddenInGame(false);

	if (ACharacter* Char = Cast<ACharacter>(RidingPlayer))
	{
		Char->GetCharacterMovement()->DisableMovement();
		Char->GetCharacterMovement()->StopMovementImmediately();
	}

	// Disable movement replication while on the ship to prevent jittering
	UE_LOG(LogTemp, Log, TEXT("AShip: [SERVER] Board - Player bReplicateMovement before disable: %s"), RidingPlayer->IsReplicatingMovement() ? TEXT("True") : TEXT("False"));
	RidingPlayer->SetReplicateMovement(false);
	UE_LOG(LogTemp, Log, TEXT("AShip: [SERVER] Board - Player bReplicateMovement after disable: %s"), RidingPlayer->IsReplicatingMovement() ? TEXT("True") : TEXT("False"));

	// Attach to buoyancy root directly without welding physics bodies to avoid physics conflicts
	FAttachmentTransformRules AttachmentRules(EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, false);
	RidingPlayer->AttachToComponent(BuoyancyRoot, AttachmentRules);
	UE_LOG(LogTemp, Log, TEXT("AShip: [SERVER] Board - Player attached to BuoyancyRoot. Relative location: %s, relative rotation: %s"), 
		*RidingPlayer->GetRootComponent()->GetRelativeLocation().ToString(), 
		*RidingPlayer->GetRootComponent()->GetRelativeRotation().ToString());

	// Possess ship pawn
	PC->Possess(this);
}

void AShip::OnDisembarkAction(const FInputActionValue& Value)
{
	ServerDisembark();
}

void AShip::ServerDisembark_Implementation()
{
	Disembark();
}

void AShip::Disembark()
{
	if (!HasAuthority()) return;
	if (!RidingPlayer) return;
	CancelBombardmentAbilityAuthoritative();

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	UE_LOG(LogTemp, Log, TEXT("AShip: [SERVER] Disembark initiated. Player pawn: %s"), *RidingPlayer->GetName());

	// Restore camera mode
	ResetToFollowCamera();
	RememberFollowCameraState(PC);

	// Detach player preserving their current world position on the ship
	RidingPlayer->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	UE_LOG(LogTemp, Log, TEXT("AShip: [SERVER] Disembark - Detached player. World location: %s"), *RidingPlayer->GetActorLocation().ToString());

	RidingPlayer->SetActorEnableCollision(true);
	RidingPlayer->SetActorHiddenInGame(false);

	if (ACharacter* Char = Cast<ACharacter>(RidingPlayer))
	{
		Char->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}

	// Restore movement replication on disembark
	RidingPlayer->SetReplicateMovement(true);
	UE_LOG(LogTemp, Log, TEXT("AShip: [SERVER] Disembark - Player bReplicateMovement after enable: %s"), RidingPlayer->IsReplicatingMovement() ? TEXT("True") : TEXT("False"));

	// Return possession to player character
	PC->Possess(RidingPlayer);

	RidingPlayer = nullptr;
}

void AShip::ShipMove(const FInputActionValue& Value)
{
	const float MoveValue = Value.Get<float>();
	CurrentMoveInput = MoveValue;

	if (!HasAuthority())
	{
		ServerMove(MoveValue);
	}
}

void AShip::ServerMove_Implementation(float MoveValue)
{
	CurrentMoveInput = MoveValue;
}

void AShip::StopShipMove(const FInputActionValue&)
{
	CurrentMoveInput = 0.0f;

	if (!HasAuthority())
	{
		ServerStopMove();
	}
}

void AShip::ServerStopMove_Implementation()
{
	CurrentMoveInput = 0.0f;
}

void AShip::ApplyForwardForce(float MoveValue)
{
	// 비동기 물리 스레드(FShipPhysicsAsync)에서 물리 힘이 연산되므로 빈 함수로 둡니다.
}

void AShip::ShipTurn(const FInputActionValue& Value)
{
	const float TurnValue = Value.Get<float>();
	CurrentTurnInput = TurnValue;

	if (!HasAuthority())
	{
		ServerTurn(TurnValue);
	}
}

void AShip::ServerTurn_Implementation(float TurnValue)
{
	CurrentTurnInput = TurnValue;
}

void AShip::StopShipTurn(const FInputActionValue&)
{
	CurrentTurnInput = 0.0f;

	if (!HasAuthority())
	{
		ServerStopTurn();
	}
}

void AShip::ServerStopTurn_Implementation()
{
	CurrentTurnInput = 0.0f;
}

void AShip::ApplyTurnTorque(float TurnValue)
{
	// 비동기 물리 스레드(FShipPhysicsAsync)에서 물리 힘이 연산되므로 빈 함수로 둡니다.
}

void AShip::ShipLook(const FInputActionValue& Value)
{
	if (bBombardmentTargeting)
	{
		return;
	}

	const FVector2D LookValue = Value.Get<FVector2D>();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// Rotate the controller (which drives the SpringArm via bUsePawnControlRotation)
		const float YawSign = bInvertShipLookYaw ? -1.0f : 1.0f;
		const float PitchSign = bInvertShipLookPitch ? -1.0f : 1.0f;
		PC->AddYawInput(LookValue.X * YawSign * ShipLookSensitivity);
		PC->AddPitchInput(LookValue.Y * PitchSign * ShipLookSensitivity);
	}
}

void AShip::ShipZoom(const FInputActionValue& Value)
{
	if (!CameraBoom || bUsingFixedCamera)
	{
		return;
	}

	const float WheelAxis = Value.Get<float>();
	if (FMath::IsNearlyZero(WheelAxis))
	{
		return;
	}

	const float MinArm = FMath::Min(MinShipZoomArmLength, MaxShipZoomArmLength);
	const float MaxArm = FMath::Max(MinShipZoomArmLength, MaxShipZoomArmLength);
	CameraBoom->TargetArmLength = FMath::Clamp(
		CameraBoom->TargetArmLength - WheelAxis * ShipZoomStep,
		MinArm,
		MaxArm);
	RememberFollowCameraState(Cast<APlayerController>(GetController()));
}

void AShip::HandleBombardmentToggle()
{
	if (!IsPlayerControlled())
	{
		return;
	}

	if (HasAuthority())
	{
		ToggleBombardmentAbilityAuthoritative();
	}
	else
	{
		ServerToggleBombardmentAbility();
	}
}

void AShip::HandleBombardmentConfirm()
{
	if (!IsLocallyControlled() || !bBombardmentTargeting || !bLocalBombardmentTargetValid)
	{
		return;
	}

	if (HasAuthority())
	{
		FVector ResolvedLocation;
		if (ValidateAndResolveBombardmentTarget(LocalBombardmentTarget, ResolvedLocation))
		{
			SpawnBombardmentAuthoritative(ResolvedLocation);
			CancelBombardmentAbilityAuthoritative();
		}
	}
	else
	{
		ServerConfirmBombardment(LocalBombardmentTarget);
	}
}

void AShip::HandleBombardmentCancel()
{
	if (!IsPlayerControlled() || !bBombardmentTargeting)
	{
		return;
	}

	if (HasAuthority())
	{
		CancelBombardmentAbilityAuthoritative();
	}
	else
	{
		ServerCancelBombardmentAbility();
	}
}

void AShip::ServerToggleBombardmentAbility_Implementation()
{
	if (RidingPlayer && IsPlayerControlled())
	{
		ToggleBombardmentAbilityAuthoritative();
	}
}

void AShip::ServerConfirmBombardment_Implementation(FVector ClientTargetLocation)
{
	if (!RidingPlayer || !IsPlayerControlled() || !bBombardmentTargeting)
	{
		return;
	}

	FVector ResolvedLocation;
	if (!ValidateAndResolveBombardmentTarget(ClientTargetLocation, ResolvedLocation))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Bombardment] Server rejected target %s for ship %s"),
			*ClientTargetLocation.ToString(), *GetName());
		return;
	}

	SpawnBombardmentAuthoritative(ResolvedLocation);
	CancelBombardmentAbilityAuthoritative();
}

void AShip::ServerCancelBombardmentAbility_Implementation()
{
	CancelBombardmentAbilityAuthoritative();
}

bool AShip::ActivateBombardmentModeFromAbility(
	UGameplayAbility* Ability,
	TSubclassOf<ABombardment> BombardmentClass)
{
	if (!HasAuthority() || !Ability || !BombardmentClass || !RidingPlayer || !IsPlayerControlled()
		|| bBombardmentTargeting)
	{
		return false;
	}

	ActiveBombardmentAbility = Ability;
	ActiveBombardmentClass = BombardmentClass;
	SetBombardmentTargetingAuthoritative(true);
	return true;
}

void AShip::DeactivateBombardmentModeFromAbility(UGameplayAbility* Ability)
{
	if (!HasAuthority() || (ActiveBombardmentAbility.IsValid() && ActiveBombardmentAbility.Get() != Ability))
	{
		return;
	}

	ActiveBombardmentAbility.Reset();
	ActiveBombardmentClass = nullptr;
	SetBombardmentTargetingAuthoritative(false);
}

void AShip::ToggleBombardmentAbilityAuthoritative()
{
	if (!HasAuthority() || !RidingPlayer || !IsPlayerControlled())
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetRidingPlayerAbilitySystem();
	if (!ASC)
	{
		return;
	}

	FGameplayTagContainer AbilityTags(GameplayAbility_Skill_Bombardment);
	if (bBombardmentTargeting || ASC->HasMatchingGameplayTag(GameplayAbility_Skill_Bombardment))
	{
		ASC->CancelAbilities(&AbilityTags);
		return;
	}

	if (!ASC->TryActivateAbilitiesByTag(AbilityTags, true))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Bombardment] GA activation failed. Grant a Bombardment GA to player=%s (tag=%s)."),
			*GetNameSafe(RidingPlayer), *GameplayAbility_Skill_Bombardment.GetTag().ToString());
	}
}

void AShip::CancelBombardmentAbilityAuthoritative()
{
	if (!HasAuthority())
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetRidingPlayerAbilitySystem())
	{
		FGameplayTagContainer AbilityTags(GameplayAbility_Skill_Bombardment);
		ASC->CancelAbilities(&AbilityTags);
	}

	if (bBombardmentTargeting)
	{
		ActiveBombardmentAbility.Reset();
		ActiveBombardmentClass = nullptr;
		SetBombardmentTargetingAuthoritative(false);
	}
}

void AShip::SetBombardmentTargetingAuthoritative(bool bEnabled)
{
	if (!HasAuthority())
	{
		return;
	}

	bBombardmentTargeting = bEnabled;
	ForceNetUpdate();
	RefreshLocalBombardmentTargeting();
}

UAbilitySystemComponent* AShip::GetRidingPlayerAbilitySystem() const
{
	const IAbilitySystemInterface* AbilitySystemInterface = Cast<IAbilitySystemInterface>(RidingPlayer);
	return AbilitySystemInterface ? AbilitySystemInterface->GetAbilitySystemComponent() : nullptr;
}

void AShip::OnRep_BombardmentTargeting()
{
	RefreshLocalBombardmentTargeting();
}

void AShip::RefreshLocalBombardmentTargeting()
{
	if (!IsLocallyControlled())
	{
		EndLocalBombardmentTargeting();
		return;
	}

	if (bBombardmentTargeting && ActiveBombardmentClass)
	{
		BeginLocalBombardmentTargeting();
	}
	else
	{
		EndLocalBombardmentTargeting();
	}
}

void AShip::BeginLocalBombardmentTargeting()
{
	if (bLocalBombardmentInputModeApplied || !ActiveBombardmentClass)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	const ABombardment* BombardmentDefaults = ActiveBombardmentClass->GetDefaultObject<ABombardment>();
	if (!PC || !PC->IsLocalController() || !BombardmentDefaults)
	{
		return;
	}

	bSavedShowMouseCursor = PC->bShowMouseCursor;
	PC->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);
	bLocalBombardmentInputModeApplied = true;

	if (BombardmentDefaults->PreviewClass && GetWorld())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		BombardmentPreviewActor = GetWorld()->SpawnActor<ABombardmentPreview>(
			BombardmentDefaults->PreviewClass,
			GetActorLocation(),
			FRotator::ZeroRotator,
			SpawnParams);
		if (BombardmentPreviewActor)
		{
			BombardmentPreviewActor->ConfigurePreview(BombardmentDefaults->SkillRadius);
		}
	}
}

void AShip::EndLocalBombardmentTargeting()
{
	if (BombardmentPreviewActor)
	{
		BombardmentPreviewActor->Destroy();
		BombardmentPreviewActor = nullptr;
	}

	if (bLocalBombardmentInputModeApplied)
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (!PC)
		{
			PC = CachedPlayerController;
		}
		if (PC)
		{
			PC->bShowMouseCursor = bSavedShowMouseCursor;
			FInputModeGameOnly InputMode;
			InputMode.SetConsumeCaptureMouseDown(false);
			PC->SetInputMode(InputMode);
		}
	}

	bLocalBombardmentInputModeApplied = false;
	bLocalBombardmentTargetValid = false;
}

void AShip::UpdateLocalBombardmentPreview()
{
	if (!bLocalBombardmentInputModeApplied)
	{
		BeginLocalBombardmentTargeting();
	}

	const ABombardment* BombardmentDefaults = ActiveBombardmentClass
		? ActiveBombardmentClass->GetDefaultObject<ABombardment>()
		: nullptr;
	FVector TargetLocation = LocalBombardmentTarget;
	bLocalBombardmentTargetValid = BombardmentDefaults
		&& ResolveBombardmentTargetFromCursor(TargetLocation);
	if (bLocalBombardmentTargetValid)
	{
		FVector RangeDelta = TargetLocation - GetActorLocation();
		RangeDelta.Z = 0.0f;
		bLocalBombardmentTargetValid =
			RangeDelta.SizeSquared() <= FMath::Square(FMath::Max(1.0f, BombardmentDefaults->MaxTargetRange));
		LocalBombardmentTarget = TargetLocation;
	}

	if (BombardmentPreviewActor && BombardmentDefaults)
	{
		if (!TargetLocation.ContainsNaN())
		{
			BombardmentPreviewActor->SetActorLocation(
				TargetLocation + FVector::UpVector * BombardmentDefaults->PreviewHeightOffset);
		}
		BombardmentPreviewActor->SetPreviewValid(bLocalBombardmentTargetValid);
	}
}

bool AShip::ResolveBombardmentTargetFromCursor(FVector& OutLocation) const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		return false;
	}

	FVector RayStart;
	FVector RayDirection;
	if (!PC->DeprojectMousePositionToWorld(RayStart, RayDirection))
	{
		return false;
	}
	RayDirection.Normalize();

	float ReferenceWaterZ = 0.0f;
	const bool bHasReferenceWater = FWaterSurfaceQueryLibrary::QueryWaterSurface(
		GetWorld(), GetActorLocation(), ReferenceWaterZ, false);
	float WaterRayDistance = TNumericLimits<float>::Max();
	FVector WaterIntersection = FVector::ZeroVector;
	if (bHasReferenceWater && FMath::Abs(RayDirection.Z) > UE_SMALL_NUMBER)
	{
		WaterRayDistance = (ReferenceWaterZ - RayStart.Z) / RayDirection.Z;
		if (WaterRayDistance > 0.0f)
		{
			WaterIntersection = RayStart + RayDirection * WaterRayDistance;
		}
		else
		{
			WaterRayDistance = TNumericLimits<float>::Max();
		}
	}

	FHitResult LandscapeHit;
	const FVector RayEnd = RayStart + RayDirection * 500000.0f;
	if (FindLandscapeHitAlongRay(RayStart, RayEnd, LandscapeHit)
		&& LandscapeHit.Distance <= WaterRayDistance + 1.0f)
	{
		float WaterAtLandscapeZ = 0.0f;
		const bool bWaterAtLandscape = FWaterSurfaceQueryLibrary::QueryWaterSurface(
			GetWorld(), LandscapeHit.ImpactPoint, WaterAtLandscapeZ, false);
		if (!bWaterAtLandscape || LandscapeHit.ImpactPoint.Z >= WaterAtLandscapeZ - 25.0f)
		{
			OutLocation = LandscapeHit.ImpactPoint;
			return true;
		}
	}

	if (WaterRayDistance == TNumericLimits<float>::Max())
	{
		return false;
	}

	float ExactWaterZ = 0.0f;
	if (!FWaterSurfaceQueryLibrary::QueryWaterSurface(GetWorld(), WaterIntersection, ExactWaterZ, false))
	{
		return false;
	}
	OutLocation = FVector(WaterIntersection.X, WaterIntersection.Y, ExactWaterZ);
	return true;
}

bool AShip::ResolveStableSurfaceAtXY(const FVector2D& XY, FVector& OutLocation) const
{
	const FVector QueryLocation(XY.X, XY.Y, GetActorLocation().Z);
	float WaterZ = 0.0f;
	const bool bHasWater = FWaterSurfaceQueryLibrary::QueryWaterSurface(
		GetWorld(), QueryLocation, WaterZ, false);

	FHitResult LandscapeHit;
	const FVector TraceStart(XY.X, XY.Y, 100000.0f);
	const FVector TraceEnd(XY.X, XY.Y, -100000.0f);
	if (FindLandscapeHitAlongRay(TraceStart, TraceEnd, LandscapeHit)
		&& (!bHasWater || LandscapeHit.ImpactPoint.Z >= WaterZ - 25.0f))
	{
		OutLocation = LandscapeHit.ImpactPoint;
		return true;
	}

	if (bHasWater)
	{
		OutLocation = FVector(XY.X, XY.Y, WaterZ);
		return true;
	}
	return false;
}

bool AShip::FindLandscapeHitAlongRay(const FVector& RayStart, const FVector& RayEnd, FHitResult& OutHit) const
{
	if (!GetWorld())
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BombardmentLandscapeTarget), true, this);
	Params.AddIgnoredActor(RidingPlayer);
	for (TActorIterator<AWaterBody> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}

	TArray<FHitResult> Hits;
	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	GetWorld()->LineTraceMultiByObjectType(Hits, RayStart, RayEnd, ObjectTypes, Params);
	for (const FHitResult& Hit : Hits)
	{
		if (Hit.GetActor() && Hit.GetActor()->IsA<ALandscapeProxy>())
		{
			OutHit = Hit;
			return true;
		}
	}
	return false;
}

bool AShip::ValidateAndResolveBombardmentTarget(const FVector& RequestedLocation, FVector& OutLocation) const
{
	if (!HasAuthority() || !bBombardmentTargeting || !ActiveBombardmentClass || RequestedLocation.ContainsNaN())
	{
		return false;
	}

	const ABombardment* BombardmentDefaults = ActiveBombardmentClass->GetDefaultObject<ABombardment>();
	if (!BombardmentDefaults || !ResolveStableSurfaceAtXY(FVector2D(RequestedLocation), OutLocation))
	{
		return false;
	}

	FVector RangeDelta = OutLocation - GetActorLocation();
	RangeDelta.Z = 0.0f;
	return RangeDelta.SizeSquared()
		<= FMath::Square(FMath::Max(1.0f, BombardmentDefaults->MaxTargetRange));
}

TSubclassOf<AActor> AShip::ResolveNormalCannonballClass() const
{
	if (!GetWorld())
	{
		return nullptr;
	}

	for (TActorIterator<ACannon> It(GetWorld()); It; ++It)
	{
		ACannon* Cannon = *It;
		if (IsValid(Cannon) && Cannon->GetOwningShip() == this)
		{
			TSubclassOf<AActor> ProjectileClass = Cannon->GetCannonballClass();
			if (ProjectileClass && ProjectileClass->IsChildOf(ACannonball::StaticClass()))
			{
				return ProjectileClass;
			}
		}
	}
	return nullptr;
}

void AShip::SpawnBombardmentAuthoritative(const FVector& TargetLocation)
{
	if (!HasAuthority() || !ActiveBombardmentClass || !GetWorld())
	{
		return;
	}

	const ABombardment* BombardmentDefaults = ActiveBombardmentClass->GetDefaultObject<ABombardment>();
	TSubclassOf<AActor> ProjectileClass = BombardmentDefaults
		? BombardmentDefaults->ProjectileClassOverride
		: nullptr;
	if (!ProjectileClass)
	{
		ProjectileClass = ResolveNormalCannonballClass();
	}
	if (!ProjectileClass || !ProjectileClass->IsChildOf(ACannonball::StaticClass()))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Bombardment] No normal cannonball class was found on Player ship %s. Set ProjectileClassOverride."),
			*GetName());
		return;
	}

	ISkillUseProvider* SkillProvider = Cast<ISkillUseProvider>(RidingPlayer);
	if (!SkillProvider || !SkillProvider->TryConsumeSkillUse(GameplayAbility_Skill_Bombardment))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Bombardment] Execution rejected because the skill is locked or has no usage material. Player=%s"),
			*GetNameSafe(RidingPlayer));
		return;
	}

	float Damage = 10.0f;
	float Speed = 3000.0f;
	if (AbilitySystemComponent)
	{
		Damage = AbilitySystemComponent->GetNumericAttribute(UShipAttributeSet::GetCannonDamageAttribute());
		Speed = AbilitySystemComponent->GetNumericAttribute(UShipAttributeSet::GetCannonballSpeedAttribute());
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = RidingPlayer;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ABombardment* Bombardment = GetWorld()->SpawnActor<ABombardment>(
		ActiveBombardmentClass,
		TargetLocation,
		FRotator::ZeroRotator,
		SpawnParams);
	if (Bombardment)
	{
		Bombardment->InitializeBombardment(
			this, RidingPlayer, TargetLocation, ProjectileClass, Damage, Speed);
		UE_LOG(LogTemp, Log,
			TEXT("[Bombardment] Started at %s Radius=%.1f Damage=%.1f Speed=%.1f"),
			*TargetLocation.ToString(), Bombardment->SkillRadius, Damage, Speed);
	}
}

void AShip::ToggleFixedCamera()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	bUsingFixedCamera = !bUsingFixedCamera;

	if (bUsingFixedCamera)
	{
		// Save current camera state before detaching
		SavedBoomRelativeTransform = CameraBoom->GetRelativeTransform();
		SavedTargetArmLength = CameraBoom->TargetArmLength;
		SavedControlRotation = PC->GetControlRotation();
		if (FollowCamera)
		{
			SavedFollowCameraRelativeLocation = FollowCamera->GetRelativeLocation();
			SavedFollowCameraRelativeRotation = FollowCamera->GetRelativeRotation();
		}

		// Detach camera boom so it stops following the ship
		CameraBoom->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		CameraBoom->bUsePawnControlRotation = false;

		// Move camera to fixed world position
		CameraBoom->SetWorldLocationAndRotation(FixedCameraLocation, FixedCameraRotation);
		if (FollowCamera)
		{
			FollowCamera->SetRelativeLocation(FVector::ZeroVector);
			FollowCamera->SetRelativeRotation(FRotator::ZeroRotator);
		}

		UE_LOG(LogTemp, Log, TEXT("Switched to fixed camera at %s"), *FixedCameraLocation.ToString());
	}
	else
	{
		// Re-attach camera boom to ship and restore saved state
		CameraBoom->AttachToComponent(BuoyancyRoot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		CameraBoom->SetRelativeTransform(SavedBoomRelativeTransform);
		CameraBoom->bUsePawnControlRotation = true;
		CameraBoom->TargetArmLength = SavedTargetArmLength;
		if (FollowCamera)
		{
			FollowCamera->SetRelativeLocation(SavedFollowCameraRelativeLocation);
			FollowCamera->SetRelativeRotation(SavedFollowCameraRelativeRotation);
		}
		PC->SetControlRotation(SavedControlRotation);

		UE_LOG(LogTemp, Log, TEXT("Switched back to follow camera."));
	}
}

void AShip::ResetToFollowCamera()
{
	if (bUsingFixedCamera)
	{
		bUsingFixedCamera = false;

		CameraBoom->AttachToComponent(BuoyancyRoot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		CameraBoom->SetRelativeTransform(SavedBoomRelativeTransform);
		CameraBoom->bUsePawnControlRotation = true;
		CameraBoom->TargetArmLength = SavedTargetArmLength;
		if (FollowCamera)
		{
			FollowCamera->SetRelativeLocation(SavedFollowCameraRelativeLocation);
			FollowCamera->SetRelativeRotation(SavedFollowCameraRelativeRotation);
		}

		APlayerController* PC = Cast<APlayerController>(GetController());
		if (!PC)
		{
			PC = CachedPlayerController;
		}
		if (PC)
		{
			PC->SetControlRotation(SavedControlRotation);
		}
	}
}

void AShip::RememberFollowCameraState(APlayerController* PlayerController)
{
	if (!CameraBoom || !PlayerController || !PlayerController->IsLocalController() || bUsingFixedCamera)
	{
		return;
	}

	RememberedFollowTargetArmLength = CameraBoom->TargetArmLength;
	RememberedFollowControlRotation = PlayerController->GetControlRotation();
	bHasRememberedFollowCameraState = true;
}

void AShip::RestoreRememberedFollowCameraState(APlayerController* PlayerController)
{
	if (!bHasRememberedFollowCameraState || !CameraBoom || !PlayerController
		|| !PlayerController->IsLocalController() || bUsingFixedCamera)
	{
		return;
	}

	CameraBoom->TargetArmLength = RememberedFollowTargetArmLength;
	PlayerController->SetControlRotation(RememberedFollowControlRotation);
}

void AShip::OnRep_RidingPlayer(APawn* OldRidingPlayer)
{
	// UE_LOG(LogTemp, Log, TEXT("AShip: [CLIENT] OnRep_RidingPlayer. OldRidingPlayer: %s, RidingPlayer: %s"), 
	// 	OldRidingPlayer ? *OldRidingPlayer->GetName() : TEXT("Null"), 
	// 	RidingPlayer ? *RidingPlayer->GetName() : TEXT("Null"));

	APlayerController* LocalPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;

	if (OldRidingPlayer && OldRidingPlayer != RidingPlayer)
	{
		// UE_LOG(LogTemp, Log, TEXT("AShip: [CLIENT] OnRep_RidingPlayer - Restoring old passenger collision and walking movement."));
		OldRidingPlayer->SetActorEnableCollision(true);
		if (ACharacter* Char = Cast<ACharacter>(OldRidingPlayer))
		{
			Char->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}

		if (LocalPC && LocalPC->IsLocalController())
		{
			LocalPC->HiddenActors.Remove(OldRidingPlayer);
		}
	}

	if (RidingPlayer)
	{
		// UE_LOG(LogTemp, Log, TEXT("AShip: [CLIENT] OnRep_RidingPlayer - Disabling current passenger collision and movement."));
		RidingPlayer->SetActorEnableCollision(false);
		if (ACharacter* Char = Cast<ACharacter>(RidingPlayer))
		{
			Char->GetCharacterMovement()->DisableMovement();
			Char->GetCharacterMovement()->StopMovementImmediately();
		}

		if (LocalPC && LocalPC->IsLocalController())
		{
			LocalPC->HiddenActors.AddUnique(RidingPlayer);
		}
	}
}

void AShip::OnRep_Controller()
{
	Super::OnRep_Controller();

	if (NetworkPhysicsComponent)
	{
		bool bRelay = IsLocallyControlled();
		NetworkPhysicsComponent->SetIsRelayingLocalInputs(bRelay);
		/* Network Physics input relay diagnostic log disabled after validation.
		UE_LOG(LogTemp, Warning, TEXT("[CLIENT-GT] OnRep_Controller - SetIsRelayingLocalInputs: %s"), bRelay ? TEXT("True") : TEXT("False"));
		*/
	}

	if (Controller == nullptr)
	{
		APlayerController* LocalPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (LocalPC && LocalPC->IsLocalController() && RidingPlayer)
		{
			LocalPC->HiddenActors.Remove(RidingPlayer);
		}

		if (CachedPlayerController)
		{
			ResetToFollowCamera();
			RememberFollowCameraState(CachedPlayerController);
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(CachedPlayerController->GetLocalPlayer()))
			{
				if (ShipInputMappingContext)
				{
					Subsystem->RemoveMappingContext(ShipInputMappingContext);
					// UE_LOG(LogTemp, Log, TEXT("AShip: Removed ShipInputMappingContext in OnRep_Controller."));
				}
			}
			CachedPlayerController = nullptr;
		}
	}
	else
	{
		CachedPlayerController = Cast<APlayerController>(Controller);
		if (CachedPlayerController)
		{
			RestoreRememberedFollowCameraState(CachedPlayerController);
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(CachedPlayerController->GetLocalPlayer()))
			{
				if (ShipInputMappingContext)
				{
					// Player and item contexts can remain registered across possession.
					// Ship input must win shared keys such as LMB, RMB, and number 5.
					constexpr int32 MinimumShipInputPriority = 20;
					Subsystem->AddMappingContext(
						ShipInputMappingContext,
						FMath::Max(ShipInputPriority, MinimumShipInputPriority));
					// UE_LOG(LogTemp, Log, TEXT("AShip: Added ShipInputMappingContext in OnRep_Controller."));
				}
			}
		}
	}
}

UAbilitySystemComponent* AShip::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AShip::InitializeDefaultAttributes()
{
	if (!HasAuthority() || !AttributeSet) return;

	if (ShipStatTable && !ShipStatRowName.IsNone())
	{
		static const FString ContextString(TEXT("Ship Stat Table Context"));
		FShipStatRow* StatRow = ShipStatTable->FindRow<FShipStatRow>(ShipStatRowName, ContextString);
		if (StatRow)
		{
			AttributeSet->InitHealth(StatRow->MaxHealth);
			AttributeSet->InitMaxHealth(StatRow->MaxHealth);
			AttributeSet->InitMoveSpeed(1.0f); // 캐릭터 기본 MoveSpeed는 1.0f로 고정 유지
			const bool bUseLegacyMovement = FMath::IsNearlyEqual(StatRow->ForwardPropulsionMultiplier, 1.0f)
				&& FMath::IsNearlyEqual(StatRow->TurnTorqueMultiplier, 1.0f)
				&& !FMath::IsNearlyEqual(StatRow->ShipSpeedMultiplier, 1.0f);
			AttributeSet->InitForwardPropulsionMultiplier(bUseLegacyMovement ? StatRow->ShipSpeedMultiplier : StatRow->ForwardPropulsionMultiplier);
			AttributeSet->InitTurnTorqueMultiplier(bUseLegacyMovement ? StatRow->ShipSpeedMultiplier : StatRow->TurnTorqueMultiplier);
			AttributeSet->InitCannonDamage(StatRow->CannonDamage);
			AttributeSet->InitCannonFireCooldown(StatRow->CannonFireCooldown);
			AttributeSet->InitCannonballSpeed(StatRow->CannonballSpeed);

			UE_LOG(LogTemp, Log, TEXT("AShip: Successfully initialized attributes from DataTable Row [%s]."), *ShipStatRowName.ToString());
			return;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("AShip: Failed to find DataTable Row [%s] in ShipStatTable."), *ShipStatRowName.ToString());
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("AShip: ShipStatTable or ShipStatRowName is not set. Initializing with default fallback stats."));
	}

	// Fallback 기본값 설정
	AttributeSet->InitHealth(100.f);
	AttributeSet->InitMaxHealth(100.f);
	AttributeSet->InitMoveSpeed(1.f);
	AttributeSet->InitForwardPropulsionMultiplier(1.f);
	AttributeSet->InitTurnTorqueMultiplier(1.f);
	AttributeSet->InitCannonDamage(20.f);
	AttributeSet->InitCannonFireCooldown(2.f);
	AttributeSet->InitCannonballSpeed(3000.f);
}

FShipStatSnapshot AShip::GetBaseStatSnapshot() const
{
	FShipStatSnapshot Snapshot;
	if (!ShipStatTable || ShipStatRowName.IsNone()) return Snapshot;
	static const FString ContextString(TEXT("Ship Stat Snapshot Context"));
	const FShipStatRow* StatRow = ShipStatTable->FindRow<FShipStatRow>(ShipStatRowName, ContextString);
	if (!StatRow) return Snapshot;

	Snapshot.MaxHealth = StatRow->MaxHealth;
	Snapshot.CannonDamage = StatRow->CannonDamage;
	Snapshot.CannonFireCooldownSeconds = StatRow->CannonFireCooldown;
	Snapshot.CannonballSpeed = StatRow->CannonballSpeed;
	Snapshot.ForwardPropulsionMultiplier = StatRow->ForwardPropulsionMultiplier;
	Snapshot.TurnTorqueMultiplier = StatRow->TurnTorqueMultiplier;
	if (FMath::IsNearlyEqual(StatRow->ForwardPropulsionMultiplier, 1.0f)
		&& FMath::IsNearlyEqual(StatRow->TurnTorqueMultiplier, 1.0f)
		&& !FMath::IsNearlyEqual(StatRow->ShipSpeedMultiplier, 1.0f))
	{
		Snapshot.ForwardPropulsionMultiplier = StatRow->ShipSpeedMultiplier;
		Snapshot.TurnTorqueMultiplier = StatRow->ShipSpeedMultiplier;
	}
	return Snapshot;
}

void AShip::ApplyStatSnapshot(const FShipStatSnapshot& Snapshot, bool bRefillHealth)
{
	if (!HasAuthority() || !AttributeSet) return;
	AttributeSet->InitMaxHealth(FMath::Max(1.0f, Snapshot.MaxHealth));
	if (bRefillHealth)
	{
		AttributeSet->InitHealth(AttributeSet->GetMaxHealth());
	}
	else
	{
		AttributeSet->SetHealth(FMath::Min(AttributeSet->GetHealth(), AttributeSet->GetMaxHealth()));
	}
	AttributeSet->InitMoveSpeed(1.0f);
	AttributeSet->InitForwardPropulsionMultiplier(Snapshot.ForwardPropulsionMultiplier);
	AttributeSet->InitTurnTorqueMultiplier(Snapshot.TurnTorqueMultiplier);
	AttributeSet->InitCannonDamage(Snapshot.CannonDamage);
	AttributeSet->InitCannonFireCooldown(Snapshot.CannonFireCooldownSeconds);
	AttributeSet->InitCannonballSpeed(Snapshot.CannonballSpeed);
}

bool AShip::ApplyPlayerUpgrades(APlayerState* InPlayerState, bool bRefillHealth)
{
	if (!HasAuthority() || !InPlayerState) return false;
	UShipUpgradeComponent* UpgradeComponent = InPlayerState->FindComponentByClass<UShipUpgradeComponent>();
	if (!UpgradeComponent || !UpgradeComponent->UpgradeTree) return false;
	UpgradeComponent->SetPreviewBaseStats(GetBaseStatSnapshot());
	ApplyStatSnapshot(UpgradeComponent->GetCurrentShipStats(), bRefillHealth);
	return true;
}

void AShip::HandlePortSeaBoarding(AActor* Interactor)
{
	if (!Interactor) return;

	if (HasAuthority())
	{
		FVector DestinationLoc = PortSeaBoardingDestination ? PortSeaBoardingDestination->GetComponentLocation() : GetActorLocation() + FVector(0.f, 0.f, 200.f);
		FRotator DestinationRot = PortSeaBoardingDestination ? PortSeaBoardingDestination->GetComponentRotation() : GetActorRotation();

		if (ACharacter* Character = Cast<ACharacter>(Interactor))
		{
			if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
			{
				MoveComp->SetMovementMode(MOVE_Walking);
				UE_LOG(LogTemp, Log, TEXT("AShip::HandlePortSeaBoarding - Character movement mode set to MOVE_Walking."));
			}
		}

		Interactor->TeleportTo(DestinationLoc, DestinationRot);

		UE_LOG(LogTemp, Log, TEXT("AShip::HandlePortSeaBoarding - Teleported %s to port boarding destination: %s"), 
			*Interactor->GetName(), *DestinationLoc.ToString());
	}
}

void AShip::HandleStarboardSeaBoarding(AActor* Interactor)
{
	if (!Interactor) return;

	if (HasAuthority())
	{
		FVector DestinationLoc = StarboardSeaBoardingDestination ? StarboardSeaBoardingDestination->GetComponentLocation() : GetActorLocation() + FVector(0.f, 0.f, 200.f);
		FRotator DestinationRot = StarboardSeaBoardingDestination ? StarboardSeaBoardingDestination->GetComponentRotation() : GetActorRotation();

		if (ACharacter* Character = Cast<ACharacter>(Interactor))
		{
			if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
			{
				MoveComp->SetMovementMode(MOVE_Walking);
				UE_LOG(LogTemp, Log, TEXT("AShip::HandleStarboardSeaBoarding - Character movement mode set to MOVE_Walking."));
			}
		}

		Interactor->TeleportTo(DestinationLoc, DestinationRot);

		UE_LOG(LogTemp, Log, TEXT("AShip::HandleStarboardSeaBoarding - Teleported %s to starboard boarding destination: %s"), 
			*Interactor->GetName(), *DestinationLoc.ToString());
	}
}


