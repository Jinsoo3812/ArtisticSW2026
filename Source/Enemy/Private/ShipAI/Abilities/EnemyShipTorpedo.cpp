#include "ShipAI/Abilities/EnemyShipTorpedo.h"

#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "Buoyancy/SWBuoyancyComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CollisionChannels.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GASCombatLibrary.h"
#include "GAS/SWCombatEffectContextLibrary.h"
#include "GASDamageInstantGameplayEffect.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Ship.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemyShipTorpedoVisual, Log, All);

AEnemyShipTorpedo::AEnemyShipTorpedo()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	DamageGEClass = UGASDamageInstantGameplayEffect::StaticClass();
	SWBuoyancyComponent = CreateDefaultSubobject<USWBuoyancyComponent>(TEXT("SWBuoyancyComponent"));
	SWBuoyancyComponent->ExecutionMode = ESWBuoyancyExecutionMode::ServerAuthority;
	SWBuoyancyComponent->ConfigureSinglePontoon(FloatingPontoonRadius);
	SWBuoyancyComponent->ForceSettings.DeepWaterBuoyancyMultiplier = 3.0f;
	FuseBurstComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FuseBurstComponent"));
	FuseBurstComponent->SetupAttachment(CannonballMesh, FuseSocketName);
	FuseBurstComponent->SetAutoActivate(false);
	FuseBurstComponent->SetAutoDestroy(false);
	ProjectileMovement->ProjectileGravityScale = 0.0f;
}

void AEnemyShipTorpedo::BeginPlay()
{
	Super::BeginPlay();
	LogVisualDiagnostics(TEXT("BeginPlay.BeforeRuntimeAssignments"));
	if (CannonballMesh)
	{
		// Fuse visibility is handled by Niagara. Preserve the authored SM_Bomba
		// surface exactly and never cover it with the legacy pulse overlay.
		CannonballMesh->SetOverlayMaterial(nullptr);
	}
	if (GetNetMode() != NM_DedicatedServer && FuseBurstComponent && FuseBurstSystem)
	{
		FuseBurstComponent->SetAsset(FuseBurstSystem);
		FuseBurstComponent->SetRelativeScale3D(FVector(FMath::Max(0.01f, FuseBurstScale)));
		FuseBurstComponent->AttachToComponent(
			CannonballMesh,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			FuseSocketName);
		RestartFuseBurst();
		GetWorldTimerManager().SetTimer(
			FuseBurstTimerHandle,
			this,
			&AEnemyShipTorpedo::RestartFuseBurst,
			FMath::Max(0.05f, FuseBurstIntervalSeconds),
			true);
	}
	LogVisualDiagnostics(TEXT("BeginPlay.AfterRuntimeAssignments"));
	GetWorldTimerManager().SetTimerForNextTick(
		this,
		&AEnemyShipTorpedo::LogPostBeginPlayVisualDiagnostics);
	if (SWBuoyancyComponent)
	{
		SWBuoyancyComponent->ConfigureSinglePontoon(FloatingPontoonRadius);
		SWBuoyancyComponent->Deactivate();
		SWBuoyancyComponent->SetComponentTickEnabled(false);
	}
}

void AEnemyShipTorpedo::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(FuseBurstTimerHandle);
	Super::EndPlay(EndPlayReason);
}

void AEnemyShipTorpedo::RestartFuseBurst()
{
	if (FuseBurstComponent && FuseBurstSystem && !IsActorBeingDestroyed())
	{
		FuseBurstComponent->ReinitializeSystem();
		if (!bHasLoggedFirstFuseActivation)
		{
			bHasLoggedFirstFuseActivation = true;
			LogVisualDiagnostics(TEXT("FirstFuseActivation"));
		}
	}
}

void AEnemyShipTorpedo::LogPostBeginPlayVisualDiagnostics()
{
	LogVisualDiagnostics(TEXT("NextTick.AfterBlueprintBeginPlay"));
}

void AEnemyShipTorpedo::LogVisualDiagnostics(const TCHAR* Phase) const
{
	const UStaticMesh* MeshAsset = CannonballMesh ? CannonballMesh->GetStaticMesh() : nullptr;
	FString ComponentMaterials;
	if (CannonballMesh)
	{
		for (int32 Index = 0; Index < CannonballMesh->GetNumMaterials(); ++Index)
		{
			ComponentMaterials += FString::Printf(
				TEXT("[%d]=%s "),
				Index,
				*GetPathNameSafe(CannonballMesh->GetMaterial(Index)));
		}
	}

	FString AssetMaterials;
	if (MeshAsset)
	{
		const TArray<FStaticMaterial>& StaticMaterials = MeshAsset->GetStaticMaterials();
		for (int32 Index = 0; Index < StaticMaterials.Num(); ++Index)
		{
			AssetMaterials += FString::Printf(
				TEXT("[%d]=%s(slot=%s) "),
				Index,
				*GetPathNameSafe(StaticMaterials[Index].MaterialInterface),
				*StaticMaterials[Index].MaterialSlotName.ToString());
		}
	}

	const UNiagaraSystem* ComponentNiagaraAsset = FuseBurstComponent
		? FuseBurstComponent->GetAsset()
		: nullptr;
	const USceneComponent* AttachParent = FuseBurstComponent
		? FuseBurstComponent->GetAttachParent()
		: nullptr;
	const TCHAR* NetModeName = GetNetMode() == NM_DedicatedServer ? TEXT("DedicatedServer")
		: GetNetMode() == NM_ListenServer ? TEXT("ListenServer")
		: GetNetMode() == NM_Client ? TEXT("Client")
		: TEXT("Standalone");

	UE_LOG(LogEnemyShipTorpedoVisual, Warning,
		TEXT("[TorpedoVisual][%s] Actor=%s Class=%s NetMode=%s Authority=%s "
			"ActorHidden=%s MeshComp=%s Registered=%s Visible=%s HiddenInGame=%s Mesh=%s "
			"ComponentMaterials={%s} AssetMaterials={%s} OverlayCurrent=%s OverlayConfigured=%s "
			"FuseComp=%s Registered=%s Active=%s AssetCurrent=%s AssetConfigured=%s "
			"AttachParent=%s AttachSocket=%s RelativeScale=%s SocketExists=%s"),
		Phase,
		*GetNameSafe(this),
		*GetPathNameSafe(GetClass()),
		NetModeName,
		HasAuthority() ? TEXT("true") : TEXT("false"),
		IsHidden() ? TEXT("true") : TEXT("false"),
		*GetNameSafe(CannonballMesh),
		CannonballMesh && CannonballMesh->IsRegistered() ? TEXT("true") : TEXT("false"),
		CannonballMesh && CannonballMesh->IsVisible() ? TEXT("true") : TEXT("false"),
		CannonballMesh && CannonballMesh->bHiddenInGame ? TEXT("true") : TEXT("false"),
		*GetPathNameSafe(MeshAsset),
		*ComponentMaterials,
		*AssetMaterials,
		*GetPathNameSafe(CannonballMesh ? CannonballMesh->GetOverlayMaterial() : nullptr),
		*GetPathNameSafe(PulseOverlayMaterial),
		*GetNameSafe(FuseBurstComponent),
		FuseBurstComponent && FuseBurstComponent->IsRegistered() ? TEXT("true") : TEXT("false"),
		FuseBurstComponent && FuseBurstComponent->IsActive() ? TEXT("true") : TEXT("false"),
		*GetPathNameSafe(ComponentNiagaraAsset),
		*GetPathNameSafe(FuseBurstSystem),
		*GetNameSafe(AttachParent),
		FuseBurstComponent ? *FuseBurstComponent->GetAttachSocketName().ToString() : TEXT("None"),
		FuseBurstComponent ? *FuseBurstComponent->GetRelativeScale3D().ToCompactString() : TEXT("None"),
		CannonballMesh && CannonballMesh->DoesSocketExist(FuseSocketName) ? TEXT("true") : TEXT("false"));
}

void AEnemyShipTorpedo::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bExplosionConsumed || IsActorBeingDestroyed())
	{
		return;
	}

	if (HasAuthority() && bIsFloating)
	{
		DetectDamageMeshContactAfterWater();
		if (bExplosionConsumed || IsActorBeingDestroyed())
		{
			return;
		}
	}

	if (!HasAuthority() && bIsFloating && bHasClientMovementTarget)
	{
		const float TimeSinceUpdate = GetWorld()
			? FMath::Max(0.0f, GetWorld()->GetTimeSeconds() - ClientMovementTargetReceiveTime)
			: 0.0f;
		const float ExtrapolationTime = FMath::Min(TimeSinceUpdate, ClientMaxExtrapolationTime);
		const FVector DesiredLocation =
			ClientMovementTargetLocation + ClientMovementTargetVelocity * ExtrapolationTime;

		// During the 0.5 s plunge, preserve the incoming projectile velocity exactly.
		// VInterpTo starts slowly when its initial error is small, which looked like a
		// one-frame pause at the ProjectileMovement -> replicated-water transition.
		if (!bBuoyancyEnabled)
		{
			SetActorLocationAndRotation(
				DesiredLocation,
				ClientMovementTargetRotation,
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
		}
		else if (FVector::DistSquared(GetActorLocation(), DesiredLocation)
			> FMath::Square(ClientNetworkSnapDistance))
		{
			SetActorLocationAndRotation(
				DesiredLocation,
				ClientMovementTargetRotation,
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
		}
		else
		{
			SetActorLocationAndRotation(
				FMath::VInterpTo(GetActorLocation(), DesiredLocation, DeltaSeconds, ClientLocationInterpSpeed),
				FMath::QInterpTo(GetActorQuat(), ClientMovementTargetRotation, DeltaSeconds, ClientRotationInterpSpeed),
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
		}
	}

	if (bWaterEntryObserved)
	{
		const float CurrentZ = GetActorLocation().Z;
		MinimumPostEntryZ = FMath::Min(MinimumPostEntryZ, CurrentZ);
		if (bBuoyancyEnabled)
		{
			MaximumPostBuoyancyZ = FMath::Max(MaximumPostBuoyancyZ, CurrentZ);
		}
	}

}

void AEnemyShipTorpedo::OnRep_ReplicatedMovement()
{
	if (!HasAuthority() && bIsFloating)
	{
		const FRepMovement& Movement = GetReplicatedMovement();
		ClientMovementTargetLocation = FRepMovement::RebaseOntoLocalOrigin(Movement.Location, this);
		ClientMovementTargetRotation = Movement.Rotation.Quaternion();
		ClientMovementTargetVelocity = Movement.LinearVelocity;
		ClientMovementTargetReceiveTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		bHasClientMovementTarget = true;

		if (SphereCollision)
		{
			if (SphereCollision->IsSimulatingPhysics())
			{
				SphereCollision->SetSimulatePhysics(false);
			}
			SphereCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		}
		SetActorTickEnabled(true);
		return;
	}

	Super::OnRep_ReplicatedMovement();
}

void AEnemyShipTorpedo::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEnemyShipTorpedo, bIsFloating);
	DOREPLIFETIME(AEnemyShipTorpedo, bBuoyancyEnabled);
}

void AEnemyShipTorpedo::InitializeTorpedo(
	AShip* InLaunchingShip,
	AShip* InDesignatedTarget,
	float InSnapshotDamage,
	float InSpeed,
	float InMaximumFlightSeconds)
{
	DesignatedTarget = InDesignatedTarget;
	InitializeProjectile(InLaunchingShip, FMath::Max(0.0f, InSnapshotDamage), InSpeed);
	SetLifeSpan(FMath::Max(0.1f, InMaximumFlightSeconds));
	LogVisualDiagnostics(TEXT("InitializeTorpedo.AfterInitializeProjectile"));
}

void AEnemyShipTorpedo::HandleShipHit(AShip* HitShip)
{
	AShip* SourceShip = GetLaunchingShip();
	if (bExplosionConsumed || !HasAuthority() || !SourceShip || !HitShip
		|| HitShip->IsEnemyShipForEffects()
		|| !HitShip->ActorHasTag(TEXT("Player"))
		|| HitShip->ActorHasTag(TEXT("Enemy")))
	{
		return;
	}
	bExplosionConsumed = true;

	UAbilitySystemComponent* SourceASC = SourceShip->GetAbilitySystemComponent();
	UAbilitySystemComponent* TargetASC = HitShip->GetAbilitySystemComponent();
	if (SourceASC && TargetASC && DamageGEClass && DamageAmount > 0.0f)
	{
		const FGameplayEffectSpecHandle DamageSpec = UGASCombatLibrary::MakeDamageEffectSpec(
			SourceASC,
			DamageGEClass,
			DamageAmount,
			SourceShip,
			this);
		if (DamageSpec.IsValid() && DamageSpec.Data.IsValid())
		{
			FGameplayEffectSpec TargetSpec(*DamageSpec.Data.Get());
			USWCombatEffectContextLibrary::EnrichCombatEffectSpec(
				TargetSpec, SourceShip, this, HitShip, nullptr, GetVelocity());
			TargetASC->ApplyGameplayEffectSpecToSelf(TargetSpec);
			const float CurrentHealth = TargetASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute());
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[EnemyShipTorpedo] 폭발 Target=%s Damage=%.2f CurrentHealth=%.2f"),
				*GetNameSafe(HitShip),
				DamageAmount,
				CurrentHealth);
		}
	}

	MulticastTorpedoExploded(GetActorLocation());
	Destroy();
}

void AEnemyShipTorpedo::HandleWaterOverlap(
	AActor* WaterActor,
	UPrimitiveComponent* WaterComponent,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	TriggerWaterRipple(GetActorLocation());
}

void AEnemyShipTorpedo::TriggerWaterRipple(const FVector& HitLocation)
{
	MarkWaterHitHandledWithoutDeactivation();
	if (!bWaterEntryObserved)
	{
		bWaterEntryObserved = true;
		WaterEntryZ = GetActorLocation().Z;
		MinimumPostEntryZ = WaterEntryZ;
	}
	if (HasAuthority() && !bIsFloating)
	{
		bIsFloating = true;
		ForceNetUpdate();
	}
	ApplyWaterEntryPhysicsState();

	if (HasAuthority())
	{
		const float Delay = FMath::Max(0.0f, BuoyancyActivationDelaySeconds);
		if (Delay <= KINDA_SMALL_NUMBER)
		{
			EnableBuoyancyAfterDelay();
		}
		else
		{
			GetWorldTimerManager().SetTimer(
				BuoyancyActivationTimerHandle,
				this,
				&AEnemyShipTorpedo::EnableBuoyancyAfterDelay,
				Delay,
				false);
		}
	}
}

void AEnemyShipTorpedo::OnRep_IsFloating()
{
	if (bIsFloating)
	{
		if (!bWaterEntryObserved)
		{
			bWaterEntryObserved = true;
			WaterEntryZ = GetActorLocation().Z;
			MinimumPostEntryZ = WaterEntryZ;
		}
		if (!HasAuthority())
		{
			// Seed the water-phase extrapolator from the locally continuous flight state.
			// The next RepMovement packet will replace this with the server target.
			ClientMovementTargetLocation = GetActorLocation();
			ClientMovementTargetRotation = GetActorQuat();
			ClientMovementTargetVelocity = ProjectileMovement
				? ProjectileMovement->Velocity
				: GetReplicatedMovement().LinearVelocity;
			ClientMovementTargetReceiveTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
			bHasClientMovementTarget = true;
		}
		MarkWaterHitHandledWithoutDeactivation();
		ApplyWaterEntryPhysicsState();
	}
}

void AEnemyShipTorpedo::ApplyWaterEntryPhysicsState()
{
	FVector EntryVelocity = FVector::ZeroVector;
	if (ProjectileMovement)
	{
		EntryVelocity = ProjectileMovement->Velocity;
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
	}

	if (SphereCollision)
	{
		PreviousWaterPhysicsLocation = SphereCollision->GetComponentLocation();
		SphereCollision->SetLinearDamping(FloatingLinearDamping);
		SphereCollision->SetAngularDamping(FloatingAngularDamping);
		if (HasAuthority())
		{
			SphereCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			SphereCollision->SetMassOverrideInKg(NAME_None, FloatingMassKg, true);
			SphereCollision->SetSimulatePhysics(true);
			SphereCollision->SetPhysicsLinearVelocity(EntryVelocity);
			SphereCollision->WakeAllRigidBodies();
		}
		else
		{
			SphereCollision->SetSimulatePhysics(false);
			SphereCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		}
	}

	if (SWBuoyancyComponent)
	{
		SWBuoyancyComponent->Deactivate();
		SWBuoyancyComponent->SetComponentTickEnabled(false);
	}
}

void AEnemyShipTorpedo::DetectDamageMeshContactAfterWater()
{
	UWorld* World = GetWorld();
	AShip* Target = DesignatedTarget.Get();
	if (!World || !SphereCollision || !IsValid(Target) || !Target->ShipDamageMesh)
	{
		return;
	}

	const FVector CurrentLocation = SphereCollision->GetComponentLocation();
	const FVector SweepStartLocation = PreviousWaterPhysicsLocation;
	const float Radius = FMath::Max(1.0f, SphereCollision->GetScaledSphereRadius());
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_ShipDamage);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EnemyShipTorpedoDamageMeshSweep), false, this);
	if (AShip* SourceShip = GetLaunchingShip())
	{
		QueryParams.AddIgnoredActor(SourceShip);
	}

	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(
		Hits,
		SweepStartLocation,
		CurrentLocation,
		FQuat::Identity,
		ObjectQueryParams,
		FCollisionShape::MakeSphere(Radius),
		QueryParams);
	PreviousWaterPhysicsLocation = CurrentLocation;

	for (const FHitResult& Hit : Hits)
	{
		if (Hit.GetActor() != Target || Hit.GetComponent() != Target->ShipDamageMesh)
		{
			continue;
		}

		HandleShipHit(Target);
		return;
	}
}

void AEnemyShipTorpedo::EnableBuoyancyAfterDelay()
{
	if (!HasAuthority() || !bIsFloating || bExplosionConsumed || !SWBuoyancyComponent)
	{
		return;
	}

	SWBuoyancyComponent->Activate();
	SWBuoyancyComponent->SetComponentTickEnabled(true);
	bBuoyancyEnabled = true;
	ForceNetUpdate();
	MaximumPostBuoyancyZ = GetActorLocation().Z;
}

void AEnemyShipTorpedo::MulticastTorpedoExploded_Implementation(const FVector& ExplosionLocation)
{
	K2_OnTorpedoExploded(ExplosionLocation);
}
