#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/InventoryComponent.h"
#include "Item/ItemData.h"
#include "Skills/PlayerSkillComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlayerSkillMaterialDatabaseTest,
	"ArtisticSW.PlayerSkill.MaterialDatabase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerSkillMaterialDatabaseTest::RunTest(const FString& Parameters)
{
	const UItemData* ItemData = LoadObject<UItemData>(
		nullptr,
		TEXT("/Game/Blueprints/Item/DA_ItemData.DA_ItemData"));
	if (!TestNotNull(TEXT("Shared item database is loadable"), ItemData))
	{
		return false;
	}

	const FItemDefinition* RareMaterial =
		ItemData->FindItemDefinition(Item_Id_Material_SkillMaterial_RareSkill);
	const FItemDefinition* EpicMaterial =
		ItemData->FindItemDefinition(Item_Id_Material_SkillMaterial_EpicSkill);
	const FItemDefinition* LegendaryMaterial =
		ItemData->FindItemDefinition(Item_Id_Material_SkillMaterial_LegendarySkill);
	TestNotNull(TEXT("Rare skill-use material is registered"), RareMaterial);
	TestNotNull(TEXT("Epic skill-use material is registered"), EpicMaterial);
	TestNotNull(TEXT("Legendary skill-use material is registered"), LegendaryMaterial);
	TestTrue(TEXT("Rare skill-use item is categorized as material"),
		RareMaterial && RareMaterial->CategoryTag.MatchesTagExact(Item_Category_Material));
	TestTrue(TEXT("Epic skill-use item is categorized as material"),
		EpicMaterial && EpicMaterial->CategoryTag.MatchesTagExact(Item_Category_Material));
	TestTrue(TEXT("Legendary skill-use item is categorized as material"),
		LegendaryMaterial && LegendaryMaterial->CategoryTag.MatchesTagExact(Item_Category_Material));
	TestNotNull(TEXT("Gravity Vortex skill identity is registered"),
		ItemData->FindItemDefinition(Item_Id_Skill_GravityVortex));
	TestNotNull(TEXT("Water Bomb skill identity is registered"),
		ItemData->FindItemDefinition(Item_Id_Skill_WaterBomb));
	TestNotNull(TEXT("Bombardment skill identity is registered"),
		ItemData->FindItemDefinition(Item_Id_Skill_Bombardment));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlayerSkillUnlockAndConsumptionTest,
	"ArtisticSW.PlayerSkill.UnlockAndInventoryConsumption",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerSkillUnlockAndConsumptionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("PlayerSkillSystemWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	auto CleanupWorld = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	ABasePlayerState* PlayerState = World->SpawnActor<ABasePlayerState>();
	APlayerController* PlayerController = World->SpawnActor<APlayerController>();
	ABasePlayer* Player = World->SpawnActor<ABasePlayer>();
	if (!TestNotNull(TEXT("PlayerState is spawned"), PlayerState)
		|| !TestNotNull(TEXT("PlayerController is spawned"), PlayerController)
		|| !TestNotNull(TEXT("Player is spawned"), Player))
	{
		CleanupWorld();
		return false;
	}

	Player->SetPlayerState(PlayerState);
	PlayerController->Possess(Player);

	UPlayerSkillComponent* SkillComponent = PlayerState->GetPlayerSkillComponent();
	UInventoryComponent* Inventory = Player->GetInventoryComponent();
	if (!TestNotNull(TEXT("Player skill component exists"), SkillComponent)
		|| !TestNotNull(TEXT("Player inventory exists"), Inventory))
	{
		CleanupWorld();
		return false;
	}

	TestEqual(TEXT("Current Generator uses the RareSkill material"),
		SkillComponent->GetUsageMaterialTag(GameplayAbility_Skill_GravityVortex),
		Item_Id_Material_SkillMaterial_RareSkill.GetTag());
	TestEqual(TEXT("Water Bomb uses the EpicSkill material"),
		SkillComponent->GetUsageMaterialTag(GameplayAbility_Skill_WaterBomb),
		Item_Id_Material_SkillMaterial_EpicSkill.GetTag());
	TestEqual(TEXT("Bombardment uses the LegendarySkill material"),
		SkillComponent->GetUsageMaterialTag(GameplayAbility_Skill_Bombardment),
		Item_Id_Material_SkillMaterial_LegendarySkill.GetTag());

	TestEqual(TEXT("Two skill-use materials are added"),
		Inventory->AddItem(Item_Id_Material_SkillMaterial_RareSkill, 2), 2);
	TestFalse(TEXT("A locked skill cannot be used even with material"),
		Player->CanUseSkill(GameplayAbility_Skill_GravityVortex));
	TestFalse(TEXT("A locked skill cannot consume material"),
		Player->TryConsumeSkillUse(GameplayAbility_Skill_GravityVortex));
	TestEqual(TEXT("Rejected use preserves the material count"),
		Inventory->GetItemCount(Item_Id_Material_SkillMaterial_RareSkill), 2);

	Player->bBypassSkillRequirementsForTesting = true;
	TestTrue(TEXT("Common player test switch bypasses lock and material requirements"),
		Player->CanUseSkill(GameplayAbility_Skill_WaterBomb));
	TestTrue(TEXT("Bypassed use succeeds without consuming an item"),
		Player->TryConsumeSkillUse(GameplayAbility_Skill_WaterBomb));
	TestEqual(TEXT("Bypassed use does not change unrelated inventory"),
		Inventory->GetItemCount(Item_Id_Material_SkillMaterial_RareSkill), 2);
	Player->bBypassSkillRequirementsForTesting = false;

	TestTrue(TEXT("Story API unlocks the skill"),
		SkillComponent->UnlockSkill(GameplayAbility_Skill_GravityVortex));
	TestTrue(TEXT("Unlocked skill with material can be used"),
		Player->CanUseSkill(GameplayAbility_Skill_GravityVortex));
	TestTrue(TEXT("Completed skill use consumes one material"),
		Player->TryConsumeSkillUse(GameplayAbility_Skill_GravityVortex));
	TestEqual(TEXT("Remaining use count mirrors inventory"),
		SkillComponent->GetSkillUseCount(GameplayAbility_Skill_GravityVortex), 1);

	CleanupWorld();
	return true;
}

#endif
