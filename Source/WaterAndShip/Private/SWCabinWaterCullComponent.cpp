#include "SWCabinWaterCullComponent.h"
#include "SWCabinWaterCullData.h"
#include "UObject/ConstructorHelpers.h"

#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWCabinWaterCull, Log, All);

namespace SWCabinWaterCull
{
	struct FWorldSelectionState
	{
		TArray<TWeakObjectPtr<USWCabinWaterCullComponent>> Components;
		TWeakObjectPtr<USWCabinWaterCullComponent> SelectedComponent;
		uint64 SelectionFrame = MAX_uint64;
		bool bGlobalCullWasEnabled = false;
	};

	TMap<TWeakObjectPtr<UWorld>, FWorldSelectionState> WorldStates;

	const FName EnabledParameter(TEXT("SW_CabinCullEnabled"));
	const FName InverseRow0Parameter(TEXT("SW_CabinCullInvRow0"));
	const FName InverseRow1Parameter(TEXT("SW_CabinCullInvRow1"));
	const FName InverseRow2Parameter(TEXT("SW_CabinCullInvRow2"));
	const FName DebugViewParameter(TEXT("SW_CabinCullDebugView"));
	const TCHAR* CollectionPath = TEXT("/Game/Blueprints/Water/MPC_Water_Custom.MPC_Water_Custom");

	FVector GetLocalViewerLocation(UWorld* World, bool& bOutFoundViewer)
	{
		bOutFoundViewer = false;
		if (!World)
		{
			return FVector::ZeroVector;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PlayerController = It->Get();
			if (PlayerController && PlayerController->IsLocalController())
			{
				if (const APawn* Pawn = PlayerController->GetPawn())
				{
					bOutFoundViewer = true;
					return Pawn->GetActorLocation();
				}
			}
		}
		return FVector::ZeroVector;
	}

	USWCabinWaterCullComponent* SelectNearestComponent(UWorld* World, FWorldSelectionState& State)
	{
		State.Components.RemoveAllSwap([](const TWeakObjectPtr<USWCabinWaterCullComponent>& Entry)
		{
			return !Entry.IsValid();
		});

		bool bFoundViewer = false;
		const FVector ViewerLocation = GetLocalViewerLocation(World, bFoundViewer);
		USWCabinWaterCullComponent* Best = nullptr;
		float BestDistanceSquared = TNumericLimits<float>::Max();
		if (bFoundViewer)
		{
			for (const TWeakObjectPtr<USWCabinWaterCullComponent>& Entry : State.Components)
			{
				USWCabinWaterCullComponent* Component = Entry.Get();
				const AActor* Owner = Component ? Component->GetOwner() : nullptr;
				if (!Component || !Owner || !Component->bWaterCullEnabled)
				{
					continue;
				}
				const float DistanceSquared = FVector::DistSquared(ViewerLocation, Owner->GetActorLocation());
				const float MaximumDistance = FMath::Max(0.0f, Component->ActivationDistance);
				if (DistanceSquared <= FMath::Square(MaximumDistance)
					&& DistanceSquared < BestDistanceSquared)
				{
					Best = Component;
					BestDistanceSquared = DistanceSquared;
				}
			}
		}
		State.SelectedComponent = Best;
		return Best;
	}

	void BuildInverseRows(
		const FTransform& Transform,
		FLinearColor& OutRow0,
		FLinearColor& OutRow1,
		FLinearColor& OutRow2)
	{
		const FVector Origin = Transform.InverseTransformPosition(FVector::ZeroVector);
		const FVector DX = Transform.InverseTransformPosition(FVector::XAxisVector) - Origin;
		const FVector DY = Transform.InverseTransformPosition(FVector::YAxisVector) - Origin;
		const FVector DZ = Transform.InverseTransformPosition(FVector::ZAxisVector) - Origin;
		OutRow0 = FLinearColor(DX.X, DY.X, DZ.X, Origin.X);
		OutRow1 = FLinearColor(DX.Y, DY.Y, DZ.Y, Origin.Y);
		OutRow2 = FLinearColor(DX.Z, DY.Z, DZ.Z, Origin.Z);
	}
}

USWCabinWaterCullComponent::USWCabinWaterCullComponent()
{
	static ConstructorHelpers::FObjectFinder<USWCabinWaterCullData> SharedCabinData(
		TEXT("/Game/Blueprints/Water/Culling/DA_SW_ShipCabinWaterCull.DA_SW_ShipCabinWaterCull"));
	CabinData = SharedCabinData.Object;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

bool USWCabinWaterCullComponent::ContainsWorldPosition(const FVector& WorldPosition) const
{
	const AActor* Owner = GetOwner();
	if (!bWaterCullEnabled || !IsValid(Owner) || !CabinData)
	{
		return false;
	}
	float Threshold = 0.35f;
	if (WaterParameterCollection && GetWorld())
	{
		if (const UMaterialParameterCollectionInstance* Instance =
			GetWorld()->GetParameterCollectionInstance(WaterParameterCollection))
		{
			Instance->GetScalarParameterValue(TEXT("SW_CabinCullThreshold"), Threshold);
		}
	}
	return CabinData->ContainsLocalPosition(
		Owner->GetActorTransform().InverseTransformPosition(WorldPosition), Threshold);
}

bool USWCabinWaterCullComponent::IsWorldPositionInsideAnyCabin(UWorld* World, const FVector& WorldPosition)
{
	const SWCabinWaterCull::FWorldSelectionState* State = SWCabinWaterCull::WorldStates.Find(World);
	if (State)
	{
		for (const TWeakObjectPtr<USWCabinWaterCullComponent>& Entry : State->Components)
		{
			const USWCabinWaterCullComponent* Component = Entry.Get();
			if (Component && Component->ContainsWorldPosition(WorldPosition))
			{
				return true;
			}
		}
	}
	return false;
}

void USWCabinWaterCullComponent::BeginPlay()
{
	Super::BeginPlay();
	WaterParameterCollection = LoadObject<UMaterialParameterCollection>(
		nullptr, SWCabinWaterCull::CollectionPath);
	bHasUploadedTransform = false;
	bUploadedDisabled = false;
	if (UWorld* World = GetWorld())
	{
		SWCabinWaterCull::WorldStates.FindOrAdd(World).Components.AddUnique(this);
	}
	if (!WaterParameterCollection)
	{
		UE_LOG(LogSWCabinWaterCull, Error, TEXT("[2/5 Asset] MPC path=%s load=FAILED"), SWCabinWaterCull::CollectionPath);
	}
}

void USWCabinWaterCullComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (SWCabinWaterCull::FWorldSelectionState* State = SWCabinWaterCull::WorldStates.Find(World))
		{
			const bool bWasSelected = State->SelectedComponent.Get() == this;
			State->Components.Remove(this);
			State->SelectedComponent.Reset();
			State->SelectionFrame = MAX_uint64;
			if (bWasSelected && State->bGlobalCullWasEnabled)
			{
				UploadDisabled(true);
				State->bGlobalCullWasEnabled = false;
			}
			if (State->Components.IsEmpty())
			{
				SWCabinWaterCull::WorldStates.Remove(World);
			}
		}
	}
	WaterParameterCollection = nullptr;
	Super::EndPlay(EndPlayReason);
}

void USWCabinWaterCullComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	DiagnosticLogAccumulator += DeltaTime;
	UWorld* World = GetWorld();
	if (!World || World->IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	SWCabinWaterCull::FWorldSelectionState& State =
		SWCabinWaterCull::WorldStates.FindOrAdd(World);
	if (State.SelectionFrame != GFrameCounter)
	{
		State.SelectionFrame = GFrameCounter;
		SWCabinWaterCull::SelectNearestComponent(World, State);
	}

	if (State.SelectedComponent.Get() == this)
	{
		UploadTransformIfChanged();
		State.bGlobalCullWasEnabled = true;
	}
	else
	{
		bHasUploadedTransform = false;
		bUploadedDisabled = false;
		if (!State.SelectedComponent.IsValid()
			&& State.bGlobalCullWasEnabled
			&& State.Components.Num() > 0
			&& State.Components[0].Get() == this)
		{
			UploadDisabled(true);
			State.bGlobalCullWasEnabled = false;
		}
	}
}

void USWCabinWaterCullComponent::UploadDisabled(bool bForceUpload)
{
	if ((!bForceUpload && bUploadedDisabled) || !WaterParameterCollection || !GetWorld())
	{
		return;
	}
	if (UMaterialParameterCollectionInstance* Instance =
		GetWorld()->GetParameterCollectionInstance(WaterParameterCollection))
	{
		Instance->SetScalarParameterValue(SWCabinWaterCull::EnabledParameter, 0.0f);
		Instance->SetScalarParameterValue(SWCabinWaterCull::DebugViewParameter, 0.0f);
		bUploadedDisabled = true;
		bHasUploadedTransform = false;
	}
}

void USWCabinWaterCullComponent::UploadTransformIfChanged()
{
	AActor* Owner = GetOwner();
	if (!bWaterCullEnabled || !Owner || !WaterParameterCollection || !GetWorld() ||
		GetWorld()->IsNetMode(NM_DedicatedServer))
	{
		UploadDisabled();
		return;
	}
	const FTransform CurrentTransform = Owner->GetActorTransform();
	if (bHasUploadedTransform && CurrentTransform.Equals(LastUploadedTransform, 0.01f))
	{
		return;
	}
	UMaterialParameterCollectionInstance* Instance =
		GetWorld()->GetParameterCollectionInstance(WaterParameterCollection);
	if (!Instance)
	{
		UE_LOG(LogSWCabinWaterCull, Error, TEXT("[3/5 MPC] GetParameterCollectionInstance FAILED owner=%s"), *GetNameSafe(Owner));
		return;
	}
	FLinearColor Row0, Row1, Row2;
	SWCabinWaterCull::BuildInverseRows(CurrentTransform, Row0, Row1, Row2);
	Instance->SetVectorParameterValue(SWCabinWaterCull::InverseRow0Parameter, Row0);
	Instance->SetVectorParameterValue(SWCabinWaterCull::InverseRow1Parameter, Row1);
	Instance->SetVectorParameterValue(SWCabinWaterCull::InverseRow2Parameter, Row2);
	Instance->SetScalarParameterValue(SWCabinWaterCull::EnabledParameter, 1.0f);
	Instance->SetScalarParameterValue(SWCabinWaterCull::DebugViewParameter, float(DebugView));
	LastUploadedTransform = CurrentTransform;
	bHasUploadedTransform = true;
	bUploadedDisabled = false;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSWCabinWaterCullInverseRowsTest,
	"ArtisticSW.Water.CabinCull.InverseRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSWCabinWaterCullInverseRowsTest::RunTest(const FString& Parameters)
{
	const FTransform Transform(
		FRotator(17.0, 31.0, -9.0), FVector(89240.0, 78420.0, 4420.0), FVector(1.1, 0.9, 1.0));
	FLinearColor R0, R1, R2;
	SWCabinWaterCull::BuildInverseRows(Transform, R0, R1, R2);
	const FVector WorldPoint(89510.0, 78220.0, 4680.0);
	const FVector Reconstructed(
		R0.R * WorldPoint.X + R0.G * WorldPoint.Y + R0.B * WorldPoint.Z + R0.A,
		R1.R * WorldPoint.X + R1.G * WorldPoint.Y + R1.B * WorldPoint.Z + R1.A,
		R2.R * WorldPoint.X + R2.G * WorldPoint.Y + R2.B * WorldPoint.Z + R2.A);
	TestTrue(TEXT("MPC inverse rows reconstruct ship-local position"),
		Reconstructed.Equals(Transform.InverseTransformPosition(WorldPoint), 0.05));
	return true;
}
#endif
