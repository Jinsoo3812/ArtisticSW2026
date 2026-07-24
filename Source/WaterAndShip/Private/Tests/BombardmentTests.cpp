#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Bombardment.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Ship.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBombardmentConfigurationTest,
	"ArtisticSW.Bombardment.Configuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBombardmentConfigurationTest::RunTest(const FString& Parameters)
{
	const ABombardment* Defaults = GetDefault<ABombardment>();
	TestNotNull(TEXT("Bombardment CDO exists"), Defaults);
	TestTrue(TEXT("Skill radius is positive"), Defaults->SkillRadius > 0.0f);
	TestTrue(TEXT("Target range is positive"), Defaults->MaxTargetRange > 0.0f);
	TestTrue(TEXT("Launch height is positive"), Defaults->LaunchHeightZ > 0.0f);
	TestTrue(TEXT("At least one projectile is fired per volley"), Defaults->ProjectilesPerVolley > 0);
	TestTrue(TEXT("At least one volley is fired"), Defaults->VolleyCount > 0);
	TestNotNull(TEXT("A preview class is available"), Defaults->PreviewClass.Get());
	const ABombardmentPreview* PreviewDefaults = GetDefault<ABombardmentPreview>();
	TestNotNull(TEXT("A single preview mesh component is available"), PreviewDefaults->PreviewMesh.Get());
	TestTrue(
		TEXT("Ship input outranks player/item contexts for shared confirm and cancel keys"),
		GetDefault<AShip>()->ShipInputPriority >= 20);

	UMaterialInterface* BombardPreviewMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/New/Skill/Bombardment/M_BombardPreview.M_BombardPreview"));
	UMaterialInterface* GhostMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/New/Skill/Bombardment/M_Ghost.M_Ghost"));
	TestNotNull(TEXT("M_BombardPreview loads"), BombardPreviewMaterial);
	TestNotNull(TEXT("M_Ghost loads"), GhostMaterial);
	for (const TPair<const TCHAR*, UMaterialInterface*> MaterialInfo : {
		TPair<const TCHAR*, UMaterialInterface*>(TEXT("M_BombardPreview"), BombardPreviewMaterial),
		TPair<const TCHAR*, UMaterialInterface*>(TEXT("M_Ghost"), GhostMaterial) })
	{
		if (MaterialInfo.Value)
		{
			const UMaterial* BaseMaterial = MaterialInfo.Value->GetMaterial();
			AddInfo(FString::Printf(
				TEXT("%s domain=%d blend=%d unlit=%s twoSided=%s"),
				MaterialInfo.Key,
				BaseMaterial ? static_cast<int32>(BaseMaterial->MaterialDomain.GetValue()) : INDEX_NONE,
				static_cast<int32>(MaterialInfo.Value->GetBlendMode()),
				MaterialInfo.Value->GetShadingModels().HasShadingModel(MSM_Unlit)
					? TEXT("true") : TEXT("false"),
				MaterialInfo.Value->IsTwoSided() ? TEXT("true") : TEXT("false")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBombardmentScheduleTest,
	"ArtisticSW.Bombardment.ShotSchedule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBombardmentScheduleTest::RunTest(const FString& Parameters)
{
	const TArray<float> Times = ABombardment::BuildShotTimes(4, 0.6f, 2.0f, 3);
	TestEqual(TEXT("Schedule contains N times volley count shots"), Times.Num(), 12);
	if (Times.Num() == 12)
	{
		TestEqual(TEXT("First projectile is immediate"), Times[0], 0.0f);
		TestEqual(TEXT("Last projectile in first volley uses full burst duration"), Times[3], 0.6f);
		TestEqual(TEXT("Second volley starts start-to-start at two seconds"), Times[4], 2.0f);
		TestEqual(TEXT("Final projectile time combines volley start and burst"), Times[11], 4.6f);
	}

	const TArray<float> OverlappingTimes = ABombardment::BuildShotTimes(4, 3.0f, 2.0f, 2);
	for (int32 Index = 1; Index < OverlappingTimes.Num(); ++Index)
	{
		TestTrue(TEXT("Overlapping volleys remain in chronological schedule order"),
			OverlappingTimes[Index] >= OverlappingTimes[Index - 1]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBombardmentDistributionTest,
	"ArtisticSW.Bombardment.StratifiedDiskDistribution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBombardmentDistributionTest::RunTest(const FString& Parameters)
{
	constexpr int32 Count = 32;
	constexpr float Radius = 3000.0f;
	const TArray<FVector2D> A = ABombardment::GenerateDiskOffsets(
		Count, Radius, 12345, EBombardmentDistributionMode::StratifiedDisk, 0.35f);
	const TArray<FVector2D> B = ABombardment::GenerateDiskOffsets(
		Count, Radius, 12345, EBombardmentDistributionMode::StratifiedDisk, 0.35f);

	TestEqual(TEXT("Requested point count is returned"), A.Num(), Count);
	TestTrue(TEXT("A fixed seed is deterministic"), A == B);
	for (int32 Index = 0; Index < A.Num(); ++Index)
	{
		const float NormalizedRadiusSquared = A[Index].SizeSquared() / FMath::Square(Radius);
		TestTrue(*FString::Printf(TEXT("Point %d stays inside the skill disk"), Index),
			NormalizedRadiusSquared <= 1.0f + UE_KINDA_SMALL_NUMBER);
		TestTrue(*FString::Printf(TEXT("Point %d occupies its equal-area radial stratum"), Index),
			NormalizedRadiusSquared + UE_KINDA_SMALL_NUMBER >= static_cast<float>(Index) / Count
			&& NormalizedRadiusSquared <= static_cast<float>(Index + 1) / Count + UE_KINDA_SMALL_NUMBER);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBombardmentBallisticSolutionTest,
	"ArtisticSW.Bombardment.BallisticSolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBombardmentBallisticSolutionTest::RunTest(const FString& Parameters)
{
	const FVector Start(6000.0f, 0.0f, 10000.0f);
	const FVector Target = FVector::ZeroVector;
	constexpr float Speed = 3000.0f;
	constexpr float GravityZ = -980.0f;
	FVector Velocity;
	if (!TestTrue(TEXT("Configured diagonal falling shot has a ballistic solution"),
		ABombardment::SolveBallisticVelocity(Start, Target, Speed, GravityZ, Velocity)))
	{
		return false;
	}

	TestEqual(TEXT("Initial velocity preserves ship cannonball speed"),
		Velocity.Size(), static_cast<double>(Speed), 0.1);
	const float HorizontalSpeed = FVector(Velocity.X, Velocity.Y, 0.0f).Size();
	const float FlightTime = FVector::Dist2D(Start, Target) / HorizontalSpeed;
	const FVector SimulatedImpact = Start + Velocity * FlightTime
		+ FVector::UpVector * (0.5f * GravityZ * FlightTime * FlightTime);
	TestTrue(TEXT("Ballistic solution reaches the intended impact point"),
		SimulatedImpact.Equals(Target, 1.0f));
	return true;
}

#endif
