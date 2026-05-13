#include "Attacker/GA_ThrowClusterGrenade.h"
#include "AbilitySystemComponent.h"
#include "BaseItem.h"
#include "BasePlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Projectiles/GrenadeProjectile.h"

void UGA_ThrowClusterGrenade::DrawTrajectory() {
  ABasePlayer *Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
  if (!Player || !IsValid(Player->EquippedItem))
    return;

  FVector StartLoc = Player->EquippedItem->GetActorLocation();
  FVector LaunchDir = Player->GetBaseAimRotation().Vector();
  LaunchDir.Z += Upper;
  LaunchDir.Normalize();

  FVector LaunchVelocity = LaunchDir * ThrowSpeed;

  // TODO: 나중에 장애물용 전용 Collision Channel(ECC)을 추가하여 교체할 것.
  // 현재는 임시로 ECC_Visibility를 사용합니다.
  FPredictProjectilePathParams PredictParams(
      5.0f, StartLoc, LaunchVelocity, PredictedSplitTime,
      ECollisionChannel::ECC_Visibility, Player);
  PredictParams.DrawDebugType = EDrawDebugTrace::ForOneFrame;
  PredictParams.DrawDebugTime = TrajectoryFrequency;
  PredictParams.bTraceWithCollision = true;

  FPredictProjectilePathResult PredictResult;
  UGameplayStatics::PredictProjectilePath(Player, PredictParams, PredictResult);
}

void UGA_ThrowClusterGrenade::OnThrowEventReceived(FGameplayEventData Payload) {
  ABasePlayer *Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
  UAbilitySystemComponent *ASC = GetAbilitySystemComponentFromActorInfo();

  if (!Player || !ASC || !IsValid(Player->EquippedItem))
    return;

  if (Player->HasAuthority()) {
    UClass *SpawnClass = Player->EquippedItem->GetSpawnClass();
    UStaticMesh *ItemMesh = Player->EquippedItem->GetStaticMesh();

    if (!SpawnClass ||
        !SpawnClass->IsChildOf(AGrenadeProjectile::StaticClass()))
      return;

    FVector LaunchDir = Player->GetBaseAimRotation().Vector();
    LaunchDir.Z += Upper;
    LaunchDir.Normalize();
    FVector LaunchVelocity = LaunchDir * ThrowSpeed;

    FVector SpawnLocation = Player->EquippedItem->GetActorLocation();
    FTransform SpawnTransform(LaunchVelocity.Rotation(), SpawnLocation);

    FGameplayEffectSpecHandle DamageSpecHandle;
    if (DamageEffectClass) {
      FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
      ContextHandle.AddInstigator(Player, Player);
      DamageSpecHandle =
          ASC->MakeOutgoingSpec(DamageEffectClass, 1.0f, ContextHandle);
    }

    AGrenadeProjectile *Grenade =
        GetWorld()->SpawnActorDeferred<AGrenadeProjectile>(
            SpawnClass, SpawnTransform, Player, Player,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

    if (Grenade) {
      Grenade->SetInstigator(Player);
      Grenade->SetOwner(Player);
      Grenade->DamageEffectSpecHandle = DamageSpecHandle;
      Grenade->SetGrenadeMesh(ItemMesh);

      // === 추가된 부분 ===
      // GA에서 설정한 PredictedSplitTime을 투사체의 실제 폭발 타이머인
      // ExplosionDelay에 주입
      Grenade->ExplosionDelay = PredictedSplitTime;

      Grenade->FinishSpawning(SpawnTransform);
      Grenade->LaunchProjectile(LaunchVelocity);
    }

    Player->UseEquippedItem(true);
  }
}
