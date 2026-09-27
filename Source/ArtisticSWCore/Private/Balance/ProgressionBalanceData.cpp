#include "Balance/ProgressionBalanceData.h"

#include "BaseGameplayTags.h"
#include "Settings_Item.h"
#include "Engine/DataTable.h"

namespace
{
	void AddLoot(FProgressionChestPool& Pool, const TCHAR* Name, FGameplayTag Tag,
		int32 MinCount, int32 MaxCount, float Weight)
	{
		if (!Tag.IsValid() || Weight <= 0.f)
		{
			return;
		}
		FProgressionLootEntry& Entry = Pool.Entries.AddDefaulted_GetRef();
		Entry.RowName = FName(Name);
		Entry.ItemTag = Tag;
		Entry.MinCount = MinCount;
		Entry.MaxCount = MaxCount;
		Entry.Weight = Weight;
	}

	void AddCost(TArray<FProgressionMaterialCost>& Costs, EProgressionMaterialTrack Track, int32 Tier,
		FGameplayTag BaseTag, int32 BaseQuantity, FGameplayTag PremiumTag = {}, int32 PremiumQuantity = 0)
	{
		FProgressionMaterialCost& Cost = Costs.AddDefaulted_GetRef();
		Cost.Track = Track;
		Cost.Tier = Tier;
		Cost.BaseMaterial = BaseTag;
		Cost.BaseQuantity = BaseQuantity;
		Cost.PremiumMaterial = PremiumTag;
		Cost.PremiumQuantity = PremiumQuantity;
	}

	void AppendCost(TArray<FCraftingItemStack>& Costs, FGameplayTag Tag, int32 Quantity)
	{
		if (Tag.IsValid() && Quantity > 0)
		{
			if (FCraftingItemStack* Existing = Costs.FindByPredicate(
				[Tag](const FCraftingItemStack& Stack) { return Stack.ItemTag == Tag; }))
			{
				Existing->Quantity += Quantity;
				return;
			}
			FCraftingItemStack& Stack = Costs.AddDefaulted_GetRef();
			Stack.ItemTag = Tag;
			Stack.Quantity = Quantity;
		}
	}

	int32 RecipeTier(const FCraftingRecipeRow& Recipe)
	{
		if (Recipe.ProgressionTier > 0) return Recipe.ProgressionTier;
		const FString Name = Recipe.ResultItemTag.ToString();
		if (Name.StartsWith(TEXT("Item.Id.Weapon.")) && !Name.IsEmpty())
		{
			const TCHAR Last = Name[Name.Len() - 1];
			return Last >= '1' && Last <= '4' ? Last - '0' : 0;
		}
		if (Name.EndsWith(TEXT(".Medicine")) || Name.EndsWith(TEXT(".Doraji"))) return 1;
		if (Name.EndsWith(TEXT(".Tangyak")) || Name.EndsWith(TEXT(".Chungshimhwan"))) return 2;
		if (Name.EndsWith(TEXT(".Elixir")) || Name.EndsWith(TEXT(".Gongjindan"))) return 3;
		if (Name.EndsWith(TEXT(".Panacea")) || Name.EndsWith(TEXT(".RoyalGongjindan"))) return 4;
		return 0;
	}

	EProgressionRecipeTrack RecipeTrack(const FCraftingRecipeRow& Recipe)
	{
		if (Recipe.ProgressionTrack != EProgressionRecipeTrack::None) return Recipe.ProgressionTrack;
		const FString Name = Recipe.ResultItemTag.ToString();
		if (Name.StartsWith(TEXT("Item.Id.Weapon."))) return EProgressionRecipeTrack::Weapon;
		if (Name.StartsWith(TEXT("Item.Id.Consumables."))) return EProgressionRecipeTrack::Consumable;
		return EProgressionRecipeTrack::None;
	}

	float Affinity(EProgressionChestKind Kind, FGameplayTag Tag)
	{
		const FString Name = Tag.ToString();
		const bool bWeapon = Name.StartsWith(TEXT("Item.Id.Material.WeaponMaterial."));
		const bool bConsumable = Name.StartsWith(TEXT("Item.Id.Material.ConsumablesMaterial."));
		const bool bShip = Name.StartsWith(TEXT("Item.Id.Material.ShipMaterials."));
		switch (Kind)
		{
		case EProgressionChestKind::ShipGuarded: return bWeapon ? .8f : bConsumable ? .7f : bShip ? 1.6f : 1.f;
		case EProgressionChestKind::IslandGuarded: return bWeapon ? 1.6f : bConsumable ? 1.3f : bShip ? .6f : 1.f;
		case EProgressionChestKind::OceanRandom: return bWeapon ? .6f : bConsumable ? .6f : bShip ? 1.4f : 1.f;
		case EProgressionChestKind::IslandRandom: return bWeapon ? 1.1f : bConsumable ? 1.1f : bShip ? .6f : 1.f;
		default: return 0.f;
		}
	}
}

UProgressionBalanceData::UProgressionBalanceData()
{
	const FGameplayTag WeaponBasic[] = {
		Item_Id_Material_WeaponMaterial_Wood,
		Item_Id_Material_WeaponMaterial_Iron,
		Item_Id_Material_WeaponMaterial_Iron,
		Item_Id_Material_WeaponMaterial_Iron};
	const FGameplayTag WeaponAdvanced[] = {
		Item_Id_Material_WeaponMaterial_GoodWood,
		Item_Id_Material_WeaponMaterial_GoodWood,
		Item_Id_Material_WeaponMaterial_GoodIron,
		Item_Id_Material_WeaponMaterial_GoodIron};
	const FGameplayTag ConsumableBasic[] = {
		Item_Id_Material_ConsumablesMaterial_Pear,
		Item_Id_Material_ConsumablesMaterial_Herbs,
		Item_Id_Material_ConsumablesMaterial_Herbs,
		Item_Id_Material_ConsumablesMaterial_Herbs};
	const FGameplayTag ConsumableAdvanced[] = {
		Item_Id_Material_ConsumablesMaterial_GoodPear,
		Item_Id_Material_ConsumablesMaterial_GoodPear,
		Item_Id_Material_ConsumablesMaterial_GoodHerbs,
		Item_Id_Material_ConsumablesMaterial_GoodHerbs};
	const FGameplayTag ShipBasic[] = {
		Item_Id_Material_ShipMaterials_WoodenPlank,
		Item_Id_Material_ShipMaterials_IronPlate,
		Item_Id_Material_ShipMaterials_IronPlate,
		Item_Id_Material_ShipMaterials_IronPlate};
	const FGameplayTag ShipAdvanced[] = {
		Item_Id_Material_ShipMaterials_GoodWoodenPlank,
		Item_Id_Material_ShipMaterials_GoodWoodenPlank,
		Item_Id_Material_ShipMaterials_GoodIronPlate,
		Item_Id_Material_ShipMaterials_GoodIronPlate};

	for (int32 ZoneIndex = 0; ZoneIndex < 4; ++ZoneIndex)
	{
		const EProgressionZone Zone = static_cast<EProgressionZone>(ZoneIndex);
		FProgressionZoneTarget& Target = ZoneTargets.AddDefaulted_GetRef();
		Target.Zone = Zone;
		FProgressionZonePlan& Plan = ZonePlans.AddDefaulted_GetRef();
		Plan.Zone = Zone;

		for (int32 KindIndex = 0; KindIndex < 4; ++KindIndex)
		{
			const EProgressionChestKind Kind = static_cast<EProgressionChestKind>(KindIndex);
			FProgressionChestPool& Pool = ChestPools.AddDefaulted_GetRef();
			Pool.Zone = Zone;
			Pool.Kind = Kind;
			Pool.RollCount = (Kind == EProgressionChestKind::IslandGuarded ? 5 :
				Kind == EProgressionChestKind::ShipGuarded ? 4 : 3) + ZoneIndex;
			Pool.SlotCount = 8 + ZoneIndex * 2;

			float WeaponWeight = 0.f;
			float ConsumableWeight = 0.f;
			float ShipWeight = 0.f;
			switch (Kind)
			{
			case EProgressionChestKind::ShipGuarded: WeaponWeight = 35.f; ConsumableWeight = 20.f; ShipWeight = 80.f; break;
			case EProgressionChestKind::IslandGuarded: WeaponWeight = 80.f; ConsumableWeight = 60.f; ShipWeight = 20.f; break;
			case EProgressionChestKind::OceanRandom: WeaponWeight = 20.f; ConsumableWeight = 15.f; ShipWeight = 70.f; break;
			case EProgressionChestKind::IslandRandom: WeaponWeight = 45.f; ConsumableWeight = 45.f; ShipWeight = 20.f; break;
			}

			const int32 BaseMin = 2 + ZoneIndex;
			const int32 BaseMax = 4 + ZoneIndex;
			const float AdvancedShare = ZoneIndex == 3 ? 0.85f : 0.50f;
			const auto AddPair = [&](const TCHAR* BaseName, const TCHAR* AdvancedName,
				FGameplayTag BaseTag, FGameplayTag AdvancedTag, float TrackWeight)
			{
				AddLoot(Pool, BaseName, BaseTag, BaseMin, BaseMax, TrackWeight);
				if (AdvancedTag != BaseTag)
				{
					AddLoot(Pool, AdvancedName, AdvancedTag, 1 + ZoneIndex / 2, 2 + ZoneIndex,
						TrackWeight * AdvancedShare);
				}
			};
			AddPair(TEXT("WeaponBasic"), TEXT("WeaponAdvanced"),
				WeaponBasic[ZoneIndex], WeaponAdvanced[ZoneIndex], WeaponWeight);
			AddPair(TEXT("ConsumableBasic"), TEXT("ConsumableAdvanced"),
				ConsumableBasic[ZoneIndex], ConsumableAdvanced[ZoneIndex], ConsumableWeight);
			AddPair(TEXT("ShipBasic"), TEXT("ShipAdvanced"),
				ShipBasic[ZoneIndex], ShipAdvanced[ZoneIndex], ShipWeight);
		}
	}

	AddCost(MaterialCosts, EProgressionMaterialTrack::Weapon, 1,
		Item_Id_Material_WeaponMaterial_Wood, 4);
	AddCost(MaterialCosts, EProgressionMaterialTrack::Weapon, 2,
		Item_Id_Material_WeaponMaterial_Wood, 8, Item_Id_Material_WeaponMaterial_GoodWood, 5);
	AddCost(MaterialCosts, EProgressionMaterialTrack::Weapon, 3,
		Item_Id_Material_WeaponMaterial_Iron, 4, Item_Id_Material_WeaponMaterial_GoodWood, 8);
	AddCost(MaterialCosts, EProgressionMaterialTrack::Weapon, 4,
		Item_Id_Material_WeaponMaterial_Iron, 6, Item_Id_Material_WeaponMaterial_GoodIron, 10);

	AddCost(MaterialCosts, EProgressionMaterialTrack::Consumable, 1,
		Item_Id_Material_ConsumablesMaterial_Pear, 2);
	AddCost(MaterialCosts, EProgressionMaterialTrack::Consumable, 2,
		Item_Id_Material_ConsumablesMaterial_Pear, 3, Item_Id_Material_ConsumablesMaterial_GoodPear, 2);
	AddCost(MaterialCosts, EProgressionMaterialTrack::Consumable, 3,
		Item_Id_Material_ConsumablesMaterial_Herbs, 2, Item_Id_Material_ConsumablesMaterial_GoodPear, 3);
	AddCost(MaterialCosts, EProgressionMaterialTrack::Consumable, 4,
		Item_Id_Material_ConsumablesMaterial_Herbs, 3, Item_Id_Material_ConsumablesMaterial_GoodHerbs, 4);

	AddCost(MaterialCosts, EProgressionMaterialTrack::Ship, 1,
		Item_Id_Material_ShipMaterials_WoodenPlank, 6);
	AddCost(MaterialCosts, EProgressionMaterialTrack::Ship, 2,
		Item_Id_Material_ShipMaterials_WoodenPlank, 8, Item_Id_Material_ShipMaterials_GoodWoodenPlank, 5);
	AddCost(MaterialCosts, EProgressionMaterialTrack::Ship, 3,
		Item_Id_Material_ShipMaterials_IronPlate, 5, Item_Id_Material_ShipMaterials_GoodWoodenPlank, 8);
	AddCost(MaterialCosts, EProgressionMaterialTrack::Ship, 4,
		Item_Id_Material_ShipMaterials_IronPlate, 7, Item_Id_Material_ShipMaterials_GoodIronPlate, 10);
	for (const FProgressionMaterialCost& Cost : MaterialCosts)
	{
		if (Cost.Track == EProgressionMaterialTrack::Ship)
		{
			ShipUpgradeCosts.Add(Cost);
		}
	}
}

const FProgressionZonePlan* UProgressionBalanceData::FindZone(EProgressionZone Zone) const
{
	return ZonePlans.FindByPredicate([Zone](const FProgressionZonePlan& Plan) { return Plan.Zone == Zone; });
}

const FProgressionZoneTarget* UProgressionBalanceData::FindTarget(EProgressionZone Zone) const
{
	return ZoneTargets.FindByPredicate([Zone](const FProgressionZoneTarget& Target) { return Target.Zone == Zone; });
}

const FProgressionChestPool* UProgressionBalanceData::FindPool(EProgressionZone Zone, EProgressionChestKind Kind) const
{
	return ChestPools.FindByPredicate([Zone, Kind](const FProgressionChestPool& Pool)
	{
		return Pool.Zone == Zone && Pool.Kind == Kind;
	});
}

const FProgressionMaterialCost* UProgressionBalanceData::FindCost(EProgressionMaterialTrack Track, int32 Tier) const
{
	const TArray<FProgressionMaterialCost>& Source = Track == EProgressionMaterialTrack::Ship && !ShipUpgradeCosts.IsEmpty()
		? ShipUpgradeCosts : MaterialCosts;
	return Source.FindByPredicate([Track, Tier](const FProgressionMaterialCost& Cost)
	{
		return Cost.Track == Track && Cost.Tier == Tier;
	});
}

bool UProgressionBalanceData::GetIngredients(EProgressionMaterialTrack Track, int32 Tier,
	TArray<FCraftingItemStack>& OutIngredients) const
{
	const FProgressionMaterialCost* Cost = FindCost(Track, Tier);
	if (!Cost)
	{
		return false;
	}
	OutIngredients.Reset();
	AppendCost(OutIngredients, Cost->BaseMaterial, Cost->BaseQuantity);
	AppendCost(OutIngredients, Cost->PremiumMaterial, Cost->PremiumQuantity);
	for (const FCraftingItemStack& Extra : Cost->AdditionalIngredients)
	{
		AppendCost(OutIngredients, Extra.ItemTag, Extra.Quantity);
	}
	return true;
}

bool UProgressionBalanceData::GetRecipeIngredients(FName RecipeId, const FGameplayTag& ResultTag,
	TArray<FCraftingItemStack>& OutIngredients) const
{
	if (const FProgressionRecipeBinding* Binding = RecipeBindings.FindByPredicate(
		[RecipeId](const FProgressionRecipeBinding& Candidate) { return Candidate.RecipeId == RecipeId; }))
	{
		if (!GetIngredients(Binding->Track, Binding->Tier, OutIngredients))
		{
			return false;
		}
		for (const FCraftingItemStack& Extra : Binding->AdditionalIngredients)
		{
			AppendCost(OutIngredients, Extra.ItemTag, Extra.Quantity);
		}
		return true;
	}

	const FString TagName = ResultTag.ToString();
	if (TagName.StartsWith(TEXT("Item.Id.Weapon.")) && !TagName.IsEmpty())
	{
		const TCHAR LastCharacter = TagName[TagName.Len() - 1];
		if (LastCharacter >= '1' && LastCharacter <= '4')
		{
			return GetIngredients(EProgressionMaterialTrack::Weapon, LastCharacter - '0', OutIngredients);
		}
	}

	if (TagName.StartsWith(TEXT("Item.Id.Consumables.")))
	{
		int32 Tier = 0;
		if (TagName.EndsWith(TEXT(".Medicine")) || TagName.EndsWith(TEXT(".Doraji"))) Tier = 1;
		else if (TagName.EndsWith(TEXT(".Tangyak")) || TagName.EndsWith(TEXT(".Chungshimhwan"))) Tier = 2;
		else if (TagName.EndsWith(TEXT(".Elixir")) || TagName.EndsWith(TEXT(".Gongjindan"))) Tier = 3;
		if (Tier > 0)
		{
			return GetIngredients(EProgressionMaterialTrack::Consumable, Tier, OutIngredients);
		}
	}
	return false;
}

int32 UProgressionBalanceData::GetActiveCount(EProgressionZone Zone, EProgressionChestKind Kind) const
{
	const FProgressionZonePlan* Plan = FindZone(Zone);
	if (!Plan) return 0;
	switch (Kind)
	{
	case EProgressionChestKind::ShipGuarded: return Plan->ShipSquads * Plan->ShipsPerSquad;
	case EProgressionChestKind::IslandGuarded: return Plan->IslandGuardSquads;
	case EProgressionChestKind::OceanRandom: return Plan->OceanActiveChests;
	case EProgressionChestKind::IslandRandom: return Plan->IslandActiveChests;
	default: return 0;
	}
}

bool UProgressionBalanceData::GetTierDemand(EProgressionZone Zone, TMap<FGameplayTag, int32>& OutDemand) const
{
	OutDemand.Reset();
	const FProgressionZonePlan* Plan = FindZone(Zone);
	const USettings_Item* Settings = GetDefault<USettings_Item>();
	const UDataTable* Recipes = Settings ? Settings->CraftingRecipeDataTable.LoadSynchronous() : nullptr;
	if (!Plan || !Recipes) return false;
	const int32 Tier = static_cast<int32>(Zone) + 1;
	for (int32 TrackIndex = 0; TrackIndex < 2; ++TrackIndex)
	{
		const EProgressionRecipeTrack Track = TrackIndex == 0 ? EProgressionRecipeTrack::Weapon : EProgressionRecipeTrack::Consumable;
		const int32 Count = TrackIndex == 0 ? Plan->WeaponCraftCount : Plan->ConsumableCraftCount;
		TMap<FGameplayTag, int32> MaximumByTag;
		for (const FName RowName : Recipes->GetRowNames())
		{
			const FCraftingRecipeRow* Row = Recipes->FindRow<FCraftingRecipeRow>(RowName, TEXT("Progression demand"), false);
			if (!Row || !Row->bEnabled || RecipeTier(*Row) != Tier || RecipeTrack(*Row) != Track) continue;
			TMap<FGameplayTag, int32> PerRecipe;
			for (const FCraftingItemStack& Ingredient : Row->Ingredients)
			{
				if (Ingredient.ItemTag.IsValid() && Ingredient.Quantity > 0)
				{
					PerRecipe.FindOrAdd(Ingredient.ItemTag) += Ingredient.Quantity;
				}
			}
			for (const TPair<FGameplayTag, int32>& Pair : PerRecipe)
			{
				int32& Maximum = MaximumByTag.FindOrAdd(Pair.Key);
				Maximum = FMath::Max(Maximum, Pair.Value);
			}
		}
		for (const TPair<FGameplayTag, int32>& Pair : MaximumByTag)
		{
			OutDemand.FindOrAdd(Pair.Key) += Pair.Value * Count;
		}
	}
	TArray<FCraftingItemStack> ShipCosts;
	if (GetIngredients(EProgressionMaterialTrack::Ship, Tier, ShipCosts))
	{
		for (const FCraftingItemStack& Ingredient : ShipCosts)
		{
			if (Ingredient.ItemTag.IsValid() && Ingredient.Quantity > 0)
			{
				OutDemand.FindOrAdd(Ingredient.ItemTag) += Ingredient.Quantity * Plan->ShipUpgradeNodeCount;
			}
		}
	}
	return true;
}

void UProgressionBalanceData::GetComputedDrops(EProgressionZone Zone, EProgressionChestKind Kind,
	TArray<FProgressionComputedDrop>& OutDrops) const
{
	OutDrops.Reset();
	const int32 ActiveCount = GetActiveCount(Zone, Kind);
	if (ActiveCount <= 0 || TargetFullClearsPerZone <= 0) return;
	TMap<FGameplayTag, int32> Demand;
	if (!GetTierDemand(Zone, Demand)) return;
	for (const TPair<FGameplayTag, int32>& Pair : Demand)
	{
		if (!Pair.Key.IsValid() || Pair.Value <= 0) continue;
		float Denominator = 0.f;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const EProgressionChestKind Other = static_cast<EProgressionChestKind>(Index);
			Denominator += GetActiveCount(Zone, Other) * Affinity(Other, Pair.Key);
		}
		if (Denominator <= 0.f) continue;
		const float Expected = static_cast<float>(Pair.Value) * Affinity(Kind, Pair.Key)
			/ (TargetFullClearsPerZone * Denominator);
		const int32 Stack = FMath::Max(1, FMath::CeilToInt(Expected / .85f));
		FProgressionComputedDrop& Drop = OutDrops.AddDefaulted_GetRef();
		Drop.ItemTag = Pair.Key;
		Drop.Chance = FMath::Clamp(Expected / Stack, 0.f, 1.f);
		Drop.MinCount = Stack;
		Drop.MaxCount = Stack;
	}
	for (const FProgressionLootEntry& Rare : GlobalLootEntries)
	{
		if (!Rare.ItemTag.IsValid() || Rare.Weight <= 0.f) continue;
		FProgressionComputedDrop& Drop = OutDrops.AddDefaulted_GetRef();
		Drop.ItemTag = Rare.ItemTag;
		Drop.Chance = FMath::Clamp(Rare.Weight, 0.f, 1.f);
		Drop.MinCount = FMath::Max(1, Rare.MinCount);
		Drop.MaxCount = FMath::Max(Drop.MinCount, Rare.MaxCount);
	}
	OutDrops.Sort([](const FProgressionComputedDrop& A, const FProgressionComputedDrop& B)
	{
		return A.ItemTag.ToString() < B.ItemTag.ToString();
	});
}

float UProgressionBalanceData::GetExpectedQuantity(EProgressionZone Zone, FGameplayTag MaterialTag,
	int32 FullClears, bool bSinkShipsInsteadOfBoarding) const
{
	if (!MaterialTag.IsValid() || FullClears <= 0) return 0.f;
	float Total = 0.f;
	for (int32 KindIndex = 0; KindIndex < 4; ++KindIndex)
	{
		const EProgressionChestKind Kind = static_cast<EProgressionChestKind>(KindIndex);
		TArray<FProgressionComputedDrop> Drops;
		GetComputedDrops(Zone, Kind, Drops);
		const float Ratio = Kind == EProgressionChestKind::ShipGuarded && bSinkShipsInsteadOfBoarding
			? SunkChestExpectedValueRatio : 1.f;
		for (const FProgressionComputedDrop& Drop : Drops)
		{
			if (Drop.ItemTag == MaterialTag)
			{
				Total += GetActiveCount(Zone, Kind) * Ratio * Drop.Chance
					* (Drop.MinCount + Drop.MaxCount) * .5f;
			}
		}
	}
	return FullClears * Total;
}

int32 UProgressionBalanceData::GetRepresentativeNextTierRequirement(EProgressionZone Zone,
	FGameplayTag MaterialTag) const
{
	if (!MaterialTag.IsValid()) return 0;
	TMap<FGameplayTag, int32> Demand;
	return GetTierDemand(Zone, Demand) ? Demand.FindRef(MaterialTag) : 0;
}

bool UProgressionBalanceData::ValidateBalance(TArray<FString>& OutErrors) const
{
	OutErrors.Reset();
	if (SunkChestExpectedValueRatio < 0.f || SunkChestExpectedValueRatio > 1.f)
		OutErrors.Add(TEXT("Invalid sunk-chest value ratio."));
	if (ZoneTargets.Num() != 4)
		OutErrors.Add(TEXT("Exactly four zone targets are required."));
	TSet<EProgressionZone> Seen;
	for (const FProgressionZoneTarget& Target : ZoneTargets)
	{
		if (Seen.Contains(Target.Zone))
			OutErrors.Add(FString::Printf(TEXT("Duplicate target for zone %d."), static_cast<int32>(Target.Zone)));
		Seen.Add(Target.Zone);
		if (Target.FullClears <= 0 || Target.WeaponCrafts < 0 || Target.ConsumableCrafts < 0 || Target.ShipUpgrades < 0)
			OutErrors.Add(FString::Printf(TEXT("Invalid M/a/b/c for zone %d."), static_cast<int32>(Target.Zone)));
	}
	for (int32 ZoneIndex = 0; ZoneIndex < 4; ++ZoneIndex)
	{
		if (!Seen.Contains(static_cast<EProgressionZone>(ZoneIndex)))
			OutErrors.Add(FString::Printf(TEXT("Missing target for zone %d."), ZoneIndex));
	}
	return OutErrors.IsEmpty();
}

const UProgressionBalanceData* UProgressionBalanceData::LoadConfigured()
{
	const USettings_Item* Settings = GetDefault<USettings_Item>();
	return Settings ? Settings->ProgressionBalanceData.LoadSynchronous() : nullptr;
}
