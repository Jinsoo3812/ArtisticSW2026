#include "SWWaterSkillOverlaySubsystem.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "UObject/ConstructorHelpers.h"

USWWaterSkillOverlaySubsystem::USWWaterSkillOverlaySubsystem()
{
	static ConstructorHelpers::FObjectFinder<UMaterialParameterCollection> Collection(
		TEXT("/Game/Blueprints/Water/MPC_Water_Custom.MPC_Water_Custom"));
	ParameterCollection = Collection.Object;
}

bool USWWaterSkillOverlaySubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool USWWaterSkillOverlaySubsystem::IsTickable() const
{
	return !IsTemplate() && GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer;
}

TStatId USWWaterSkillOverlaySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWWaterSkillOverlaySubsystem, STATGROUP_Tickables);
}

double USWWaterSkillOverlaySubsystem::GetServerTime() const
{
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void USWWaterSkillOverlaySubsystem::Upsert(UObject* Owner, const FVector& Center, float Radius,
	const FLinearColor& Color, double StartTime, uint64 SortOrder, double ExpireTime)
{
	if (!IsValid(Owner) || !IsTickable() || Center.ContainsNaN() || !FMath::IsFinite(Radius))
	{
		return;
	}
	FOverlay* Entry = Overlays.FindByPredicate([Owner](const FOverlay& Candidate)
	{
		return Candidate.Owner == Owner;
	});
	if (!Entry)
	{
		Entry = &Overlays.AddDefaulted_GetRef();
		Entry->Owner = Owner;
		Entry->StartTime = StartTime >= 0.0 ? StartTime : GetServerTime();
		Entry->RegistrationOrder = ++NextOrder;
		Entry->SortOrder = SortOrder ? SortOrder : Entry->RegistrationOrder;
		bDirty = true;
	}
	const float SafeRadius = FMath::Max(1.0f, Radius);
	const double EffectiveStart = StartTime >= 0.0 ? StartTime : Entry->StartTime;
	const uint64 EffectiveOrder = SortOrder ? SortOrder : Entry->SortOrder;
	if (!Entry->Center.Equals(Center, 0.01) || Entry->Radius != SafeRadius
		|| Entry->Color != Color || Entry->StartTime != EffectiveStart
		|| Entry->SortOrder != EffectiveOrder || Entry->ExpireTime != ExpireTime)
	{
		Entry->Center = Center;
		Entry->Radius = SafeRadius;
		Entry->Color = Color;
		Entry->StartTime = EffectiveStart;
		Entry->SortOrder = EffectiveOrder;
		Entry->ExpireTime = ExpireTime;
		bDirty = true;
	}
}

void USWWaterSkillOverlaySubsystem::Remove(UObject* Owner)
{
	bDirty |= Overlays.RemoveAll([Owner](const FOverlay& Entry) { return Entry.Owner == Owner; }) > 0;
}

void USWWaterSkillOverlaySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const double Now = GetServerTime();
	bDirty |= Overlays.RemoveAll([Now](const FOverlay& Entry)
	{
		return !Entry.Owner.IsValid() || (Entry.ExpireTime >= 0.0 && Now >= Entry.ExpireTime);
	}) > 0;
	if (bDirty)
	{
		Upload();
	}
}

void USWWaterSkillOverlaySubsystem::Upload()
{
	UMaterialParameterCollectionInstance* Instance = ParameterCollection && GetWorld()
		? GetWorld()->GetParameterCollectionInstance(ParameterCollection) : nullptr;
	if (!Instance)
	{
		return;
	}
	Overlays.Sort([](const FOverlay& A, const FOverlay& B)
	{
		if (A.StartTime != B.StartTime) return A.StartTime < B.StartTime;
		if (A.SortOrder != B.SortOrder) return A.SortOrder < B.SortOrder;
		return A.RegistrationOrder < B.RegistrationOrder;
	});
	const int32 First = FMath::Max(0, Overlays.Num() - MaxOverlayCount);
	const int32 Count = Overlays.Num() - First;
	if (First > 0 && !bWarnedOverflow)
	{
		UE_LOG(LogTemp, Warning, TEXT("Water skill overlays: displaying the newest %d of %d areas."),
			MaxOverlayCount, Overlays.Num());
	}
	bWarnedOverflow = First > 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FOverlay& Entry = Overlays[First + Index];
		Instance->SetVectorParameterValue(*FString::Printf(TEXT("SW_WaterSkillCenterRadius%d"), Index),
			FLinearColor(Entry.Center.X, Entry.Center.Y, Entry.Center.Z, Entry.Radius));
		Instance->SetVectorParameterValue(*FString::Printf(TEXT("SW_WaterSkillColor%d"), Index), Entry.Color);
	}
	Instance->SetScalarParameterValue(TEXT("SW_WaterSkillCount"), Count);
	Instance->SetScalarParameterValue(TEXT("SW_VortexPreviewEnabled"), 0.0f);
	bDirty = false;
}

void USWWaterSkillOverlaySubsystem::Deinitialize()
{
	Overlays.Reset();
	if (GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer)
	{
		Upload();
	}
	Super::Deinitialize();
}
