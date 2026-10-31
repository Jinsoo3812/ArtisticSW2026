#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PlayerLifeTestWorld.h"
#include "Equipment/PlayerEquipmentComponent.h"
#include "Equipment/WeaponDefinition.h"
#include "Equipment/WeaponAnimationDataAsset.h"
#include "Animation/AN_EquipmentAttachItem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "BaseGameplayTags.h"
#include "ItemSubSystem.h"
#include "Item/BaseItem.h"
#include "Item/Weapons/BowItem.h"
#include "ItemData.h"
#include "SwimmingComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentNotifyLifecycleTest,
 "ArtisticSW.Weapon.EquipmentNotifyLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEquipmentNotifyLifecycleTest::RunTest(const FString& Parameters)
{
 PlayerLifeTests::FWorld Fixture;
 auto Life = Fixture.SpawnLife();
 auto* Player = Life.Player;
 Player->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
 Player->GetMesh()->InitAnim(true);
 auto* Equipment = Player->GetEquipmentComponent();
 Equipment->PlayerOwner = Player;
 auto* Items = Fixture.World->GetSubsystem<UItemSubsystem>();
 auto Start = [&](FGameplayTag Tag) {
  auto* Item = Items->SpawnItem(Tag, Player->GetActorTransform(), EItemState::InItemSlot, Player);
  // This fixture does not begin play for the world; initialize spawned item actors explicitly.
  if (Item && !Item->HasActorBegunPlay()) Item->DispatchBeginPlay();
  const auto* Entry = Equipment->ResolveWeaponAnimationEntry(Item);
  AddInfo(FString::Printf(TEXT("Equip fixture: tag=%s allowed=%d montage=%s swimming=%d state=%d anim=%s"),
   *Tag.ToString(), Equipment->CanChangeEquipment(), *GetNameSafe(Entry ? Entry->EquipMontage.Get() : nullptr),
   Player->GetSwimmingComponent() && Player->GetSwimmingComponent()->IsCustomSwimming(), static_cast<int32>(Equipment->EquipmentState), *GetNameSafe(Player->GetMesh()->GetAnimInstance())));
  Equipment->StartEquipItem(Item, FGameplayTag());
  return Item;
 };
 auto* Sword = Start(Item_Id_Weapon_Sword_SwordA1);
 if (!TestNotNull(TEXT("Pending sword"), Sword) || !TestNotNull(TEXT("Sword montage"), Equipment->ActiveEquipmentMontage.Get())) return false;
 auto* Montage = Equipment->ActiveEquipmentMontage.Get();
 TestTrue(TEXT("Sword hidden before notify"), Sword->IsHidden());
 TestNull(TEXT("Sword not committed before notify"), Player->EquippedItem.Get());
 TestFalse(TEXT("Equip does not extract movement root motion"), Montage->HasRootMotion());
 TestEqual(TEXT("Upper body slot"), Montage->SlotAnimTracks[0].SlotName, FName(TEXT("UpperBody")));
 Equipment->HandleEquipmentAttachNotify(NewObject<UAnimMontage>());
 TestNull(TEXT("Foreign montage cannot equip"), Player->EquippedItem.Get());
 auto* Notify = NewObject<UAN_EquipmentAttachItem>();
 Notify->Notify(Player->GetMesh(), Montage, FAnimNotifyEventReference());
 TestTrue(TEXT("Notify equips sword"), Player->EquippedItem == Sword);
 TestFalse(TEXT("Sword appears at notify"), Sword->IsHidden());
 TestTrue(TEXT("Sword attached to character"), Sword->GetAttachParentActor() == Player);
 TestFalse(TEXT("Pose refresh released at commit"), Equipment->bOwnsEquipmentPoseRefresh);
 Notify->Notify(Player->GetMesh(), Montage, FAnimNotifyEventReference());
 TestTrue(TEXT("Duplicate notify leaves committed sword intact"), Player->EquippedItem == Sword);
 auto* MissingNotify = Start(Item_Id_Weapon_Sword_SwordA2);
 Equipment->OnEquipmentMontageEnded(Equipment->ActiveEquipmentMontage, false);
 TestTrue(TEXT("Missing notify preserves previous weapon"), Player->EquippedItem == Sword);
 TestFalse(TEXT("Missing notify destroys pending representation"), IsValid(MissingNotify));
 TestFalse(TEXT("No stuck equipment transition"), Equipment->IsEquipmentTransitioning());
 auto* Interrupted = Start(Item_Id_Weapon_Bow_ShortBow1);
 Equipment->OnEquipmentMontageEnded(Equipment->ActiveEquipmentMontage, true);
 TestFalse(TEXT("Interrupted representation removed"), IsValid(Interrupted));
 TestTrue(TEXT("Interrupted equip preserves sword"), Player->EquippedItem == Sword);
 auto* Bow = Cast<ABowItem>(Start(Item_Id_Weapon_Bow_ShortBow1));
 Montage = Equipment->ActiveEquipmentMontage;
 if (!TestNotNull(TEXT("Bow montage"), Montage)) return false;
 TestTrue(TEXT("Bow hidden before notify"), Bow->IsHidden());
 TestEqual(TEXT("Bow upper body slot"), Montage->SlotAnimTracks[0].SlotName, FName(TEXT("UpperBody")));
 Notify->Notify(Player->GetMesh(), Montage, FAnimNotifyEventReference());
 TestTrue(TEXT("Notify equips bow"), Player->EquippedItem == Bow);
 TestFalse(TEXT("Bow visible at notify"), Bow->IsHidden());
 TestTrue(TEXT("Bow arrow anchor bound"), Bow->GetArrowAnchorMesh() == Player->GetMesh());
 return !HasAnyErrors();
}
#endif
