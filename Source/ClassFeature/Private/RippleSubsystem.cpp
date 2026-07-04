#include "RippleSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "WaterSubsystem.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Rendering/Texture2DResource.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "Engine/Engine.h"
#include "Stats/Stats.h"
#include "EngineUtils.h"
#include "WaterBodyActor.h"

URippleSubsystem::URippleSubsystem()
{
}

void URippleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Create a 32x2 transient float texture
	RippleTexture = UTexture2D::CreateTransient(MaxActiveRipples, 2, PF_A32B32G32R32F);
	if (RippleTexture)
	{
		RippleTexture->SRGB = false;
		RippleTexture->CompressionSettings = TC_VectorDisplacementmap;
		RippleTexture->Filter = TF_Nearest; // Ensure nearest filtering to prevent bilinear interpolation artifacts
		RippleTexture->AddressX = TA_Clamp;
		RippleTexture->AddressY = TA_Clamp;
		RippleTexture->UpdateResource();
	}

	UpdateTexture();
}

void URippleSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AWaterBody> It(World); It; ++It)
		{
			AWaterBody* WaterBody = *It;
			if (WaterBody)
			{
				WaterBody->OnActorBeginOverlap.RemoveAll(this);
			}
		}
	}

	if (RippleTexture)
	{
		RippleTexture = nullptr;
	}

	Super::Deinitialize();
}

void URippleSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Bind to OnActorBeginOverlap for all WaterBody actors in the level
	for (TActorIterator<AWaterBody> It(&InWorld); It; ++It)
	{
		AWaterBody* WaterBody = *It;
		if (WaterBody)
		{
			WaterBody->OnActorBeginOverlap.AddDynamic(this, &URippleSubsystem::OnWaterBodyActorOverlap);
			// UE_LOG(LogTemp, Warning, TEXT("[RippleSubsystem] Bound overlap listener to WaterBody: %s"), *WaterBody->GetName());
		}
	}
}

void URippleSubsystem::OnWaterBodyActorOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	if (!OtherActor || OtherActor == OverlappedActor) return;

	// Use actor's current velocity
	float DownwardSpeed = -OtherActor->GetVelocity().Z;

	if (DownwardSpeed >= MinVelocityThreshold)
	{
		FVector ContactLoc = OtherActor->GetActorLocation();
		float InitialAmplitude = FMath::Clamp(DownwardSpeed * AmplitudeMultiplier, 10.0f, MaxInitialAmplitude);

		// Spawn ripple locally using Default configurations
		AddRipple(FVector2D(ContactLoc.X, ContactLoc.Y), InitialAmplitude, DefaultWaveSpeed, DefaultDecayRate, DefaultWaveLength);

		// UE_LOG(LogTemp, Warning, TEXT("[RippleSubsystem] Actor: %s entered WaterBody: %s. Speed: %.2f. Spawning Ripple Amp: %.2f"),
		// 	*OtherActor->GetName(), *OverlappedActor->GetName(), DownwardSpeed, InitialAmplitude);
	}
}

TStatId URippleSubsystem::GetStatId() const
{
	return TStatId();
}

void URippleSubsystem::Tick(float DeltaTime)
{
	if (!GetWorld()) return;

	float ServerTime = GetServerTime();

	// 1. Remove expired ripples
	bool bChanged = false;
	{
		FWriteScopeLock WriteLock(RipplesLock);
		for (int32 i = ActiveRipples.Num() - 1; i >= 0; --i)
		{
			if (ServerTime >= ActiveRipples[i].ExpireTime)
			{
				ActiveRipples.RemoveAtSwap(i);
				bChanged = true;
			}
		}
	}

	// 2. Update transient texture
	UpdateTexture();

	// 3. Update server time to MPC
	UpdateServerTimeMPC(ServerTime);

	// 4. Debug Diagnostics & Dynamic Texture Binding (Every 1 second)
	static float LastDebugLogTime = 0.0f;
	float CurrentTime = GetServerTime();
	
	bool bShouldLog = (CurrentTime - LastDebugLogTime >= 1.0f);
	
	// We bind to MID texture parameters every frame to ensure runtime dynamic materials are updated,
	// but only log every 1 second to avoid console spam.
	int32 BoundWaterBodiesCount = 0;
	int32 FailedWaterBodiesCount = 0;
	for (TActorIterator<AWaterBody> It(GetWorld()); It; ++It)
	{
		if (AWaterBody* WaterBody = *It)
		{
			if (UWaterBodyComponent* WaterComp = WaterBody->GetWaterBodyComponent())
			{
				if (UMaterialInstanceDynamic* WaterMID = WaterComp->GetWaterMaterialInstance())
				{
					WaterMID->SetTextureParameterValue(FName(TEXT("RippleTex")), RippleTexture);
					BoundWaterBodiesCount++;
				}
				else
				{
					FailedWaterBodiesCount++;
				}
			}
		}
	}

	if (bShouldLog)
	{
		LastDebugLogTime = CurrentTime;
		
		FString MPCStatus = TEXT("Failed (No MPC)");
		if (UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld()))
		{
			if (UMaterialParameterCollection* MPC = WaterSubsystem->GetMaterialParameterCollection())
			{
				if (UMaterialParameterCollectionInstance* MPCInstance = GetWorld()->GetParameterCollectionInstance(MPC))
				{
					MPCStatus = TEXT("Success (Updated ServerTime)");
				}
			}
		}

		float TestHeight = 0.0f;
		FString ActiveWaveInfo = TEXT("None");
		{
			FReadScopeLock ReadLock(RipplesLock);
			if (ActiveRipples.Num() > 0)
			{
				const FWaveData& FirstRipple = ActiveRipples[0];
				FVector TestPos(FirstRipple.Origin.X + 100.0f, FirstRipple.Origin.Y, 0.0f);
				TestHeight = GetRippleHeight(TestPos);
				ActiveWaveInfo = FString::Printf(TEXT("First Wave Origin: (%f, %f), Amp: %f"), FirstRipple.Origin.X, FirstRipple.Origin.Y, FirstRipple.InitialAmplitude);
			}
		}

		// UE_LOG(LogTemp, Warning, TEXT("[RippleSubsystem Diagnostics] Time: %.2f | MPC: %s | Bound MIDs: %d (Failed: %d) | Active Waves: %d (%s) | Test Height (+100cm): %.4f"),
		// 	ServerTime, *MPCStatus, BoundWaterBodiesCount, FailedWaterBodiesCount, ActiveRipples.Num(), *ActiveWaveInfo, TestHeight);
	}
}

void URippleSubsystem::AddRipple(FVector2D Origin, float InitialAmplitude, float WaveSpeed, float DecayRate, float WaveLength)
{
	UWorld* World = GetWorld();
	if (!World) return;

	float ServerTime = GetServerTime();

	// Range Culling: Check distance to all player pawns
	bool bIsNearPlayer = false;
	TArray<AActor*> Players;
	UGameplayStatics::GetAllActorsOfClass(World, APawn::StaticClass(), Players);

	for (AActor* Actor : Players)
	{
		APawn* Pawn = Cast<APawn>(Actor);
		if (Pawn && Pawn->IsPlayerControlled())
		{
			FVector ActorLoc = Actor->GetActorLocation();
			float DistSq = FVector2D::DistSquared(Origin, FVector2D(ActorLoc.X, ActorLoc.Y));
			if (DistSq <= MaxGenerationDistance * MaxGenerationDistance)
			{
				bIsNearPlayer = true;
				break;
			}
		}
	}

	// If no players are nearby, do not generate the ripple
	if (!bIsNearPlayer && Players.Num() > 0)
	{
		// UE_LOG(LogTemp, Log, TEXT("[RippleSubsystem] AddRipple Culled: No players near %s (MaxDist: %.2f)"), *Origin.ToString(), MaxGenerationDistance);
		return;
	}

	// Calculate ripple expiration time
	float EffectiveDecayRate = FMath::Max(DecayRate, 0.01f);
	float Tmax = FMath::Loge(FMath::Max(InitialAmplitude, AmplitudeCullThreshold) / AmplitudeCullThreshold) / EffectiveDecayRate;
	
	// Safety clamping for Tmax (max 10 seconds lifespan)
	Tmax = FMath::Clamp(Tmax, 0.5f, 10.0f);

	FWaveData NewWave;
	NewWave.Origin = Origin;
	NewWave.StartTime = ServerTime;
	NewWave.InitialAmplitude = InitialAmplitude;
	NewWave.WaveSpeed = WaveSpeed;
	NewWave.DecayRate = DecayRate;
	NewWave.WaveLength = WaveLength;
	NewWave.ExpireTime = ServerTime + Tmax;

	{
		FWriteScopeLock WriteLock(RipplesLock);
		if (ActiveRipples.Num() >= MaxActiveRipples)
		{
			// Replace the oldest ripple or the one closest to expiration
			int32 BestIndex = 0;
			float MinExpireTime = ActiveRipples[0].ExpireTime;
			for (int32 i = 1; i < ActiveRipples.Num(); ++i)
			{
				if (ActiveRipples[i].ExpireTime < MinExpireTime)
				{
					MinExpireTime = ActiveRipples[i].ExpireTime;
					BestIndex = i;
				}
			}
			// UE_LOG(LogTemp, Warning, TEXT("[RippleSubsystem] Active slot limit reached. Replacing ripple at index %d. New Location: %s, Amp: %.2f, Tmax: %.2fs"), BestIndex, *Origin.ToString(), InitialAmplitude, Tmax);
			ActiveRipples[BestIndex] = NewWave;
		}
		else
		{
			ActiveRipples.Add(NewWave);
			// UE_LOG(LogTemp, Warning, TEXT("[RippleSubsystem] Spawned Ripple successfully at %s (Amp: %.2f, Speed: %.2f, Tmax: %.2fs). Active Count: %d"), *Origin.ToString(), InitialAmplitude, WaveSpeed, Tmax, ActiveRipples.Num());
		}
	}
}

float URippleSubsystem::GetRippleHeight(const FVector& Location) const
{
	UWorld* World = GetWorld();
	if (!World) return 0.0f;

	float ServerTime = GetServerTime();

	float TotalHeight = 0.0f;
	FVector2D QueryPos(Location.X, Location.Y);

	FReadScopeLock ReadLock(RipplesLock);
	for (const FWaveData& Ripple : ActiveRipples)
	{
		if (Ripple.InitialAmplitude <= 0.0f || ServerTime >= Ripple.ExpireTime)
		{
			continue;
		}

		float dt = ServerTime - Ripple.StartTime;
		if (dt < 0.0f)
		{
			continue;
		}

		float dx = QueryPos.X - Ripple.Origin.X;
		float dy = QueryPos.Y - Ripple.Origin.Y;
		float dSq = dx * dx + dy * dy;

		float R = Ripple.WaveSpeed * dt;
		float W = Ripple.WaveLength * 2.0f;

		// Bounding Sphere Culling (Distance squared check to avoid Sqrt on culled elements)
		float Rmin = FMath::Max(0.0f, R - W);
		float Rmax = R + W;

		if (dSq > Rmax * Rmax || (Rmin > 0.0f && dSq < Rmin * Rmin))
		{
			continue;
		}

		float d = FMath::Sqrt(dSq);
		float DistFromWavefront = FMath::Abs(d - R);

		// Envelope: cubic smoothstep approximation [0.0, 1.0]
		float T = FMath::Clamp(DistFromWavefront / W, 0.0f, 1.0f);
		float Envelope = 1.0f - (T * T * (3.0f - 2.0f * T));

		float Decay = FMath::Exp(-Ripple.DecayRate * dt);
		float Phase = (d - R) / Ripple.WaveLength * 2.0f * PI;
		float Height = Ripple.InitialAmplitude * Decay * FMath::Cos(Phase) * Envelope;

		TotalHeight += Height;
	}

	return TotalHeight;
}

void URippleSubsystem::UpdateTexture()
{
	if (!RippleTexture) return;

	// Use static to avoid allocations, but must be careful with thread-safety. 
	// Since this runs on the Game Thread (Tick), static local is fine.
	static FLinearColor PixelData[64];
	FMemory::Memzero(PixelData, sizeof(PixelData));

	{
		FReadScopeLock ReadLock(RipplesLock);
		int32 Count = FMath::Min(ActiveRipples.Num(), MaxActiveRipples);
		for (int32 i = 0; i < Count; ++i)
		{
			const FWaveData& Ripple = ActiveRipples[i];
			// Row 0 (Y=0): Origin.X, Origin.Y, StartTime, InitialAmplitude
			PixelData[i] = FLinearColor(Ripple.Origin.X, Ripple.Origin.Y, Ripple.StartTime, Ripple.InitialAmplitude);
			// Row 1 (Y=1): WaveSpeed, DecayRate, WaveLength, ExpireTime
			PixelData[i + MaxActiveRipples] = FLinearColor(Ripple.WaveSpeed, Ripple.DecayRate, Ripple.WaveLength, Ripple.ExpireTime);
		}
	}

	FTexture2DResource* TextureResource = (FTexture2DResource*)RippleTexture->GetResource();
	if (TextureResource)
	{
		// Enqueue render command to safely upload texture data on the Render Thread
		ENQUEUE_RENDER_COMMAND(UpdateRippleTextureCmd)(
			[TextureResource, DataCopy = TArray<FLinearColor>(PixelData, 64)](FRHICommandListImmediate& RHICmdList)
			{
				FUpdateTextureRegion2D Region(0, 0, 0, 0, MaxActiveRipples, 2);
				RHICmdList.UpdateTexture2D(
					TextureResource->GetTexture2DRHI(),
					0,
					Region,
					MaxActiveRipples * sizeof(FLinearColor),
					(uint8*)DataCopy.GetData()
				);
			});
	}
}

void URippleSubsystem::UpdateServerTimeMPC(float ServerTime)
{
	if (!GetWorld()) return;

	if (UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld()))
	{
		if (UMaterialParameterCollection* MPC = WaterSubsystem->GetMaterialParameterCollection())
		{
			if (UMaterialParameterCollectionInstance* MPCInstance = GetWorld()->GetParameterCollectionInstance(MPC))
			{
				MPCInstance->SetScalarParameterValue(FName(TEXT("ServerTime")), ServerTime);
			}
		}
	}
}

float URippleSubsystem::GetServerTime() const
{
	if (UWorld* World = GetWorld())
	{
		if (AGameStateBase* GameState = World->GetGameState())
		{
			return GameState->GetServerWorldTimeSeconds();
		}
		return World->GetTimeSeconds();
	}
	return 0.0f;
}
