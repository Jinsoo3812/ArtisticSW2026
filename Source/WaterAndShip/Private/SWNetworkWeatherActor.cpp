#include "SWNetworkWeatherActor.h"

#include "Components/TimelineComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Net/UnrealNetwork.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
	// Only persistent assets and value types; never local MIDs, components, or effect actors.
	const TArray<FName>& WeatherProperties()
	{
		static const TArray<FName> Names = {
			TEXT("InitWeather"), TEXT("NextWeather"), TEXT("NextWeather Buf"),
			TEXT("CurrentParam"), TEXT("NextParam"),
			TEXT("CurrentSunColor"), TEXT("NextSunColor"),
			TEXT("CurrentSkyLightColor"), TEXT("NextSkyLightColor"),
			TEXT("CurrentFogColor"), TEXT("NextFogColor"),
			TEXT("CurrentOverallColor"), TEXT("NextOverallColor"), TEXT("SunVisible")
		};
		return Names;
	}
	const TArray<FName>& WeatherMaterials()
	{
		static const TArray<FName> Names = { TEXT("CloudMaterial"), TEXT("SphereMaterial"), TEXT("CurrentDynamicParam") };
		return Names;
	}
}

ASWNetworkWeatherActor::ASWNetworkWeatherActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(10.f);
	SkyParameters = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/StylizedWeather/Material/MPC_Sky.MPC_Sky"));
}

void ASWNetworkWeatherActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASWNetworkWeatherActor, WeatherState);
	DOREPLIFETIME(ASWNetworkWeatherActor, WindState);
}

bool ASWNetworkWeatherActor::ValidateAdapter() const
{
	for (FName Name : WeatherProperties()) if (!GetClass()->FindPropertyByName(Name)) return false;
	for (FName Name : WeatherMaterials()) if (!FindFProperty<FObjectPropertyBase>(GetClass(), Name)) return false;
	for (FName Name : { FName(TEXT("CreateDynamicMat")), FName(TEXT("InitializeSetting")), FName(TEXT("WeatherRaffleSetting")),
		FName(TEXT("SetNextWeather")), FName(TEXT("Change Param")), FName(TEXT("WeatherEventCall Setup")), FName(TEXT("WeatherEventCall")) })
		if (!FindFunction(Name)) return false;
	return SkyParameters != nullptr;
}

bool ASWNetworkWeatherActor::Invoke(FName Name, const TMap<FName, double>& Arguments)
{
	UFunction* Function = FindFunction(Name);
	if (!Function) return false;
	FStructOnScope Parameters(Function);
	for (const TPair<FName, double>& Argument : Arguments)
	{
		FNumericProperty* Property = FindFProperty<FNumericProperty>(Function, Argument.Key);
		if (!Property) return false;
		void* Address = Property->ContainerPtrToValuePtr<void>(Parameters.GetStructMemory());
		if (Property->IsFloatingPoint()) Property->SetFloatingPointPropertyValue(Address, Argument.Value);
		else Property->SetIntPropertyValue(Address, static_cast<int64>(Argument.Value));
	}
	ProcessEvent(Function, Parameters.GetStructMemory());
	return true;
}

double ASWNetworkWeatherActor::ReadNumber(FName Name) const
{
	if (const FNumericProperty* Property = FindFProperty<FNumericProperty>(GetClass(), Name))
	{
		const void* Address = Property->ContainerPtrToValuePtr<void>(this);
		return Property->IsFloatingPoint() ? Property->GetFloatingPointPropertyValue(Address) : Property->GetSignedIntPropertyValue(Address);
	}
	if (const FBoolProperty* Property = FindFProperty<FBoolProperty>(GetClass(), Name)) return Property->GetPropertyValue_InContainer(this) ? 1. : 0.;
	return 0.;
}

void ASWNetworkWeatherActor::WriteNumber(FName Name, double Value)
{
	if (FNumericProperty* Property = FindFProperty<FNumericProperty>(GetClass(), Name))
	{
		void* Address = Property->ContainerPtrToValuePtr<void>(this);
		if (Property->IsFloatingPoint()) Property->SetFloatingPointPropertyValue(Address, Value);
		else Property->SetIntPropertyValue(Address, static_cast<int64>(Value));
	}
	else if (FBoolProperty* Bool = FindFProperty<FBoolProperty>(GetClass(), Name)) Bool->SetPropertyValue_InContainer(this, Value != 0.);
}

UObject* ASWNetworkWeatherActor::ReadObject(FName Name) const
{
	const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(GetClass(), Name);
	return Property ? Property->GetObjectPropertyValue_InContainer(this) : nullptr;
}

double ASWNetworkWeatherActor::GetServerTime() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	return State ? State->GetServerWorldTimeSeconds() : (HasAuthority() && World ? World->GetTimeSeconds() : 0.);
}

double ASWNetworkWeatherActor::ServerWeatherRandomFloat(double Min, double Max) const
{
	return GetWorld() && GetWorld()->IsGameWorld() && HasAuthority() ? FMath::FRandRange(Min, Max) : Min;
}

int32 ASWNetworkWeatherActor::ServerWeatherRandomInteger(int32 Min, int32 Max) const
{
	return GetWorld() && GetWorld()->IsGameWorld() && HasAuthority() ? FMath::RandRange(Min, Max) : Min;
}

void ASWNetworkWeatherActor::SetNetworkSunVisible()
{
	WriteNumber(TEXT("SunVisible"), ReadNumber(TEXT("SunVisible")) == 0. ? 1. : 0.);
}

void ASWNetworkWeatherActor::BeginPlay()
{
	Super::BeginPlay();
	bAdapterReady = ValidateAdapter();
	if (!bAdapterReady)
	{
		UE_LOG(LogTemp, Error, TEXT("SWWeather: incompatible Blueprint adapter on %s"), *GetPathName());
		SetActorTickEnabled(false);
		return;
	}
	Invoke(TEXT("CreateDynamicMat"));
	TInlineComponentArray<UTimelineComponent*> Timelines(this);
	for (UTimelineComponent* Timeline : Timelines) Timeline->Stop();
	if (HasAuthority())
	{
		WriteNumber(TEXT("WeatherChangeCycle"), FMath::Max(1., ReadNumber(TEXT("WeatherChangeCycle"))));
		Invoke(TEXT("WeatherRaffleSetting"));
		SetNetworkWeatherTime(static_cast<int32>(ReadNumber(TEXT("Init Hour"))), static_cast<int32>(ReadNumber(TEXT("Init Minute"))), static_cast<uint8>(ReadNumber(TEXT("InitWeather"))));
		const double Now = GetServerTime();
		WindState.bInitialized = true;
		WindState.StartServerTime = Now;
		WindState.Speed = ReadNumber(TEXT("WindSpeed"));
		if (FStructProperty* Property = FindFProperty<FStructProperty>(GetClass(), TEXT("WindDir")))
			WindState.From = WindState.To = *Property->ContainerPtrToValuePtr<FVector>(this);
		NextWindServerTime = Now;
		AdvanceWind(Now);
	}
	else if (WeatherState.Revision != 0) ApplyWeatherState();
}

void ASWNetworkWeatherActor::SetNetworkWeatherTime(int32 Hour, int32 Minute, uint8 Weather)
{
	if (!HasAuthority() || !bAdapterReady) return;
	Hour = FMath::Clamp(Hour, 0, 23);
	Minute = FMath::Clamp(Minute, 0, 59);
	WriteNumber(TEXT("InitWeather"), Weather);
	Invoke(TEXT("InitializeSetting"), {{TEXT("Hour"), static_cast<double>(Hour)}, {TEXT("Minute"), static_cast<double>(Minute)}, {TEXT("Weather"), static_cast<double>(Weather)}});
	WeatherState.Hour = Hour;
	WeatherState.SecondsPerHour = FMath::Max(0., ReadNumber(TEXT("1 Hour Seconds")));
	WeatherState.FrozenAlpha = Minute / 60.;
	WeatherState.HourStartServerTime = GetServerTime() - WeatherState.FrozenAlpha * WeatherState.SecondsPerHour;
	PendingVisibilityFrom = static_cast<float>(ReadNumber(TEXT("SunVisible")));
	CaptureWeatherState();
	EvaluateWeather(GetServerTime());
}

void ASWNetworkWeatherActor::CaptureWeatherState()
{
	WeatherState.Properties.Reset();
	for (FName Name : WeatherProperties())
	{
		FProperty* Property = GetClass()->FindPropertyByName(Name);
		FSWWeatherPropertyValue& Entry = WeatherState.Properties.AddDefaulted_GetRef();
		Entry.Name = Name;
		const void* Address = Property->ContainerPtrToValuePtr<void>(this);
		Property->ExportTextItem_Direct(Entry.Value, Address, nullptr, this, PPF_None);
	}
	WeatherState.Materials.Reset();
	for (FName Name : WeatherMaterials())
	{
		UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(ReadObject(Name));
		if (!Material) continue;
		FSWWeatherMaterialState& Entry = WeatherState.Materials.AddDefaulted_GetRef();
		Entry.PropertyName = Name;
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Guids;
		Material->GetAllScalarParameterInfo(Infos, Guids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			float Value;
			if (Info.Association == EMaterialParameterAssociation::GlobalParameter && Material->GetScalarParameterValue(Info, Value))
			{
				FSWWeatherScalarValue& Parameter = Entry.Scalars.AddDefaulted_GetRef(); Parameter.Name = Info.Name; Parameter.Value = Value;
			}
		}
		Material->GetAllVectorParameterInfo(Infos, Guids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			FLinearColor Value;
			if (Info.Association == EMaterialParameterAssociation::GlobalParameter && Material->GetVectorParameterValue(Info, Value))
			{
				FSWWeatherVectorValue& Parameter = Entry.Vectors.AddDefaulted_GetRef(); Parameter.Name = Info.Name; Parameter.Value = Value;
			}
		}
		Material->GetAllTextureParameterInfo(Infos, Guids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			UTexture* Value = nullptr;
			if (Info.Association == EMaterialParameterAssociation::GlobalParameter && Material->GetTextureParameterValue(Info, Value))
			{
				FSWWeatherTextureValue& Parameter = Entry.Textures.AddDefaulted_GetRef(); Parameter.Name = Info.Name; Parameter.Value = Value;
			}
		}
	}
	WeatherState.SunVisibilityFrom = PendingVisibilityFrom;
	WeatherState.SunVisibilityTo = static_cast<float>(ReadNumber(TEXT("SunVisible")));
	++WeatherState.Revision;
	AppliedRevision = WeatherState.Revision;
	if (!bEventsReady) { Invoke(TEXT("WeatherEventCall Setup")); bEventsReady = true; }
	Invoke(TEXT("WeatherEventCall"));
	ForceNetUpdate();
}

void ASWNetworkWeatherActor::OnRep_WeatherState()
{
	if (bAdapterReady) ApplyWeatherState();
}

void ASWNetworkWeatherActor::ApplyWeatherState()
{
	if (WeatherState.Revision == 0 || AppliedRevision == WeatherState.Revision) return;
	for (const FSWWeatherPropertyValue& Entry : WeatherState.Properties)
	{
		if (!WeatherProperties().Contains(Entry.Name)) continue;
		FProperty* Property = GetClass()->FindPropertyByName(Entry.Name);
		if (!Property || !Property->ImportText_Direct(*Entry.Value, Property->ContainerPtrToValuePtr<void>(this), this, PPF_None))
		{
			UE_LOG(LogTemp, Error, TEXT("SWWeather: cannot apply replicated property %s"), *Entry.Name.ToString());
			return;
		}
	}
	for (const FSWWeatherMaterialState& Entry : WeatherState.Materials)
	{
		if (!WeatherMaterials().Contains(Entry.PropertyName)) continue;
		UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(ReadObject(Entry.PropertyName));
		if (!Material) return;
		for (const FSWWeatherScalarValue& Value : Entry.Scalars) Material->SetScalarParameterValue(Value.Name, Value.Value);
		for (const FSWWeatherVectorValue& Value : Entry.Vectors) Material->SetVectorParameterValue(Value.Name, Value.Value);
		for (const FSWWeatherTextureValue& Value : Entry.Textures) Material->SetTextureParameterValue(Value.Name, Value.Value);
	}
	AppliedRevision = WeatherState.Revision;
	if (!bEventsReady) { Invoke(TEXT("WeatherEventCall Setup")); bEventsReady = true; }
	Invoke(TEXT("WeatherEventCall"));
	EvaluateWeather(GetServerTime());
}

FVector ASWNetworkWeatherActor::ResolveWindOffset(double ServerTime) const
{
	const double Age = FMath::Max(0., ServerTime - WindState.StartServerTime);
	const double Duration = FMath::Max(0.001, WindState.Duration);
	const double BlendTime = FMath::Min(Age, Duration);
	const FVector Integral = WindState.From * BlendTime + (WindState.To - WindState.From) * (0.5 * BlendTime * BlendTime / Duration)
		+ WindState.To * FMath::Max(0., Age - Duration);
	return WindState.OffsetAtStart + Integral * WindState.Speed;
}

void ASWNetworkWeatherActor::AdvanceWind(double Now)
{
	if (!HasAuthority() || ReadNumber(TEXT("WindChange")) == 0. || Now < NextWindServerTime) return;
	const FVector Offset = ResolveWindOffset(Now);
	const double Alpha = FMath::Clamp((Now - WindState.StartServerTime) / FMath::Max(0.001, WindState.Duration), 0., 1.);
	WindState.From = FMath::Lerp(WindState.From, WindState.To, Alpha);
	WindState.To = FVector(ServerWeatherRandomFloat(-1., 1.), ServerWeatherRandomFloat(-1., 1.), 0.);
	WindState.OffsetAtStart = Offset;
	WindState.StartServerTime = Now;
	if (UTimelineComponent* Timeline = Cast<UTimelineComponent>(ReadObject(TEXT("TL_Change Wind Dir"))))
		WindState.Duration = FMath::Max(0.001, static_cast<double>(Timeline->GetTimelineLength()) / FMath::Max(0.001f, Timeline->GetPlayRate()));
	const double Min = FMath::Max(0., ReadNumber(TEXT("WindChangeCycle Seconds Min")));
	const double Max = FMath::Max(Min, ReadNumber(TEXT("WindChangeCycle Seconds Max")));
	NextWindServerTime = Now + WindState.Duration + ServerWeatherRandomFloat(Min, Max);
	ForceNetUpdate();
}

void ASWNetworkWeatherActor::EvaluateWeather(double Now)
{
	if (WeatherState.Revision == 0) return;
	const double Alpha = WeatherState.SecondsPerHour > 0.
		? FMath::Clamp((Now - WeatherState.HourStartServerTime) / WeatherState.SecondsPerHour, 0., 1.) : WeatherState.FrozenAlpha;
	WriteNumber(TEXT("CurrentHour"), WeatherState.Hour);
	WriteNumber(TEXT("Current Minute"), FMath::Min(59, FMath::FloorToInt(Alpha * 60.)));
	Invoke(TEXT("Change Param"), {{TEXT("time"), Alpha}});
	if (UMaterialParameterCollectionInstance* Parameters = GetWorld()->GetParameterCollectionInstance(SkyParameters))
	{
		float Visibility = WeatherState.SunVisibilityTo;
		if (WeatherState.SunVisibilityFrom != WeatherState.SunVisibilityTo)
		{
			Visibility = SunVisibilityCurve
				? SunVisibilityCurve->GetFloatValue(static_cast<float>(Visibility == 0.f ? Alpha : 1. - Alpha))
				: FMath::Lerp(WeatherState.SunVisibilityFrom, WeatherState.SunVisibilityTo, static_cast<float>(Alpha));
		}
		Parameters->SetScalarParameterValue(TEXT("SunVisible"), Visibility);
		if (WindState.bInitialized)
		{
			const FVector Offset = ResolveWindOffset(Now);
			Parameters->SetVectorParameterValue(TEXT("WindOffset"), FLinearColor(Offset.X, Offset.Y, Offset.Z, 1.f));
		}
	}
}

void ASWNetworkWeatherActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bAdapterReady || (!HasAuthority() && !GetWorld()->GetGameState())) return;
	const double Now = GetServerTime();
	if (HasAuthority())
	{
		int32 Steps = 0;
		while (WeatherState.SecondsPerHour > 0. && Now >= WeatherState.HourStartServerTime + WeatherState.SecondsPerHour && Steps++ < 256)
		{
			Invoke(TEXT("Change Param"), {{TEXT("time"), 1.}});
			PendingVisibilityFrom = WeatherState.SunVisibilityTo;
			WeatherState.Hour = (WeatherState.Hour + 1) % 24;
			WeatherState.HourStartServerTime += WeatherState.SecondsPerHour;
			WriteNumber(TEXT("CurrentHour"), WeatherState.Hour);
			Invoke(TEXT("SetNextWeather"), {{TEXT("NextHour"), static_cast<double>(WeatherState.Hour)}});
			CaptureWeatherState();
		}
		AdvanceWind(Now);
	}
	else if (AppliedRevision != WeatherState.Revision) ApplyWeatherState();
	EvaluateWeather(Now);
}
