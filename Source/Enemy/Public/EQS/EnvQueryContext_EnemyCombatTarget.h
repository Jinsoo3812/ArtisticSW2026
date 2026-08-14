#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"

#include "EnvQueryContext_EnemyCombatTarget.generated.h"

/**
 * Supplies the combat target owned by ABaseAIController to an Environment Query.
 *
 * The context deliberately does not know the Blackboard key name. This keeps EQS
 * assets reusable when an AIController subclass changes its Blackboard contract.
 * Both Pawn-owned and AIController-owned queries are supported.
 */
UCLASS(meta = (DisplayName = "Enemy Combat Target"))
class ENEMY_API UEnvQueryContext_EnemyCombatTarget : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;

	/** Shared resolver used by the runtime context and lightweight contract tests. */
	static AActor* ResolveCombatTarget(const UObject* QueryOwner);
};
