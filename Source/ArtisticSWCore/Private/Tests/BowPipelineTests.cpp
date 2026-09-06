#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AN_SendGameplayEvent.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimMontage.h"
#include "BaseGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "GameFramework/Character.h"
#include "Item/Components/BowComponent.h"
#include "Item/ItemData.h"
#include "Item/Projectiles/ArrowImpactVisual.h"
#include "Item/Projectiles/ArrowProjectile.h"
#include "Item/Weapons/BowItem.h"
#include "CollisionChannels.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArrowCollisionProfileTest,
	"ArtisticSW.Item.Arrow.CollisionProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FArrowCollisionProfileTest::RunTest(const FString& Parameters)
{
	FCollisionResponseTemplate ArrowProfile;
	if (!TestTrue(TEXT("ArrowProjectile profile is registered"),
		UCollisionProfile::Get()->GetProfileTemplate(TEXT("ArrowProjectile"), ArrowProfile)))
	{
		return false;
	}

	TestEqual(TEXT("ArrowProjectile is query-only"),
		ArrowProfile.CollisionEnabled, ECollisionEnabled::QueryOnly);
	TestEqual(TEXT("ArrowProjectile uses its dedicated object channel"),
		ArrowProfile.ObjectType, ECC_Arrow);
	TestEqual(TEXT("ArrowProjectile blocks ordinary static meshes"),
		ArrowProfile.ResponseToChannels.GetResponse(ECC_WorldStatic), ECR_Block);
	TestEqual(TEXT("ArrowProjectile blocks moving meshes"),
		ArrowProfile.ResponseToChannels.GetResponse(ECC_WorldDynamic), ECR_Block);
	TestEqual(TEXT("ArrowProjectile blocks ship query hulls"),
		ArrowProfile.ResponseToChannels.GetResponse(ECC_ShipDamage), ECR_Block);

	for (const FName ShipProfileName : {FName(TEXT("PlayerShipDamage")), FName(TEXT("EnemyShipDamage"))})
	{
		FCollisionResponseTemplate ShipProfile;
		if (TestTrue(*FString::Printf(TEXT("%s profile is registered"), *ShipProfileName.ToString()),
			UCollisionProfile::Get()->GetProfileTemplate(ShipProfileName, ShipProfile)))
		{
			TestEqual(*FString::Printf(TEXT("%s blocks character arrows"), *ShipProfileName.ToString()),
				ShipProfile.ResponseToChannels.GetResponse(ECC_Arrow), ECR_Block);
		}
	}

	const AArrowProjectile* ArrowCDO = GetDefault<AArrowProjectile>();
	if (TestNotNull(TEXT("Native arrow CDO exists"), ArrowCDO)
		&& TestNotNull(TEXT("Native arrow owns collision"), ArrowCDO->GetCollisionComp()))
	{
		TestEqual(TEXT("Native arrow reasserts the immutable profile"),
			ArrowCDO->GetCollisionComp()->GetCollisionProfileName(), FName(TEXT("ArrowProjectile")));
		TestEqual(TEXT("Native arrow collision object is Arrow"),
			ArrowCDO->GetCollisionComp()->GetCollisionObjectType(), ECC_Arrow);
	}

	const AArrowImpactVisual* VisualCDO = GetDefault<AArrowImpactVisual>();
	if (TestNotNull(TEXT("Impact visual CDO exists"), VisualCDO))
	{
		TestFalse(TEXT("Impact visuals never replicate"), VisualCDO->GetIsReplicated());
		TestFalse(TEXT("Impact visuals never tick"), VisualCDO->PrimaryActorTick.bCanEverTick);
	}

	UClass* PlayerArrowClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Weapon/BP_Arrow.BP_Arrow_C"));
	AArrowProjectile* BlueprintArrowCDO = PlayerArrowClass
		? PlayerArrowClass->GetDefaultObject<AArrowProjectile>()
		: nullptr;
	if (TestNotNull(TEXT("BP_Arrow CDO is loaded"), BlueprintArrowCDO)
		&& TestNotNull(TEXT("BP_Arrow CDO owns collision"), BlueprintArrowCDO->GetCollisionComp()))
	{
		BlueprintArrowCDO->OnConstruction(FTransform::Identity);
		TestEqual(TEXT("BP_Arrow construction overrides legacy Blueprint collision settings"),
			BlueprintArrowCDO->GetCollisionComp()->GetCollisionProfileName(), FName(TEXT("ArrowProjectile")));
		TestEqual(TEXT("Constructed BP_Arrow blocks ship query hulls"),
			BlueprintArrowCDO->GetCollisionComp()->GetCollisionResponseToChannel(ECC_ShipDamage), ECR_Block);
	}

	return true;
}

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
		TestNull(TEXT("BP_Bow does not duplicate the BP_Arrow mesh"),
			NockedArrowMesh->GetStaticMesh().Get());
		TestTrue(TEXT("BP_Bow does not duplicate the BP_Arrow transform"),
			NockedArrowMesh->GetRelativeTransform().Equals(FTransform::Identity));
		TestEqual(TEXT("Nocked arrow collision is disabled"),
			NockedArrowMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("Nocked arrow starts hidden"), BowBlueprintCDO->IsNockedArrowVisible());
	}

	const UClass* ArrowBlueprintClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/GameplayAbilitySystem/Weapon/BP_Arrow.BP_Arrow_C"));
	const AArrowProjectile* ArrowBlueprintCDO = ArrowBlueprintClass
		? ArrowBlueprintClass->GetDefaultObject<AArrowProjectile>()
		: nullptr;
	if (TestNotNull(TEXT("BP_Arrow loads as AArrowProjectile"), ArrowBlueprintCDO))
	{
		TestNotNull(TEXT("BP_Arrow owns the authored arrow mesh"), ArrowBlueprintCDO->GetArrowVisualMesh());
		TestTrue(TEXT("BP_Arrow collision root scale remains normalized"),
			ArrowBlueprintCDO->GetCollisionComp()->GetRelativeScale3D().Equals(FVector::OneVector));
		TestTrue(TEXT("BP_Arrow collision extent is independently authored"),
			ArrowBlueprintCDO->GetCollisionComp()->GetUnscaledBoxExtent().Equals(
				ArrowBlueprintCDO->GetCollisionHalfExtent()));

		UStaticMeshComponent* PreviewMesh = NewObject<UStaticMeshComponent>();
		if (TestTrue(TEXT("BP_Arrow can configure a presentation mesh"),
			ArrowBlueprintCDO->ApplyVisualTo(PreviewMesh)))
		{
			TestTrue(TEXT("Presentation uses BP_Arrow mesh"),
				PreviewMesh->GetStaticMesh() == ArrowBlueprintCDO->GetArrowVisualMesh());
			TestTrue(TEXT("Presentation uses BP_Arrow relative transform"),
				PreviewMesh->GetRelativeTransform().Equals(
					ArrowBlueprintCDO->GetArrowVisualRelativeTransform()));
		}
	}

	FCollisionResponseTemplate ArrowProfile;
	if (TestTrue(TEXT("ArrowProjectile profile is registered"),
		UCollisionProfile::Get()->GetProfileTemplate(TEXT("ArrowProjectile"), ArrowProfile)))
	{
		TestEqual(TEXT("ArrowProjectile uses the dedicated Arrow channel"),
			ArrowProfile.ObjectType, ECC_Arrow);
		TestEqual(TEXT("ArrowProjectile blocks ordinary static meshes"),
			ArrowProfile.ResponseToChannels.GetResponse(ECC_WorldStatic), ECR_Block);
		TestEqual(TEXT("ArrowProjectile blocks moving meshes"),
			ArrowProfile.ResponseToChannels.GetResponse(ECC_WorldDynamic), ECR_Block);
		TestEqual(TEXT("ArrowProjectile blocks ship damage query hulls"),
			ArrowProfile.ResponseToChannels.GetResponse(ECC_ShipDamage), ECR_Block);
	}

	const UItemData* ItemData = LoadObject<UItemData>(
		nullptr,
		TEXT("/Game/Blueprints/Item/DA_ItemData.DA_ItemData"));
	if (TestNotNull(TEXT("Item data loads"), ItemData))
	{
		const FItemDefinition* ShortBowDefinition =
			ItemData->FindItemDefinition(Item_Id_Weapon_Bow_ShortBow1);
		if (TestNotNull(TEXT("ShortBow1 item definition exists"), ShortBowDefinition))
		{
			TestTrue(TEXT("ShortBow1 spawns BP_Arrow"),
				ShortBowDefinition->SpawnClass.LoadSynchronous() == ArrowBlueprintClass);
		}
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
