#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SWImprovedFoamSubsystem.generated.h"

class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;

/** Kelvin/Ripple-only world-space Foam emission history. Gerstner Foam is untouched. */
UCLASS()
class WATERANDSHIP_API USWImprovedFoamSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	UTextureRenderTarget2D* GetCurrentHistory() const;
	FVector2D GetHistoryCenter() const { return CurrentCenter; }
	float GetHistorySize() const { return FieldSizeCm; }

private:
	void CreateHistoryResources();
	UTextureRenderTarget2D* CreateHistoryTarget(const FName& Name) const;
	FVector2D ResolveFallbackCenter() const;
	void DispatchHistory(float DeltaTime);
	void RefreshWaterMaterials();
	void BindToWaterMaterials();
	void TickDiagnostics(float DeltaTime);
	void LogRenderTargetStats(const TCHAR* Label, UTextureRenderTarget2D* Target, bool bHistory) const;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> HistoryA;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> HistoryB;

	TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> WaterMaterials;
	FVector2D CurrentCenter = FVector2D::ZeroVector;
	FVector2D PreviousCenter = FVector2D::ZeroVector;
	float FieldSizeCm = 30000.0f;
	int32 Resolution = 512;
	bool bCurrentIsA = true;
	bool bHasHistory = false;
	bool bDiagnosticsEnabled = false;
	bool bInjectKelvinTest = false;
	bool bKelvinTestInjected = false;
	float MaterialRefreshAccumulator = 0.0f;
	float SummaryAccumulator = 0.0f;
	float ReadbackAccumulator = 0.0f;
	uint64 DispatchCount = 0;
	int32 LastBoundMaterialCount = 0;
};
