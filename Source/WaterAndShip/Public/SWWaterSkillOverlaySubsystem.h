#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SWWaterSkillOverlaySubsystem.generated.h"

class UMaterialParameterCollection;

UCLASS()
class WATERANDSHIP_API USWWaterSkillOverlaySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	USWWaterSkillOverlaySubsystem();
	static constexpr int32 MaxOverlayCount = 64;
	void Upsert(UObject* Owner, const FVector& Center, float Radius,
		const FLinearColor& Color = FLinearColor::Transparent,
		double StartTime = -1.0, uint64 SortOrder = 0, double ExpireTime = -1.0);
	void Remove(UObject* Owner);
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	struct FOverlay
	{
		TWeakObjectPtr<UObject> Owner;
		FVector Center = FVector::ZeroVector;
		float Radius = 1.0f;
		FLinearColor Color = FLinearColor::Transparent;
		double StartTime = 0.0;
		double ExpireTime = -1.0;
		uint64 SortOrder = 0;
		uint64 RegistrationOrder = 0;
	};

	double GetServerTime() const;
	void Upload();
	TArray<FOverlay> Overlays;
	uint64 NextOrder = 0;
	bool bDirty = true;
	bool bWarnedOverflow = false;

	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> ParameterCollection;
};
