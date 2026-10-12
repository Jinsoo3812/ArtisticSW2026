#include "BossAI/BossGunAimLine.h"
#include "BossAI/GunnerBossEnemy.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Net/UnrealNetwork.h"
#include "ShipAI/EnemyShip.h"

ABossGunAimLine::ABossGunAimLine()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
	LineMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LineMesh"));
	SetRootComponent(LineMesh);
	LineMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LineMesh->SetGenerateOverlapEvents(false);
	LineMesh->SetCanEverAffectNavigation(false);
	LineMesh->SetCastShadow(false);
	WarningNiagara = CreateDefaultSubobject<UNiagaraComponent>(TEXT("WarningNiagara"));
	WarningNiagara->SetupAttachment(RootComponent);
	WarningNiagara->SetAutoActivate(false);
	WarningNiagara->SetAbsolute(true, true, true);
}

bool ABossGunAimLine::HasWarningVisual() const { return WarningMesh || WarningEffect; }

void ABossGunAimLine::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer) return;
	LineMesh->SetStaticMesh(WarningMesh);
	if (WarningMaterial) LineMesh->SetMaterial(0, WarningMaterial);
	if (LineMesh->GetMaterial(0)) LineMaterial = LineMesh->CreateDynamicMaterialInstance(0);
	WarningNiagara->SetAsset(WarningEffect);
	PresentState();
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

void ABossGunAimLine::Initialize(AGunnerBossEnemy* Boss, FName Socket)
{
	if (!HasAuthority()) return;
	VisualState.Boss = Boss;
	VisualState.Socket = Socket;
	ForceNetUpdate();
}

void ABossGunAimLine::SetAim(EBossGunAimPhase Phase, const FVector& End, float StartTime, float EndTime)
{
	if (!HasAuthority()) return;
	VisualState.Phase = Phase;
	VisualState.End = End;
	const AEnemyShip* Ship = VisualState.Boss ? VisualState.Boss->GetHostShip() : nullptr;
	const UStaticMeshComponent* Frame = Ship ? Ship->GetDeckMeshComplex() : nullptr;
	VisualState.bDeckLocalEnd = Frame != nullptr;
	VisualState.LocalEnd = Frame ? Frame->GetComponentTransform().InverseTransformPosition(End) : End;
	VisualState.PhaseStartTime = StartTime;
	VisualState.PhaseEndTime = EndTime;
	ForceNetUpdate();
	PresentState();
}

void ABossGunAimLine::ShowShot(const FVector& Start, const FVector& End, bool bBlockingHit)
{
	if (!HasAuthority()) return;
	++VisualState.ShotSerial;
	VisualState.ShotStart = Start;
	VisualState.ShotEnd = End;
	VisualState.ShotTime = ServerTime();
	VisualState.bBlockingHit = bBlockingHit;
	ForceNetUpdate();
	PresentState();
}

void ABossGunAimLine::OnRep_VisualState() { PresentState(); }

void ABossGunAimLine::FinishPresentation()
{
	if (!HasAuthority()) return;
	VisualState.Phase = EBossGunAimPhase::Hidden;
	ForceNetUpdate();
	PresentState();
	SetLifeSpan(FMath::Max(EndPresentationLifetime, ShotEffectLifetime));
}

FVector ABossGunAimLine::ResolveEnd() const
{
	const AEnemyShip* Ship = VisualState.Boss ? VisualState.Boss->GetHostShip() : nullptr;
	const UStaticMeshComponent* Frame = Ship ? Ship->GetDeckMeshComplex() : nullptr;
	return VisualState.bDeckLocalEnd && Frame ? Frame->GetComponentTransform().TransformPosition(VisualState.LocalEnd)
		: FVector(VisualState.End);
}

void ABossGunAimLine::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ShotNiagara) ShotNiagara->DestroyComponent();
	Super::EndPlay(EndPlayReason);
}

void ABossGunAimLine::PresentState()
{
	if (GetNetMode() == NM_DedicatedServer) return;
	if (PresentedPhase != VisualState.Phase || PresentedPhaseStart != VisualState.PhaseStartTime)
	{
		// Snap at stage boundaries; stale tracking interpolation must not conceal locked aim.
		RenderedEnd = ResolveEnd();
		bHaveEndpoint = true;
		PresentedPhase = VisualState.Phase;
		PresentedPhaseStart = VisualState.PhaseStartTime;
		if (ServerTime() < VisualState.PhaseEndTime)
		{
			USoundBase* Sound = VisualState.Phase == EBossGunAimPhase::Tracking ? PrepareSound.Get()
				: (VisualState.Phase == EBossGunAimPhase::Locked ? LockSound.Get() : nullptr);
			if (Sound && IsValid(VisualState.Boss)) UGameplayStatics::PlaySoundAtLocation(this, Sound, VisualState.Boss->GetActorLocation());
		}
	}
	if (VisualState.ShotSerial != PresentedShotSerial)
	{
		PresentedShotSerial = VisualState.ShotSerial;
		const float Age = ServerTime() - VisualState.ShotTime;
		if (Age < ShotEffectLifetime)
		{
			if (ShotNiagara) ShotNiagara->DestroyComponent();
			if (ShotEffect)
			{
				ShotNiagara = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ShotEffect, VisualState.ShotStart,
					(FVector(VisualState.ShotEnd) - FVector(VisualState.ShotStart)).Rotation(), FVector::OneVector, true, false);
				if (ShotNiagara)
				{
					ShotNiagara->SetVariableVec3(StartParameter, VisualState.ShotStart);
					ShotNiagara->SetVariableVec3(EndParameter, VisualState.ShotEnd);
					ShotNiagara->Activate();
				}
			}
			if (MuzzleEffect) UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, MuzzleEffect, VisualState.ShotStart);
			if (ImpactEffect && VisualState.bBlockingHit) UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ImpactEffect, VisualState.ShotEnd);
			if (FireSound) UGameplayStatics::PlaySoundAtLocation(this, FireSound, VisualState.ShotStart);
		}
	}
}

void ABossGunAimLine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_DedicatedServer) return;
	const bool bVisible = IsValid(VisualState.Boss) && (VisualState.Phase == EBossGunAimPhase::Tracking
		|| VisualState.Phase == EBossGunAimPhase::Locked) && ServerTime() <= VisualState.PhaseEndTime;
	LineMesh->SetVisibility(bVisible && WarningMesh != nullptr);
	if (bVisible && WarningEffect)
	{
		if (!WarningNiagara->IsActive()) WarningNiagara->Activate();
	}
	else if (WarningNiagara->IsActive()) WarningNiagara->DeactivateImmediate();
	if (ShotNiagara && ServerTime() - VisualState.ShotTime >= ShotEffectLifetime)
	{
		ShotNiagara->DestroyComponent();
		ShotNiagara = nullptr;
	}
	if (!bVisible || !VisualState.Boss->GetMesh()->DoesSocketExist(VisualState.Socket)) return;
	const FVector Start = VisualState.Boss->GetMesh()->GetSocketLocation(VisualState.Socket);
	if (!bHaveEndpoint || HasAuthority() || VisualState.Phase == EBossGunAimPhase::Locked || EndpointInterpolationSpeed <= 0.f)
		RenderedEnd = ResolveEnd();
	else RenderedEnd = FMath::VInterpTo(RenderedEnd, ResolveEnd(), DeltaSeconds, EndpointInterpolationSpeed);
	bHaveEndpoint = true;
	const FVector Delta = RenderedEnd - Start;
	const FLinearColor Color = VisualState.Phase == EBossGunAimPhase::Locked ? LockedColor : TrackingColor;
	if (WarningMesh)
	{
		const FVector Size = WarningMesh->GetBounds().BoxExtent * 2.f;
		LineMesh->SetWorldLocationAndRotation((Start + RenderedEnd) * 0.5f, Delta.Rotation());
		LineMesh->SetWorldScale3D(FVector(Delta.Size() / FMath::Max(Size.X, 1.f),
			LineThickness / FMath::Max(Size.Y, 1.f), LineThickness / FMath::Max(Size.Z, 1.f)));
	}
	if (LineMaterial) LineMaterial->SetVectorParameterValue(ColorParameter, Color);
	WarningNiagara->SetVariableVec3(StartParameter, Start);
	WarningNiagara->SetVariableVec3(EndParameter, RenderedEnd);
	WarningNiagara->SetVariableLinearColor(NiagaraColorParameter, Color);
	WarningNiagara->SetVariableFloat(ProgressParameter, FMath::Clamp((ServerTime() - VisualState.PhaseStartTime)
		/ FMath::Max(VisualState.PhaseEndTime - VisualState.PhaseStartTime, KINDA_SMALL_NUMBER), 0.f, 1.f));
}
