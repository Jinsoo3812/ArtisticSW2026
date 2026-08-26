#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyShipChargeTelegraph.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class UStaticMeshComponent;

/** Replicated, collision-free ground strip used while an Enemy Ship is aiming its charge. */
UCLASS(Blueprintable)
class ENEMY_API AEnemyShipChargeTelegraph : public AActor
{
	GENERATED_BODY()

public:
	AEnemyShipChargeTelegraph();

	void InitializeTelegraph(
		const FVector& InStart,
		const FVector& InDirection,
		float InDistance,
		float InWidth,
		float InWorldZ);

	void UpdateTelegraph(const FVector& InStart, const FVector& InDirection);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Charge|Telegraph")
	FVector GetTelegraphStart() const { return TelegraphStart; }

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Charge|Telegraph")
	FVector GetTelegraphEnd() const { return TelegraphEnd; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> WarningPlane;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge|Telegraph")
	TObjectPtr<UMaterialInterface> WarningMaterial;

	/** One scrolling symbol per this many centimetres of strip length. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Charge|Telegraph", meta = (ClampMin = "1.0", Units = "cm"))
	float SymbolSpacing = 800.0f;

private:
	UFUNCTION()
	void OnRep_TelegraphGeometry();

	void RefreshVisual();

	UPROPERTY(ReplicatedUsing = OnRep_TelegraphGeometry)
	FVector_NetQuantize TelegraphStart = FVector::ZeroVector;

	UPROPERTY(ReplicatedUsing = OnRep_TelegraphGeometry)
	FVector_NetQuantize TelegraphEnd = FVector::ZeroVector;

	UPROPERTY(ReplicatedUsing = OnRep_TelegraphGeometry)
	float TelegraphWidth = 1000.0f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicWarningMaterial;
};
