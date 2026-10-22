#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SamuraiDashAuthoringCommandlet.generated.h"

/** Migrates only Samurai Combat's Dash routing. -AuditOnly reads the saved structure; no automation tests. */
UCLASS()
class ENEMY_API USamuraiDashAuthoringCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	USamuraiDashAuthoringCommandlet();
	virtual int32 Main(const FString& Params) override;
};
