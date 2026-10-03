#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWNetworkWeatherActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UTexture;
class UCurveFloat;

USTRUCT()
struct FSWWeatherPropertyValue
{
	GENERATED_BODY()
	UPROPERTY() FName Name;
	UPROPERTY() FString Value;
};

USTRUCT()
struct FSWWeatherScalarValue
{
	GENERATED_BODY()
	UPROPERTY() FName Name;
	UPROPERTY() float Value = 0.f;
};

USTRUCT()
struct FSWWeatherVectorValue
{
	GENERATED_BODY()
	UPROPERTY() FName Name;
	UPROPERTY() FLinearColor Value = FLinearColor::Black;
};

USTRUCT()
struct FSWWeatherTextureValue
{
	GENERATED_BODY()
	UPROPERTY() FName Name;
	UPROPERTY() TObjectPtr<UTexture> Value = nullptr;
};

USTRUCT()
struct FSWWeatherMaterialState
{
	GENERATED_BODY()
	UPROPERTY() FName PropertyName;
	UPROPERTY() TArray<FSWWeatherScalarValue> Scalars;
	UPROPERTY() TArray<FSWWeatherVectorValue> Vectors;
	UPROPERTY() TArray<FSWWeatherTextureValue> Textures;
};

USTRUCT()
struct FSWWeatherNetworkState
{
	GENERATED_BODY()
	UPROPERTY() uint32 Revision = 0;
	UPROPERTY() int32 Hour = 0;
	UPROPERTY() double HourStartServerTime = 0.;
	UPROPERTY() double SecondsPerHour = 10.;
	UPROPERTY() double FrozenAlpha = 0.;
	UPROPERTY() float SunVisibilityFrom = 1.f;
	UPROPERTY() float SunVisibilityTo = 1.f;
	UPROPERTY() TArray<FSWWeatherPropertyValue> Properties;
	UPROPERTY() TArray<FSWWeatherMaterialState> Materials;
};

USTRUCT()
struct FSWWeatherWindState
{
	GENERATED_BODY()
	UPROPERTY() bool bInitialized = false;
	UPROPERTY() double StartServerTime = 0.;
	UPROPERTY() double Duration = 1.;
	UPROPERTY() double Speed = 0.005;
	UPROPERTY() FVector From = FVector(1., 0., 0.);
	UPROPERTY() FVector To = FVector(1., 0., 0.);
	UPROPERTY() FVector OffsetAtStart = FVector::ZeroVector;
};

/** Native clock/authority adapter for the copied StylizedWeather Blueprint. */
UCLASS(Blueprintable)
class WATERANDSHIP_API ASWNetworkWeatherActor : public AActor
{
	GENERATED_BODY()
public:
	ASWNetworkWeatherActor();
	UPROPERTY(EditDefaultsOnly, Category = "Weather|Network")
	TObjectPtr<UCurveFloat> SunVisibilityCurve;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Weather|Network")
	double ServerWeatherRandomFloat(double Min, double Max) const;
	UFUNCTION(BlueprintPure, Category = "Weather|Network")
	int32 ServerWeatherRandomInteger(int32 Min, int32 Max) const;
	UFUNCTION(BlueprintCallable, Category = "Weather|Network")
	void SetNetworkSunVisible();
	UFUNCTION(BlueprintCallable, Category = "Weather|Network")
	void SetNetworkWeatherTime(int32 Hour, int32 Minute, uint8 Weather);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(ReplicatedUsing = OnRep_WeatherState) FSWWeatherNetworkState WeatherState;
	UPROPERTY(Replicated) FSWWeatherWindState WindState;
	UPROPERTY() TObjectPtr<UMaterialParameterCollection> SkyParameters;
	UFUNCTION() void OnRep_WeatherState();
	bool ValidateAdapter() const;
	bool Invoke(FName FunctionName, const TMap<FName, double>& Arguments = {});
	double ReadNumber(FName Name) const;
	void WriteNumber(FName Name, double Value);
	UObject* ReadObject(FName Name) const;
	void CaptureWeatherState();
	void ApplyWeatherState();
	void EvaluateWeather(double ServerTime);
	void AdvanceWind(double ServerTime);
	FVector ResolveWindOffset(double ServerTime) const;
	double GetServerTime() const;
	bool bAdapterReady = false;
	bool bEventsReady = false;
	uint32 AppliedRevision = 0;
	double NextWindServerTime = 0.;
	float PendingVisibilityFrom = 1.f;
};
