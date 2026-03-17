// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BaseProjectile.generated.h"

class UBoxComponent;
class UProjectileMovementComponent;

UCLASS()
class ARTISTICSWCORE_API ABaseProjectile : public AActor
{
	GENERATED_BODY()
	
protected:	
	// Sets default values for this actor's properties
	ABaseProjectile();

	// 루트 콜리전 컴포넌트
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> CollisionComp;

	// 렌더링용 스태틱 메시 (BP에서 할당)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComp;

	// 투척 이동 컴포넌트 캐싱
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovementComp;

public:
	// 외부(ThrowItem GA)에서 즉시 접근하기 위한 Getter
	FORCEINLINE UProjectileMovementComponent* GetProjectileMovement() const { return ProjectileMovementComp; }
	FORCEINLINE UBoxComponent* GetCollisionComp() const { return CollisionComp; }
};
