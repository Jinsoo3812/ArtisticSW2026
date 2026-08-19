// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

// Custom Trace Channel
static constexpr ECollisionChannel ECC_Interactable = ECC_GameTraceChannel1;
static constexpr ECollisionChannel ECC_WeaponAim = ECC_GameTraceChannel4;
static constexpr ECollisionChannel ECC_ShipDamage = ECC_GameTraceChannel5;
static constexpr ECollisionChannel ECC_EnemyShipObstacle = ECC_GameTraceChannel6;
static constexpr ECollisionChannel ECC_FootPlacement = ECC_GameTraceChannel7;
