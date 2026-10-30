#include "BossAI/BossGunAimLine.h"
#include "BossAI/GunnerBossEnemy.h"
#include "Components/LineBatchComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "ShipAI/EnemyShip.h"

ABossGunAimLine::ABossGunAimLine()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RayLines = CreateDefaultSubobject<ULineBatchComponent>(TEXT("RayLines"));
	SetRootComponent(RayLines);
	RayLines->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RayLines->SetGenerateOverlapEvents(false);
	RayLines->SetCanEverAffectNavigation(false);
	RayLines->SetCastShadow(false);
	RayLines->SetComponentTickEnabled(false);
	RayLines->bCalculateAccurateBounds = false;
	RayLines->bUseEditorCompositing = false;
}

void ABossGunAimLine::BeginPlay()
{
	Super::BeginPlay();
	RayLines->SetComponentTickEnabled(false);
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetActorTickEnabled(false);
		RayLines->SetVisibility(false);
	}
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
	OnRep_VisualState();
}

void ABossGunAimLine::ShowShot(const FVector& Start, const FVector& End)
{
	if (!HasAuthority()) return;
	++VisualState.ShotSerial;
	VisualState.ShotStart = Start;
	VisualState.ShotEnd = End;
	VisualState.ShotTime = ServerTime();
	ForceNetUpdate();
}

void ABossGunAimLine::FinishPresentation()
{
	if (!HasAuthority()) return;
	VisualState.Phase = EBossGunAimPhase::Hidden;
	ForceNetUpdate();
	OnRep_VisualState();
	SetLifeSpan(FMath::Max(VisualState.Ray.EndPresentationLifetime, VisualState.Ray.ShotLifetime));
}

void ABossGunAimLine::OnRep_VisualState()
{
	if (PresentedPhase != VisualState.Phase || PresentedPhaseStart != VisualState.PhaseStartTime)
	{
		// Never interpolate an old tracking direction into a newly locked shot.
		RenderedEnd = ResolveEnd();
		bHaveEndpoint = true;
		PresentedPhase = VisualState.Phase;
		PresentedPhaseStart = VisualState.PhaseStartTime;
		RayLines->Flush();
	}
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
	RayLines->Flush();
	Super::EndPlay(EndPlayReason);
}

void ABossGunAimLine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_DedicatedServer) return;
	// Own component: never flush the world's lines or another boss's presentation.
	RayLines->Flush();
	const float Now = ServerTime();
	const bool bPreparing = IsValid(VisualState.Boss) && (VisualState.Phase == EBossGunAimPhase::Tracking
		|| VisualState.Phase == EBossGunAimPhase::Locked) && Now <= VisualState.PhaseEndTime;
	const USkeletalMeshComponent* Mesh = IsValid(VisualState.Boss) ? VisualState.Boss->GetMesh() : nullptr;
	if (bPreparing && Mesh && Mesh->DoesSocketExist(VisualState.Socket))
	{
		const FVector Start = Mesh->GetSocketLocation(VisualState.Socket);
		if (!bHaveEndpoint || HasAuthority() || VisualState.Phase == EBossGunAimPhase::Locked
			|| VisualState.Ray.EndpointInterpolationSpeed <= 0.f) RenderedEnd = ResolveEnd();
		else RenderedEnd = FMath::VInterpTo(RenderedEnd, ResolveEnd(), DeltaSeconds, VisualState.Ray.EndpointInterpolationSpeed);
		bHaveEndpoint = true;
		// Direct runtime line drawing has no DrawDebugLine / ENABLE_DRAW_DEBUG shipping guard.
		RayLines->DrawLine(Start, RenderedEnd, VisualState.Ray.Color, SDPG_World, VisualState.Ray.Thickness);
	}
	if (VisualState.ShotSerial != 0 && Now - VisualState.ShotTime < VisualState.Ray.ShotLifetime)
	{
		RayLines->DrawLine(VisualState.ShotStart, VisualState.ShotEnd, VisualState.Ray.Color,
			SDPG_World, VisualState.Ray.ShotThickness);
	}
}
