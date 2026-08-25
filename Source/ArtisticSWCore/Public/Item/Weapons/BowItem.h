#pragma once

#include "CoreMinimal.h"
#include "Item/BaseItem.h"
#include "BowItem.generated.h"

class UBowComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

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

	/** Binds the character animation-owned origin used by both presentation and projectile spawning. */
	bool BindArrowAnchor(USkeletalMeshComponent* CharacterMesh);
	void UnbindArrowAnchor();

	/** Resolves the character Arrow_socket without a weapon-mesh or actor-root fallback. */
	bool TryGetArrowSpawnTransform(FTransform& OutSpawnTransform) const;

	UFUNCTION(BlueprintPure, Category = "Bow")
	FName GetCharacterArrowSocketName() const { return CharacterArrowSocketName; }

	UFUNCTION(BlueprintPure, Category = "Bow")
	USkeletalMeshComponent* GetArrowAnchorMesh() const { return ArrowAnchorMesh.Get(); }

	/** Presentation only; the firing ability owns projectile creation and movement. */
	bool SetNockedArrowVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category = "Bow")
	bool IsNockedArrowVisible() const;

	UFUNCTION(BlueprintPure, Category = "Bow")
	UStaticMeshComponent* GetNockedArrowMesh() const { return NockedArrowMesh; }

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
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	void HandleItemInitialized(ABaseItem* InitializedItem);
	bool RefreshNockedArrowVisual();

	UFUNCTION(BlueprintImplementableEvent, Category = "Bow")
	void K2_OnReleaseFX();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bow|Components")
	TObjectPtr<USkeletalMeshComponent> BowMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bow|Components")
	TObjectPtr<UBowComponent> BowComponent;

	/** Collision-free preview populated from the configured ArrowProjectile class. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bow|Components")
	TObjectPtr<UStaticMeshComponent> NockedArrowMesh;

	/** Socket authored on the equipped character skeleton, not on the bow mesh. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow")
	FName CharacterArrowSocketName = FName("Arrow_socket");

	UPROPERTY(Transient)
	TWeakObjectPtr<USkeletalMeshComponent> ArrowAnchorMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|IK")
	FName StringRestSocketName = FName("String_Rest_Socket");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|IK")
	FName StringDrawSocketName = FName("String_Draw_Socket");

	/** Socket on the equipped character mesh where the drawing fingers grip the string. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|String")
	FName CharacterStringGripSocketName = FName("BowStringGrip");
};
