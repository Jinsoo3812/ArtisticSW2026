#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "Item/BaseProjectile.h"
#include "ArrowProjectile.generated.h"

class UPrimitiveComponent;
class UStaticMesh;

UCLASS()
class ARTISTICSWCORE_API AArrowProjectile : public ABaseProjectile
{
	GENERATED_BODY()

public:
	AArrowProjectile();

	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category = "Arrow")
	void LaunchArrow(const FVector& LaunchVelocity);

	UFUNCTION(BlueprintCallable, Category = "Arrow")
	void SetArrowMesh(UStaticMesh* InMesh);

	UFUNCTION(BlueprintCallable, Category = "Arrow")
	void SetDamageEffectSpecHandle(const FGameplayEffectSpecHandle& InDamageEffectSpecHandle);

	UFUNCTION(NetMulticast, Unreliable, BlueprintCallable, Category = "Arrow")
	void Multicast_PlayImpactFX(const FHitResult& Hit);

protected:
	UFUNCTION()
	void OnArrowHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	bool ShouldIgnoreHitActor(const AActor* OtherActor) const;
	void ApplyDamageToActor(AActor* TargetActor);

	UFUNCTION(BlueprintImplementableEvent, Category = "Arrow")
	void K2_OnImpactFX(const FHitResult& Hit);

protected:
	UPROPERTY(BlueprintReadWrite, meta = (ExposeOnSpawn = "true"), Category = "GAS")
	FGameplayEffectSpecHandle DamageEffectSpecHandle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow")
	bool bDestroyOnImpact = true;
};
