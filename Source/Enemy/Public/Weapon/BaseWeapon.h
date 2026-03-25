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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Weapon")
	FGameplayTag WeaponTag;;

public:
	FORCEINLINE USkeletalMeshComponent* GetWeaponMesh() const { return WeaponMesh; }
	FORCEINLINE const FGameplayTag& GetWeaponTag() const { return WeaponTag; }

	void SetWeaponTag(FGameplayTag InWeaponTag) { WeaponTag = InWeaponTag; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
