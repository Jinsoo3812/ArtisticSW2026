#include "Bombardment.h"

#include "BaseCharacter.h"
#include "Cannonball.h"
#include "Components/MeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInterface.h"
#include "Ship.h"

ABombardmentPreview::ABombardmentPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;

	PreviewRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot"));
	SetRootComponent(PreviewRoot);

	PreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	PreviewMesh->SetupAttachment(PreviewRoot);
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMesh->SetGenerateOverlapEvents(false);
	PreviewMesh->SetCastShadow(false);
	PreviewMesh->TranslucencySortPriority = 10;
}

void ABombardmentPreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	HighlightRefreshAccumulator += DeltaSeconds;
	if (HighlightRefreshAccumulator >= FMath::Max(0.02f, HighlightRefreshInterval))
	{
		HighlightRefreshAccumulator = 0.0f;
		RefreshHighlightedTargets();
	}
}

void ABombardmentPreview::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreHighlights();
	Super::EndPlay(EndPlayReason);
}

void ABombardmentPreview::ConfigurePreview(float InSkillRadius)
{
	SkillRadius = FMath::Max(1.0f, InSkillRadius);
	if (!PreviewMesh)
	{
		return;
	}

	if (PreviewStaticMesh)
	{
		PreviewMesh->SetStaticMesh(PreviewStaticMesh);
	}
	if (PreviewMaterial)
	{
		PreviewMesh->SetMaterial(0, PreviewMaterial);
	}
	PreviewMesh->SetVisibility(true);
	PreviewMesh->SetHiddenInGame(false);
	if (!PreviewMesh->GetStaticMesh())
	{
		return;
	}

	const FBoxSphereBounds MeshBounds = PreviewMesh->GetStaticMesh()->GetBounds();
	const float AuthoredRadius = FMath::Max(MeshBounds.BoxExtent.X, MeshBounds.BoxExtent.Y);
	if (AuthoredRadius > UE_SMALL_NUMBER)
	{
		const float UniformXYScale = SkillRadius / AuthoredRadius;
		PreviewMesh->SetRelativeScale3D(FVector(UniformXYScale, UniformXYScale, 1.0f));
	}
}

void ABombardmentPreview::SetPreviewValid(bool bInValid)
{
	bPreviewValid = bInValid;
}

void ABombardmentPreview::RefreshHighlightedTargets()
{
	if (!bPreviewValid || SkillRadius <= 0.0f || !TargetHighlightMaterial)
	{
		RestoreHighlights();
		return;
	}

	TSet<UMeshComponent*> DesiredMeshes;
	const float RadiusSquared = FMath::Square(SkillRadius);
	for (TActorIterator<AShip> It(GetWorld()); It; ++It)
	{
		AShip* Ship = *It;
		if (!IsValid(Ship) || !Ship->IsEnemyShipForEffects())
		{
			continue;
		}
		FVector Delta = Ship->GetActorLocation() - GetActorLocation();
		Delta.Z = 0.0f;
		if (Delta.SizeSquared() <= RadiusSquared)
		{
			CollectHighlightMeshes(Ship, DesiredMeshes);
		}
	}

	for (TActorIterator<ABaseCharacter> It(GetWorld()); It; ++It)
	{
		ABaseCharacter* Character = *It;
		if (!IsValid(Character) || !Character->IsEnemyCharacterForEffects())
		{
			continue;
		}

		FVector Delta = Character->GetActorLocation() - GetActorLocation();
		Delta.Z = 0.0f;
		if (Delta.SizeSquared() <= RadiusSquared)
		{
			CollectHighlightMeshes(Character, DesiredMeshes);
		}
	}

	// Keep unchanged targets highlighted without toggling Nanite fallback every
	// refresh. Restore only targets that actually left the disk.
	for (int32 StateIndex = HighlightedMeshes.Num() - 1; StateIndex >= 0; --StateIndex)
	{
		UMeshComponent* Mesh = HighlightedMeshes[StateIndex].Mesh.Get();
		if (!IsValid(Mesh) || !DesiredMeshes.Contains(Mesh))
		{
			RestoreHighlight(HighlightedMeshes[StateIndex]);
			HighlightedMeshes.RemoveAtSwap(StateIndex);
		}
		else
		{
			DesiredMeshes.Remove(Mesh);
		}
	}

	for (UMeshComponent* Mesh : DesiredMeshes)
	{
		ApplyHighlight(Mesh);
	}
}

void ABombardmentPreview::CollectHighlightMeshes(
	AActor* Actor,
	TSet<UMeshComponent*>& OutMeshes) const
{
	TArray<UMeshComponent*> Meshes;
	Actor->GetComponents<UMeshComponent>(Meshes);
	for (UMeshComponent* Mesh : Meshes)
	{
		// Do not apply a world-space highlight material to WidgetComponents such as
		// ship health bars. Target only actual rendered ship geometry.
		if (!IsValid(Mesh) || !Mesh->IsVisible()
			|| (!Cast<UStaticMeshComponent>(Mesh) && !Cast<USkeletalMeshComponent>(Mesh)))
		{
			continue;
		}
		OutMeshes.Add(Mesh);
	}
}

void ABombardmentPreview::ApplyHighlight(UMeshComponent* Mesh)
{
	if (!IsValid(Mesh))
	{
		return;
	}

	FBombardmentMeshHighlightState& State = HighlightedMeshes.AddDefaulted_GetRef();
	State.Mesh = Mesh;
	State.OverlayMaterial = Mesh->GetOverlayMaterial();
	if (UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Mesh))
	{
		State.bDisallowNanite = StaticMesh->bDisallowNanite;
		if (!StaticMesh->bDisallowNanite)
		{
			// Translucent overlay materials are not a valid Nanite base pass.
			// Temporarily use the fallback mesh so the original material and
			// the translucent blue overlay can both render.
			StaticMesh->bDisallowNanite = true;
			StaticMesh->MarkRenderStateDirty();
		}
	}
	Mesh->SetOverlayMaterial(TargetHighlightMaterial);
}

void ABombardmentPreview::RestoreHighlight(const FBombardmentMeshHighlightState& State)
{
	if (UMeshComponent* Mesh = State.Mesh.Get())
	{
		Mesh->SetOverlayMaterial(State.OverlayMaterial.Get());
		if (UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Mesh))
		{
			if (StaticMesh->bDisallowNanite != State.bDisallowNanite)
			{
				StaticMesh->bDisallowNanite = State.bDisallowNanite;
				StaticMesh->MarkRenderStateDirty();
			}
		}
	}
}

void ABombardmentPreview::RestoreHighlights()
{
	for (const FBombardmentMeshHighlightState& State : HighlightedMeshes)
	{
		RestoreHighlight(State);
	}
	HighlightedMeshes.Reset();
}

ABombardment::ABombardment()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetActorEnableCollision(false);
	PreviewClass = ABombardmentPreview::StaticClass();
}

void ABombardment::InitializeBombardment(
	AShip* InSourceShip,
	APawn* InInstigatorPawn,
	const FVector& InTargetCenter,
	TSubclassOf<AActor> InResolvedProjectileClass,
	float InProjectileDamage,
	float InProjectileSpeed)
{
	if (!HasAuthority() || !IsValid(InSourceShip) || !InResolvedProjectileClass
		|| !InResolvedProjectileClass->IsChildOf(ACannonball::StaticClass()))
	{
		Destroy();
		return;
	}

	SourceShip = InSourceShip;
	SkillInstigator = InInstigatorPawn;
	TargetCenter = InTargetCenter;
	ResolvedProjectileClass = InResolvedProjectileClass;
	ProjectileDamage = FMath::Max(0.0f, InProjectileDamage);
	ProjectileSpeed = FMath::Max(1.0f, InProjectileSpeed);
	BuildSchedule();
	bStarted = ScheduledShots.Num() > 0;

	if (!bStarted)
	{
		Destroy();
	}
}

void ABombardment::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !bStarted)
	{
		return;
	}

	ElapsedSeconds += DeltaSeconds;
	while (ScheduledShots.IsValidIndex(NextShotIndex)
		&& ScheduledShots[NextShotIndex].FireTime <= ElapsedSeconds + UE_KINDA_SMALL_NUMBER)
	{
		FireShot(ScheduledShots[NextShotIndex]);
		++NextShotIndex;
	}

	if (NextShotIndex >= ScheduledShots.Num())
	{
		Destroy();
	}
}

TArray<FVector2D> ABombardment::GenerateDiskOffsets(
	int32 Count,
	float Radius,
	int32 Seed,
	EBombardmentDistributionMode Mode,
	float Jitter)
{
	TArray<FVector2D> Result;
	Count = FMath::Max(0, Count);
	Radius = FMath::Max(0.0f, Radius);
	Jitter = FMath::Clamp(Jitter, 0.0f, 1.0f);
	Result.Reserve(Count);

	FRandomStream Random(Seed);
	const float RandomRotation = Random.FRandRange(0.0f, 2.0f * UE_PI);
	constexpr float GoldenAngle = 2.39996323f;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		float PointRadius = 0.0f;
		float Angle = 0.0f;
		if (Mode == EBombardmentDistributionMode::PureRandom)
		{
			PointRadius = Radius * FMath::Sqrt(Random.FRand());
			Angle = Random.FRandRange(0.0f, 2.0f * UE_PI);
		}
		else
		{
			const float RadialStratum = (static_cast<float>(Index) + Random.FRand())
				/ static_cast<float>(FMath::Max(1, Count));
			PointRadius = Radius * FMath::Sqrt(RadialStratum);
			Angle = RandomRotation + GoldenAngle * static_cast<float>(Index)
				+ Random.FRandRange(-0.5f, 0.5f) * GoldenAngle * Jitter;
		}
		Result.Add(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * PointRadius);
	}
	return Result;
}

TArray<float> ABombardment::BuildShotTimes(
	int32 InProjectilesPerVolley,
	float InBurstSpreadDuration,
	float InVolleyInterval,
	int32 InVolleyCount)
{
	const int32 SafeProjectilesPerVolley = FMath::Max(1, InProjectilesPerVolley);
	const int32 SafeVolleyCount = FMath::Max(1, InVolleyCount);
	const float SafeBurstDuration = FMath::Max(0.0f, InBurstSpreadDuration);
	const float SafeVolleyInterval = FMath::Max(0.01f, InVolleyInterval);

	TArray<float> Result;
	Result.Reserve(SafeProjectilesPerVolley * SafeVolleyCount);
	for (int32 VolleyIndex = 0; VolleyIndex < SafeVolleyCount; ++VolleyIndex)
	{
		const float VolleyStart = static_cast<float>(VolleyIndex) * SafeVolleyInterval;
		for (int32 ShotIndex = 0; ShotIndex < SafeProjectilesPerVolley; ++ShotIndex)
		{
			const float ShotAlpha = SafeProjectilesPerVolley > 1
				? static_cast<float>(ShotIndex) / static_cast<float>(SafeProjectilesPerVolley - 1)
				: 0.0f;
			Result.Add(VolleyStart + SafeBurstDuration * ShotAlpha);
		}
	}
	Result.Sort();
	return Result;
}

bool ABombardment::SolveBallisticVelocity(
	const FVector& Start,
	const FVector& Target,
	float Speed,
	float GravityZ,
	FVector& OutVelocity)
{
	OutVelocity = FVector::ZeroVector;
	if (Speed <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector Displacement = Target - Start;
	const FVector HorizontalDelta(Displacement.X, Displacement.Y, 0.0f);
	const float HorizontalDistance = HorizontalDelta.Size();
	if (HorizontalDistance <= UE_SMALL_NUMBER)
	{
		if (Displacement.Z >= 0.0f)
		{
			return false;
		}
		OutVelocity = FVector(0.0f, 0.0f, -Speed);
		return true;
	}

	const float GravityMagnitude = FMath::Abs(GravityZ);
	if (GravityMagnitude <= UE_SMALL_NUMBER)
	{
		OutVelocity = Displacement.GetSafeNormal() * Speed;
		return !OutVelocity.IsNearlyZero();
	}

	const double SpeedSquared = static_cast<double>(Speed) * Speed;
	const double Discriminant = SpeedSquared * SpeedSquared
		- static_cast<double>(GravityMagnitude)
		* (static_cast<double>(GravityMagnitude) * HorizontalDistance * HorizontalDistance
			+ 2.0 * Displacement.Z * SpeedSquared);
	if (Discriminant < 0.0)
	{
		return false;
	}

	// The minus branch is the direct/lower trajectory and naturally points downward from a high launch disk.
	const double TanTheta = (SpeedSquared - FMath::Sqrt(Discriminant))
		/ (static_cast<double>(GravityMagnitude) * HorizontalDistance);
	const float CosTheta = static_cast<float>(1.0 / FMath::Sqrt(1.0 + TanTheta * TanTheta));
	const float SinTheta = static_cast<float>(TanTheta) * CosTheta;
	OutVelocity = HorizontalDelta.GetSafeNormal() * (Speed * CosTheta)
		+ FVector::UpVector * (Speed * SinTheta);
	return !OutVelocity.ContainsNaN();
}

void ABombardment::BuildSchedule()
{
	ScheduledShots.Reset();
	const int32 BaseSeed = FMath::Rand();
	const int32 SafeProjectilesPerVolley = FMath::Max(1, ProjectilesPerVolley);
	const float SafeBurstDuration = FMath::Max(0.0f, BurstSpreadDuration);
	const float SafeVolleyInterval = FMath::Max(0.01f, VolleyInterval);
	for (int32 VolleyIndex = 0; VolleyIndex < FMath::Max(1, VolleyCount); ++VolleyIndex)
	{
		const TArray<FVector2D> Offsets = GenerateDiskOffsets(
			SafeProjectilesPerVolley,
			SkillRadius,
			BaseSeed + VolleyIndex * 7919,
			DistributionMode,
			DistributionJitter);
		for (int32 ShotIndex = 0; ShotIndex < Offsets.Num(); ++ShotIndex)
		{
			FScheduledShot& Shot = ScheduledShots.AddDefaulted_GetRef();
			const float ShotAlpha = SafeProjectilesPerVolley > 1
				? static_cast<float>(ShotIndex) / static_cast<float>(SafeProjectilesPerVolley - 1)
				: 0.0f;
			Shot.FireTime = static_cast<float>(VolleyIndex) * SafeVolleyInterval
				+ SafeBurstDuration * ShotAlpha;
			Shot.DiskOffset = Offsets[ShotIndex];
		}
	}

	ScheduledShots.Sort([](const FScheduledShot& A, const FScheduledShot& B)
	{
		return A.FireTime < B.FireTime;
	});
}

void ABombardment::FireShot(const FScheduledShot& Shot)
{
	if (!IsValid(SourceShip) || !ResolvedProjectileClass || !GetWorld())
	{
		return;
	}

	const FVector DiskOffset(Shot.DiskOffset.X, Shot.DiskOffset.Y, 0.0f);
	const FVector ImpactLocation = TargetCenter + DiskOffset;
	const FVector SpawnLocation = ImpactLocation
		+ FVector(LaunchXYOffset.X, LaunchXYOffset.Y, LaunchHeightZ);

	FVector LaunchVelocity;
	if (!SolveBallisticVelocity(
		SpawnLocation,
		ImpactLocation,
		ProjectileSpeed,
		GetWorld()->GetGravityZ(),
		LaunchVelocity))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Bombardment] No ballistic solution. Start=%s Target=%s Speed=%.1f"),
			*SpawnLocation.ToString(), *ImpactLocation.ToString(), ProjectileSpeed);
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = SourceShip;
	SpawnParams.Instigator = SkillInstigator;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* SpawnedActor = GetWorld()->SpawnActor<AActor>(
		ResolvedProjectileClass,
		SpawnLocation,
		LaunchVelocity.Rotation(),
		SpawnParams);
	if (ACannonball* Cannonball = Cast<ACannonball>(SpawnedActor))
	{
		Cannonball->InitializeProjectile(SourceShip, ProjectileDamage, ProjectileSpeed);
		Cannonball->SetDesignatedImpactLocation(ImpactLocation);
	}

#if !UE_SERVER
	if (bDrawDebug)
	{
		DrawDebugLine(GetWorld(), SpawnLocation, ImpactLocation, FColor::Cyan, false, 5.0f, 0, 2.0f);
		DrawDebugSphere(GetWorld(), ImpactLocation, 40.0f, 12, FColor::Blue, false, 5.0f);
	}
#endif
}
