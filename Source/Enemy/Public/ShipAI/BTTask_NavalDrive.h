#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_NavalDrive.generated.h"

UENUM(BlueprintType)
enum class ENavalCombatState : uint8
{
	Idle,
	Approach,
	Orbit,
	Retreat,
	Return
};

UCLASS()
class ENEMY_API UBTTask_NavalDrive : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_NavalDrive();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

protected:
	// 블랙보드 키: 감지 대상 배
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetShipKey;

	// 발견 거리 (이 범위 밖이면 대기/Idle 상태)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Naval AI|Distances")
	float DetectionDistance = 10000.f;

	// 선회 타겟 거리 (이상적인 대치 유지 거리)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Naval AI|Distances")
	float IdealDistance = 2000.f;

	// 선회 상태 유지 오프셋 (완충 마진: 이 범위 내에서는 선회가 흔들림 없이 유지됨)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Naval AI|Distances")
	float OrbitTolerance = 1500.f;

	// 위험 거리 (이 거리보다 좁혀지면 회피/후퇴 개시)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Naval AI|Distances")
	float DangerCloseDistance = 1000.f;

	// Orbit 기동 시 시계 방향(true) 또는 반시계 방향(false)으로 돌지 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Naval AI")
	bool bOrbitClockwise = true;

	// 배의 앞뒤 추진력 배율 (기본 1.0f)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Naval AI|Speed Override")
	float ForwardForceMultiplier = 1.0f;

	// 배의 조타 회전력 배율 (기본 1.0f)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Naval AI|Speed Override")
	float TurnTorqueMultiplier = 1.0f;

	// 조타 범위(이상적 거리, 위험 거리)를 바다 위에 디버그 원으로 그릴지 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Naval AI|Debug")
	bool bShowDebugRanges = true;

	// 디버그 라인의 Z축 오프셋 (바다에 파묻히지 않게 위로 띄울 값)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Naval AI|Debug")
	float DebugZOffset = 100.f;

private:
	// 각 AI Pawn 객체별 고유 인스턴스 전용 상태 변수
	ENavalCombatState CurrentState = ENavalCombatState::Idle;

	// 플레이어가 조종 중이거나 탑승 중인 배를 월드에서 찾아 반환하는 헬퍼 함수
	class AShip* FindPlayerShip(UWorld* World, APawn* AIPawn) const;
};
