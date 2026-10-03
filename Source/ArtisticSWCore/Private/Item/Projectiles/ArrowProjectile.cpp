#include "Item/Projectiles/ArrowProjectile.h"
#include "Components/CombatHurtboxComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GAS/CombatHitResolver.h"
#include "BaseGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Character.h"
#include "GAS/SWCombatEffectContextLibrary.h"
#include "Item/Projectiles/ArrowImpactVisual.h"
#include "StatusEffectLibrary.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Movement/MovementFrameVelocity.h"
#include "Movement/MovementFrameVelocityProvider.h"
#include "Item/Projectiles/ArrowProjectileMovementComponent.h"
#include "Item/Projectiles/ProjectileShotPreparation.h"
#include "Item/Projectiles/ProjectileLaunchInitialization.h"

namespace
{
	TAutoConsoleVariable<int32> CVarProjectileDebugLaunch(TEXT("sw.Projectile.DebugLaunch"), 0,
		TEXT("Log shot policy/timing; shooter velocity (cyan), world launch (yellow), character box (green), obstacle box (orange)."), ECVF_Cheat);

#include "Room/SWRoomSnapshotComponent.h"
#include "GameplayEffect.h"
#include "AbilitySystemGlobals.h"

namespace
{
	FSWRoomGameplayEffectState CaptureArrowSpec(const FGameplayEffectSpecHandle& Handle)
	{
		FSWRoomGameplayEffectState State;
		if (!Handle.IsValid() || !Handle.Data.IsValid() || !Handle.Data->Def) return State;
		const FGameplayEffectSpec& Spec = *Handle.Data.Get();
		State.EffectClass = FSoftClassPath(Spec.Def->GetClass());
		State.Level = Spec.GetLevel();
		if (const FGameplayTagContainer* Tags = Spec.CapturedSourceTags.GetAggregatedTags())
			State.CapturedSourceTags = *Tags;
		for (const TPair<FGameplayTag, float>& Entry : Spec.SetByCallerTagMagnitudes)
		{
			FSWRoomSetByCallerTagValue& Value = State.TagMagnitudes.AddDefaulted_GetRef();
			Value.Tag = Entry.Key;
			Value.Value = Entry.Value;
		}
		for (const TPair<FName, float>& Entry : Spec.SetByCallerNameMagnitudes)
		{
			FSWRoomSetByCallerNameValue& Value = State.NameMagnitudes.AddDefaulted_GetRef();
			Value.Name = Entry.Key;
			Value.Value = Entry.Value;
		}
		return State;
	}

	bool RestoreArrowSpec(const FSWRoomGameplayEffectState& State, AActor* Source, AArrowProjectile* Arrow,
		FGameplayEffectSpecHandle& OutSpec, FString& OutError)
	{
		OutSpec = FGameplayEffectSpecHandle();
		if (State.EffectClass.IsNull()) return true;
		UClass* EffectClass = State.EffectClass.TryLoadClass<UGameplayEffect>();
		if (!EffectClass || !FMath::IsFinite(State.Level) || State.Level <= 0.f)
		{
			OutError = FString::Printf(TEXT("Arrow effect invalid or missing: %s"), *State.EffectClass.ToString());
			return false;
		}
		FGameplayEffectContextHandle Context(UAbilitySystemGlobals::Get().AllocGameplayEffectContext());
		Context.AddInstigator(Source, Arrow);
		OutSpec = FGameplayEffectSpecHandle(new FGameplayEffectSpec(EffectClass->GetDefaultObject<UGameplayEffect>(), Context, State.Level));
		OutSpec.Data->CapturedSourceTags.GetSpecTags().AppendTags(State.CapturedSourceTags);
		for (const FSWRoomSetByCallerTagValue& Value : State.TagMagnitudes)
			OutSpec.Data->SetSetByCallerMagnitude(Value.Tag, Value.Value);
		for (const FSWRoomSetByCallerNameValue& Value : State.NameMagnitudes)
			OutSpec.Data->SetSetByCallerMagnitude(Value.Name, Value.Value);
		return true;
	}

	FString GetHitMeshPath(const UPrimitiveComponent* HitComponent)
	{
		if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(HitComponent))
		{
			return GetPathNameSafe(StaticMeshComponent->GetStaticMesh());
		}

		if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(HitComponent))
		{
			return GetPathNameSafe(SkeletalMeshComponent->GetSkeletalMeshAsset());
		}

		return TEXT("None");
	}
}

AArrowProjectile::AArrowProjectile(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UArrowProjectileMovementComponent>(TEXT("ProjectileComp")))
{
	CreateDefaultSubobject<USWRoomSnapshotComponent>(TEXT("RoomSnapshot"));
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	ObstacleCollisionComp = CreateDefaultSubobject<UBoxComponent>(TEXT("ObstacleCollisionComp"));
	ObstacleCollisionComp->SetupAttachment(RootComponent);
	ObstacleCollisionComp->SetCanEverAffectNavigation(false);
	if (CollisionComp)
	{
		ApplyCollisionShape();
		ApplyArrowCollisionProfile();
		CollisionComp->SetNotifyRigidBodyCollision(false);
	}

	if (ProjectileMovementComp)
	{
		ProjectileMovementComp->bAutoActivate = false;
		ProjectileMovementComp->InitialSpeed = 0.0f;
		ProjectileMovementComp->MaxSpeed = 0.0f;
		ProjectileMovementComp->ProjectileGravityScale = FlightGravityScale;
		ProjectileMovementComp->bInitialVelocityInLocalSpace = false;
		ProjectileMovementComp->bRotationFollowsVelocity = true;
		ProjectileMovementComp->bShouldBounce = false;
	}
}

void AArrowProjectile::CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const
{
	FSWRoomArrowState State;
	State.FlightGravityScale = ProjectileMovementComp ? ProjectileMovementComp->ProjectileGravityScale : FlightGravityScale;
	State.bImpactHandled = bImpactHandled;
	State.bDestroyOnImpact = bDestroyOnImpact;
	State.bEnableTeamDamageFiltering = bEnableTeamDamageFiltering;
	AActor* Source = InstigatorActor ? InstigatorActor.Get() : GetOwner();
	if (Source)
	{
		State.SourceActorClass = FSoftClassPath(Source->GetClass());
		if (const USWRoomSnapshotComponent* Id = Source->FindComponentByClass<USWRoomSnapshotComponent>())
			State.SourceActorId = Id->StableId;
	}
	State.DirectDamageSpec = CaptureArrowSpec(DirectDamageSpec);
	if (const UCombatHitResolver* Resolver = FindComponentByClass<UCombatHitResolver>())
		State.ResolverClass = FSoftClassPath(Resolver->GetClass());
	if (State.DirectDamageSpec.EffectClass.IsNull())
	{
		FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("Projectile");
		Issue.FieldKey = TEXT("DirectDamageSpec");
		Issue.Reason = TEXT("Arrow direct damage spec is unavailable");
	}
	for (const FGameplayEffectSpecHandle& Spec : StatusEffectSpecHandles)
		State.StatusEffectSpecs.Add(CaptureArrowSpec(Spec));
	State.RefreshGrantedTags = StatusEffectRefreshGrantedTags;
	for (const TWeakObjectPtr<AActor>& Ignored : MovementIgnoredActors)
	{
		if (!Ignored.IsValid()) continue;
		if (const USWRoomSnapshotComponent* Id = Ignored->FindComponentByClass<USWRoomSnapshotComponent>(); Id && Id->StableId.IsValid())
			State.IgnoredActorIds.AddUnique(Id->StableId);
		else
		{
			FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
			Issue.Domain = TEXT("Projectile");
			Issue.FieldKey = FName(*(TEXT("IgnoredActor:") + Ignored->GetPathName()));
			Issue.Reason = TEXT("Ignored actor has no stable ID");
		}
	}
	FSWRoomDomainPart& Part = OutParts.AddDefaulted_GetRef();
	Part.Domain = ESWRoomDomain::Projectile;
	Part.Version = 1;
	if (!FSWRoomStructCodec::Write(State, Part.Bytes))
	{
		OutParts.Pop();
		FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("Projectile");
		Issue.FieldKey = TEXT("ArrowState");
		Issue.Reason = TEXT("Arrow state serialization failed");
	}
}

bool AArrowProjectile::RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError)
{
	FSWRoomArrowState State;
	if (Part.Domain != ESWRoomDomain::Projectile || Part.Version != 1 || !FSWRoomStructCodec::Read(Part.Bytes, State)
		|| !FMath::IsFinite(State.FlightGravityScale) || State.FlightGravityScale < 0.f
		|| State.StatusEffectSpecs.Num() != State.RefreshGrantedTags.Num())
	{
		OutError = TEXT("Invalid arrow state");
		return false;
	}
	bImpactHandled = State.bImpactHandled;
	bDestroyOnImpact = State.bDestroyOnImpact;
	bEnableTeamDamageFiltering = State.bEnableTeamDamageFiltering;
	FlightGravityScale = State.FlightGravityScale;
	if (ProjectileMovementComp) ProjectileMovementComp->ProjectileGravityScale = FlightGravityScale;
	PendingRoomState = MoveTemp(State);
	bHasPendingRoomState = true;
	return true;
}

bool AArrowProjectile::FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError)
{
	if (!bHasPendingRoomState) return true;
	bHasPendingRoomState = false;
	AActor* Source = nullptr;
	if (AActor* const* Found = RegisteredActors.Find(PendingRoomState.SourceActorId)) Source = *Found;
	InstigatorActor = Source;
	SourceASC = Source ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Source) : nullptr;
	if (Source)
	{
		SetOwner(Source);
		SetInstigator(Cast<APawn>(Source));
	}
	for (const FGuid& Id : PendingRoomState.IgnoredActorIds)
		if (AActor* const* Found = RegisteredActors.Find(Id)) IgnoreActorForMovement(*Found);
	if (!RestoreArrowSpec(PendingRoomState.DirectDamageSpec, Source, this, DirectDamageSpec, OutError)) return false;
	StatusEffectSpecHandles.Reset();
	StatusEffectRefreshGrantedTags = PendingRoomState.RefreshGrantedTags;
	for (const FSWRoomGameplayEffectState& Saved : PendingRoomState.StatusEffectSpecs)
	{
		FGameplayEffectSpecHandle Restored;
		if (!RestoreArrowSpec(Saved, Source, this, Restored, OutError)) return false;
		StatusEffectSpecHandles.Add(MoveTemp(Restored));
	}
	if (DirectDamageSpec.IsValid() && !bImpactHandled)
	{
		UCombatHitResolver* Resolver = FindComponentByClass<UCombatHitResolver>();
		if (!Resolver && !PendingRoomState.ResolverClass.IsNull())
		{
			UClass* ResolverClass = PendingRoomState.ResolverClass.TryLoadClass<UCombatHitResolver>();
			if (ResolverClass)
			{
				Resolver = NewObject<UCombatHitResolver>(this, ResolverClass);
				AddInstanceComponent(Resolver);
				Resolver->RegisterComponent();
			}
		}
		if (!Resolver || !Resolver->OpenWindow(DirectDamageSpec))
		{
			OutError = TEXT("Arrow hit resolver did not reopen");
			return false;
		}
	}
	return true;
}

void AArrowProjectile::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyCollisionShape();
	ApplyArrowCollisionProfile();
}

void AArrowProjectile::ApplyCollisionShape()
{
	if (!CollisionComp)
	{
		return;
	}

	// Collision 크기는 Box Extent만 사용하고 자식 Mesh에 전달되는 Root Scale은 제거한다.
	CollisionComp->SetRelativeScale3D(FVector::OneVector);
	CollisionComp->SetBoxExtent(GetCollisionHalfExtent(), false);
	if (ObstacleCollisionComp)
	{
		ObstacleCollisionComp->SetRelativeTransform(FTransform::Identity);
		ObstacleCollisionComp->SetBoxExtent(GetObstacleCollisionHalfExtent(), false);
	}
}

void AArrowProjectile::ApplyArrowCollisionProfile()
{
	if (!CollisionComp)
	{
		return;
	}

	// Both shapes are swept explicitly by ArrowCollisionQuery. No overlap/hit event
	// path may race that result, and BoxComp must not block the world at its large size.
	CollisionComp->SetCollisionProfileName(TEXT("ArrowProjectile"), true);
	CollisionComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollisionComp->SetGenerateOverlapEvents(false);
	if (ObstacleCollisionComp)
	{
		ObstacleCollisionComp->SetCollisionProfileName(TEXT("ArrowObstacle"), true);
		ObstacleCollisionComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ObstacleCollisionComp->SetGenerateOverlapEvents(false);
	}
}

void AArrowProjectile::BeginPlay()
{
	Super::BeginPlay();
	ApplyCollisionShape();
	ApplyArrowCollisionProfile();

	if (APawn* InstigatorPawn = GetInstigator())
	{
		IgnoreActorForMovement(InstigatorPawn);
	}

	if (AActor* OwnerActor = GetOwner())
	{
		IgnoreActorForMovement(OwnerActor);
	}
}

void AArrowProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (auto* Resolver = FindComponentByClass<UCombatHitResolver>()) Resolver->CloseWindow();
	if (CollisionComp)
	{
		for (const TWeakObjectPtr<AActor>& IgnoredActorPtr : MovementIgnoredActors)
		{
			if (AActor* IgnoredActor = IgnoredActorPtr.Get())
			{
				CollisionComp->IgnoreActorWhenMoving(IgnoredActor, false);
			}
		}
	}

	MovementIgnoredActors.Reset();

	Super::EndPlay(EndPlayReason);
}

void AArrowProjectile::LaunchArrow(const FVector& LaunchVelocity)
{
	if (!HasAuthority() || !DirectDamageSpec.IsValid() || LaunchVelocity.ContainsNaN()
		|| !FMath::IsFinite(GetFlightGravityZ()) || !ProjectileMovementComp || !CollisionComp) return;
	bImpactHandled = false;
	ApplyCollisionShape();
	ApplyArrowCollisionProfile();
	CollisionComp->SetSimulatePhysics(false);
	ProjectileMovementComp->SetUpdatedComponent(CollisionComp);
	ProjectileMovementComp->ProjectileGravityScale = FlightGravityScale;
	ProjectileMovementComp->bSimulationEnabled = true;
	// Enforce after Blueprint construction. Natural gravity may change speed in flight.
	if (!ProjectileLaunchInitialization::ApplyWorldVelocity(ProjectileMovementComp, LaunchVelocity)) return;
	ProjectileMovementComp->bIsHomingProjectile = false;
	ProjectileMovementComp->bShouldBounce = false;
	ProjectileMovementComp->bSweepCollision = true;
	if (!LaunchVelocity.IsNearlyZero()) SetActorRotation(LaunchVelocity.Rotation());
	ProjectileMovementComp->Activate(true);
	ProjectileMovementComp->SetComponentTickEnabled(true);
	if (auto* Movement = Cast<UArrowProjectileMovementComponent>(ProjectileMovementComp)) Movement->MarkLaunchFrame();
	ForceNetUpdate();
}

bool AArrowProjectile::LaunchShot(const FProjectileShotSnapshot& Shot)
{
	if (!HasAuthority() || !GetWorld() || !Shot.Input.ShotId.IsValid() || !DirectDamageSpec.IsValid()
		|| !ProjectileMovementComp || !CollisionComp || !FMath::IsFinite(Shot.Input.GravityZ)
		|| Shot.WorldVelocity.ContainsNaN() || Shot.WorldVelocity.IsNearlyZero()) return false;
	const double WorldGravity = GetWorld()->GetGravityZ();
	if (FMath::IsNearlyZero(WorldGravity) && !FMath::IsNearlyZero(Shot.Input.GravityZ)) return false;
	FlightGravityScale = FMath::IsNearlyZero(WorldGravity) ? 0.0f : Shot.Input.GravityZ / WorldGravity;
	LaunchArrow(Shot.WorldVelocity);
	ProjectileShotPreparation::DebugShot(GetInstigator(), Shot);
	DebugLaunch(Shot.ShooterVelocity, Shot.Input.AimPoint);
	return true;
}

float AArrowProjectile::GetFlightGravityZ() const
{
	return GetWorld() ? GetWorld()->GetGravityZ() * FlightGravityScale : 0.0f;
}

void AArrowProjectile::DebugLaunch(const FVector& ShooterVelocity, const FVector& AimLocation) const
{
	if (CVarProjectileDebugLaunch.GetValueOnGameThread() == 0 || !GetWorld() || !ProjectileMovementComp) return;
	const FVector Origin = GetActorLocation();
	const FVector WorldVelocity = ProjectileMovementComp->Velocity;
	FName BoneName;
	const USceneComponent* Carrier = MovementFrameVelocity::GetCarrier(GetInstigator(), BoneName);
	UE_LOG(LogTemp, Display, TEXT("[ProjectileLaunch] Instigator=%s Carrier=%s Time=%.3f InitialSpeed=%.2f Shooter=%s World=%s GravityZ=%.2f"),
		*GetNameSafe(GetInstigator()), *GetNameSafe(Carrier), GetWorld()->GetTimeSeconds(), WorldVelocity.Size(),
		*ShooterVelocity.ToCompactString(), *WorldVelocity.ToCompactString(), GetFlightGravityZ());
	DrawDebugLine(GetWorld(), Origin, Origin + ShooterVelocity * 0.15, FColor::Cyan, false, 3.0f, 0, 2.0f);
	DrawDebugLine(GetWorld(), Origin, Origin + WorldVelocity * 0.15, FColor::Yellow, false, 3.0f, 0, 2.0f);
	DrawDebugBox(GetWorld(), Origin, GetCollisionHalfExtent(), GetActorQuat(), FColor::Green, false, 3.0f);
	DrawDebugBox(GetWorld(), Origin, GetObstacleCollisionHalfExtent(), GetActorQuat(), FColor::Orange, false, 3.0f);
	DrawDebugPoint(GetWorld(), AimLocation, 12.0f, FColor::White, false, 3.0f);
	const double Duration = 1.0;
	FVector Previous = Origin;
	for (int32 Index = 1; Index <= 30; ++Index)
	{
		const double Time = Duration * Index / 30.0;
		const FVector Next = Origin + WorldVelocity * Time + FVector(0, 0, 0.5 * GetFlightGravityZ() * Time * Time);
		DrawDebugLine(GetWorld(), Previous, Next, FColor::Magenta, false, 3.0f);
		Previous = Next;
	}
}

FCollisionQueryParams AArrowProjectile::MakeFlightQueryParams() const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ArrowCollision), false, this);
	Params.bFindInitialOverlaps = true;
	for (const TWeakObjectPtr<AActor>& Actor : MovementIgnoredActors)
	{
		if (Actor.IsValid()) Params.AddIgnoredActor(Actor.Get());
	}
	return Params;
}

void AArrowProjectile::IgnoreActorForMovement(AActor* ActorToIgnore)
{
	if (!ActorToIgnore || ActorToIgnore == this || Cast<IMovementFrameVelocityProvider>(ActorToIgnore))
	{
		return;
	}

	if (CollisionComp)
	{
		CollisionComp->IgnoreActorWhenMoving(ActorToIgnore, true);
	}

	MovementIgnoredActors.AddUnique(ActorToIgnore);
}

bool AArrowProjectile::ApplyVisualTo(UStaticMeshComponent* TargetMesh) const
{
	if (!TargetMesh || !MeshComp || !MeshComp->GetStaticMesh())
	{
		return false;
	}

	TargetMesh->SetStaticMesh(MeshComp->GetStaticMesh());
	TargetMesh->SetRelativeTransform(MeshComp->GetRelativeTransform());
	TargetMesh->EmptyOverrideMaterials();
	for (int32 MaterialIndex = 0; MaterialIndex < MeshComp->GetNumOverrideMaterials(); ++MaterialIndex)
	{
		TargetMesh->SetMaterial(MaterialIndex, MeshComp->GetMaterial(MaterialIndex));
	}
	return true;
}

UStaticMesh* AArrowProjectile::GetArrowVisualMesh() const
{
	return MeshComp ? MeshComp->GetStaticMesh() : nullptr;
}

FTransform AArrowProjectile::GetArrowVisualRelativeTransform() const
{
	return MeshComp ? MeshComp->GetRelativeTransform() : FTransform::Identity;
}

bool AArrowProjectile::InitializeStrengthDamage(
	UAbilitySystemComponent* InSourceASC,
	AActor* InInstigatorActor,
	const FGameplayEffectSpecHandle& InDirectDamageSpec)
{
	if (!HasAuthority())
	{
		return false;
	}

	SourceASC = InSourceASC;
	InstigatorActor = InInstigatorActor;
	DirectDamageSpec = FGameplayEffectSpecHandle();
	StatusEffectSpecHandles.Reset();
	StatusEffectRefreshGrantedTags.Reset();

	if (!SourceASC)
	{
		UE_LOG(LogTemp, Warning, TEXT("AArrowProjectile::InitializeStrengthDamage: SourceASC is missing."));
		return false;
	}

	if (!InDirectDamageSpec.IsValid() || !InDirectDamageSpec.Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("AArrowProjectile::InitializeStrengthDamage: invalid Damage Spec."));
		return false;
	}

	auto* Resolver = FindComponentByClass<UCombatHitResolver>();
	if (!Resolver || !Resolver->OpenWindow(InDirectDamageSpec)) return false;
	DirectDamageSpec = InDirectDamageSpec;
	BuildStatusEffectSpecs();
	return true;
}

void AArrowProjectile::Multicast_PlayImpactFX_Implementation(const FHitResult& Hit)
{
	K2_OnImpactFX(Hit);
}

void AArrowProjectile::Multicast_PlayImpactPresentation_Implementation(
	const FArrowImpactPresentationData& ImpactData)
{
	FHitResult CosmeticHit;
	CosmeticHit.ImpactPoint = ImpactData.ImpactLocation;
	CosmeticHit.Location = ImpactData.ImpactLocation;
	CosmeticHit.ImpactNormal = ImpactData.ImpactNormal;
	CosmeticHit.Normal = ImpactData.ImpactNormal;
	CosmeticHit.Component = Cast<UPrimitiveComponent>(ImpactData.AttachComponent.Get());
	CosmeticHit.BoneName = ImpactData.BoneName;
	K2_OnImpactFX(CosmeticHit);

	if (GetNetMode() == NM_DedicatedServer || !GetWorld())
	{
		return;
	}

	AArrowImpactVisual* ImpactVisual = GetWorld()->SpawnActor<AArrowImpactVisual>(
		AArrowImpactVisual::StaticClass(),
		FTransform::Identity);
	if (ImpactVisual)
	{
		ImpactVisual->InitializeFromProjectile(
			*this,
			ImpactData,
			ImpactEmbedDepth,
			StuckArrowLifeSpan);
	}
}

void AArrowProjectile::HandleFlightImpact(const FHitResult& Hit)
{
	if (!HasAuthority() || bImpactHandled)
	{
		return;
	}

	AActor* OtherActor = Hit.GetActor();
	UPrimitiveComponent* OtherComp = Hit.GetComponent();
	const bool bIgnoredHit = ShouldIgnoreHitActor(OtherActor);
	UE_LOG(LogTemp, Display,
		TEXT("[ArrowHit] Actor=%s Component=%s Mesh=%s Profile=%s ObjectType=%d Bone=%s Point=%s Ignored=%s"),
		*GetNameSafe(OtherActor),
		*GetNameSafe(OtherComp),
		*GetHitMeshPath(OtherComp),
		OtherComp ? *OtherComp->GetCollisionProfileName().ToString() : TEXT("None"),
		OtherComp ? static_cast<int32>(OtherComp->GetCollisionObjectType()) : INDEX_NONE,
		*Hit.BoneName.ToString(),
		*Hit.ImpactPoint.ToCompactString(),
		bIgnoredHit ? TEXT("true") : TEXT("false"));

	if (bIgnoredHit || !UCombatHurtboxComponent::IsValidHitSurface(OtherActor, Hit))
	{
		return;
	}
	bImpactHandled = true;

	const bool bDamageTarget = CanApplyDamageToActor(OtherActor);
	const bool bConfirmed = bDamageTarget && ApplyDamageToActor(OtherActor, Hit);
	if (!bDamageTarget || bConfirmed || !Cast<ACharacter>(OtherActor))
	{
		Multicast_PlayImpactPresentation(BuildImpactPresentationData(OtherComp, Hit));
	}

	if (bDestroyOnImpact)
	{
		if (ProjectileMovementComp)
		{
			ProjectileMovementComp->StopSimulating(Hit);
		}
		if (CollisionComp)
		{
			CollisionComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		SetReplicateMovement(false);
		Destroy();
	}
}

FArrowImpactPresentationData AArrowProjectile::BuildImpactPresentationData(
	UPrimitiveComponent* OtherComp,
	const FHitResult& Hit) const
{
	FArrowImpactPresentationData Result;
	Result.ImpactLocation = Hit.ImpactPoint;
	Result.ImpactNormal = Hit.ImpactNormal.GetSafeNormal();
	Result.IncomingDirection = GetVelocity().GetSafeNormal();
	if (FVector(Result.IncomingDirection).IsNearlyZero())
	{
		Result.IncomingDirection = GetActorForwardVector().GetSafeNormal();
	}
	Result.BoneName = Hit.BoneName;

	// Only stable components owned by replicated actors are safe RPC references.
	// Static geometry needs no attachment; its world-space impact is sufficient.
	if (OtherComp
		&& OtherComp->Mobility != EComponentMobility::Static
		&& OtherComp->IsNameStableForNetworking()
		&& OtherComp->GetOwner()
		&& OtherComp->GetOwner()->GetIsReplicated())
	{
		Result.AttachComponent = OtherComp;
	}

	return Result;
}

bool AArrowProjectile::ShouldIgnoreHitActor(const AActor* OtherActor) const
{
	if (!OtherActor || OtherActor == this)
	{
		return true;
	}

	if (OtherActor == GetOwner() || OtherActor == GetInstigator())
	{
		return true;
	}

	for (const TWeakObjectPtr<AActor>& IgnoredActorPtr : MovementIgnoredActors)
	{
		if (IgnoredActorPtr.Get() == OtherActor)
		{
			return true;
		}
	}

	return false;
}

bool AArrowProjectile::CanApplyDamageToActor(const AActor* OtherActor) const
{
	return IsValidDamageTarget(OtherActor);
}

bool AArrowProjectile::IsValidDamageTarget(const AActor* TargetActor) const
{
	if (!IsValid(TargetActor)
		|| TargetActor == this
		|| TargetActor == GetOwner()
		|| TargetActor == GetInstigator())
	{
		return false;
	}

	const UAbilitySystemComponent* TargetASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(TargetActor));
	if (!TargetASC)
	{
		return false;
	}

	if (!bEnableTeamDamageFiltering)
	{
		return true;
	}

	const UAbilitySystemComponent* ProjectileSourceASC = SourceASC;
	if (!ProjectileSourceASC)
	{
		AActor* SourceActor = GetInstigator() ? static_cast<AActor*>(GetInstigator()) : GetOwner();
		ProjectileSourceASC = SourceActor
			? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(SourceActor)
			: nullptr;
	}

	if (!ProjectileSourceASC)
	{
		return true;
	}

	const bool bBothPlayers = ProjectileSourceASC->HasMatchingGameplayTag(Team_Player)
		&& TargetASC->HasMatchingGameplayTag(Team_Player);
	const bool bBothEnemies = ProjectileSourceASC->HasMatchingGameplayTag(Team_Enemy)
		&& TargetASC->HasMatchingGameplayTag(Team_Enemy);
	return !bBothPlayers && !bBothEnemies;
}

void AArrowProjectile::BuildStatusEffectSpecs()
{
	if (!HasAuthority() || !SourceASC)
	{
		return;
	}

	if (StatusEffectSpecHandles.Num() == 0)
	{
		StatusEffectRefreshGrantedTags.Reset();

		for (const FArrowStatusEffect& StatusEffect : DamageData.StatusEffects)
		{
			if (!StatusEffect.StatusEffectClass)
			{
				continue;
			}

			FGameplayEffectContextHandle ContextHandle =
				USWCombatEffectContextLibrary::MakeCombatEffectContext(
					SourceASC, InstigatorActor.Get(), this);

			FGameplayEffectSpecHandle StatusSpecHandle = SourceASC->MakeOutgoingSpec(
				StatusEffect.StatusEffectClass,
				FMath::Max(1, StatusEffect.EffectLevel),
				ContextHandle);

			if (StatusSpecHandle.IsValid())
			{
				StatusEffectSpecHandles.Add(StatusSpecHandle);
				StatusEffectRefreshGrantedTags.Add(StatusEffect.RefreshGrantedTag);
			}
		}


	}
}

bool AArrowProjectile::ApplyDamageToActor(AActor* TargetActor, const FHitResult& HitResult)
{
	if (!HasAuthority() || !TargetActor)
	{
		return false;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	if (!TargetASC)
	{
		UE_LOG(LogTemp, Warning, TEXT("AArrowProjectile::ApplyDamageToActor: TargetASC is missing for %s."), *GetNameSafe(TargetActor));
		return false;
	}

	if (!DirectDamageSpec.IsValid()) return false;

	if (!HasAuthority()) return false;
	auto* Resolver = FindComponentByClass<UCombatHitResolver>();
	// The shared flight query already checks static and moving cover.
	if (!Resolver || !Resolver->ResolveHit(TargetASC, HitResult, bEnableTeamDamageFiltering, true, false)) return false;

	for (int32 StatusEffectIndex = 0; StatusEffectIndex < StatusEffectSpecHandles.Num(); ++StatusEffectIndex)
	{
		const FGameplayEffectSpecHandle& StatusSpecHandle = StatusEffectSpecHandles[StatusEffectIndex];
		if (StatusSpecHandle.IsValid() && StatusSpecHandle.Data.IsValid())
		{
			const FGameplayTag RefreshGrantedTag = StatusEffectRefreshGrantedTags.IsValidIndex(StatusEffectIndex)
				? StatusEffectRefreshGrantedTags[StatusEffectIndex]
				: FGameplayTag();

			FGameplayEffectSpec TargetStatusSpec(*StatusSpecHandle.Data.Get());
			USWCombatEffectContextLibrary::EnrichCombatEffectSpec(
				TargetStatusSpec,
				InstigatorActor.Get(),
				this,
				TargetActor,
				&HitResult,
				GetVelocity());
			const FGameplayEffectSpecHandle TargetStatusSpecHandle(new FGameplayEffectSpec(TargetStatusSpec));
			UStatusEffectLibrary::ApplyDurationDamageEffectSpecToTarget(
				TargetASC, TargetStatusSpecHandle, RefreshGrantedTag);
		}
	}
	return true;
}
