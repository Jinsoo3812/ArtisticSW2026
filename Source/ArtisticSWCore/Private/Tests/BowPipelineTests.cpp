#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AN_SendGameplayEvent.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimMontage.h"
#include "BaseGameplayTags.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "GameFramework/Character.h"
#include "Item/Components/BowComponent.h"
#include "Item/Weapons/BowItem.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBowSocketAndPresentationAssetTest,
	"ArtisticSW.Item.Bow.SocketAndNockedArrowAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBowSocketAndPresentationAssetTest::RunTest(const FString& Parameters)
{
	const ABowItem* NativeBowCDO = GetDefault<ABowItem>();
	if (!TestNotNull(TEXT("Native Bow CDO exists"), NativeBowCDO))
	{
		return false;
	}
	TestEqual(TEXT("Player Bow resolves the character-owned Arrow_socket contract"),
		NativeBowCDO->GetCharacterArrowSocketName(), FName(TEXT("Arrow_socket")));

	const UClass* PlayerBlueprintClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/Blueprints/Player/BP_Player.BP_Player_C"));
	const ACharacter* PlayerBlueprintCDO = PlayerBlueprintClass
		? PlayerBlueprintClass->GetDefaultObject<ACharacter>()
		: nullptr;
	if (TestNotNull(TEXT("BP_Player loads as ACharacter"), PlayerBlueprintCDO)
		&& TestNotNull(TEXT("BP_Player has a character mesh"), PlayerBlueprintCDO->GetMesh()))
	{
		TestTrue(TEXT("BP_Player character skeleton contains Arrow_socket"),
			PlayerBlueprintCDO->GetMesh()->DoesSocketExist(NativeBowCDO->GetCharacterArrowSocketName()));

		const USkeletalMesh* CharacterMeshAsset = PlayerBlueprintCDO->GetMesh()->GetSkeletalMeshAsset();
		const USkeleton* CharacterSkeleton = CharacterMeshAsset ? CharacterMeshAsset->GetSkeleton() : nullptr;
		const USkeletalMeshSocket* ArrowSocket = CharacterSkeleton
			? CharacterSkeleton->FindSocket(NativeBowCDO->GetCharacterArrowSocketName())
			: nullptr;
		if (TestNotNull(TEXT("Character Arrow_socket is authored on the shared skeleton"), ArrowSocket))
		{
			TestFalse(TEXT("Arrow_socket remains attached to an authored character bone"),
				ArrowSocket->BoneName.IsNone());
		}
	}

	const UClass* BowBlueprintClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Weapon/BP_Bow.BP_Bow_C"));
	const ABowItem* BowBlueprintCDO = BowBlueprintClass
		? BowBlueprintClass->GetDefaultObject<ABowItem>()
		: nullptr;
	if (!TestNotNull(TEXT("BP_Bow loads as ABowItem"), BowBlueprintCDO))
	{
		return false;
	}
	TestEqual(TEXT("BP_Bow uses the character Arrow_socket contract"),
		BowBlueprintCDO->GetCharacterArrowSocketName(), FName(TEXT("Arrow_socket")));

	const USkeletalMeshComponent* BowMesh = BowBlueprintCDO->GetBowMesh();
	const UStaticMeshComponent* NockedArrowMesh = BowBlueprintCDO->GetNockedArrowMesh();
	if (TestNotNull(TEXT("BP_Bow has BowMesh"), BowMesh))
	{
		TestFalse(TEXT("BP_Bow no longer owns the Player Arrow_socket"),
			BowMesh->DoesSocketExist(BowBlueprintCDO->GetCharacterArrowSocketName()));
	}
	if (TestNotNull(TEXT("BP_Bow has a presentation-only nocked arrow component"), NockedArrowMesh))
	{
		TestNotNull(TEXT("Nocked arrow uses an authored static mesh"), NockedArrowMesh->GetStaticMesh().Get());
		TestEqual(TEXT("Nocked arrow collision is disabled"),
			NockedArrowMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("Nocked arrow starts hidden"), BowBlueprintCDO->IsNockedArrowVisible());
	}

	const FProperty* ReplicatedNockedProperty = FindFProperty<FProperty>(
		UBowComponent::StaticClass(), TEXT("bArrowNocked"));
	if (TestNotNull(TEXT("BowComponent exposes replicated nocked state"), ReplicatedNockedProperty))
	{
		TestTrue(TEXT("Nocked state is replicated"), ReplicatedNockedProperty->HasAnyPropertyFlags(CPF_Net));
		TestEqual(TEXT("Nocked state uses its presentation RepNotify"),
			ReplicatedNockedProperty->RepNotifyFunc, FName(TEXT("OnRep_ArrowNocked")));
	}

	const UAnimMontage* AimCycleMontage = LoadObject<UAnimMontage>(
		nullptr,
		TEXT("/Game/Anim_Logic/Anim_Assets/Bow/Bow_Anims/AM_BowAimCycle.AM_BowAimCycle"));
	if (TestNotNull(TEXT("Bow aim-cycle montage loads"), AimCycleMontage))
	{
		const int32 DrawSectionIndex = AimCycleMontage->GetSectionIndex(TEXT("Bow_Draw"));
		if (TestTrue(TEXT("Bow aim-cycle montage contains Bow_Draw"), DrawSectionIndex != INDEX_NONE))
		{
			float DrawStartTime = 0.0f;
			float DrawEndTime = 0.0f;
			AimCycleMontage->GetSectionStartAndEndTime(DrawSectionIndex, DrawStartTime, DrawEndTime);

			const FAnimNotifyEvent* NockEvent = AimCycleMontage->Notifies.FindByPredicate(
				[](const FAnimNotifyEvent& NotifyEvent)
				{
					const UAN_SendGameplayEvent* GameplayEventNotify =
						Cast<UAN_SendGameplayEvent>(NotifyEvent.Notify);
					return GameplayEventNotify
						&& GameplayEventNotify->GetEventTag() == Event_Montage_NockArrow;
				});
			if (TestNotNull(TEXT("Bow_Draw contains Event.Montage.NockArrow notify"), NockEvent))
			{
				const float NotifyTime = NockEvent->GetTime();
				TestTrue(TEXT("Nock notify remains inside Bow_Draw"),
					NotifyTime >= DrawStartTime && NotifyTime < DrawEndTime);
				TestTrue(TEXT("Nock notify also executes on a dedicated server"),
					NockEvent->bTriggerOnDedicatedServer);
			}
		}
	}

	return true;
}

#endif
