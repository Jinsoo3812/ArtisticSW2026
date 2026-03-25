// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "BaseGameplayTags.h"

#include "BaseWeapon.generated.h"

class UWeaponDataAsset;
class USkeletalMeshComponent;

UCLASS()
class ENEMY_API ABaseWeapon : public AActor
{
	GENERATED_BODY()

public:
	ABaseWeapon();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UWeaponDataAsset> WeaponData;

public:
	FORCEINLINE USkeletalMeshComponent* GetWeaponMesh() const { return WeaponMesh; }
	FORCEINLINE UWeaponDataAsset* GetWeaponData() const { return WeaponData; }

	void SetWeaponData(UWeaponDataAsset* InWeaponData) { WeaponData = InWeaponData; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
