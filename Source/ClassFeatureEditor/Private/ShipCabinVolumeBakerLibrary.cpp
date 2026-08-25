#include "ShipCabinVolumeBakerLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Editor.h"
#include "Engine/InstancedStaticMesh.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/VolumeTexture.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "SWCabinWaterCullData.h"

using namespace UE::Geometry;

namespace
{
	constexpr TCHAR ShipTag[] = TEXT("SW_CabinShip");
	constexpr TCHAR BarrierTag[] = TEXT("SW_CabinBarrier");
	constexpr TCHAR SeedTag[] = TEXT("SW_CabinSeed");
	constexpr TCHAR DebugTag[] = TEXT("SW_CabinDebug");
	constexpr TCHAR CullTargetTag[] = TEXT("SW_CabinCullTarget");
	constexpr TCHAR MaskPackagePath[] = TEXT("/Game/Blueprints/Water/Culling/VT_SW_ShipCabinMask");
	constexpr TCHAR MaskAssetName[] = TEXT("VT_SW_ShipCabinMask");
	constexpr TCHAR DataPackagePath[] = TEXT("/Game/Blueprints/Water/Culling/DA_SW_ShipCabinWaterCull");
	constexpr TCHAR DataAssetName[] = TEXT("DA_SW_ShipCabinWaterCull");

	UVolumeTexture* CreateOrUpdateMaskTexture(
		const TArray<uint8>& Voxels,
		int32 NX,
		int32 NY,
		int32 NZ)
	{
		UPackage* Package = CreatePackage(MaskPackagePath);
		if (!Package)
		{
			return nullptr;
		}
		Package->FullyLoad();
		UVolumeTexture* Texture = FindObject<UVolumeTexture>(Package, MaskAssetName);
		const bool bNewAsset = Texture == nullptr;
		if (!Texture)
		{
			Texture = NewObject<UVolumeTexture>(
				Package, MaskAssetName, RF_Public | RF_Standalone | RF_Transactional);
		}
		Texture->Modify();
		Texture->Source.Init(NX, NY, NZ, 1, TSF_G8);
		{
			FTextureSource::FMipLock MipLock(
				FTextureSource::ELockState::ReadWrite, &Texture->Source, 0);
			if (!MipLock.IsValid() || MipLock.Image.GetImageSizeBytes() != Voxels.Num())
			{
				return nullptr;
			}
			FMemory::Memcpy(MipLock.Image.RawData, Voxels.GetData(), Voxels.Num());
		}
		Texture->SRGB = false;
		Texture->CompressionSettings = TC_Masks;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->Filter = TF_Bilinear;
		Texture->AddressMode = TA_Clamp;
		Texture->PostEditChange();
		Texture->MarkPackageDirty();
		if (bNewAsset)
		{
			FAssetRegistryModule::AssetCreated(Texture);
		}
		return Texture;
	}

	USWCabinWaterCullData* CreateOrUpdateCullData(
		UVolumeTexture* MaskTexture,
		const FVector& LocalMin,
		const FVector& LocalMax,
		int32 NX,
		int32 NY,
		int32 NZ,
		float VoxelSize)
	{
		UPackage* Package = CreatePackage(DataPackagePath);
		if (!Package || !MaskTexture)
		{
			return nullptr;
		}
		Package->FullyLoad();
		USWCabinWaterCullData* Data = FindObject<USWCabinWaterCullData>(Package, DataAssetName);
		const bool bNewAsset = Data == nullptr;
		if (!Data)
		{
			Data = NewObject<USWCabinWaterCullData>(
				Package, DataAssetName, RF_Public | RF_Standalone | RF_Transactional);
		}
		Data->Modify();
		Data->MaskTexture = MaskTexture;
		Data->LocalBoundsMin = LocalMin;
		Data->LocalBoundsMax = LocalMax;
		Data->Resolution = FIntVector(NX, NY, NZ);
		Data->VoxelSizeCm = VoxelSize;
		Data->MarkPackageDirty();
		if (bNewAsset)
		{
			FAssetRegistryModule::AssetCreated(Data);
		}
		return Data;
	}

	bool AppendComponentMesh(
		UStaticMeshComponent* Component,
		const FTransform& ShipTransform,
		FDynamicMesh3& Combined,
		FBox& LocalBounds)
	{
		if (!IsValid(Component) || !IsValid(Component->GetStaticMesh()))
		{
			return false;
		}

		const FMeshDescription* Description = Component->GetStaticMesh()->GetMeshDescription(0);
		if (!Description)
		{
			return false;
		}

		FDynamicMesh3 Source;
		FMeshDescriptionToDynamicMesh Converter;
		Converter.bDisableAttributes = true;
		Converter.bEnableOutputGroups = false;
		Converter.bCalculateMaps = false;
		Converter.Convert(Description, Source);

		TMap<int32, int32> VertexMap;
		VertexMap.Reserve(Source.VertexCount());
		for (int32 VertexID : Source.VertexIndicesItr())
		{
			const FVector SourcePosition(Source.GetVertex(VertexID));
			const FVector WorldPosition = Component->GetComponentTransform().TransformPosition(SourcePosition);
			const FVector LocalPosition = ShipTransform.InverseTransformPosition(WorldPosition);
			VertexMap.Add(VertexID, Combined.AppendVertex(FVector3d(LocalPosition)));
			LocalBounds += LocalPosition;
		}

		for (int32 TriangleID : Source.TriangleIndicesItr())
		{
			const FIndex3i Triangle = Source.GetTriangle(TriangleID);
			Combined.AppendTriangle(
				VertexMap.FindChecked(Triangle.A),
				VertexMap.FindChecked(Triangle.B),
				VertexMap.FindChecked(Triangle.C));
		}
		return true;
	}

	int32 GridIndex(int32 X, int32 Y, int32 Z, int32 NX, int32 NY)
	{
		return X + NX * (Y + NY * Z);
	}
}

FSWCabinBakeResult UShipCabinVolumeBakerLibrary::BakeTaggedCabinDebug(
	float VoxelSize,
	float SurfaceThickness,
	float BoundsPadding)
{
	FSWCabinBakeResult Result;
	VoxelSize = FMath::Clamp(VoxelSize, 10.0f, 100.0f);
	SurfaceThickness = FMath::Max(SurfaceThickness, VoxelSize * 0.55f);
	BoundsPadding = FMath::Max(BoundsPadding, VoxelSize * 2.0f);

	if (!GEditor)
	{
		Result.Message = TEXT("GEditor is unavailable.");
		return Result;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!IsValid(World))
	{
		Result.Message = TEXT("No editor world is loaded.");
		return Result;
	}

	AStaticMeshActor* ShipActor = nullptr;
	APointLight* SeedActor = nullptr;
	TArray<AStaticMeshActor*> Barriers;
	TArray<AActor*> OldDebugActors;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->ActorHasTag(DebugTag))
		{
			OldDebugActors.Add(Actor);
			continue;
		}
		if (Actor->ActorHasTag(SeedTag))
		{
			SeedActor = Cast<APointLight>(Actor);
			continue;
		}
		if (Actor->ActorHasTag(BarrierTag))
		{
			if (AStaticMeshActor* Barrier = Cast<AStaticMeshActor>(Actor))
			{
				Barriers.Add(Barrier);
			}
			continue;
		}

		AStaticMeshActor* Candidate = Cast<AStaticMeshActor>(Actor);
		if (!Candidate || !IsValid(Candidate->GetStaticMeshComponent()) ||
			!IsValid(Candidate->GetStaticMeshComponent()->GetStaticMesh()))
		{
			continue;
		}
		const bool bExplicitTag = Candidate->ActorHasTag(ShipTag);
		const bool bExactFallback = Candidate->GetActorLabel() == TEXT("SM_Ship") &&
			Candidate->GetStaticMeshComponent()->GetStaticMesh()->GetPathName() ==
			TEXT("/Game/Blueprints/Ship/Mesh/SM_Ship.SM_Ship");
		if (bExplicitTag || bExactFallback)
		{
			ShipActor = Candidate;
		}
	}

	if (!ShipActor || !SeedActor)
	{
		Result.Message = FString::Printf(TEXT("Required actors missing: ship=%s seed=%s"),
			ShipActor ? TEXT("yes") : TEXT("no"), SeedActor ? TEXT("yes") : TEXT("no"));
		return Result;
	}
	if (Barriers.IsEmpty())
	{
		Result.Message = TEXT("No SW_CabinBarrier static-mesh actors were found.");
		return Result;
	}

	ShipActor->Modify();
	ShipActor->Tags.AddUnique(ShipTag);
	ShipActor->Tags.AddUnique(CullTargetTag);
	const FTransform ShipTransform = ShipActor->GetActorTransform();
	FDynamicMesh3 Combined;
	FBox LocalBounds(ForceInit);
	if (!AppendComponentMesh(ShipActor->GetStaticMeshComponent(), ShipTransform, Combined, LocalBounds))
	{
		Result.Message = TEXT("Could not read SM_Ship LOD0 mesh description.");
		return Result;
	}
	for (AStaticMeshActor* Barrier : Barriers)
	{
		AppendComponentMesh(Barrier->GetStaticMeshComponent(), ShipTransform, Combined, LocalBounds);
	}

	if (Combined.TriangleCount() == 0 || !LocalBounds.IsValid)
	{
		Result.Message = TEXT("The combined ship/barrier triangle shell is empty.");
		return Result;
	}

	const FVector GridMin = LocalBounds.Min - FVector(BoundsPadding);
	const FVector GridMax = LocalBounds.Max + FVector(BoundsPadding);
	const FVector GridSize = GridMax - GridMin;
	const int32 NX = FMath::CeilToInt(GridSize.X / VoxelSize);
	const int32 NY = FMath::CeilToInt(GridSize.Y / VoxelSize);
	const int32 NZ = FMath::CeilToInt(GridSize.Z / VoxelSize);
	const int64 CellCount64 = int64(NX) * int64(NY) * int64(NZ);
	if (NX < 3 || NY < 3 || NZ < 3 || CellCount64 > 25000000)
	{
		Result.Message = FString::Printf(TEXT("Unsafe voxel grid size: %d x %d x %d"), NX, NY, NZ);
		return Result;
	}
	const int32 CellCount = int32(CellCount64);

	FDynamicMeshAABBTree3 Spatial(&Combined, true);
	TArray<uint8> Blocked;
	Blocked.SetNumZeroed(CellCount);
	const double ThicknessSquared = FMath::Square(double(SurfaceThickness));
	ParallelFor(CellCount, [&](int32 Index)
	{
		const int32 X = Index % NX;
		const int32 Y = (Index / NX) % NY;
		const int32 Z = Index / (NX * NY);
		const FVector Center = GridMin + FVector(X + 0.5f, Y + 0.5f, Z + 0.5f) * VoxelSize;
		double DistanceSquared = TNumericLimits<double>::Max();
		Spatial.FindNearestTriangle(FVector3d(Center), DistanceSquared);
		Blocked[Index] = DistanceSquared <= ThicknessSquared ? 1 : 0;
	});

	const FVector SeedLocal = ShipTransform.InverseTransformPosition(SeedActor->GetActorLocation());
	auto CoordFromPoint = [&](const FVector& Point)
	{
		return FIntVector(
			FMath::FloorToInt((Point.X - GridMin.X) / VoxelSize),
			FMath::FloorToInt((Point.Y - GridMin.Y) / VoxelSize),
			FMath::FloorToInt((Point.Z - GridMin.Z) / VoxelSize));
	};
	FIntVector SeedCoord = CoordFromPoint(SeedLocal);
	if (SeedCoord.X < 0 || SeedCoord.X >= NX || SeedCoord.Y < 0 || SeedCoord.Y >= NY || SeedCoord.Z < 0 || SeedCoord.Z >= NZ)
	{
		Result.Message = TEXT("SW_CabinSeed is outside the ship voxel bounds.");
		return Result;
	}

	int32 SeedIndex = GridIndex(SeedCoord.X, SeedCoord.Y, SeedCoord.Z, NX, NY);
	if (Blocked[SeedIndex])
	{
		bool bFound = false;
		for (int32 Radius = 1; Radius <= 4 && !bFound; ++Radius)
		{
			for (int32 Z = FMath::Max(1, SeedCoord.Z - Radius); Z <= FMath::Min(NZ - 2, SeedCoord.Z + Radius) && !bFound; ++Z)
			for (int32 Y = FMath::Max(1, SeedCoord.Y - Radius); Y <= FMath::Min(NY - 2, SeedCoord.Y + Radius) && !bFound; ++Y)
			for (int32 X = FMath::Max(1, SeedCoord.X - Radius); X <= FMath::Min(NX - 2, SeedCoord.X + Radius); ++X)
			{
				const int32 Candidate = GridIndex(X, Y, Z, NX, NY);
				if (!Blocked[Candidate])
				{
					SeedCoord = FIntVector(X, Y, Z);
					SeedIndex = Candidate;
					bFound = true;
					break;
				}
			}
		}
		if (!bFound)
		{
			Result.Message = TEXT("SW_CabinSeed is embedded in the triangle shell; no nearby open voxel exists.");
			return Result;
		}
	}

	TArray<uint8> Filled;
	Filled.SetNumZeroed(CellCount);
	TArray<int32> Queue;
	Queue.Reserve(CellCount / 8);
	Filled[SeedIndex] = 1;
	Queue.Add(SeedIndex);
	bool bLeaked = false;
	for (int32 Read = 0; Read < Queue.Num(); ++Read)
	{
		const int32 Index = Queue[Read];
		const int32 X = Index % NX;
		const int32 Y = (Index / NX) % NY;
		const int32 Z = Index / (NX * NY);
		if (X == 0 || X == NX - 1 || Y == 0 || Y == NY - 1 || Z == 0 || Z == NZ - 1)
		{
			bLeaked = true;
			break;
		}
		const int32 Neighbours[6] = {
			Index - 1, Index + 1, Index - NX, Index + NX, Index - NX * NY, Index + NX * NY };
		for (int32 Neighbour : Neighbours)
		{
			if (!Blocked[Neighbour] && !Filled[Neighbour])
			{
				Filled[Neighbour] = 1;
				Queue.Add(Neighbour);
			}
		}
	}

	Result.bLeakedToExterior = bLeaked;
	Result.FilledVoxelCount = Queue.Num();
	if (bLeaked)
	{
		Result.Message = FString::Printf(
			TEXT("LEAK: the seed fill reached the exterior boundary after %d voxels. Seal remaining openings with SW_CabinBarrier actors."),
			Queue.Num());
		return Result;
	}

	// The exclusion volume should overlap the physical floor and walls instead of
	// ending exactly on their zero-thickness render triangles. Expand one voxel
	// sideways and two voxels downward in ship-local space; do not expand upward
	// onto the exposed deck.
	TArray<uint8> ExpandedFilled = Filled;
	for (int32 Index : Queue)
	{
		const int32 X = Index % NX;
		const int32 Y = (Index / NX) % NY;
		const int32 Z = Index / (NX * NY);
		if (X > 0)      ExpandedFilled[GridIndex(X - 1, Y, Z, NX, NY)] = 1;
		if (X + 1 < NX) ExpandedFilled[GridIndex(X + 1, Y, Z, NX, NY)] = 1;
		if (Y > 0)      ExpandedFilled[GridIndex(X, Y - 1, Z, NX, NY)] = 1;
		if (Y + 1 < NY) ExpandedFilled[GridIndex(X, Y + 1, Z, NX, NY)] = 1;
		if (Z > 0)      ExpandedFilled[GridIndex(X, Y, Z - 1, NX, NY)] = 1;
		if (Z > 1)      ExpandedFilled[GridIndex(X, Y, Z - 2, NX, NY)] = 1;
	}

	// TSF_G8 is sampled as normalized UNorm in the material. A logical value of
	// 1 would become 1/255 (0.00392) and never pass the default 0.35 threshold.
	// Encode occupied cells as full-scale white so the GPU observes 1.0.
	for (uint8& Voxel : ExpandedFilled)
	{
		Voxel = Voxel != 0 ? 255 : 0;
	}

	UVolumeTexture* MaskTexture = CreateOrUpdateMaskTexture(
		ExpandedFilled, NX, NY, NZ);
	USWCabinWaterCullData* CullData = CreateOrUpdateCullData(
		MaskTexture, GridMin, GridMax, NX, NY, NZ, VoxelSize);
	if (!MaskTexture || !CullData)
	{
		Result.Message = TEXT("Fill succeeded, but the runtime Volume Texture/Data Asset could not be created.");
		return Result;
	}

	for (AActor* OldDebug : OldDebugActors)
	{
		World->DestroyActor(OldDebug);
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.OverrideLevel = World->GetCurrentLevel();
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags = RF_Transactional;
	AActor* DebugActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (!DebugActor)
	{
		Result.Message = TEXT("Fill succeeded, but the debug actor could not be spawned.");
		return Result;
	}
	DebugActor->SetActorLabel(TEXT("SW_CabinVolume_Debug"));
	DebugActor->Tags.Add(DebugTag);
	UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(
		DebugActor, TEXT("CabinVoxelRuns"), RF_Transactional);
	DebugActor->SetRootComponent(Instances);
	DebugActor->AddInstanceComponent(Instances);
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Instances->SetCastShadow(false);
	Instances->SetMobility(EComponentMobility::Static);
	Instances->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Instances->RegisterComponent();

	if (UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, DebugActor);
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.0f, 0.35f, 1.0f, 1.0f));
		Instances->SetMaterial(0, Material);
	}

	int32 InstanceCount = 0;
	for (int32 Z = 0; Z < NZ; ++Z)
	for (int32 Y = 0; Y < NY; ++Y)
	{
		int32 X = 0;
		while (X < NX)
		{
			while (X < NX && !ExpandedFilled[GridIndex(X, Y, Z, NX, NY)])
			{
				++X;
			}
			if (X >= NX)
			{
				break;
			}
			const int32 StartX = X;
			while (X < NX && ExpandedFilled[GridIndex(X, Y, Z, NX, NY)])
			{
				++X;
			}
			const int32 RunLength = X - StartX;
			const FVector LocalCenter = GridMin + FVector(StartX + RunLength * 0.5f, Y + 0.5f, Z + 0.5f) * VoxelSize;
			const FVector WorldCenter = ShipTransform.TransformPosition(LocalCenter);
			const FVector WorldScale = ShipTransform.GetScale3D() * FVector(RunLength * VoxelSize / 100.0f, VoxelSize / 100.0f, VoxelSize / 100.0f);
			Instances->AddInstance(FTransform(ShipTransform.GetRotation(), WorldCenter, WorldScale), true);
			++InstanceCount;
		}
	}

	Result.bSuccess = true;
	Result.DebugInstanceCount = InstanceCount;
	Result.Message = FString::Printf(
		TEXT("Cabin sealed: %d interior voxels, %d expanded debug instances, grid %dx%dx%d."),
		Queue.Num(), InstanceCount, NX, NY, NZ);
	World->GetCurrentLevel()->MarkPackageDirty();
	return Result;
}
