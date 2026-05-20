#include "Attacker/GA_ThrowClusterGrenade.h"
#include "AbilitySystemComponent.h"
#include "BaseItem.h"
#include "BasePlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Projectiles/GrenadeProjectile.h"
#include "Projectiles/ClusterGrenadeProjectile.h"
#include "Projectiles/SubMunitionProjectile.h"
#include "Inventory/InventoryComponent.h"
#include "UI/ClusterGrenadeSelectionWidget.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "BaseGameplayTags.h"

void UGA_ThrowClusterGrenade::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
  ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
  if (Player && Player->GetInventoryComponent())
  {
      UInventoryComponent* Inv = Player->GetInventoryComponent();
      AvailableSubMunitions.Empty();

      // Find available sub-munitions from Inventory
      const TArray<FInventorySlot>& Materials = Inv->GetSlots();
      for (const FInventorySlot& Entry : Materials)
      {
          if (Entry.ItemTag.MatchesTag(Item_Material_Submunition) && Entry.Count > 0)
          {
              AvailableSubMunitions.Add(Entry.ItemTag);
          }
      }

      if (AvailableSubMunitions.IsEmpty())
      {
          UE_LOG(LogTemp, Warning, TEXT("UGA_ThrowClusterGrenade::ActivateAbility : No Sub-munitions available. Cancel."));
          EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
          return;
      }
      
      UE_LOG(LogTemp, Log, TEXT("UGA_ThrowClusterGrenade::ActivateAbility : Found %d types of sub-munitions."), AvailableSubMunitions.Num());

      // Initialize Selection
      CurrentSelectionIndex = 0;

      // Show UI (Only for local player)
      if (Player->IsLocallyControlled() && SelectionWidgetClass)
      {
          SelectionWidget = CreateWidget<UClusterGrenadeSelectionWidget>(GetWorld(), SelectionWidgetClass);
          if (SelectionWidget)
          {
              SelectionWidget->AddToViewport();
              UpdateSelectionUI();
          }
      }

      // Wait for Mouse Wheel Events
      WheelUpTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_WheelUp);
      if (WheelUpTask)
      {
          WheelUpTask->EventReceived.AddDynamic(this, &UGA_ThrowClusterGrenade::OnMouseWheelUp);
          WheelUpTask->ReadyForActivation();
      }

      WheelDownTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_WheelDown);
      if (WheelDownTask)
      {
          WheelDownTask->EventReceived.AddDynamic(this, &UGA_ThrowClusterGrenade::OnMouseWheelDown);
          WheelDownTask->ReadyForActivation();
      }
  }

  Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UGA_ThrowClusterGrenade::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
  if (SelectionWidget)
  {
      SelectionWidget->RemoveFromParent();
      SelectionWidget = nullptr;
  }

  if (WheelUpTask)
  {
      WheelUpTask->EndTask();
      WheelUpTask = nullptr;
  }

  if (WheelDownTask)
  {
      WheelDownTask->EndTask();
      WheelDownTask = nullptr;
  }

  Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_ThrowClusterGrenade::OnMouseWheelUp(FGameplayEventData Payload)
{
  if (AvailableSubMunitions.Num() > 0)
  {
      CurrentSelectionIndex = (CurrentSelectionIndex - 1 + AvailableSubMunitions.Num()) % AvailableSubMunitions.Num();
      FGameplayTag SelectedTag = AvailableSubMunitions[CurrentSelectionIndex];
      UE_LOG(LogTemp, Log, TEXT("UGA_ThrowClusterGrenade::OnMouseWheelUp : Index changed to %d, Tag: %s"), CurrentSelectionIndex, *SelectedTag.ToString());
      UpdateSelectionUI();
  }
}

void UGA_ThrowClusterGrenade::OnMouseWheelDown(FGameplayEventData Payload)
{
  if (AvailableSubMunitions.Num() > 0)
  {
      CurrentSelectionIndex = (CurrentSelectionIndex + 1) % AvailableSubMunitions.Num();
      FGameplayTag SelectedTag = AvailableSubMunitions[CurrentSelectionIndex];
      UE_LOG(LogTemp, Log, TEXT("UGA_ThrowClusterGrenade::OnMouseWheelDown : Index changed to %d, Tag: %s"), CurrentSelectionIndex, *SelectedTag.ToString());
      UpdateSelectionUI();
  }
}

void UGA_ThrowClusterGrenade::UpdateSelectionUI()
{
  if (SelectionWidget && AvailableSubMunitions.IsValidIndex(CurrentSelectionIndex))
  {
      ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
      if (Player && Player->GetInventoryComponent())
      {
          FGameplayTag SelectedTag = AvailableSubMunitions[CurrentSelectionIndex];
          UInventoryComponent* Inv = Player->GetInventoryComponent();
          
          FText ItemName = Inv->GetMaterialName(SelectedTag);
          UTexture2D* ItemIcon = Inv->GetMaterialIcon(SelectedTag);
          int32 Count = Inv->GetMaterialCount(SelectedTag);

          SelectionWidget->UpdateSelection(SelectedTag, ItemName, ItemIcon, Count);
          SelectionWidget->OnSelectionChanged();
      }
  }
}


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
  PredictParams.OverrideGravityZ = 0.001f; // 직선 궤적을 위해 중력을 거의 0으로 설정
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
      // Set SubMunition Class via Selected Tag and Consume from Inventory
      AClusterGrenadeProjectile* ClusterGrenade = Cast<AClusterGrenadeProjectile>(Grenade);
      if (ClusterGrenade && AvailableSubMunitions.IsValidIndex(CurrentSelectionIndex))
      {
          FGameplayTag SelectedTag = AvailableSubMunitions[CurrentSelectionIndex];
          UE_LOG(LogTemp, Log, TEXT("UGA_ThrowClusterGrenade::OnThrowEventReceived : Firing with Submunition Tag: %s"), *SelectedTag.ToString());
          
          if (Player->HasAuthority())
          {
              Player->GetInventoryComponent()->RemoveMaterial(SelectedTag, 1);
          }

          TSubclassOf<ASubMunitionProjectile>* SubClassPtr = SubMunitionClassMap.Find(SelectedTag);
          if (SubClassPtr && *SubClassPtr)
          {
              UE_LOG(LogTemp, Log, TEXT("UGA_ThrowClusterGrenade::OnThrowEventReceived : Found class %s in map. Injecting..."), *(*SubClassPtr)->GetName());
              ClusterGrenade->SetSubMunitionClass(*SubClassPtr);
          }
          else
          {
              UE_LOG(LogTemp, Warning, TEXT("UGA_ThrowClusterGrenade::OnThrowEventReceived : Could NOT find class for Tag %s in SubMunitionClassMap!"), *SelectedTag.ToString());
          }
      }

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
