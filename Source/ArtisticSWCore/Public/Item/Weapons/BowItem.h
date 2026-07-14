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

	virtual USceneComponent* GetAttachmentReferenceComponent() const override;

	UFUNCTION(BlueprintCallable, Category = "Bow")
	bool GetStringIKTargetTransform(float DrawAlpha, FTransform& OutWorldTransform) const;

	/**
	 * Returns the character's string-grip socket in BowMesh component space.
	 * Bow animation blueprints use this to move their string-center bone toward
	 * the authored finger grip instead of pulling the character hand with IK.
	 */
	UFUNCTION(BlueprintCallable, Category = "Bow|String")
	bool GetCharacterStringGripTargetTransform(FTransform& OutBowComponentSpaceTransform) const;

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|IK")
	FName StringRestSocketName = FName("String_Rest_Socket");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|IK")
	FName StringDrawSocketName = FName("String_Draw_Socket");

	/** Socket on the equipped character mesh where the drawing fingers grip the string. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|String")
	FName CharacterStringGripSocketName = FName("BowStringGrip");
};
