#include "SWCabinWaterCullComponent.h"

#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWCabinWaterCull, Log, All);

namespace SWCabinWaterCull
{
	const FName EnabledParameter(TEXT("SW_CabinCullEnabled"));
	const FName InverseRow0Parameter(TEXT("SW_CabinCullInvRow0"));
	const FName InverseRow1Parameter(TEXT("SW_CabinCullInvRow1"));
	const FName InverseRow2Parameter(TEXT("SW_CabinCullInvRow2"));
	const FName DebugViewParameter(TEXT("SW_CabinCullDebugView"));
	const TCHAR* CollectionPath = TEXT("/Game/Blueprints/Water/MPC_Water_Custom.MPC_Water_Custom");

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
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void USWCabinWaterCullComponent::BeginPlay()
{
	Super::BeginPlay();
	WaterParameterCollection = LoadObject<UMaterialParameterCollection>(
		nullptr, SWCabinWaterCull::CollectionPath);
	bHasUploadedTransform = false;
	bUploadedDisabled = false;
	UploadTransformIfChanged();
	UE_LOG(LogSWCabinWaterCull, Display,
		TEXT("[1/5 BeginPlay] component=%s owner=%s world=%s netMode=%d enabled=%d debugView=%d"),
		*GetNameSafe(this), *GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()),
		GetWorld() ? int32(GetWorld()->GetNetMode()) : -1, bWaterCullEnabled ? 1 : 0, int32(DebugView));
	if (WaterParameterCollection)
	{
		UE_LOG(LogSWCabinWaterCull, Display, TEXT("[2/5 Asset] MPC path=%s load=SUCCESS"), SWCabinWaterCull::CollectionPath);
	}
	else
	{
		UE_LOG(LogSWCabinWaterCull, Error, TEXT("[2/5 Asset] MPC path=%s load=FAILED"), SWCabinWaterCull::CollectionPath);
	}
}

void USWCabinWaterCullComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bWaterCullEnabled = false;
	UploadDisabled();
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
	if (bWaterCullEnabled)
	{
		UploadTransformIfChanged();
	}
	else
	{
		UploadDisabled();
	}
	if (bDiagnosticLogging && DiagnosticLogAccumulator >= FMath::Max(0.25f, DiagnosticLogInterval))
	{
		DiagnosticLogAccumulator = 0.0f;
		UMaterialParameterCollectionInstance* Instance = WaterParameterCollection && GetWorld()
			? GetWorld()->GetParameterCollectionInstance(WaterParameterCollection) : nullptr;
		float Enabled = -1.0f, Debug = -1.0f, Threshold = -1.0f;
		const bool bEnabledRead = Instance && Instance->GetScalarParameterValue(SWCabinWaterCull::EnabledParameter, Enabled);
		const bool bDebugRead = Instance && Instance->GetScalarParameterValue(SWCabinWaterCull::DebugViewParameter, Debug);
		const bool bThresholdRead = Instance && Instance->GetScalarParameterValue(TEXT("SW_CabinCullThreshold"), Threshold);
		UE_LOG(LogSWCabinWaterCull, Display,
			TEXT("[5/5 Heartbeat] owner=%s transformUploaded=%d MPCInstance=%s enabled=%s%.2f debug=%s%.2f threshold=%s%.3f loc=%s"),
			*GetNameSafe(GetOwner()), bHasUploadedTransform ? 1 : 0, *GetNameSafe(Instance),
			bEnabledRead ? TEXT("") : TEXT("READ_FAIL:"), Enabled,
			bDebugRead ? TEXT("") : TEXT("READ_FAIL:"), Debug,
			bThresholdRead ? TEXT("") : TEXT("READ_FAIL:"), Threshold,
			GetOwner() ? *GetOwner()->GetActorLocation().ToCompactString() : TEXT("NO_OWNER"));
	}
}

void USWCabinWaterCullComponent::UploadDisabled()
{
	if (bUploadedDisabled || !WaterParameterCollection || !GetWorld())
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
	if (bDiagnosticLogging)
	{
		UE_LOG(LogSWCabinWaterCull, Display,
			TEXT("[3/5 MPC] instance=%s [4/5 Upload] owner=%s loc=%s rot=%s scale=%s row0=(%.6f %.6f %.6f %.3f)"),
			*GetNameSafe(Instance), *GetNameSafe(Owner), *CurrentTransform.GetLocation().ToCompactString(),
			*CurrentTransform.Rotator().ToCompactString(), *CurrentTransform.GetScale3D().ToCompactString(),
			Row0.R, Row0.G, Row0.B, Row0.A);
	}
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
