// Fill out your copyright notice in the Description page of Project Settings.

#include "Component/PathMovement.h"
#include "AI/EnemyPathActor.h"

// Unreal
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UPathMovement::UPathMovement()
{
	PrimaryComponentTick.bCanEverTick = true;
	//PrimaryComponentTick.bStartWithTickEnabled = false;

	SetIsReplicatedByDefault(true);
}

void UPathMovement::BeginPlay()
{
	Super::BeginPlay();

	SetComponentTickEnabled(false);

	ClientVisualDistance = CurrentDistanceAlongPath;
	ClientTargetDistance = CurrentDistanceAlongPath;

	// 클라이언트에 이미 경로 상태가 도착한 경우 즉시 반영
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		ApplyTransformFromCurrentDistance();
		UpdateComponentTickState();
	}
}

void UPathMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 서버 authoritative 이동
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	if (OwnerActor->HasAuthority())
	{
		UpdatePathMovement(DeltaTime);
	}
	else
	{
		SmoothReplicatedMovement(DeltaTime);
	}
}

void UPathMovement::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UPathMovement, CurrentPath);
	DOREPLIFETIME(UPathMovement, CurrentDistanceAlongPath);
	DOREPLIFETIME(UPathMovement, BaseMoveSpeed);
	DOREPLIFETIME(UPathMovement, bPathMovementActive);
	DOREPLIFETIME(UPathMovement, bReachedGoal);
}

void UPathMovement::InitializePath(AEnemyPathActor* InPath, float InStartDistance)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	CurrentPath = InPath;
	bReachedGoal = false;
	bPathMovementActive = false;

	if (!CurrentPath)
	{
		CurrentDistanceAlongPath = 0.0f;
		ClientVisualDistance = 0.0f;
		ClientTargetDistance = 0.0f;
		UpdateComponentTickState();
		return;
	}

	CurrentDistanceAlongPath = CurrentPath->ClampDistanceToPath(InStartDistance);
	ClientVisualDistance = CurrentDistanceAlongPath;
	ClientTargetDistance = CurrentDistanceAlongPath;

	ApplyTransformFromCurrentDistance();
	UpdateComponentTickState();
}

void UPathMovement::StartPathMovement()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	if (!CurrentPath || bReachedGoal)
	{
		return;
	}

	bPathMovementActive = true;
	UpdateComponentTickState();
}

void UPathMovement::StopPathMovement()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	bPathMovementActive = false;
	UpdateComponentTickState();
}

// Enemy Pooling을 사용하기 위해 재상용전에 초기화용 함수
void UPathMovement::ResetPathState()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	CurrentPath = nullptr;
	CurrentDistanceAlongPath = 0.0f;
	bPathMovementActive = false;
	bReachedGoal = false;

	ClientVisualDistance = 0.0f;
	ClientTargetDistance = 0.0f;

	UpdateComponentTickState();
}

bool UPathMovement::CanMoveAlongPath() const
{
	return bPathMovementActive && CurrentPath && !bReachedGoal;
}

// 이후에 속도 가감속 로직을 추가할 수 있다.
float UPathMovement::GetCurrentMoveSpeed() const
{
	return BaseMoveSpeed;
}

void UPathMovement::UpdatePathMovement(float DeltaTime)
{
	if (!CanMoveAlongPath())
	{
		return;
	}

	const float MoveSpeed = GetCurrentMoveSpeed();
	if (MoveSpeed <= 0.0f)
	{
		return;
	}

	const float PathLength = CurrentPath->GetPathLength();
	CurrentDistanceAlongPath += MoveSpeed * DeltaTime;

	if (CurrentDistanceAlongPath >= PathLength)
	{
		CurrentDistanceAlongPath = PathLength;
		ApplyTransformFromCurrentDistance();
		HandleReachedGoal();
		return;
	}

	ApplyTransformFromCurrentDistance();
}

void UPathMovement::HandleReachedGoal()
{
	if (bReachedGoal)
	{
		return;
	}

	bReachedGoal = true;
	bPathMovementActive = false;

	UpdateComponentTickState();

	// 게임플레이 판정은 서버에서만 브로드캐스트
	if (AActor* OwnerActor = GetOwner(); OwnerActor && OwnerActor->HasAuthority())
	{
		OnPathGoalReached.Broadcast(OwnerActor);
	}
}

void UPathMovement::SetBaseMoveSpeed(float NewSpeed)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	BaseMoveSpeed = FMath::Max(0.0f, NewSpeed);
}

void UPathMovement::OnRep_CurrentPath()
{
	ClientVisualDistance = CurrentDistanceAlongPath;
	ClientTargetDistance = CurrentDistanceAlongPath;

	ApplyTransformFromCurrentDistance();
	UpdateComponentTickState();
}

void UPathMovement::OnRep_CurrentDistanceAlongPath()
{
	ClientTargetDistance = CurrentDistanceAlongPath;

	// 경로가 아직 없으면 보간할 수 없음
	if (!CurrentPath)
	{
		return;
	}

	// 큰 보정이 오면 즉시 맞춰 줌
	if (FMath::Abs(ClientVisualDistance - ClientTargetDistance) > 300.0f)
	{
		ClientVisualDistance = ClientTargetDistance;
		ApplyTransformFromDistance(ClientVisualDistance);
	}

	UpdateComponentTickState();
}

void UPathMovement::OnRep_PathMovementActive()
{
	UpdateComponentTickState();

	// 정지 상태가 복제되면 마지막 서버 위치로 맞춤
	if (!bPathMovementActive)
	{
		ClientVisualDistance = CurrentDistanceAlongPath;
		ClientTargetDistance = CurrentDistanceAlongPath;
		ApplyTransformFromCurrentDistance();
	}
}

void UPathMovement::OnRep_ReachedGoal()
{
	ClientVisualDistance = CurrentDistanceAlongPath;
	ClientTargetDistance = CurrentDistanceAlongPath;
	ApplyTransformFromCurrentDistance();
	UpdateComponentTickState();
}

void UPathMovement::ApplyTransformFromCurrentDistance()
{
	ApplyTransformFromDistance(CurrentDistanceAlongPath);
}

void UPathMovement::ApplyTransformFromDistance(float DistanceAlongPath)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !CurrentPath)
	{
		return;
	}

	const FTransform NewTransform = CurrentPath->GetWorldTransformAtDistance(DistanceAlongPath);
	
	OwnerActor->SetActorLocationAndRotation(
		NewTransform.GetLocation(),
		NewTransform.GetRotation()
	);
}

void UPathMovement::SmoothReplicatedMovement(float DeltaTime)
{
	if (!CurrentPath)
	{
		return;
	}

	// 활성 이동 중이면 타겟 거리로 보간
	if (bPathMovementActive && !bReachedGoal)
	{
		ClientVisualDistance = FMath::FInterpTo(
			ClientVisualDistance,
			ClientTargetDistance,
			DeltaTime,
			ClientInterpolationSpeed
		);

		ApplyTransformFromDistance(ClientVisualDistance);
		return;
	}

	// 멈춤/도착 상태는 정확히 서버 값에 맞춤
	ClientVisualDistance = CurrentDistanceAlongPath;
	ApplyTransformFromDistance(ClientVisualDistance);

	UpdateComponentTickState();
}

void UPathMovement::UpdateComponentTickState()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (OwnerActor->HasAuthority())
	{
		SetComponentTickEnabled(CanMoveAlongPath());
		return;
	}

	const bool bNeedsClientTick =
		CurrentPath &&
		(
			bPathMovementActive ||
			!FMath::IsNearlyEqual(ClientVisualDistance, ClientTargetDistance, 1.0f)
		);

	SetComponentTickEnabled(bNeedsClientTick);
}