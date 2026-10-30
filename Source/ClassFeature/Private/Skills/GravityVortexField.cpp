#include "Skills/GravityVortexField.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "SWWaterSkillOverlaySubsystem.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Ship.h"
#include "Effects/SWNiagaraScaleLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomSnapshotSubsystem.h"

namespace
{
	uint64 NextGravitySurfaceOrder = 0;
}

AGravityVortexField::AGravityVortexField()
{
	CreateDefaultSubobject<USWRoomSnapshotComponent>(TEXT("RoomSnapshot"));
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FieldEffectComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FieldEffect"));
	FieldEffectComponent->SetupAttachment(SceneRoot);
	FieldEffectComponent->SetAutoActivate(false);

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> DefaultFieldEffect(
		TEXT("/Game/Resources_Assets/Splash_Effects/Effects/NS_Stream_Splash_01.NS_Stream_Splash_01"));
	FieldEffect = DefaultFieldEffect.Object;
}

void AGravityVortexField::BeginPlay()
{
	Super::BeginPlay();

	// Each replicated field owns its local visual, including on the listen server.
	// The component is destroyed with the field so looping effects cannot linger.
	if (GetNetMode() != NM_DedicatedServer && FieldEffectComponent && FieldEffect)
	{
		FieldEffectComponent->SetRelativeLocation(FVector(0.0f, 0.0f, FieldEffectHeightOffset));
		FieldEffectComponent->SetAsset(FieldEffect);
		USWNiagaraScaleLibrary::ApplyEffectTuning(
			FieldEffectComponent, FieldEffectScale, FieldEffectLifetimeScale, FieldEffectPlaybackSpeed);
		FieldEffectComponent->Activate(true);
	}

	if (HasAuthority() && !GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot())
	{
		SourceId = FGuid::NewGuid();
		SurfaceOrder = ++NextGravitySurfaceOrder;
		ActivationServerTime = GetSynchronizedServerTime();
		ExpireServerTime = ActivationServerTime + FMath::Max(0.05f, Duration);
		SetLifeSpan(FMath::Max(0.05f, Duration));
		RefreshTargets();
	}
	UpdateSurfaceOverlay();
}

void AGravityVortexField::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority() && !GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot())
	{
		RefreshAccumulator += DeltaSeconds;
		if (RefreshAccumulator >= FMath::Max(0.01f, TargetRefreshInterval))
		{
			RefreshAccumulator = 0.0f;
			RefreshTargets();
		}
	}

	UpdateSurfaceOverlay();
}

void AGravityVortexField::UpdateSurfaceOverlay()
{
	if (!GetWorld() || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (USWWaterSkillOverlaySubsystem* Overlays = GetWorld()->GetSubsystem<USWWaterSkillOverlaySubsystem>())
	{
		if (ExpireServerTime <= ActivationServerTime
			|| GetSynchronizedServerTime() >= ExpireServerTime)
		{
			Overlays->Remove(this);
			return;
		}
		FLinearColor Color = SurfaceTint;
		Color.A = 1.0f;
		Overlays->Upsert(this, GetActorLocation(), PullRadius, Color,
			ActivationServerTime, SurfaceOrder, ExpireServerTime);
	}
}

void AGravityVortexField::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		if (USWWaterSkillOverlaySubsystem* Overlays = GetWorld()->GetSubsystem<USWWaterSkillOverlaySubsystem>())
		{
			Overlays->Remove(this);
		}
	}
	if (HasAuthority())
	{
		for (const TWeakObjectPtr<AShip>& ShipPtr : AffectedShips)
		{
			ReleaseShip(ShipPtr.Get());
		}
		AffectedShips.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void AGravityVortexField::RefreshTargets()
{
	if (!HasAuthority() || !GetWorld() || !SourceId.IsValid())
	{
		return;
	}

	TSet<TWeakObjectPtr<AShip>> NewAffectedShips;
	const float RadiusSq = FMath::Square(FMath::Max(PullRadius, 1.0f));

	for (TActorIterator<AShip> It(GetWorld()); It; ++It)
	{
		AShip* Ship = *It;
		if (!IsValid(Ship) || !Ship->IsEnemyShipForEffects() || !IsValid(Ship->BuoyancyRoot))
		{
			continue;
		}

		FVector Delta = GetActorLocation() - Ship->GetActorLocation();
		Delta.Z = 0.0f;
		if (Delta.SizeSquared() > RadiusSq)
		{
			continue;
		}

		NewAffectedShips.Add(Ship);
		Ship->SetExternalAccelerationSource(SourceId, CalculateAcceleration(Ship));
		if (bSuppressEnemyPropulsion)
		{
			Ship->AddPropulsionSuppression(SourceId);
		}
	}

	for (const TWeakObjectPtr<AShip>& PreviousShip : AffectedShips)
	{
		if (!NewAffectedShips.Contains(PreviousShip))
		{
			ReleaseShip(PreviousShip.Get());
		}
	}

	AffectedShips = MoveTemp(NewAffectedShips);
}

void AGravityVortexField::ReleaseShip(AShip* Ship)
{
	if (!IsValid(Ship) || !SourceId.IsValid())
	{
		return;
	}

	Ship->RemoveExternalAccelerationSource(SourceId);
	Ship->RemovePropulsionSuppression(SourceId);
}

FVector AGravityVortexField::CalculateAcceleration(AShip* Ship) const
{
	if (!IsValid(Ship))
	{
		return FVector::ZeroVector;
	}

	FVector ToCenter = GetActorLocation() - Ship->GetActorLocation();
	ToCenter.Z = 0.0f;
	const float Distance = ToCenter.Size();
	if (Distance <= FMath::Max(0.0f, InnerDeadZoneRadius) || Distance > PullRadius)
	{
		return FVector::ZeroVector;
	}

	const float DistanceAlpha = FMath::Clamp(Distance / FMath::Max(PullRadius, 1.0f), 0.0f, 1.0f);
	const float Falloff = PullFalloffCurve
		? FMath::Max(0.0f, PullFalloffCurve->GetFloatValue(DistanceAlpha))
		: FMath::Lerp(1.0f, FMath::Clamp(EdgePullStrengthRatio, 0.0f, 1.0f), DistanceAlpha);

	const FVector PullDirection = ToCenter / Distance;
	FVector Acceleration = PullDirection * FMath::Max(0.0f, PullAcceleration) * Falloff;

	if (Ship->BuoyancyRoot && RadialDamping > 0.0f)
	{
		const float InwardSpeed = FVector::DotProduct(Ship->BuoyancyRoot->GetPhysicsLinearVelocity(), PullDirection);
		if (InwardSpeed > MaxInwardSpeed)
		{
			Acceleration -= PullDirection * (InwardSpeed - MaxInwardSpeed) * RadialDamping;
		}
	}

	Acceleration.Z = 0.0f;
	return Acceleration.GetClampedToMaxSize(FMath::Max(0.0f, MaxPullAcceleration));
}

double AGravityVortexField::GetSynchronizedServerTime() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			return GameState->GetServerWorldTimeSeconds();
		}
		return World->GetTimeSeconds();
	}
	return 0.0;
}

void AGravityVortexField::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGravityVortexField, ActivationServerTime);
	DOREPLIFETIME(AGravityVortexField, ExpireServerTime);
	DOREPLIFETIME(AGravityVortexField, PullRadius);
	DOREPLIFETIME(AGravityVortexField, SurfaceTint);
	DOREPLIFETIME(AGravityVortexField, SurfaceOrder);
}

void AGravityVortexField::CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const
{
	FSWRoomGravityFieldState State;
	State.SourceId = SourceId;
	State.SurfaceOrder = SurfaceOrder;
	State.RemainingLife = GetLifeSpan();
	State.RefreshAccumulator = RefreshAccumulator;
	for (const TWeakObjectPtr<AShip>& Ship : AffectedShips)
	{
		if (!Ship.IsValid()) continue;
		if (const USWRoomSnapshotComponent* Id = Ship->FindComponentByClass<USWRoomSnapshotComponent>(); Id && Id->StableId.IsValid())
			State.AffectedShipIds.AddUnique(Id->StableId);
		else
		{
			FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Domain = TEXT("Area");
			Issue.FieldKey = FName(*(TEXT("AffectedShip:") + Ship->GetPathName()));
			Issue.Reason = TEXT("Affected ship has no stable ID");
		}
	}
	State.AffectedShipIds.Sort();
	FSWRoomDomainPart& Part = OutParts.AddDefaulted_GetRef();
	Part.Domain = ESWRoomDomain::Area;
	Part.Version = 1;
	if (!FSWRoomStructCodec::Write(State, Part.Bytes))
	{
		OutParts.Pop();
		FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("Area");
		Issue.FieldKey = TEXT("GravityFieldState");
		Issue.Reason = TEXT("Gravity vortex field serialization failed");
	}
}

bool AGravityVortexField::RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError)
{
	FSWRoomGravityFieldState State;
	if (Part.Domain != ESWRoomDomain::Area || Part.Version != 1 || !FSWRoomStructCodec::Read(Part.Bytes, State)
		|| !State.SourceId.IsValid() || !FMath::IsFinite(State.RemainingLife) || State.RemainingLife < 0.f
		|| !FMath::IsFinite(State.RefreshAccumulator) || State.RefreshAccumulator < 0.f)
	{
		OutError = TEXT("Invalid gravity vortex field state");
		return false;
	}
	SourceId = State.SourceId;
	SurfaceOrder = State.SurfaceOrder;
	NextGravitySurfaceOrder = FMath::Max(NextGravitySurfaceOrder, SurfaceOrder);
	RefreshAccumulator = State.RefreshAccumulator;
	AffectedShips.Reset();
	PendingRoomState = MoveTemp(State);
	bHasPendingRoomState = true;
	return true;
}

bool AGravityVortexField::FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError)
{
	if (!bHasPendingRoomState) return true;
	bHasPendingRoomState = false;
	const float Remaining = FMath::Max(KINDA_SMALL_NUMBER, PendingRoomState.RemainingLife);
	ActivationServerTime = GetSynchronizedServerTime() - FMath::Max(0.f, Duration - Remaining);
	ExpireServerTime = GetSynchronizedServerTime() + Remaining;
	SetLifeSpan(Remaining);
	for (const FGuid& Id : PendingRoomState.AffectedShipIds)
	{
		AActor* const* Found = RegisteredActors.Find(Id);
		AShip* Ship = Found ? Cast<AShip>(*Found) : nullptr;
		if (!Ship) continue;
		AffectedShips.Add(Ship);
		Ship->SetExternalAccelerationSource(SourceId, CalculateAcceleration(Ship));
		if (bSuppressEnemyPropulsion) Ship->AddPropulsionSuppression(SourceId);
	}
	RefreshTargets();
	UpdateSurfaceOverlay();
	return true;
}
