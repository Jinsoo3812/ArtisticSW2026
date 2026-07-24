#include "Bombardment.h"

#include "BaseCharacter.h"
#include "Cannonball.h"
#include "Components/DecalComponent.h"
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

	PreviewDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("PreviewDecal"));
	PreviewDecal->SetupAttachment(PreviewRoot);
	PreviewDecal->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
	PreviewDecal->DecalSize = FVector(DecalProjectionDepth, 100.0f, 100.0f);
	PreviewDecal->SetVisibility(false);
	PreviewDecal->SetHiddenInGame(true);
	PreviewDecal->SortOrder = 10;
}

void ABombardmentPreview::BeginPlay()
{
	Super::BeginPlay();
	AuthoredPreviewMaterial = PreviewMesh ? PreviewMesh->GetMaterial(0) : nullptr;
	AuthoredPreviewDecalMaterial = PreviewDecal ? PreviewDecal->GetDecalMaterial() : nullptr;

	UE_LOG(LogTemp, Warning,
		TEXT("[BombardmentPreview] BeginPlay Actor=%s Class=%s MeshComponent=%s StaticMesh=%s Material0=%s Overlay=%s "
			"HiddenActor=%s ComponentVisible=%s Registered=%s Location=%s Scale=%s"),
		*GetNameSafe(this),
		*GetPathNameSafe(GetClass()),
		*GetNameSafe(PreviewMesh),
		PreviewMesh ? *GetPathNameSafe(PreviewMesh->GetStaticMesh()) : TEXT("None"),
		PreviewMesh ? *GetPathNameSafe(PreviewMesh->GetMaterial(0)) : TEXT("None"),
		*GetPathNameSafe(TargetHighlightOverlayMaterial),
		IsHidden() ? TEXT("true") : TEXT("false"),
		PreviewMesh && PreviewMesh->IsVisible() ? TEXT("true") : TEXT("false"),
		PreviewMesh && PreviewMesh->IsRegistered() ? TEXT("true") : TEXT("false"),
		*GetActorLocation().ToCompactString(),
		PreviewMesh ? *PreviewMesh->GetRelativeScale3D().ToCompactString() : TEXT("None"));
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
	if (bUseDecalPreview)
	{
		if (PreviewMesh)
		{
			PreviewMesh->SetVisibility(false);
			PreviewMesh->SetHiddenInGame(true);
		}
		if (PreviewDecal)
		{
			PreviewDecal->DecalSize = FVector(
				FMath::Max(1.0f, DecalProjectionDepth),
				SkillRadius,
				SkillRadius);
			PreviewDecal->SetVisibility(true);
			PreviewDecal->SetHiddenInGame(false);
		}
		return;
	}

	if (PreviewDecal)
	{
		PreviewDecal->SetVisibility(false);
		PreviewDecal->SetHiddenInGame(true);
	}
	if (PreviewMesh)
	{
		PreviewMesh->SetVisibility(true);
		PreviewMesh->SetHiddenInGame(false);
	}

	if (!PreviewMesh || !PreviewMesh->GetStaticMesh())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BombardmentPreview] Configure FAILED Actor=%s Class=%s PreviewMesh=%s StaticMesh=%s Radius=%.1f"),
			*GetNameSafe(this),
			*GetPathNameSafe(GetClass()),
			*GetNameSafe(PreviewMesh),
			PreviewMesh ? *GetPathNameSafe(PreviewMesh->GetStaticMesh()) : TEXT("None"),
			SkillRadius);
		return;
	}

	const FBoxSphereBounds MeshBounds = PreviewMesh->GetStaticMesh()->GetBounds();
	const float AuthoredRadius = FMath::Max(MeshBounds.BoxExtent.X, MeshBounds.BoxExtent.Y);
	float ZScale = 1.0f;
	if (MeshBounds.BoxExtent.Z > UE_SMALL_NUMBER)
	{
		ZScale = FMath::Max(0.01f, PreviewWorldThickness) / (2.0f * MeshBounds.BoxExtent.Z);
	}
	if (AuthoredRadius > UE_SMALL_NUMBER)
	{
		const float UniformXYScale = SkillRadius / AuthoredRadius;
		PreviewMesh->SetRelativeScale3D(FVector(UniformXYScale, UniformXYScale, ZScale));
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[BombardmentPreview] Configure OK Actor=%s StaticMesh=%s Radius=%.1f AuthoredRadius=%.2f "
			"AuthoredThickness=%.2f WorldThickness=%.2f BoundsExtent=%s RelativeScale=%s Material0=%s"),
		*GetNameSafe(this),
		*GetPathNameSafe(PreviewMesh->GetStaticMesh()),
		SkillRadius,
		AuthoredRadius,
		MeshBounds.BoxExtent.Z * 2.0f,
		FMath::Max(0.01f, PreviewWorldThickness),
		*MeshBounds.BoxExtent.ToCompactString(),
		*PreviewMesh->GetRelativeScale3D().ToCompactString(),
		*GetPathNameSafe(PreviewMesh->GetMaterial(0)));
}

void ABombardmentPreview::SetPreviewValid(bool bInValid)
{
	const bool bValidityChanged = bPreviewValid != bInValid;
	bPreviewValid = bInValid;
	if (bUseDecalPreview)
	{
		if (!PreviewDecal)
		{
			return;
		}

		UMaterialInterface* DesiredDecalMaterial = bPreviewValid
			? (ValidPreviewDecalMaterial ? ValidPreviewDecalMaterial.Get() : AuthoredPreviewDecalMaterial.Get())
			: (InvalidPreviewDecalMaterial ? InvalidPreviewDecalMaterial.Get() : AuthoredPreviewDecalMaterial.Get());
		if (DesiredDecalMaterial)
		{
			PreviewDecal->SetDecalMaterial(DesiredDecalMaterial);
		}
		return;
	}

	if (!PreviewMesh)
	{
		if (!bHasLoggedPreviewValidity || bValidityChanged)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[BombardmentPreview] Target update Actor=%s Valid=%s Location=%s PreviewMesh=None"),
				*GetNameSafe(this),
				bPreviewValid ? TEXT("true") : TEXT("false"),
				*GetActorLocation().ToCompactString());
			bHasLoggedPreviewValidity = true;
		}
		return;
	}

	UMaterialInterface* DesiredMaterial = bPreviewValid
		? (ValidPreviewMaterial ? ValidPreviewMaterial.Get() : AuthoredPreviewMaterial.Get())
		: (InvalidPreviewMaterial ? InvalidPreviewMaterial.Get() : AuthoredPreviewMaterial.Get());
	if (DesiredMaterial)
	{
		PreviewMesh->SetMaterial(0, DesiredMaterial);
	}

	if (!bHasLoggedPreviewValidity || bValidityChanged)
	{
		const FBoxSphereBounds WorldBounds = PreviewMesh->Bounds;
		UE_LOG(LogTemp, Warning,
			TEXT("[BombardmentPreview] Target update Actor=%s Valid=%s Location=%s StaticMesh=%s Material0=%s "
				"Visible=%s HiddenInGame=%s WorldBoundsOrigin=%s WorldBoundsExtent=%s"),
			*GetNameSafe(this),
			bPreviewValid ? TEXT("true") : TEXT("false"),
			*GetActorLocation().ToCompactString(),
			*GetPathNameSafe(PreviewMesh->GetStaticMesh()),
			*GetPathNameSafe(PreviewMesh->GetMaterial(0)),
			PreviewMesh->IsVisible() ? TEXT("true") : TEXT("false"),
			PreviewMesh->bHiddenInGame ? TEXT("true") : TEXT("false"),
			*WorldBounds.Origin.ToCompactString(),
			*WorldBounds.BoxExtent.ToCompactString());
		bHasLoggedPreviewValidity = true;
	}
}

void ABombardmentPreview::RefreshHighlightedTargets()
{
	RestoreHighlights();
	if (!bPreviewValid || SkillRadius <= 0.0f || (!TargetHighlightOverlayMaterial && !bUseCustomDepthHighlight))
	{
		if (!bHasLoggedHighlightDisabledReason)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[BombardmentPreview] Highlight inactive Actor=%s Valid=%s Radius=%.1f Overlay=%s UseCustomDepth=%s"),
				*GetNameSafe(this),
				bPreviewValid ? TEXT("true") : TEXT("false"),
				SkillRadius,
				*GetPathNameSafe(TargetHighlightOverlayMaterial),
				bUseCustomDepthHighlight ? TEXT("true") : TEXT("false"));
			bHasLoggedHighlightDisabledReason = true;
		}
		return;
	}
	bHasLoggedHighlightDisabledReason = false;

	const float RadiusSquared = FMath::Square(SkillRadius);
	int32 EnemyShipCount = 0;
	int32 InRangeEnemyShipCount = 0;
	for (TActorIterator<AShip> It(GetWorld()); It; ++It)
	{
		AShip* Ship = *It;
		if (!IsValid(Ship) || !Ship->IsEnemyShipForEffects())
		{
			continue;
		}
		++EnemyShipCount;

		FVector Delta = Ship->GetActorLocation() - GetActorLocation();
		Delta.Z = 0.0f;
		if (Delta.SizeSquared() <= RadiusSquared)
		{
			++InRangeEnemyShipCount;
			HighlightActor(Ship);
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
			HighlightActor(Character);
		}
	}

	if (EnemyShipCount != LastLoggedEnemyShipCount
		|| InRangeEnemyShipCount != LastLoggedInRangeEnemyShipCount
		|| HighlightedMeshes.Num() != LastLoggedHighlightedMeshCount)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BombardmentPreview] Highlight scan Actor=%s EnemyShips=%d InRangeEnemyShips=%d HighlightedMeshes=%d "
				"Radius=%.1f Overlay=%s CustomDepth=%s"),
			*GetNameSafe(this),
			EnemyShipCount,
			InRangeEnemyShipCount,
			HighlightedMeshes.Num(),
			SkillRadius,
			*GetPathNameSafe(TargetHighlightOverlayMaterial),
			bUseCustomDepthHighlight ? TEXT("true") : TEXT("false"));
		LastLoggedEnemyShipCount = EnemyShipCount;
		LastLoggedInRangeEnemyShipCount = InRangeEnemyShipCount;
		LastLoggedHighlightedMeshCount = HighlightedMeshes.Num();
	}
}

void ABombardmentPreview::HighlightActor(AActor* Actor)
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

		FBombardmentMeshHighlightState& State = HighlightedMeshes.AddDefaulted_GetRef();
		State.Mesh = Mesh;
		State.OverlayMaterial = Mesh->GetOverlayMaterial();
		State.bRenderedCustomDepth = Mesh->bRenderCustomDepth;
		State.CustomDepthStencilValue = Mesh->CustomDepthStencilValue;
		UMaterialInterface* PreviousOverlay = Mesh->GetOverlayMaterial();
		const int32 MaterialCount = Mesh->GetNumMaterials();
		State.Materials.Reserve(MaterialCount);
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			State.Materials.Add(Mesh->GetMaterial(MaterialIndex));
		}

		if (TargetHighlightOverlayMaterial)
		{
			if (bReplaceTargetMaterials)
			{
				for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
				{
					Mesh->SetMaterial(MaterialIndex, TargetHighlightOverlayMaterial);
				}
				State.bMaterialsReplaced = true;
			}
			else
			{
				Mesh->SetOverlayMaterial(TargetHighlightOverlayMaterial);
			}
		}
		if (bUseCustomDepthHighlight)
		{
			Mesh->SetRenderCustomDepth(true);
			Mesh->SetCustomDepthStencilValue(FMath::Clamp(TargetHighlightStencilValue, 0, 255));
		}

		const FString MeshPath = Mesh->GetPathName();
		if (!LoggedHighlightMeshPaths.Contains(MeshPath))
		{
			LoggedHighlightMeshPaths.Add(MeshPath);
			UE_LOG(LogTemp, Warning,
				TEXT("[BombardmentPreview] Highlight mesh Actor=%s ActorClass=%s ActorHidden=%s Mesh=%s MeshClass=%s "
					"Visible=%s HiddenInGame=%s RecentlyRendered=%s NumMaterials=%d Material0Before=%s Material0After=%s "
					"Mode=%s OverlayBefore=%s OverlayAfter=%s RenderCustomDepth=%s Stencil=%d BoundsOrigin=%s BoundsExtent=%s"),
				*GetNameSafe(Actor),
				*GetPathNameSafe(Actor->GetClass()),
				Actor->IsHidden() ? TEXT("true") : TEXT("false"),
				*MeshPath,
				*GetPathNameSafe(Mesh->GetClass()),
				Mesh->IsVisible() ? TEXT("true") : TEXT("false"),
				Mesh->bHiddenInGame ? TEXT("true") : TEXT("false"),
				Mesh->WasRecentlyRendered(0.5f) ? TEXT("true") : TEXT("false"),
				MaterialCount,
				MaterialCount > 0 ? *GetPathNameSafe(State.Materials[0].Get()) : TEXT("None"),
				*GetPathNameSafe(Mesh->GetMaterial(0)),
				bReplaceTargetMaterials ? TEXT("ReplaceMaterials") : TEXT("Overlay"),
				*GetPathNameSafe(PreviousOverlay),
				*GetPathNameSafe(Mesh->GetOverlayMaterial()),
				Mesh->bRenderCustomDepth ? TEXT("true") : TEXT("false"),
				Mesh->CustomDepthStencilValue,
				*Mesh->Bounds.Origin.ToCompactString(),
				*Mesh->Bounds.BoxExtent.ToCompactString());
		}
	}
}

void ABombardmentPreview::RestoreHighlights()
{
	for (const FBombardmentMeshHighlightState& State : HighlightedMeshes)
	{
		if (UMeshComponent* Mesh = State.Mesh.Get())
		{
			if (State.bMaterialsReplaced)
			{
				for (int32 MaterialIndex = 0; MaterialIndex < State.Materials.Num(); ++MaterialIndex)
				{
					Mesh->SetMaterial(MaterialIndex, State.Materials[MaterialIndex].Get());
				}
			}
			Mesh->SetOverlayMaterial(State.OverlayMaterial.Get());
			Mesh->SetRenderCustomDepth(State.bRenderedCustomDepth);
			Mesh->SetCustomDepthStencilValue(State.CustomDepthStencilValue);
		}
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
