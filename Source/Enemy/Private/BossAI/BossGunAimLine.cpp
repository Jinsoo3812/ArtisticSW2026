#include "BossAI/BossGunAimLine.h"
#include "BossAI/GunnerBossEnemy.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BaseHealthComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ABossGunAimLine::ABossGunAimLine()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	ShotCylinder = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShotCylinder"));
	SetRootComponent(ShotCylinder);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ShotCylinder->SetStaticMesh(Cylinder.Object);
	ShotCylinder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShotCylinder->SetGenerateOverlapEvents(false);
	ShotCylinder->SetCanEverAffectNavigation(false);
	ShotCylinder->SetCastShadow(false);
	ShotCylinder->SetVisibility(false);
}

void ABossGunAimLine::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer) SetActorTickEnabled(false);
}

void ABossGunAimLine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABossGunAimLine, VisualState);
}

float ABossGunAimLine::ServerTime() const
{
	const AGameStateBase* State = GetWorld()->GetGameState();
	return State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void ABossGunAimLine::Initialize(AGunnerBossEnemy* Boss, FName Socket, const FGunnerBossRaySettings& Ray)
{
	if (!HasAuthority()) return;
	VisualState.Boss = Boss;
	VisualState.Socket = Socket;
	VisualState.Ray = Ray;
	ForceNetUpdate();
	OnRep_VisualState();
}

void ABossGunAimLine::SetEndpoint(const FVector& End, USceneComponent* Anchor, FName Bone)
{
	VisualState.End = End;
	VisualState.EndAnchor = Anchor;
	VisualState.EndBone = Bone;
	VisualState.bHasEndAnchor = IsValid(Anchor);
	VisualState.LocalEnd = IsValid(Anchor)
		? (Bone.IsNone() ? Anchor->GetComponentTransform() : Anchor->GetSocketTransform(Bone)).InverseTransformPosition(End) : End;
}

void ABossGunAimLine::SetAim(EBossGunAimPhase Phase, const FVector& End, float StartTime, float EndTime,
	USceneComponent* EndAnchor, FName EndBone, float PlannedShotTime)
{
	if (!HasAuthority()) return;
	VisualState.Phase = Phase;
	VisualState.PhaseStartTime = StartTime;
	VisualState.PhaseEndTime = EndTime;
	if (Phase == EBossGunAimPhase::Tracking || Phase == EBossGunAimPhase::Locked)
	{
		SetEndpoint(End, EndAnchor, EndBone);
		VisualState.bActive = true;
		VisualState.PlannedShotTime = PlannedShotTime >= 0.f ? PlannedShotTime : EndTime;
	}
	// After firing, keep the live connection until this attack finishes.
	ForceNetUpdate();
	OnRep_VisualState();
}

void ABossGunAimLine::ShowShot(const FVector& Start, const FVector& End, USceneComponent* EndAnchor, FName EndBone)
{
	if (!HasAuthority()) return;
	++VisualState.ShotSerial;
	VisualState.ShotStart = Start;
	VisualState.ShotEnd = End;
	VisualState.ShotTime = ServerTime();
	SetEndpoint(End, EndAnchor, EndBone);
	VisualState.bActive = true;
	ForceNetUpdate();
	OnRep_VisualState();
}

void ABossGunAimLine::FinishPresentation(bool /*bCancelled*/)
{
	if (!HasAuthority()) return;
	VisualState.bAttackFinished = true;
	VisualState.bActive = false;
	ShotCylinder->SetVisibility(false);
	ForceNetUpdate();
	Destroy();
}

void ABossGunAimLine::OnRep_VisualState()
{
	if (GetNetMode() == NM_DedicatedServer) return;
	if (AppliedMaterial.Get() != VisualState.Ray.ShotMaterial || !ShotMaterialInstance)
	{
		AppliedMaterial = VisualState.Ray.ShotMaterial;
		ShotMaterialInstance = VisualState.Ray.ShotMaterial
			? UMaterialInstanceDynamic::Create(VisualState.Ray.ShotMaterial, this) : nullptr;
	}
	if (ShotMaterialInstance)
	{
		ShotCylinder->SetMaterial(0, ShotMaterialInstance);
		ShotMaterialInstance->SetVectorParameterValue(TEXT("ShotColor"), VisualState.Ray.ShotColor);
		ShotMaterialInstance->SetScalarParameterValue(TEXT("Brightness"), VisualState.Ray.ShotBrightness);
	}
	RefreshShotPresentation();
}

bool ABossGunAimLine::ResolveEndpoints(FVector& OutStart, FVector& OutEnd) const
{
	const auto* Boss = VisualState.Boss.Get();
	const auto* Mesh = IsValid(Boss) ? Boss->GetMesh() : nullptr;
	if (!Mesh || Boss->IsActorBeingDestroyed() || (Boss->GetHealthComponent() && Boss->GetHealthComponent()->IsDead())
		|| !Mesh->DoesSocketExist(VisualState.Socket)) return false;
	OutStart = Mesh->GetSocketLocation(VisualState.Socket);
	if (VisualState.bHasEndAnchor)
	{
		if (!IsValid(VisualState.EndAnchor)) return false;
		const auto* Anchor = VisualState.EndAnchor.Get();
		OutEnd = (VisualState.EndBone.IsNone() ? Anchor->GetComponentTransform() : Anchor->GetSocketTransform(VisualState.EndBone))
			.TransformPosition(VisualState.LocalEnd);
	}
	else OutEnd = VisualState.End;
	return !OutStart.ContainsNaN() && !OutEnd.ContainsNaN();
}

bool ABossGunAimLine::IsVisibleAt(float ServerSeconds) const
{
	if (!VisualState.bActive || VisualState.bAttackFinished) return false;
	const bool bBeforeShot = VisualState.PlannedShotTime >= 0.f
		&& (VisualState.Phase == EBossGunAimPhase::Tracking || VisualState.Phase == EBossGunAimPhase::Locked)
		&& ServerSeconds >= VisualState.PlannedShotTime - VisualState.Ray.PreShotShowTime
		&& ServerSeconds <= VisualState.PlannedShotTime;
	const bool bAfterShot = VisualState.ShotSerial != 0 && ServerSeconds >= VisualState.ShotTime;
	return bBeforeShot || bAfterShot;
}

void ABossGunAimLine::RefreshShotPresentation()
{
	FVector Start, End;
	const bool bHaveEndpoints = ResolveEndpoints(Start, End);
	const FVector Delta = bHaveEndpoints ? End - Start : FVector::ZeroVector;
	const bool bVisible = IsVisibleAt(ServerTime()) && bHaveEndpoints && !Delta.IsNearlyZero() && ShotMaterialInstance;
	ShotCylinder->SetVisibility(bVisible);
	if (!bVisible) return;
	// Engine cylinder: radius 50 cm, length 100 cm, longitudinal axis +Z.
	SetActorLocationAndRotation((Start + End) * 0.5f,
		FRotationMatrix::MakeFromZ(Delta).Rotator());
	SetActorScale3D(FVector(VisualState.Ray.ShotRadius / 50.f, VisualState.Ray.ShotRadius / 50.f, Delta.Size() / 100.f));
	ShotMaterialInstance->SetScalarParameterValue(TEXT("Fade"), 1.f);
}

void ABossGunAimLine::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ShotCylinder->SetVisibility(false);
	Super::EndPlay(EndPlayReason);
}

void ABossGunAimLine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() != NM_DedicatedServer) RefreshShotPresentation();
}
