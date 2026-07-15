#include "RippleSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/Texture2DResource.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "Engine/Engine.h"
#include "Stats/Stats.h"
#include "EngineUtils.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "WaterBodyTypes.h"
#include "Components/PrimitiveComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	const TCHAR* GetRippleDiagnosticsNetMode(const UWorld* World)
	{
		if (!World)
		{
			return TEXT("NoWorld");
		}

		switch (World->GetNetMode())
		{
		case NM_Standalone: return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Unknown");
		}
	}
}

URippleSubsystem::URippleSubsystem()
{
}

void URippleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bDiagnosticsEnabled = FParse::Param(FCommandLine::Get(), TEXT("RippleDiagnostics"));

	// [Antigravity] 데디케이트 서버에서는 텍스처 생성 및 렌더링 관련 초기화를 진행하지 않음
	if (IsRunningDedicatedServer())
	{
		if (bDiagnosticsEnabled)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-DIAG][DedicatedServer] TextureInitSkippedDedicated"));
		}
		return;
	}

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

	if (bDiagnosticsEnabled)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-DIAG][%s] TextureCreated Texture=%s Size=%dx%d Format=PF_A32B32G32R32F ResourceImmediatelyValid=%s"),
			GetRippleDiagnosticsNetMode(GetWorld()),
			*GetNameSafe(RippleTexture),
			MaxActiveRipples,
			2,
			RippleTexture && RippleTexture->GetResource() ? TEXT("true") : TEXT("false"));
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
	bDiagnosticsEnabled = FParse::Param(FCommandLine::Get(), TEXT("RippleDiagnostics"));
	DiagnosticsStartTime = InWorld.GetTimeSeconds();

	if (bDiagnosticsEnabled)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-DIAG][%s] HarnessEnabled World=%s Time=%.3f"),
			GetRippleDiagnosticsNetMode(&InWorld), *InWorld.GetName(), DiagnosticsStartTime);
	}

	// Bind to OnActorBeginOverlap for all WaterBody actors in the level
	int32 BoundWaterBodyCount = 0;
	for (TActorIterator<AWaterBody> It(&InWorld); It; ++It)
	{
		AWaterBody* WaterBody = *It;
		if (WaterBody)
		{
			WaterBody->OnActorBeginOverlap.AddUniqueDynamic(this, &URippleSubsystem::OnWaterBodyActorOverlap);
			++BoundWaterBodyCount;

			if (bDiagnosticsEnabled)
			{
				const FBox Bounds = WaterBody->GetComponentsBoundingBox(true);
				UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-DIAG][%s] BoundWaterBody Name=%s Location=%s BoundsMin=%s BoundsMax=%s"),
					GetRippleDiagnosticsNetMode(&InWorld),
					*WaterBody->GetName(),
					*WaterBody->GetActorLocation().ToString(),
					*Bounds.Min.ToString(),
					*Bounds.Max.ToString());

				TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(WaterBody);
				for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
				{
					if (PrimitiveComponent && PrimitiveComponent->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
					{
						UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-DIAG][%s] WaterCollision Component=%s Profile=%s Enabled=%d GenerateOverlap=%s Bounds=%s"),
							GetRippleDiagnosticsNetMode(&InWorld),
							*PrimitiveComponent->GetName(),
							*PrimitiveComponent->GetCollisionProfileName().ToString(),
							static_cast<int32>(PrimitiveComponent->GetCollisionEnabled()),
							PrimitiveComponent->GetGenerateOverlapEvents() ? TEXT("true") : TEXT("false"),
							*PrimitiveComponent->Bounds.GetBox().ToString());
					}
				}
			}
			// UE_LOG(LogTemp, Warning, TEXT("[RippleSubsystem] Bound overlap listener to WaterBody: %s"), *WaterBody->GetName());
		}
	}

	if (bDiagnosticsEnabled)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-DIAG][%s] BindingComplete Count=%d"),
			GetRippleDiagnosticsNetMode(&InWorld), BoundWaterBodyCount);
	}
}

void URippleSubsystem::OnWaterBodyActorOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	if (!OtherActor || OtherActor == OverlappedActor) return;

	// Use actor's current velocity
	float DownwardSpeed = -OtherActor->GetVelocity().Z;
	UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(OtherActor->GetRootComponent());
	const FVector PhysicsVelocity = RootPrimitive ? RootPrimitive->GetPhysicsLinearVelocity() : FVector::ZeroVector;

	float FlatWaterSurfaceZ = OverlappedActor ? OverlappedActor->GetActorLocation().Z : 0.0f;
	if (AWaterBody* WaterBody = Cast<AWaterBody>(OverlappedActor))
	{
		if (UWaterBodyComponent* WaterComponent = WaterBody->GetWaterBodyComponent())
		{
			const EWaterBodyQueryFlags QueryFlags = EWaterBodyQueryFlags::ComputeLocation;
			const auto QueryResult = WaterComponent->TryQueryWaterInfoClosestToWorldLocation(OtherActor->GetActorLocation(), QueryFlags);
			if (QueryResult.HasValue())
			{
				FlatWaterSurfaceZ = QueryResult.GetValue().GetWaterSurfaceLocation().Z;
			}
		}
	}

	if (bDiagnosticsEnabled)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-DIAG][%s] BeginOverlap Water=%s Other=%s Class=%s Location=%s ActorVelocity=%s PhysicsVelocity=%s FlatSurfaceZ=%.2f HeightAboveSurface=%.2f DownwardSpeed=%.2f Threshold=%.2f"),
			GetRippleDiagnosticsNetMode(GetWorld()),
			*GetNameSafe(OverlappedActor),
			*OtherActor->GetName(),
			*OtherActor->GetClass()->GetName(),
			*OtherActor->GetActorLocation().ToString(),
			*OtherActor->GetVelocity().ToString(),
			*PhysicsVelocity.ToString(),
			FlatWaterSurfaceZ,
			OtherActor->GetActorLocation().Z - FlatWaterSurfaceZ,
			DownwardSpeed,
			MinVelocityThreshold);
	}

	if (DownwardSpeed >= MinVelocityThreshold)
	{
		FVector ContactLoc = OtherActor->GetActorLocation();
		float InitialAmplitude = FMath::Clamp(DownwardSpeed * AmplitudeMultiplier, 10.0f, MaxInitialAmplitude);

		// Spawn ripple locally using Default configurations
		AddRipple(FVector2D(ContactLoc.X, ContactLoc.Y), InitialAmplitude, DefaultWaveSpeed, DefaultDecayRate, DefaultWaveLength);

		// UE_LOG(LogTemp, Warning, TEXT("[RippleSubsystem] Actor: %s entered WaterBody: %s. Speed: %.2f. Spawning Ripple Amp: %.2f"),
		// 	*OtherActor->GetName(), *OverlappedActor->GetName(), DownwardSpeed, InitialAmplitude);
	}
	else
	{
		// UE_LOG(LogTemp, Warning, TEXT("[RippleSubsystem] Actor %s entered WaterBody %s but skipped: DownwardSpeed (%.2f) < MinVelocityThreshold (%.2f)"), 
		// 	*OtherActor->GetName(), *OverlappedActor->GetName(), DownwardSpeed, MinVelocityThreshold);
	}
}

TStatId URippleSubsystem::GetStatId() const
{
	return TStatId();
}

void URippleSubsystem::Tick(float DeltaTime)
{
	// [Antigravity] 데디케이트 서버에서는 틱 연산을 수행하지 않음
	if (IsRunningDedicatedServer()) return;

	if (!GetWorld()) return;

	TickDiagnostics();

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

	// 3. Debug Diagnostics & Dynamic Material Binding (Every 1 second)
	static float LastDebugLogTime = 0.0f;
	float CurrentTime = GetServerTime();
	
	bool bShouldLog = (CurrentTime - LastDebugLogTime >= 1.0f);
	
	// We bind to MID texture parameters every frame to ensure runtime dynamic materials are updated,
	// but only log every 1 second to avoid console spam.
	int32 BoundWaterBodiesCount = 0;
	int32 FailedWaterBodiesCount = 0;
	int32 MatchingRippleTextureCount = 0;
	int32 MissingRippleTextureCount = 0;
	int32 MatchingServerTimeCount = 0;
	int32 MismatchingServerTimeCount = 0;
	FString FirstMIDName = TEXT("None");
	FString FirstReadbackTextureName = TEXT("None");
	float FirstReadbackServerTime = 0.0f;
	static const FName RippleTextureParameterName(TEXT("RippleTex"));
	static const FName ServerTimeParameterName(TEXT("ServerTime"));
	for (TActorIterator<AWaterBody> It(GetWorld()); It; ++It)
	{
		if (AWaterBody* WaterBody = *It)
		{
			if (UWaterBodyComponent* WaterComp = WaterBody->GetWaterBodyComponent())
			{
				if (UMaterialInstanceDynamic* WaterMID = WaterComp->GetWaterMaterialInstance())
				{
					WaterMID->SetTextureParameterValue(RippleTextureParameterName, RippleTexture);
					WaterMID->SetScalarParameterValue(ServerTimeParameterName, ServerTime);
					BoundWaterBodiesCount++;
					if (bDiagnosticsEnabled)
					{
						UTexture* ReadbackTexture = WaterMID->K2_GetTextureParameterValue(RippleTextureParameterName);
						const float ReadbackServerTime = WaterMID->K2_GetScalarParameterValue(ServerTimeParameterName);
						if (ReadbackTexture == RippleTexture)
						{
							MatchingRippleTextureCount++;
						}
						else
						{
							MissingRippleTextureCount++;
						}
						if (FMath::IsNearlyEqual(ReadbackServerTime, ServerTime, 0.01f))
						{
							MatchingServerTimeCount++;
						}
						else
						{
							MismatchingServerTimeCount++;
						}

						if (FirstMIDName == TEXT("None"))
						{
							FirstMIDName = WaterMID->GetPathName();
							FirstReadbackTextureName = GetNameSafe(ReadbackTexture);
							FirstReadbackServerTime = ReadbackServerTime;
						}
					}
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

		float TestHeight = 0.0f;
		FString ActiveWaveInfo = TEXT("None");
		FWaveData FirstRipple;
		bool bHasRipples = false;
		int32 ActiveRippleCount = 0;
		{
			// [Antigravity] 데드락 방지: lock을 잡은 상태에서 GetRippleHeight(내부에서 다시 lock을 잡음)를 호출하지 않고, 데이터를 복사한 후 lock을 해제하고 호출함
			FReadScopeLock ReadLock(RipplesLock);
			ActiveRippleCount = ActiveRipples.Num();
			if (ActiveRipples.Num() > 0)
			{
				FirstRipple = ActiveRipples[0];
				bHasRipples = true;
			}
		}

		if (bHasRipples)
		{
			FVector TestPos(FirstRipple.Origin.X + 100.0f, FirstRipple.Origin.Y, 0.0f);
			TestHeight = GetRippleHeight(TestPos);
			ActiveWaveInfo = FString::Printf(TEXT("First Wave Origin: (%f, %f), Amp: %f"), FirstRipple.Origin.X, FirstRipple.Origin.Y, FirstRipple.InitialAmplitude);
		}

		if (bDiagnosticsEnabled)
		{
			const FTexture2DResource* TextureResource = RippleTexture
				? static_cast<const FTexture2DResource*>(RippleTexture->GetResource())
				: nullptr;
			const bool bTextureRHIValid = TextureResource && TextureResource->GetTexture2DRHI() != nullptr;
			UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-DIAG][%s] MaterialPipeline ServerTime=%.3f Texture=%s ResourceValid=%s RHIValid=%s BoundMIDs=%d FailedMIDs=%d RippleTexMatches=%d RippleTexMissing=%d MIDServerTimeMatches=%d MIDServerTimeMismatches=%d FirstMID=%s ReadbackTexture=%s ReadbackServerTime=%.3f ServerTimeDelta=%.4f ActiveRipples=%d TestHeight100cm=%.4f Wave={%s}"),
				GetRippleDiagnosticsNetMode(GetWorld()),
				ServerTime,
				*GetNameSafe(RippleTexture),
				TextureResource ? TEXT("true") : TEXT("false"),
				bTextureRHIValid ? TEXT("true") : TEXT("false"),
				BoundWaterBodiesCount,
				FailedWaterBodiesCount,
				MatchingRippleTextureCount,
				MissingRippleTextureCount,
				MatchingServerTimeCount,
				MismatchingServerTimeCount,
				*FirstMIDName,
				*FirstReadbackTextureName,
				FirstReadbackServerTime,
				FMath::Abs(FirstReadbackServerTime - ServerTime),
				ActiveRippleCount,
				TestHeight,
				*ActiveWaveInfo);
		}
	}
}

void URippleSubsystem::AddRipple(FVector2D Origin, float InitialAmplitude, float WaveSpeed, float DecayRate, float WaveLength)
{
	// [Antigravity] 데디케이트 서버에서는 리플을 생성하지 않음
	if (IsRunningDedicatedServer())
	{
		if (bDiagnosticsEnabled)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-DIAG][DedicatedServer] AddRippleSkippedDedicated Origin=%s Amp=%.2f"),
				*Origin.ToString(), InitialAmplitude);
		}
		return;
	}

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
		if (bDiagnosticsEnabled)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-DIAG][%s] AddRippleCulledDistance Origin=%s MaxDistance=%.2f CandidatePawns=%d"),
				GetRippleDiagnosticsNetMode(World), *Origin.ToString(), MaxGenerationDistance, Players.Num());
		}
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

		if (bDiagnosticsEnabled)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-DIAG][%s] AddRippleAccepted Origin=%s Amp=%.2f ActiveCount=%d ExpireTime=%.3f"),
				GetRippleDiagnosticsNetMode(World), *Origin.ToString(), InitialAmplitude, ActiveRipples.Num(), NewWave.ExpireTime);
		}
	}
}

void URippleSubsystem::TickDiagnostics()
{
	if (!bDiagnosticsEnabled)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Elapsed = World->GetTimeSeconds() - DiagnosticsStartTime;
	if (DiagnosticsLastSummaryTime < 0.0f || Elapsed - DiagnosticsLastSummaryTime >= 1.0f)
	{
		DiagnosticsLastSummaryTime = Elapsed;
		int32 ActiveCount = 0;
		{
			FReadScopeLock ReadLock(RipplesLock);
			ActiveCount = ActiveRipples.Num();
		}

		UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-DIAG][%s] Summary Elapsed=%.2f ActiveRipples=%d"),
			GetRippleDiagnosticsNetMode(World),
			Elapsed,
			ActiveCount);
	}
}

float URippleSubsystem::GetRippleHeight(const FVector& Location) const
{
	// [Antigravity] 데디케이트 서버에서는 높이 연산을 건너뛰고 0을 반환
	if (IsRunningDedicatedServer()) return 0.0f;

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

	int32 EncodedRippleCount = 0;
	{
		FReadScopeLock ReadLock(RipplesLock);
		int32 Count = FMath::Min(ActiveRipples.Num(), MaxActiveRipples);
		EncodedRippleCount = Count;
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
	const bool bTextureResourceValid = TextureResource != nullptr;
	if (bDiagnosticsEnabled &&
		(EncodedRippleCount != DiagnosticsLastUploadedRippleCount || bTextureResourceValid != bDiagnosticsLastTextureResourceValid))
	{
		DiagnosticsLastUploadedRippleCount = EncodedRippleCount;
		bDiagnosticsLastTextureResourceValid = bTextureResourceValid;
		const bool bTextureRHIValid = TextureResource && TextureResource->GetTexture2DRHI() != nullptr;
		UE_LOG(LogTemp, Warning, TEXT("[RIPPLE-DIAG][%s] TextureUploadBoundary EncodedRipples=%d ResourceValid=%s RHIValid=%s FirstPixel=(%.2f,%.2f,%.3f,%.2f)"),
			GetRippleDiagnosticsNetMode(GetWorld()),
			EncodedRippleCount,
			bTextureResourceValid ? TEXT("true") : TEXT("false"),
			bTextureRHIValid ? TEXT("true") : TEXT("false"),
			PixelData[0].R,
			PixelData[0].G,
			PixelData[0].B,
			PixelData[0].A);
	}
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
