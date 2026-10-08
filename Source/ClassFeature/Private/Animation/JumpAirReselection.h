#pragma once

#include "Animation/LocomotionAnimStateComponent.h"

class UAnimationAsset;

namespace JumpAirReselection
{
    inline EMovementDirection ResolveDirection(const FVector2D& LocalDirection, EMovementDirection SelectedDirection)
    {
        if (LocalDirection.IsNearlyZero()) return SelectedDirection;
        const float Angle = FMath::RadiansToDegrees(FMath::Atan2(LocalDirection.X, LocalDirection.Y));
        // Enum sectors run counter-clockwise from Forward. Keep the selected
        // sector through 10 degrees of jitter around its 22.5-degree boundary.
        const float SelectedAngle = -45.0f * static_cast<uint8>(SelectedDirection);
        if (FMath::Abs(FMath::FindDeltaAngleDegrees(SelectedAngle, Angle)) <= 32.5f)
            return SelectedDirection;
        const int32 Sector = FMath::RoundToInt(-Angle / 45.0f);
        return static_cast<EMovementDirection>((Sector % 8 + 8) % 8);
    }

    struct FDirectionCandidate
    {
        EMovementDirection Direction = EMovementDirection::Forward;
        float StableTime = 0.0f;
        bool bValid = false;

        void Reset() { bValid = false; StableTime = 0.0f; }

        bool Update(EMovementDirection Requested, EMovementDirection Selected, bool bSupported,
            float DeltaTime, float ConfirmationTime)
        {
            if (!bSupported || Requested == Selected)
            {
                Reset();
                return false;
            }
            if (!bValid || Requested != Direction)
            {
                Direction = Requested;
                StableTime = 0.0f;
                bValid = true;
            }
            StableTime += FMath::Max(0.0f, DeltaTime);
            return StableTime >= FMath::Max(0.0f, ConfirmationTime);
        }
    };

    inline bool CanReselect(float Elapsed, float PlayableDuration, float LastReselectElapsed, float BlendDuration,
        float MinBlendProgress = 1.0f)
    {
        // Track actual player blends, including the initial takeoff blend.
        // Waiting for completion avoids chaining partially mixed air poses.
        const float Wait = LastReselectElapsed < 0.0f ? 0.05f :
            FMath::Max(0.08f, BlendDuration * FMath::Clamp(MinBlendProgress, 0.7f, 1.0f));
        const float Age = LastReselectElapsed < 0.0f ? Elapsed : Elapsed - LastReselectElapsed;
        return PlayableDuration > 0.05f && Elapsed < PlayableDuration * 0.85f && Age >= Wait;
    }

    inline float ResolveBlendTime(EMovementDirection PreviousDirection, EMovementDirection NextDirection,
        float AuthoredBlendTime, float WideTurnBlendTime)
    {
        const float Angle = FMath::Abs(FMath::FindDeltaAngleDegrees(
            -45.0f * static_cast<uint8>(PreviousDirection), -45.0f * static_cast<uint8>(NextDirection)));
        const float BaseBlend = AuthoredBlendTime > 0.0f ? AuthoredBlendTime : 0.15f;
        // Opposite poses need more time than an adjacent F -> L/R transition.
        const float MinBlend = FMath::Lerp(0.15f, FMath::Max(0.15f, WideTurnBlendTime),
            FMath::Clamp((Angle - 90.0f) / 90.0f, 0.0f, 1.0f));
        return FMath::Max(BaseBlend, MinBlend);
    }

    struct FPlaybackSelection
    {
        bool bKeepPlayback = true;
        float StartTime = 0.0f;
        float PlayableDuration = 0.0f;
    };

    inline FPlaybackSelection SelectPlayback(const UAnimationAsset* PreviousAsset, const UAnimationAsset* NextAsset,
        float AuthoredStartTime, float Elapsed, float AssetLength)
    {
        FPlaybackSelection Result;
        // F/FL/FR (or B/BL/BR) may intentionally resolve to the same clip.
        // Changing a direction row is not a command to restart that player.
        if (!NextAsset || NextAsset == PreviousAsset) return Result;
        const float EntryTime = FMath::Clamp(AuthoredStartTime, 0.0f, FMath::Max(0.0f, AssetLength));
        Result.StartTime = EntryTime + FMath::Max(0.0f, Elapsed);
        Result.PlayableDuration = AssetLength - EntryTime;
        // Do not jump to a shorter clip's last frame just to change direction.
        Result.bKeepPlayback = Result.StartTime >= AssetLength - 0.05f;
        return Result;
    }
}
