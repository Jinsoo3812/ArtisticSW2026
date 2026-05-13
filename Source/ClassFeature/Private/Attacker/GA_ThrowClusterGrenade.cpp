#include "Attacker/GA_ThrowClusterGrenade.h"
#include "BasePlayer.h"
#include "BaseItem.h"
#include "Kismet/GameplayStatics.h"

void UGA_ThrowClusterGrenade::DrawTrajectory()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || !IsValid(Player->EquippedItem)) return;

	FVector StartLoc = Player->EquippedItem->GetActorLocation();
	FVector LaunchDir = Player->GetBaseAimRotation().Vector();
	LaunchDir.Z += Upper;
	LaunchDir.Normalize();

	FVector LaunchVelocity = LaunchDir * ThrowSpeed;

	// TODO: 나중에 장애물용 전용 Collision Channel(ECC)을 추가하여 교체할 것.
	// 현재는 임시로 ECC_Visibility를 사용합니다.
	FPredictProjectilePathParams PredictParams(5.0f, StartLoc, LaunchVelocity, PredictedSplitTime, ECollisionChannel::ECC_Visibility, Player);
	PredictParams.DrawDebugType = EDrawDebugTrace::ForOneFrame;
	PredictParams.DrawDebugTime = TrajectoryFrequency;
	PredictParams.bTraceWithCollision = true;

	FPredictProjectilePathResult PredictResult;
	UGameplayStatics::PredictProjectilePath(Player, PredictParams, PredictResult);
}
