#include "Room/SWVoyageResetParticipant.h"

ESWVoyagePolicy ISWVoyageResetParticipant::GetVoyagePolicy_Implementation() const { return ESWVoyagePolicy::Unsupported; }
ESWVoyageRestoreStage ISWVoyageResetParticipant::GetVoyageRestoreStage_Implementation() const { return ESWVoyageRestoreStage::Readiness; }
FName ISWVoyageResetParticipant::GetVoyageParticipantId_Implementation() const { return NAME_None; }
TArray<FName> ISWVoyageResetParticipant::GetVoyageAfterParticipants_Implementation() const { return {}; }
ESWVoyageStepResult ISWVoyageResetParticipant::PrepareVoyageReset_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	OutError = TEXT("ParticipantNotImplemented");
	return ESWVoyageStepResult::Failed;
}
ESWVoyageStepResult ISWVoyageResetParticipant::ResetVoyageTransientState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	OutError = TEXT("ParticipantNotImplemented");
	return ESWVoyageStepResult::Failed;
}
ESWVoyageStepResult ISWVoyageResetParticipant::RestoreVoyageState_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	OutError = TEXT("ParticipantNotImplemented");
	return ESWVoyageStepResult::Failed;
}
ESWVoyageStepResult ISWVoyageResetParticipant::IsVoyageReady_Implementation(const FSWVoyageResetContext& Context, FString& OutError)
{
	OutError = TEXT("ParticipantNotImplemented");
	return ESWVoyageStepResult::Failed;
}
void ISWVoyageResetParticipant::ResumeVoyage_Implementation(const FSWVoyageResetContext& Context) {}
void ISWVoyageResetParticipant::CancelVoyagePreparation_Implementation(const FSWVoyageResetContext& Context) {}
