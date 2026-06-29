#pragma once

#include "CoreMinimal.h"
#include "Item/BaseItem.h"
#include "BowItem.generated.h"

class UBowComponent;
class USkeletalMeshComponent;

UCLASS()
class ARTISTICSWCORE_API ABowItem : public ABaseItem
{
	GENERATED_BODY()

public:
	ABowItem();

	UFUNCTION(BlueprintPure, Category = "Bow")
	USkeletalMeshComponent* GetBowMesh() const { return BowMesh; }

	UFUNCTION(BlueprintPure, Category = "Bow")
	UBowComponent* GetBowComponent() const { return BowComponent; }

	UFUNCTION(BlueprintCallable, Category = "Bow")
	void SetAiming(bool bNewAiming);

	UFUNCTION(BlueprintCallable, Category = "Bow")
	FTransform GetArrowSpawnTransform() const;

	UFUNCTION(NetMulticast, Unreliable, BlueprintCallable, Category = "Bow")
	void Multicast_PlayReleaseFX();

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Bow")
	void K2_OnReleaseFX();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bow|Components")
	TObjectPtr<USkeletalMeshComponent> BowMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bow|Components")
	TObjectPtr<UBowComponent> BowComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow")
	FName ArrowSocketName = FName("ArrowSocket");
};
