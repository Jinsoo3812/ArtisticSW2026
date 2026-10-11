#pragma once
#include "CoreMinimal.h"
#include "GameplayCueNotify_Actor.h"
#include "BossStunGameplayCue.generated.h"

class AShipBossEnemy;
class UStaticMeshComponent;
class UMaterialInterface;

/** Cosmetic orbiting stars. Existing Stun GE owns cue lifetime; no damage/status/AI logic. */
UCLASS(Blueprintable)
class ENEMY_API ABossStunGameplayCue : public AGameplayCueNotify_Actor
{
	GENERATED_BODY()
public:
	ABossStunGameplayCue();
	virtual bool OnActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) override;
	virtual bool WhileActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) override;
	virtual bool OnRemove_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) override;
	virtual bool Recycle() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stun VFX")
	TObjectPtr<UMaterialInterface> StarMaterial;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stun VFX")
	FName HeadBone = TEXT("head");
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stun VFX", meta = (Units = "cm"))
	float HeightAboveHead = 38.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stun VFX", meta = (ClampMin = "1", Units = "cm"))
	float OrbitRadius = 32.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stun VFX", meta = (ClampMin = "1", Units = "cm"))
	float StarSize = 18.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stun VFX", meta = (Units = "deg/s"))
	float OrbitSpeed = 160.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stun VFX")
	FLinearColor StarColor = FLinearColor(1.f, 0.65f, 0.08f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stun VFX", meta = (ClampMin = "0"))
	float GlowIntensity = 5.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stun VFX")
	TArray<TObjectPtr<UStaticMeshComponent>> Stars;
private:
	bool StartStars(AActor* Target);
	void StopStars();
	void ApplyMaterial();
	TWeakObjectPtr<AShipBossEnemy> StunnedBoss;
	float Elapsed = 0.f;
};
