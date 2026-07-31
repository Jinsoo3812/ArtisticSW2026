#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BasePlayer.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "SwimmingComponent.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealEdGlobals.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"

namespace SwimmingNetworkQuantitative
{
	struct FPhaseMetrics
	{
		FString Name;
		int32 Samples = 0;
		double SumPositionErrorCm = 0.0;
		double SumVerticalErrorCm = 0.0;
		double MaxPositionErrorCm = 0.0;
		double MaxVerticalErrorCm = 0.0;
		int32 MovementModeMismatchSamples = 0;
		int32 DepthModeMismatchSamples = 0;
		int32 UnderwaterMismatchSamples = 0;
		int32 SwimAnimationStateMismatchSamples = 0;
		int32 ServerSwimmingSamples = 0;
		int32 ClientSwimmingSamples = 0;
		int32 DiveInputMismatchSamples = 0;
		int32 AscendInputMismatchSamples = 0;
		bool bHasFirstSample = false;
		double FirstServerZ = 0.0;
		double LastServerZ = 0.0;
		double MinServerZ = TNumericLimits<double>::Max();
		double MaxServerZ = TNumericLimits<double>::Lowest();
		double MaxAbsServerVelocityZ = 0.0;

		void Add(const ABasePlayer* ServerPawn, const ABasePlayer* ClientPawn)
		{
			if (!ServerPawn || !ClientPawn)
			{
				return;
			}

			const double PositionError = FVector::Distance(
				ServerPawn->GetActorLocation(),
				ClientPawn->GetActorLocation());
			const double VerticalError = FMath::Abs(
				ServerPawn->GetActorLocation().Z - ClientPawn->GetActorLocation().Z);
			SumPositionErrorCm += PositionError;
			SumVerticalErrorCm += VerticalError;
			MaxPositionErrorCm = FMath::Max(MaxPositionErrorCm, PositionError);
			MaxVerticalErrorCm = FMath::Max(MaxVerticalErrorCm, VerticalError);

			const UCharacterMovementComponent* ServerMove = ServerPawn->GetCharacterMovement();
			const UCharacterMovementComponent* ClientMove = ClientPawn->GetCharacterMovement();
			if (!ServerMove || !ClientMove
				|| ServerMove->MovementMode != ClientMove->MovementMode
				|| ServerMove->CustomMovementMode != ClientMove->CustomMovementMode)
			{
				++MovementModeMismatchSamples;
			}

			const USwimmingComponent* ServerSwim = ServerPawn->GetSwimmingComponent();
			const USwimmingComponent* ClientSwim = ClientPawn->GetSwimmingComponent();
			if (!ServerSwim || !ClientSwim)
			{
				++DepthModeMismatchSamples;
				++UnderwaterMismatchSamples;
				++SwimAnimationStateMismatchSamples;
			}
			else
			{
				const FSwimmingAnimationState ServerAnimation = ServerSwim->GetAnimationState();
				const FSwimmingAnimationState ClientAnimation = ClientSwim->GetAnimationState();
				DepthModeMismatchSamples += ServerSwim->GetDepthMode() != ClientSwim->GetDepthMode();
				UnderwaterMismatchSamples += ServerSwim->IsUnderwater() != ClientSwim->IsUnderwater();
				SwimAnimationStateMismatchSamples +=
					ServerAnimation.bIsSwimming != ClientAnimation.bIsSwimming;
				ServerSwimmingSamples += ServerAnimation.bIsSwimming;
				ClientSwimmingSamples += ClientAnimation.bIsSwimming;
				DiveInputMismatchSamples +=
					ServerAnimation.bDiveInputHeld != ClientAnimation.bDiveInputHeld;
				AscendInputMismatchSamples +=
					ServerAnimation.bAscendInputHeld != ClientAnimation.bAscendInputHeld;
			}
			const double ServerZ = ServerPawn->GetActorLocation().Z;
			if (!bHasFirstSample)
			{
				bHasFirstSample = true;
				FirstServerZ = ServerZ;
			}
			LastServerZ = ServerZ;
			MinServerZ = FMath::Min(MinServerZ, ServerZ);
			MaxServerZ = FMath::Max(MaxServerZ, ServerZ);
			MaxAbsServerVelocityZ = FMath::Max(
				MaxAbsServerVelocityZ,
				FMath::Abs(static_cast<double>(ServerPawn->GetVelocity().Z)));
			++Samples;
		}

		void Log() const
		{
			const double SafeSamples = FMath::Max(1, Samples);
			UE_LOG(LogTemp, Display,
				TEXT("[SWIM-NET-METRIC] Phase=%s Samples=%d Mean3Dcm=%.3f Max3Dcm=%.3f MeanZcm=%.3f MaxZcm=%.3f ServerDeltaZcm=%.3f ServerZRangeCm=%.3f MaxAbsServerVelZ=%.3f ServerSwimmingPct=%.2f ClientSwimmingPct=%.2f MoveModeMismatchPct=%.2f DepthModeMismatchPct=%.2f UnderwaterMismatchPct=%.2f DiveInputMismatchPct=%.2f AscendInputMismatchPct=%.2f SwimAnimStateMismatchPct=%.2f"),
				*Name,
				Samples,
				SumPositionErrorCm / SafeSamples,
				MaxPositionErrorCm,
				SumVerticalErrorCm / SafeSamples,
				MaxVerticalErrorCm,
				LastServerZ - FirstServerZ,
				bHasFirstSample ? MaxServerZ - MinServerZ : 0.0,
				MaxAbsServerVelocityZ,
				100.0 * ServerSwimmingSamples / SafeSamples,
				100.0 * ClientSwimmingSamples / SafeSamples,
				100.0 * MovementModeMismatchSamples / SafeSamples,
				100.0 * DepthModeMismatchSamples / SafeSamples,
				100.0 * UnderwaterMismatchSamples / SafeSamples,
				100.0 * DiveInputMismatchSamples / SafeSamples,
				100.0 * AscendInputMismatchSamples / SafeSamples,
				100.0 * SwimAnimationStateMismatchSamples / SafeSamples);
		}
	};

	struct FTestState
	{
		TObjectPtr<UWorld> ServerWorld;
		TObjectPtr<UWorld> ClientWorld;
		TObjectPtr<ABasePlayer> ServerPawn;
		TObjectPtr<ABasePlayer> ClientPawn;
		TObjectPtr<AWaterBody> ServerWaterBody;
		TObjectPtr<AWaterBody> ClientWaterBody;
		TObjectPtr<ULevelEditorPlaySettings> PlaySettings;
		double StartWaitSeconds = FPlatformTime::Seconds();
		double MeasurementStartSeconds = 0.0;
		bool bTeleported = false;
		bool bLagEnabled = false;
		bool bDiveStarted = false;
		bool bDiveStopped = false;
		bool bAscendStarted = false;
		bool bAscendStopped = false;
		bool bPreparationSucceeded = false;
		int32 ForcedMovementModeRecoveries = 0;
		TArray<FPhaseMetrics> Metrics;
	};

	bool ResolveWorldsAndPawns(FTestState& State)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (Context.WorldType != EWorldType::PIE || !World || !World->GetNetDriver())
			{
				continue;
			}
			if (World->GetNetDriver()->IsServer())
			{
				State.ServerWorld = World;
			}
			else
			{
				State.ClientWorld = World;
			}
		}

		if (!State.ServerWorld || !State.ClientWorld)
		{
			return false;
		}

		APlayerController* ClientController = State.ClientWorld->GetFirstPlayerController();
		State.ClientPawn = ClientController ? Cast<ABasePlayer>(ClientController->GetPawn()) : nullptr;
		const APlayerState* ClientPlayerState = ClientController ? ClientController->PlayerState : nullptr;
		if (!State.ClientPawn || !ClientPlayerState)
		{
			return false;
		}

		for (FConstPlayerControllerIterator It = State.ServerWorld->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* ServerController = It->Get();
			if (ServerController
				&& ServerController->PlayerState
				&& ServerController->PlayerState->GetPlayerId() == ClientPlayerState->GetPlayerId())
			{
				State.ServerPawn = Cast<ABasePlayer>(ServerController->GetPawn());
				break;
			}
		}
		return State.ServerPawn && State.ClientPawn;
	}

	bool TeleportServerPawnToWater(FTestState& State)
	{
		for (TActorIterator<AWaterBody> It(State.ServerWorld); It; ++It)
		{
			AWaterBody* WaterBody = *It;
			UWaterBodyComponent* WaterComponent = WaterBody ? WaterBody->GetWaterBodyComponent() : nullptr;
			if (!WaterComponent)
			{
				continue;
			}

			const FBoxSphereBounds Bounds = WaterComponent->Bounds;
			TArray<FVector2D> CandidatePositions;
			TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
			WaterBody->GetComponents(PrimitiveComponents);
			for (const UPrimitiveComponent* Primitive : PrimitiveComponents)
			{
				if (Primitive
					&& Primitive->IsCollisionEnabled()
					&& Primitive->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Ignore
					&& Primitive->Bounds.BoxExtent.X > 10.0f
					&& Primitive->Bounds.BoxExtent.Y > 10.0f)
				{
					CandidatePositions.Add(FVector2D(
						Primitive->Bounds.Origin.X,
						Primitive->Bounds.Origin.Y));
				}
			}

			const FVector2D Center(Bounds.Origin.X, Bounds.Origin.Y);
			const FVector2D Offset(Bounds.BoxExtent.X * 0.4, Bounds.BoxExtent.Y * 0.4);
			CandidatePositions.Append({
				Center + FVector2D(Offset.X, 0.0),
				Center + FVector2D(-Offset.X, 0.0),
				Center + FVector2D(0.0, Offset.Y),
				Center + FVector2D(0.0, -Offset.Y)
			});

			for (const FVector2D& Candidate : CandidatePositions)
			{
				const FVector QueryLocation(Candidate.X, Candidate.Y, Bounds.Origin.Z - 100.0);
				if (WaterComponent->IsWorldLocationInExclusionVolume(QueryLocation))
				{
					continue;
				}

				const EWaterBodyQueryFlags QueryFlags =
					EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::ComputeDepth;
				const TValueOrError<FWaterBodyQueryResult, EWaterBodyQueryError> Query =
					WaterComponent->TryQueryWaterInfoClosestToWorldLocation(QueryLocation, QueryFlags, -1.0f);
				if (!Query.HasValue() || Query.GetValue().GetWaterSurfaceDepth() < 200.0f)
				{
					continue;
				}

				const FVector SurfaceLocation = Query.GetValue().GetWaterSurfaceLocation();
				const FVector SwimStartLocation(
					SurfaceLocation.X,
					SurfaceLocation.Y,
					SurfaceLocation.Z - 50.0);
				State.ServerPawn->TeleportTo(
					SwimStartLocation,
					FRotator::ZeroRotator);
				State.ServerPawn->GetCharacterMovement()->StopMovementImmediately();
				State.ServerPawn->GetCapsuleComponent()->UpdateOverlaps();
				State.ServerPawn->ForceNetUpdate();

				// Test setup only: autonomous proxies intentionally do not consume
				// ordinary server RepMovement teleports. Put the owning client at
				// the same starting transform, then measure only subsequent CMC traffic.
				State.ClientPawn->TeleportTo(SwimStartLocation, FRotator::ZeroRotator);
				State.ClientPawn->GetCharacterMovement()->StopMovementImmediately();
				State.ClientPawn->GetCapsuleComponent()->UpdateOverlaps();

				// Isolate movement prediction from the map's generated Ocean collision.
				// Water queries still use the real WaterBodyOcean on each PIE world.
				FHitResult EmptySweep;
				State.ServerPawn->GetSwimmingComponent()->OnOverlapBegin(
					State.ServerPawn->GetCapsuleComponent(),
					WaterBody,
					WaterComponent,
					0,
					false,
					EmptySweep);
				State.ServerWaterBody = WaterBody;
				for (TActorIterator<AWaterBody> ClientWaterIt(State.ClientWorld); ClientWaterIt; ++ClientWaterIt)
				{
					if (AWaterBody* ClientWaterBody = *ClientWaterIt)
					{
						State.ClientWaterBody = ClientWaterBody;
						State.ClientPawn->GetSwimmingComponent()->OnOverlapBegin(
							State.ClientPawn->GetCapsuleComponent(),
							ClientWaterBody,
							ClientWaterBody->GetWaterBodyComponent(),
							0,
							false,
							EmptySweep);
						break;
					}
				}
				State.ServerPawn->GetCharacterMovement()->SetMovementMode(
					MOVE_Custom,
					static_cast<uint8>(ECustomMovementMode::CMOVE_Swimming));
				State.ClientPawn->GetCharacterMovement()->SetMovementMode(
					MOVE_Custom,
					static_cast<uint8>(ECustomMovementMode::CMOVE_Swimming));
				State.ServerPawn->GetCharacterMovement()->Buoyancy = 0.0f;
				State.ClientPawn->GetCharacterMovement()->Buoyancy = 0.0f;
				State.bTeleported = true;
				UE_LOG(LogTemp, Display,
					TEXT("[SWIM-NET-METRIC] Teleport Water=%s Surface=%s Depth=%.1f BoundsExtent=%s Pawn=%s"),
					*GetNameSafe(WaterBody),
					*SurfaceLocation.ToString(),
					Query.GetValue().GetWaterSurfaceDepth(),
					*Bounds.BoxExtent.ToString(),
					*State.ServerPawn->GetName());
				return true;
			}
		}
		return false;
	}

	class FWaitForNetworkPIE final : public IAutomationLatentCommand
	{
	public:
		FWaitForNetworkPIE(TSharedPtr<FTestState> InState, FAutomationTestBase* InTest)
			: State(MoveTemp(InState)), Test(InTest)
		{
		}

		virtual bool Update() override
		{
			if (ResolveWorldsAndPawns(*State))
			{
				return true;
			}
			if (FPlatformTime::Seconds() - State->StartWaitSeconds > 30.0)
			{
				Test->AddError(TEXT("Timed out waiting for listen-server/client PIE pawns."));
				return true;
			}
			return false;
		}

	private:
		TSharedPtr<FTestState> State;
		FAutomationTestBase* Test;
	};

	class FPrepareSwimming final : public IAutomationLatentCommand
	{
	public:
		FPrepareSwimming(TSharedPtr<FTestState> InState, FAutomationTestBase* InTest)
			: State(MoveTemp(InState)), Test(InTest), StartSeconds(FPlatformTime::Seconds())
		{
		}

		virtual bool Update() override
		{
			if (!State->bTeleported && !TeleportServerPawnToWater(*State))
			{
				Test->AddError(TEXT("No queryable water body was found in Test_Level."));
				return true;
			}

			USwimmingComponent* ServerSwim = State->ServerPawn->GetSwimmingComponent();
			USwimmingComponent* ClientSwim = State->ClientPawn->GetSwimmingComponent();
			FHitResult EmptySweep;
			if (ServerSwim && State->ServerWaterBody)
			{
				ServerSwim->OnOverlapBegin(
					State->ServerPawn->GetCapsuleComponent(),
					State->ServerWaterBody,
					State->ServerWaterBody->GetWaterBodyComponent(),
					0,
					false,
					EmptySweep);
			}
			if (ClientSwim && State->ClientWaterBody)
			{
				ClientSwim->OnOverlapBegin(
					State->ClientPawn->GetCapsuleComponent(),
					State->ClientWaterBody,
					State->ClientWaterBody->GetWaterBodyComponent(),
					0,
					false,
					EmptySweep);
			}
			if (ServerSwim && !ServerSwim->IsCustomSwimming())
			{
				State->ServerPawn->GetCharacterMovement()->SetMovementMode(
					MOVE_Custom,
					static_cast<uint8>(ECustomMovementMode::CMOVE_Swimming));
			}
			if (ClientSwim && !ClientSwim->IsCustomSwimming())
			{
				State->ClientPawn->GetCharacterMovement()->SetMovementMode(
					MOVE_Custom,
					static_cast<uint8>(ECustomMovementMode::CMOVE_Swimming));
			}
			if (ServerSwim && ClientSwim
				&& ServerSwim->IsCustomSwimming()
				&& ClientSwim->IsCustomSwimming())
			{
				if (BothSwimmingStartSeconds <= 0.0)
				{
					BothSwimmingStartSeconds = FPlatformTime::Seconds();
					return false;
				}
				if (FPlatformTime::Seconds() - BothSwimmingStartSeconds < 1.0)
				{
					return false;
				}

				State->Metrics = {
					{ TEXT("SurfaceIdle_NoLag") },
					{ TEXT("SurfaceMove_NoLag") },
					{ TEXT("SurfaceMove_RTT100") },
					{ TEXT("Dive_RTT100") },
					{ TEXT("SubmergedMove_RTT100") },
					{ TEXT("Ascend_RTT100") }
				};
				State->bPreparationSucceeded = true;
				State->MeasurementStartSeconds = FPlatformTime::Seconds();
				return true;
			}
			BothSwimmingStartSeconds = 0.0;

			if (FPlatformTime::Seconds() - StartSeconds > 15.0)
			{
				Test->AddError(TEXT("Server/client did not both enter custom swimming after water teleport."));
				return true;
			}
			return false;
		}

	private:
		TSharedPtr<FTestState> State;
		FAutomationTestBase* Test;
		double StartSeconds;
		double BothSwimmingStartSeconds = 0.0;
	};

	class FMeasureSwimming final : public IAutomationLatentCommand
	{
	public:
		FMeasureSwimming(TSharedPtr<FTestState> InState, FAutomationTestBase* InTest)
			: State(MoveTemp(InState)), Test(InTest)
		{
		}

		virtual bool Update() override
		{
			if (!State->bPreparationSucceeded || State->Metrics.Num() != 6)
			{
				Test->AddError(TEXT("Swimming measurement did not start because PIE water entry preparation failed."));
				return true;
			}

			if (!State->ServerPawn || !State->ClientPawn)
			{
				Test->AddError(TEXT("A PIE pawn was destroyed during the swimming measurement."));
				return true;
			}

			MaintainIsolatedSwimmingState();
			const double Elapsed = FPlatformTime::Seconds() - State->MeasurementStartSeconds;
			if (Elapsed < 0.0)
			{
				State->ClientPawn->StopMoveInput();
				return false;
			}
			int32 PhaseIndex = INDEX_NONE;
			if (Elapsed < 2.0)
			{
				PhaseIndex = 0;
				State->ClientPawn->StopMoveInput();
			}
			else if (Elapsed < 5.0)
			{
				PhaseIndex = 1;
				State->ClientPawn->DoMove(0.0f, 1.0f);
			}
			else if (Elapsed < 6.0)
			{
				State->ClientPawn->StopMoveInput();
				EnableLag();
			}
			else if (Elapsed < 9.0)
			{
				PhaseIndex = 2;
				State->ClientPawn->DoMove(0.0f, 1.0f);
			}
			else if (Elapsed < 10.5)
			{
				PhaseIndex = 3;
				if (!State->bDiveStarted)
				{
					State->bDiveStarted = true;
					State->ClientPawn->StartSwimDive();
				}
			}
			else if (Elapsed < 13.5)
			{
				PhaseIndex = 4;
				if (!State->bDiveStopped)
				{
					State->bDiveStopped = true;
					State->ClientPawn->StopSwimDive();
				}
				State->ClientPawn->DoMove(0.0f, 1.0f);
			}
			else if (Elapsed < 15.0)
			{
				PhaseIndex = 5;
				if (!State->bAscendStarted)
				{
					State->bAscendStarted = true;
					State->ClientPawn->DoJumpStart();
				}
			}
			else
			{
				if (!State->bAscendStopped)
				{
					State->bAscendStopped = true;
					State->ClientPawn->DoJumpEnd();
					State->ClientPawn->StopMoveInput();
				}
				for (const FPhaseMetrics& Metric : State->Metrics)
				{
					Metric.Log();
					Test->TestTrue(
						FString::Printf(TEXT("%s collected samples"), *Metric.Name),
						Metric.Samples > 0);
				}
				UE_LOG(LogTemp, Display,
					TEXT("[SWIM-NET-METRIC] IsolatedHarness ForcedMovementModeRecoveries=%d"),
					State->ForcedMovementModeRecoveries);
				Test->TestEqual(
					TEXT("Surface state drives swimming animation on the owning client"),
					State->Metrics[0].SwimAnimationStateMismatchSamples,
					0);
				return true;
			}

			if (PhaseIndex != INDEX_NONE)
			{
				State->Metrics[PhaseIndex].Add(State->ServerPawn, State->ClientPawn);
			}
			return false;
		}

	private:
		void MaintainIsolatedSwimmingState()
		{
			FHitResult EmptySweep;
			const auto MaintainPawn = [&EmptySweep, this](ABasePlayer* Pawn, AWaterBody* WaterBody)
			{
				if (!Pawn || !WaterBody || !Pawn->GetSwimmingComponent())
				{
					return;
				}
				Pawn->GetSwimmingComponent()->OnOverlapBegin(
					Pawn->GetCapsuleComponent(),
					WaterBody,
					WaterBody->GetWaterBodyComponent(),
					0,
					false,
					EmptySweep);
				if (!Pawn->GetSwimmingComponent()->IsCustomSwimming())
				{
					++State->ForcedMovementModeRecoveries;
					Pawn->GetCharacterMovement()->SetMovementMode(
						MOVE_Custom,
						static_cast<uint8>(ECustomMovementMode::CMOVE_Swimming));
					Pawn->GetCharacterMovement()->Buoyancy = 0.0f;
				}
			};
			MaintainPawn(State->ServerPawn, State->ServerWaterBody);
			MaintainPawn(State->ClientPawn, State->ClientWaterBody);
		}

		void EnableLag()
		{
			if (State->bLagEnabled)
			{
				return;
			}
			State->bLagEnabled = true;
			FPacketSimulationSettings PacketSettings;
			PacketSettings.PktLag = 50;
			State->ServerWorld->GetNetDriver()->SetPacketSimulationSettings(PacketSettings);
			State->ClientWorld->GetNetDriver()->SetPacketSimulationSettings(PacketSettings);
			UE_LOG(LogTemp, Display,
				TEXT("[SWIM-NET-METRIC] NetworkEmulation OneWayEachDriverMs=50 ApproxRTTms=100 PacketLossPct=0"));
		}

		TSharedPtr<FTestState> State;
		FAutomationTestBase* Test;
	};

	class FEndNetworkPIE final : public IAutomationLatentCommand
	{
	public:
		virtual bool Update() override
		{
			if (GUnrealEd && GUnrealEd->PlayWorld)
			{
				GUnrealEd->RequestEndPlayMap();
				return false;
			}
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSwimmingNetworkQuantitativeTest,
	"ArtisticSW.Network.SwimmingPredictionQuantitative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSwimmingNetworkQuantitativeTest::RunTest(const FString& Parameters)
{
	using namespace SwimmingNetworkQuantitative;

	if (GUnrealEd->PlayWorld)
	{
		GUnrealEd->RequestEndPlayMap();
		AddError(TEXT("A PIE session was already active; run this test again after PIE ends."));
		return false;
	}

	FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/New/Level/Test_Level"));
	TSharedPtr<FTestState> State = MakeShared<FTestState>();
	State->PlaySettings = NewObject<ULevelEditorPlaySettings>();
	State->PlaySettings->SetPlayNetMode(EPlayNetMode::PIE_ListenServer);
	State->PlaySettings->SetPlayNumberOfClients(2);
	State->PlaySettings->SetRunUnderOneProcess(true);
	State->PlaySettings->bLaunchSeparateServer = false;
	State->PlaySettings->GameGetsMouseControl = false;

	FLevelEditorModule& LevelEditor =
		FModuleManager::Get().GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	FRequestPlaySessionParams SessionParams;
	SessionParams.WorldType = EPlaySessionWorldType::PlayInEditor;
	SessionParams.DestinationSlateViewport = LevelEditor.GetFirstActiveViewport();
	SessionParams.EditorPlaySettings = State->PlaySettings.Get();
	GUnrealEd->RequestPlaySession(SessionParams);
	GUnrealEd->StartQueuedPlaySessionRequest();

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForNetworkPIE(State, this));
	ADD_LATENT_AUTOMATION_COMMAND(FPrepareSwimming(State, this));
	ADD_LATENT_AUTOMATION_COMMAND(FMeasureSwimming(State, this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndNetworkPIE());
	return true;
}

#endif
