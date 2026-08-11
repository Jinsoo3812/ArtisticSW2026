#include "ShipAI/Abilities/EnemyShipObstacle.h"

#include "Buoyancy/SWBuoyancyComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/BodyInstance.h"
#include "TimerManager.h"
#include "WaterBodyActor.h"

AEnemyShipObstacle::AEnemyShipObstacle()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.0f);
	SetMinNetUpdateFrequency(10.0f);

	ObstacleCollision = CreateDefaultSubobject<USphereComponent>(TEXT("ObstacleCollision"));
	ObstacleCollision->InitSphereRadius(BuoyancyPontoonRadius);
	ObstacleCollision->SetCollisionProfileName(TEXT("EnemyShipObstacle"));
	ObstacleCollision->SetGenerateOverlapEvents(true);
	ObstacleCollision->BodyInstance.DOFMode = EDOFMode::SixDOF;
	ObstacleCollision->BodyInstance.bLockXTranslation = true;
	ObstacleCollision->BodyInstance.bLockYTranslation = true;
	ObstacleCollision->BodyInstance.bLockZTranslation = false;
	ObstacleCollision->BodyInstance.bLockXRotation = true;
	ObstacleCollision->BodyInstance.bLockYRotation = true;
	ObstacleCollision->BodyInstance.bLockZRotation = true;
	SetRootComponent(ObstacleCollision);

	ObstacleBlocker = CreateDefaultSubobject<UBoxComponent>(TEXT("ObstacleBlocker"));
	ObstacleBlocker->SetupAttachment(ObstacleCollision);
	ObstacleBlocker->InitBoxExtent(CollisionHalfExtent);
	ObstacleBlocker->SetCollisionProfileName(TEXT("EnemyShipObstacle"));
	ObstacleBlocker->SetGenerateOverlapEvents(false);
	ObstacleBlocker->BodyInstance.bAutoWeld = false;
	ObstacleBlocker->SetSimulatePhysics(false);

	ObstacleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ObstacleMesh"));
	ObstacleMesh->SetupAttachment(ObstacleCollision);
	ObstacleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	SWBuoyancyComponent = CreateDefaultSubobject<USWBuoyancyComponent>(TEXT("SWBuoyancyComponent"));
	SWBuoyancyComponent->ExecutionMode = ESWBuoyancyExecutionMode::ServerAuthority;
	SWBuoyancyComponent->ConfigureSinglePontoon(BuoyancyPontoonRadius);
	SWBuoyancyComponent->ForceSettings.DeepWaterBuoyancyMultiplier = 3.0f;
}

void AEnemyShipObstacle::BeginPlay()
{
	Super::BeginPlay();

	ObstacleCollision->SetSphereRadius(FMath::Max(1.0f, BuoyancyPontoonRadius));
	ObstacleBlocker->SetBoxExtent(CollisionHalfExtent.ComponentMax(FVector(1.0f)));
	ObstacleBlocker->SetCollisionProfileName(TEXT("EnemyShipObstacle"));
	ObstacleBlocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ObstacleCollision->OnComponentBeginOverlap.AddUniqueDynamic(this, &AEnemyShipObstacle::OnObstacleOverlap);
	SWBuoyancyComponent->ConfigureSinglePontoon(FMath::Max(1.0f, BuoyancyPontoonRadius));
	SWBuoyancyComponent->Deactivate();
	SWBuoyancyComponent->SetComponentTickEnabled(false);
	ApplyPhysicsState();
	SetLifeSpan(FMath::Max(0.0f, MaximumLifetimeSeconds));
}

void AEnemyShipObstacle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() || !bHasClientMovementTarget)
	{
		return;
	}

	const float TimeSinceUpdate = GetWorld()
		? FMath::Max(0.0f, GetWorld()->GetTimeSeconds() - ClientMovementTargetReceiveTime)
		: 0.0f;
	const FVector DesiredLocation = ClientMovementTargetLocation
		+ ClientMovementTargetVelocity * FMath::Min(TimeSinceUpdate, ClientMaxExtrapolationTime);

	if (FVector::DistSquared(GetActorLocation(), DesiredLocation) > FMath::Square(ClientNetworkSnapDistance))
	{
		SetActorLocationAndRotation(DesiredLocation, ClientMovementTargetRotation, false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}

	SetActorLocationAndRotation(
		FMath::VInterpTo(GetActorLocation(), DesiredLocation, DeltaSeconds, ClientLocationInterpSpeed),
		FMath::QInterpTo(GetActorQuat(), ClientMovementTargetRotation, DeltaSeconds, ClientRotationInterpSpeed),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
}

void AEnemyShipObstacle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEnemyShipObstacle, bHasEnteredWater);
	DOREPLIFETIME(AEnemyShipObstacle, bBuoyancyEnabled);
}

void AEnemyShipObstacle::OnRep_ReplicatedMovement()
{
	if (!HasAuthority())
	{
		const FRepMovement& Movement = GetReplicatedMovement();
		ClientMovementTargetLocation = FRepMovement::RebaseOntoLocalOrigin(Movement.Location, this);
		ClientMovementTargetRotation = Movement.Rotation.Quaternion();
		ClientMovementTargetVelocity = Movement.LinearVelocity;
		ClientMovementTargetReceiveTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		bHasClientMovementTarget = true;
		if (ObstacleCollision->IsSimulatingPhysics())
		{
			ObstacleCollision->SetSimulatePhysics(false);
		}
		// The locally predicted Ship is a Chaos rigid body. A QueryOnly obstacle is
		// invisible to that simulation and produces server corrections instead of a
		// predicted wall contact. Keep a non-simulating (kinematic) physics shape on
		// clients at the replicated transform so rewind/resimulation sees the wall.
		ObstacleCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		ObstacleBlocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		return;
	}
	Super::OnRep_ReplicatedMovement();
}

void AEnemyShipObstacle::OnObstacleOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!HasAuthority() || bHasEnteredWater || !OtherActor)
	{
		return;
	}

	const bool bIsWater = OtherActor->IsA(AWaterBody::StaticClass())
		|| (OtherComponent && (OtherComponent->GetCollisionProfileName().ToString().Contains(TEXT("Water"))
			|| OtherComponent->GetName().Contains(TEXT("Water"))));
	if (!bIsWater)
	{
		return;
	}

	bHasEnteredWater = true;
	ForceNetUpdate();
	const float Delay = FMath::Max(0.0f, BuoyancyActivationDelaySeconds);
	if (Delay <= KINDA_SMALL_NUMBER)
	{
		EnableBuoyancy();
	}
	else
	{
		GetWorldTimerManager().SetTimer(
			BuoyancyActivationTimerHandle,
			this,
			&AEnemyShipObstacle::EnableBuoyancy,
			Delay,
			false);
	}
}

void AEnemyShipObstacle::OnRep_HasEnteredWater()
{
	// Presentation Blueprints can read HasEnteredWater. Physics remains server-authoritative.
}

void AEnemyShipObstacle::ApplyPhysicsState()
{
	if (HasAuthority())
	{
		ObstacleCollision->SetCollisionProfileName(TEXT("EnemyShipObstacle"));
		ObstacleCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		ObstacleCollision->SetMassOverrideInKg(NAME_None, FMath::Max(1.0f, FloatingMassKg), true);
		ObstacleCollision->SetLinearDamping(FMath::Max(0.0f, FloatingLinearDamping));
		ObstacleCollision->SetAngularDamping(FMath::Max(0.0f, FloatingAngularDamping));
		ObstacleCollision->SetSimulatePhysics(true);

		// Buoyancy remains free on Z. Horizontal and angular motion are constrained,
		// so ship contact is absorbed by a stable world anchor instead of pushing the
		// obstacle away. This also gives client resimulation a stable XY collision.
		FBodyInstance& BodyInstance = ObstacleCollision->BodyInstance;
		BodyInstance.bLockXTranslation = true;
		BodyInstance.bLockYTranslation = true;
		BodyInstance.bLockZTranslation = false;
		BodyInstance.bLockXRotation = true;
		BodyInstance.bLockYRotation = true;
		BodyInstance.bLockZRotation = true;
		BodyInstance.SetDOFLock(EDOFMode::SixDOF);
		ObstacleCollision->SetPhysicsLinearVelocity(FVector::ZeroVector);
		ObstacleCollision->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		ObstacleCollision->WakeAllRigidBodies();
		ObstacleBlocker->SetSimulatePhysics(false);
		ObstacleBlocker->SetCollisionProfileName(TEXT("EnemyShipObstacle"));
		ObstacleBlocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	else
	{
		ObstacleCollision->SetSimulatePhysics(false);
		ObstacleCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		ObstacleBlocker->SetSimulatePhysics(false);
		ObstacleBlocker->SetCollisionProfileName(TEXT("EnemyShipObstacle"));
		ObstacleBlocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
}

void AEnemyShipObstacle::EnableBuoyancy()
{
	if (!HasAuthority() || !bHasEnteredWater || !SWBuoyancyComponent)
	{
		return;
	}
	SWBuoyancyComponent->Activate();
	SWBuoyancyComponent->SetComponentTickEnabled(true);
	bBuoyancyEnabled = true;
	ForceNetUpdate();
}
