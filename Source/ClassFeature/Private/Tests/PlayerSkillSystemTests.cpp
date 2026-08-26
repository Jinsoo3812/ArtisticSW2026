#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "Crafting/CraftingAccessComponent.h"
#include "Crafting/CraftingComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/InventoryComponent.h"
#include "Item/ItemData.h"
#include "Item/ItemSubsystem.h"
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
	TestFalse(TEXT("A new skill has not met its crafting unlock condition"),
		SkillComponent->IsSkillUnlockConditionMet(GameplayAbility_Skill_GravityVortex));
	TestTrue(TEXT("Progression can open crafting without granting the skill"),
		SkillComponent->SetSkillUnlockConditionMet(GameplayAbility_Skill_GravityVortex, true));
	TestTrue(TEXT("The crafting unlock condition is stored separately"),
		SkillComponent->IsSkillUnlockConditionMet(GameplayAbility_Skill_GravityVortex));
	TestFalse(TEXT("Meeting the condition does not grant the skill"),
		SkillComponent->IsSkillUnlocked(GameplayAbility_Skill_GravityVortex));
	TestEqual(TEXT("Rejected use preserves the material count"),
		Inventory->GetItemCount(Item_Id_Material_SkillMaterial_RareSkill), 2);
	TestEqual(TEXT("Gravity Vortex skill identity is added"),
		Inventory->AddItem(Item_Id_Skill_GravityVortex, 1), 1);
	TestFalse(TEXT("Owning the skill identity does not bypass permanent progression"),
		SkillComponent->IsSkillUnlocked(GameplayAbility_Skill_GravityVortex));
	TestTrue(TEXT("Skill identity can be removed"),
		Inventory->RemoveItem(Item_Id_Skill_GravityVortex, 1));
	TestFalse(TEXT("Removing the skill identity does not change progression"),
		SkillComponent->IsSkillUnlocked(GameplayAbility_Skill_GravityVortex));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlayerSkillCraftingUnlockTest,
	"ArtisticSW.PlayerSkill.CraftingPermanentUnlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerSkillCraftingUnlockTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("PlayerSkillCraftingWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	auto CleanupWorld = [World]()
	{
		if (UItemSubsystem* Items = World->GetSubsystem<UItemSubsystem>())
		{
			Items->ClearCraftingRecipesForTesting();
		}
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	UItemSubsystem* Items = World->GetSubsystem<UItemSubsystem>();
	ABasePlayerState* PlayerState = World->SpawnActor<ABasePlayerState>();
	APlayerController* PlayerController = World->SpawnActor<APlayerController>();
	ABasePlayer* Player = World->SpawnActor<ABasePlayer>();
	AActor* Facility = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Item subsystem exists"), Items)
		|| !TestNotNull(TEXT("Player state exists"), PlayerState)
		|| !TestNotNull(TEXT("Player controller exists"), PlayerController)
		|| !TestNotNull(TEXT("Player exists"), Player)
		|| !TestNotNull(TEXT("Facility exists"), Facility))
	{
		CleanupWorld();
		return false;
	}
	Items->ClearCraftingRecipesForTesting();
	Player->SetPlayerState(PlayerState);
	PlayerController->Possess(Player);
	UCraftingAccessComponent* Access = NewObject<UCraftingAccessComponent>(Facility, TEXT("SkillCraftingAccess"));
	Facility->AddInstanceComponent(Access);
	Access->RegisterComponent();

	FCraftingRecipeRow SkillRecipe;
	SkillRecipe.ResultItemTag = Item_Id_Skill_GravityVortex;
	SkillRecipe.ResultQuantity = 1;
	FCraftingItemStack Ingredient;
	Ingredient.ItemTag = Item_Id_Material_WeaponMaterial_Wood;
	Ingredient.Quantity = 2;
	SkillRecipe.Ingredients.Add(Ingredient);
	Items->AddCraftingRecipeForTesting(TEXT("Test_GravityVortexUnlock"), SkillRecipe);
	Items->AddCraftingRecipeForTesting(TEXT("Test_GravityVortexDuplicate"), SkillRecipe);
	TArray<FString> ValidationErrors;
	TestFalse(TEXT("Duplicate recipes for one skill result are rejected"), Items->ValidateCraftingRecipes(ValidationErrors));
	TestTrue(TEXT("Duplicate skill validation explains the single-recipe rule"), ValidationErrors.ContainsByPredicate([](const FString& Error)
	{
		return Error.Contains(TEXT("exactly one recipe"));
	}));
	Items->ClearCraftingRecipesForTesting();
	Items->AddCraftingRecipeForTesting(TEXT("Test_GravityVortexUnlock"), SkillRecipe);

	World->BeginPlay();
	UInventoryComponent* Inventory = Player->GetInventoryComponent();
	UCraftingComponent* Crafting = Player->GetCraftingComponent();
	UPlayerSkillComponent* Skills = PlayerState->GetPlayerSkillComponent();
	if (!TestNotNull(TEXT("Inventory exists"), Inventory)
		|| !TestNotNull(TEXT("Crafting component exists"), Crafting)
		|| !TestNotNull(TEXT("Skill component exists"), Skills))
	{
		CleanupWorld();
		return false;
	}

	FCraftingListQuery SkillQuery;
	SkillQuery.ResultItemTag = Item_Id_Skill_GravityVortex;
	TestEqual(TEXT("Exact skill result query returns its unique recipe"), Crafting->GetCraftableList(SkillQuery).Num(), 1);
	Inventory->AddItem(Item_Id_Material_WeaponMaterial_Wood, 2);
	Crafting->OpenCraftingScreen(Facility);

	FCraftingRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.RecipeId = TEXT("Test_GravityVortexUnlock");
	Request.CraftCount = 1;
	Request.Output.Type = ECraftingOutputType::SkillUnlock;
	Crafting->RequestCraft(Request);
	TestEqual(TEXT("Skill crafting rejects an unmet progression condition"),
		Crafting->GetLastCraftingResult().Reason, ECraftingFailureReason::UnlockConditionNotMet);
	TestEqual(TEXT("Rejected progression condition preserves materials"),
		Inventory->GetItemCount(Ingredient.ItemTag), 2);
	TestTrue(TEXT("Progression condition opens skill crafting"),
		Skills->SetSkillUnlockConditionMet(GameplayAbility_Skill_GravityVortex, true));

	Request.RequestId = FGuid::NewGuid();
	Crafting->RequestCraft(Request);
	TestEqual(TEXT("Skill crafting succeeds"), Crafting->GetLastCraftingResult().Reason, ECraftingFailureReason::Success);
	TestTrue(TEXT("Crafted skill is permanently unlocked"), Skills->IsSkillUnlocked(GameplayAbility_Skill_GravityVortex));
	TestEqual(TEXT("Skill crafting consumes all required materials"), Inventory->GetItemCount(Ingredient.ItemTag), 0);
	TestEqual(TEXT("Permanent unlock does not add a removable skill item"), Inventory->GetItemCount(Item_Id_Skill_GravityVortex), 0);

	Request.RequestId = FGuid::NewGuid();
	Crafting->RequestCraft(Request);
	TestEqual(TEXT("An unlocked skill cannot be crafted again"), Crafting->GetLastCraftingResult().Reason, ECraftingFailureReason::AlreadyUnlocked);
	CleanupWorld();
	return true;
}

#endif
