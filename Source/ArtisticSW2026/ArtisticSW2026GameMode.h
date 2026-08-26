// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MultiGameMode.h"
#include "ArtisticSW2026GameMode.generated.h"

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class AArtisticSW2026GameMode : public AMultiGameMode
{
	GENERATED_BODY()

public:
	
	/** Constructor */
	AArtisticSW2026GameMode();
};



