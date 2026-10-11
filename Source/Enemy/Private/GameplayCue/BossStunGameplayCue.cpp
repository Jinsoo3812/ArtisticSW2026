#include "GameplayCue/BossStunGameplayCue.h"

#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BossAI/ShipBossEnemy.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ABossStunGameplayCue::ABossStunGameplayCue()
{
	// The authored Blueprint owns its cue tag, allowing UE to store the registry lookup name correctly.
	bAutoDestroyOnRemove = true;
	bAutoAttachToOwner = false;
	bUniqueInstancePerInstigator = false;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("StarOrbitRoot")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	for (int32 Index = 0; Index < 3; ++Index)
	{
		auto* Star = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("StunStar%d"), Index));
		Star->SetupAttachment(GetRootComponent());
		Star->SetStaticMesh(Plane.Object);
		Star->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Star->SetGenerateOverlapEvents(false);
		Star->SetCanEverAffectNavigation(false);
		Star->SetCastShadow(false);
		Star->SetRelativeScale3D(FVector(StarSize / 100.f));
		Star->SetRelativeLocation(FVector(0.f, (Index - 1) * 25.f, HeightAboveHead));
		Star->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
		Stars.Add(Star);
	}
}

void ABossStunGameplayCue::ApplyMaterial()
{
	if (!StarMaterial) return;
	for (auto& Star : Stars)
	{
		if (!Star) continue;
		auto* Material = UMaterialInstanceDynamic::Create(StarMaterial, this);
		Material->SetVectorParameterValue(TEXT("StarColor"), StarColor);
		Material->SetScalarParameterValue(TEXT("GlowIntensity"), GlowIntensity);
		Star->SetMaterial(0, Material);
	}
}

void ABossStunGameplayCue::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyMaterial();
}

bool ABossStunGameplayCue::StartStars(AActor* Target)
{
	if (GetNetMode() == NM_DedicatedServer) return true;
	auto* Boss = Cast<AShipBossEnemy>(Target);
	// The existing shared Stun cue also runs for ordinary characters. Only bosses use these stars.
	if (!IsValid(Boss) || !StarMaterial) { StopStars(); return false; }
	if (StunnedBoss.Get() != Boss)
	{
		StopStars();
		StunnedBoss = Boss;
		ApplyMaterial();
	}
	SetActorHiddenInGame(false);
	for (auto& Star : Stars) if (Star) Star->SetVisibility(true);
	SetActorTickEnabled(true);
	Tick(0.f);
	return true;
}

bool ABossStunGameplayCue::OnActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
	return StartStars(Target);
}

bool ABossStunGameplayCue::WhileActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
	// Also initializes persistent feedback when a client first observes an already-stunned boss.
	return StartStars(Target);
}

void ABossStunGameplayCue::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	auto* Boss = StunnedBoss.Get();
	if (!IsValid(Boss) || Boss->IsActorBeingDestroyed()
		|| (Boss->GetAbilitySystemComponent() && Boss->GetAbilitySystemComponent()->HasMatchingGameplayTag(State_Dead)))
	{
		StopStars();
		return;
	}
	Elapsed += DeltaSeconds;
	const auto* Mesh = Boss->GetMesh();
	FVector Center = Mesh && Mesh->DoesSocketExist(HeadBone) ? Mesh->GetSocketLocation(HeadBone)
		: Boss->GetActorLocation() + FVector(0.f, 0.f, Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	Center.Z += HeightAboveHead;
	SetActorLocation(Center);
	const auto* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	for (int32 Index = 0; Index < Stars.Num(); ++Index)
	{
		auto* Star = Stars[Index].Get();
		if (!Star) continue;
		const float Angle = FMath::DegreesToRadians(Elapsed * OrbitSpeed) + Index * 2.f * PI / Stars.Num();
		const FVector Position = Center + FVector(FMath::Cos(Angle) * OrbitRadius, FMath::Sin(Angle) * OrbitRadius,
			FMath::Sin(Angle * 2.f) * 4.f);
		Star->SetWorldLocation(Position);
		const FVector Facing = Camera ? (Camera->GetCameraLocation() - Position).GetSafeNormal() : FVector::ForwardVector;
		Star->SetWorldRotation(FRotationMatrix::MakeFromZ(Facing).Rotator());
		const float Pulse = 1.f + 0.12f * FMath::Sin(Elapsed * 6.f + Index * 2.f);
		Star->SetWorldScale3D(FVector(FMath::Max(1.f, StarSize) / 100.f * Pulse));
	}
}

void ABossStunGameplayCue::StopStars()
{
	SetActorTickEnabled(false);
	SetActorHiddenInGame(true);
	for (auto& Star : Stars) if (Star) Star->SetVisibility(false);
	StunnedBoss.Reset();
	Elapsed = 0.f;
}

bool ABossStunGameplayCue::OnRemove_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
	StopStars();
	return true;
}

bool ABossStunGameplayCue::Recycle()
{
	StopStars();
	return Super::Recycle();
}
