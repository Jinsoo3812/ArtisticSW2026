#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "PlayerSkillComponent.generated.h"

class UInventoryComponent;

USTRUCT(BlueprintType)
struct FPlayerSkillDefinition
{
	GENERATED_BODY()

	/** GameplayAbility.Skill.* tag shared by input, ability, and execution actors. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skill", meta = (Categories = "GameplayAbility.Skill"))
	FGameplayTag SkillTag;

	/** Item.Id.Skill.* identity used by UI for the skill name/icon. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skill", meta = (Categories = "Item.Id.Skill"))
	FGameplayTag SkillItemTag;

	/** One inventory item is consumed for each completed skill use. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skill", meta = (Categories = "Item.Id.Material"))
	FGameplayTag UsageMaterialTag;

	/** Story skills are locked by default; enable only for prototypes or migrated saves. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skill")
	bool bUnlockedByDefault = false;
};

USTRUCT(BlueprintType)
struct FPlayerSkillState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skill")
	FGameplayTag SkillTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skill")
	bool bUnlocked = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerSkillChanged, FGameplayTag, SkillTag);

/**
 * Server-authoritative story unlock state and inventory-backed skill-use facade.
 * This component intentionally owns no separate charge count: the corresponding
 * material stack in UInventoryComponent is the single source of truth.
 */
UCLASS(ClassGroup = (Player), meta = (BlueprintSpawnableComponent))
class CLASSFEATURE_API UPlayerSkillComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerSkillComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Skill")
	bool IsSkillUnlocked(FGameplayTag SkillTag) const;

	UFUNCTION(BlueprintPure, Category = "Skill")
	int32 GetSkillUseCount(FGameplayTag SkillTag) const;

	UFUNCTION(BlueprintPure, Category = "Skill")
	bool CanUseSkill(FGameplayTag SkillTag) const;

	/** Must be called on the server at the actual, irreversible execution point. */
	bool TryConsumeSkillUse(FGameplayTag SkillTag);

	bool CanUseSkillWithInventory(FGameplayTag SkillTag, const UInventoryComponent* Inventory) const;
	bool TryConsumeSkillUseWithInventory(FGameplayTag SkillTag, UInventoryComponent* Inventory);
	bool IsSkillUnlockedWithInventory(FGameplayTag SkillTag, const UInventoryComponent* Inventory) const;

	/** Keeps HUD queries valid while the controller is possessing a ship or cannon. */
	void RegisterInventorySource(UInventoryComponent* Inventory);

	/** Server-authoritative story/progression API. */
	UFUNCTION(BlueprintCallable, Category = "Skill")
	bool UnlockSkill(FGameplayTag SkillTag);

	/** Server-authoritative save/load and test API. */
	UFUNCTION(BlueprintCallable, Category = "Skill")
	bool SetSkillUnlocked(FGameplayTag SkillTag, bool bUnlocked);

	UFUNCTION(BlueprintPure, Category = "Skill")
	FGameplayTag GetSkillItemTag(FGameplayTag SkillTag) const;

	UFUNCTION(BlueprintPure, Category = "Skill")
	FGameplayTag GetUsageMaterialTag(FGameplayTag SkillTag) const;

	UFUNCTION(BlueprintPure, Category = "Skill")
	TArray<FGameplayTag> GetRegisteredSkillTags() const;

	const FPlayerSkillDefinition* FindSkillDefinition(FGameplayTag SkillTag) const;

	/** Called by the player when replicated/server inventory contents change. */
	void NotifyInventoryChanged();

	UPROPERTY(BlueprintAssignable, Category = "Skill")
	FOnPlayerSkillChanged OnSkillChanged;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skill")
	TArray<FPlayerSkillDefinition> SkillDefinitions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_SkillStates, Category = "Skill")
	TArray<FPlayerSkillState> SkillStates;

	UFUNCTION()
	void OnRep_SkillStates();

private:
	void InitializeSkillStates();
	UInventoryComponent* ResolveInventory() const;
	FPlayerSkillState* FindMutableSkillState(FGameplayTag SkillTag);
	const FPlayerSkillState* FindSkillState(FGameplayTag SkillTag) const;

	TWeakObjectPtr<UInventoryComponent> InventorySource;
};
