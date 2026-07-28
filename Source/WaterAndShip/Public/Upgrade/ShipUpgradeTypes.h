#pragma once

#include "CoreMinimal.h"
#include "Crafting/CraftingRecipeTypes.h"
#include "ShipUpgradeTypes.generated.h"

class AActor;

UENUM(BlueprintType)
enum class EShipStatType : uint8
{
	CannonDamage,
	CannonFireCooldown,
	CannonballSpeed,
	MaxHealth,
	ForwardPropulsion,
	TurnSpeed
};

UENUM(BlueprintType)
enum class EShipStatModifierOperation : uint8
{
	AddFlat,
	AddPercent
};

UENUM(BlueprintType)
enum class EShipUpgradeNodeState : uint8
{
	Locked,
	Available,
	Active
};

/** Selects the visual treatment used by the upgrade details panel. */
UENUM(BlueprintType)
enum class EShipUpgradePreviewType : uint8
{
	Icon2D,
	Cannon3D,
	Ship3D
};

UENUM(BlueprintType)
enum class EShipUpgradeActivationResult : uint8
{
	Success,
	AlreadyActive,
	UnknownNode,
	MissingPrerequisite,
	NotAuthority,
	SaveFailed,
	NotConfigured,
	MissingMaterials,
	InvalidCost
};

USTRUCT(BlueprintType)
struct WATERANDSHIP_API FShipStatSnapshot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Stats", meta = (ClampMin = "0.0"))
	float CannonDamage = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Stats", meta = (ClampMin = "0.05", Units = "s"))
	float CannonFireCooldownSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Stats", meta = (ClampMin = "0.0", Units = "cm/s"))
	float CannonballSpeed = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Stats", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Stats", meta = (ClampMin = "0.0"))
	float ForwardPropulsionMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Stats", meta = (ClampMin = "0.0"))
	float TurnTorqueMultiplier = 1.0f;

	bool Equals(const FShipStatSnapshot& Other, float Tolerance = KINDA_SMALL_NUMBER) const;
};

USTRUCT(BlueprintType)
struct WATERANDSHIP_API FShipStatModifier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Upgrade")
	EShipStatType StatType = EShipStatType::CannonDamage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Upgrade")
	EShipStatModifierOperation Operation = EShipStatModifierOperation::AddFlat;

	/** AddPercent uses decimal form: 0.15 means +15%. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship Upgrade")
	float Value = 0.0f;
};

USTRUCT(BlueprintType)
struct WATERANDSHIP_API FShipUpgradeNodeDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName NodeId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (MultiLine = true))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UTexture2D> Icon;

	/** 2D icon, isolated cannon actor, or isolated ship actor shown for this node. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation|3D")
	EShipUpgradePreviewType PreviewType = EShipUpgradePreviewType::Icon2D;

	/** Optional actor spawned only in the UI preview stage for the selected node. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation|3D")
	TSoftClassPtr<AActor> PreviewActorClass;

	/** Optional cumulative ship appearance selected while this node is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation|3D")
	TSoftClassPtr<AActor> ActivatedShipActorClass;

	/** Optional cumulative cannon appearance selected while this node is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation|3D")
	TSoftClassPtr<AActor> ActivatedCannonActorClass;

	/** Highest active priority wins when several nodes supply a cumulative appearance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation|3D")
	int32 VisualPriority = 0;

	/** Optional UI-defined camera preset key, for example ShipWide or CannonClose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation|3D")
	FName CameraPreset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Graph")
	FVector2D GraphPosition = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Graph")
	TArray<FName> PrerequisiteNodeIds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	TArray<FShipStatModifier> StatModifiers;

	/** All listed item stacks are required and consumed together on activation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation Cost")
	TArray<FCraftingItemStack> ActivationCosts;
};

USTRUCT(BlueprintType)
struct WATERANDSHIP_API FShipUpgradeMaterialView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	FGameplayTag ItemTag;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	int32 OwnedQuantity = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	int32 RequiredQuantity = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	bool bEnough = false;
};

USTRUCT(BlueprintType)
struct WATERANDSHIP_API FShipStatChangeView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	EShipStatType StatType = EShipStatType::CannonDamage;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	float BeforeValue = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	float AfterValue = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	float DeltaValue = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	FText Unit;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	bool bImprovesStat = false;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	FText FormattedText;
};

USTRUCT(BlueprintType)
struct WATERANDSHIP_API FShipUpgradeNodeView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	FName NodeId;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	FText Description;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	EShipUpgradePreviewType PreviewType = EShipUpgradePreviewType::Icon2D;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	TSoftClassPtr<AActor> PreviewActorClass;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	TSoftClassPtr<AActor> ActivatedShipActorClass;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	TSoftClassPtr<AActor> ActivatedCannonActorClass;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	int32 VisualPriority = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	FName CameraPreset;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	FVector2D GraphPosition = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	TArray<FName> PrerequisiteNodeIds;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	EShipUpgradeNodeState State = EShipUpgradeNodeState::Locked;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	TArray<FShipStatChangeView> StatChanges;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	TArray<FShipUpgradeMaterialView> MaterialCosts;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	bool bHasEnoughMaterials = true;

	UPROPERTY(BlueprintReadOnly, Category = "Ship Upgrade|UI")
	FText UnavailableReason;
};

struct WATERANDSHIP_API FShipUpgradeCalculator
{
	static FShipStatSnapshot Calculate(
		const FShipStatSnapshot& BaseStats,
		const TArray<FShipUpgradeNodeDefinition>& Nodes,
		const TArray<FName>& ActiveNodeIds);

	static float GetStatValue(const FShipStatSnapshot& Stats, EShipStatType StatType);
	static void SetStatValue(FShipStatSnapshot& Stats, EShipStatType StatType, float Value);
	static FText GetStatDisplayName(EShipStatType StatType);
	static FText GetStatUnit(EShipStatType StatType);
	static bool IsPositiveDeltaBeneficial(EShipStatType StatType);
};
