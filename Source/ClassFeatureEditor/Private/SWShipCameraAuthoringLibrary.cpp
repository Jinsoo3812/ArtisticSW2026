#include "SWShipCameraAuthoringLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshCompiler.h"

UStaticMesh* USWShipCameraAuthoringLibrary::BuildCameraProxy(UStaticMesh* Source,
	const FString& PackagePath, const TArray<FVector>& BoundsMin, const TArray<FVector>& BoundsMax,
	int32 ExpectedRemovedTriangles)
{
	if (!Source || BoundsMin.IsEmpty() || BoundsMin.Num() != BoundsMax.Num()
		|| ExpectedRemovedTriangles <= 0 || !PackagePath.StartsWith(TEXT("/Game/Blueprints/Ship/Camera/"))) return nullptr;
	FStaticMeshCompilingManager::Get().FinishCompilation({Source});
	const FMeshDescription* Original = Source->GetMeshDescription(0);
	if (!Original || FindObject<UObject>(nullptr, *(PackagePath + TEXT(".") + FPackageName::GetLongPackageAssetName(PackagePath)))) return nullptr;
	FMeshDescription Description(*Original);
	const TVertexAttributesConstRef<FVector3f> Positions = Description.GetVertexPositions();
	TArray<FPolygonID> Remove;
	TArray<int32> Counts;
	Counts.Init(0, BoundsMin.Num());
	int32 RemovedTriangles = 0;
	for (const FPolygonID Polygon : Description.Polygons().GetElementIDs())
	{
		FBox Bounds(ForceInit);
		for (const FVertexID Vertex : Description.GetPolygonVertices(Polygon)) Bounds += FVector(Positions[Vertex]);
		for (int32 Index = 0; Index < BoundsMin.Num(); ++Index)
		{
			const FBox Selection = FBox(BoundsMin[Index], BoundsMax[Index]).ExpandBy(0.02);
			if (Selection.IsInsideOrOn(Bounds.Min) && Selection.IsInsideOrOn(Bounds.Max))
			{
				Remove.Add(Polygon);
				const int32 Count = Description.GetPolygonTriangles(Polygon).Num();
				Counts[Index] += Count;
				RemovedTriangles += Count;
				break;
			}
		}
	}
	if (RemovedTriangles != ExpectedRemovedTriangles || Counts.Contains(0))
	{
		UE_LOG(LogTemp, Error, TEXT("[ShipCameraAuthor] Selection mismatch: expected %d, found %d"), ExpectedRemovedTriangles, RemovedTriangles);
		return nullptr;
	}
	for (const FPolygonID Polygon : Remove) Description.DeletePolygon(Polygon);
	FElementIDRemappings Remappings;
	Description.Compact(Remappings);
	UPackage* Package = CreatePackage(*PackagePath);
	UStaticMesh* Mesh = NewObject<UStaticMesh>(Package, *FPackageName::GetLongPackageAssetName(PackagePath), RF_Public | RF_Standalone | RF_Transactional);
	Mesh->SetNumSourceModels(1);
	Mesh->GetStaticMaterials() = Source->GetStaticMaterials();
	Mesh->GetSourceModel(0).BuildSettings = Source->GetSourceModel(0).BuildSettings;
	Mesh->CreateMeshDescription(0, MoveTemp(Description));
	Mesh->CommitMeshDescription(0);
	Mesh->CreateBodySetup();
	Mesh->GetBodySetup()->CollisionTraceFlag = CTF_UseComplexAsSimple;
	Mesh->GetBodySetup()->bDoubleSidedGeometry = true;
	// Use the regular editor build, including its resource lifecycle and cook data.
	// BuildFromStaticMeshDescriptions on an asynchronously compiling duplicate can
	// replace initialized render resources without releasing them (UE 5.7).
	Mesh->Build(false);
	FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
	Mesh->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Mesh);
	return Mesh;
}
