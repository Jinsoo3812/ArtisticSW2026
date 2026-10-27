#include "Audio/SWFoleyTypes.h"

namespace SW::Foley
{
	UE_DEFINE_GAMEPLAY_TAG(Walk, "Foley.Event.Walk");
	UE_DEFINE_GAMEPLAY_TAG(Run, "Foley.Event.Run");
	UE_DEFINE_GAMEPLAY_TAG(Jump, "Foley.Event.Jump");
	UE_DEFINE_GAMEPLAY_TAG(Land, "Foley.Event.Land");
	UE_DEFINE_GAMEPLAY_TAG(Scuff, "Foley.Event.Scuff");
	UE_DEFINE_GAMEPLAY_TAG(Handplant, "Foley.Event.Handplant");
	UE_DEFINE_GAMEPLAY_TAG(RunBackwards, "Foley.Event.RunBackwds");
	UE_DEFINE_GAMEPLAY_TAG(ScuffPivot, "Foley.Event.ScuffPivot");
	UE_DEFINE_GAMEPLAY_TAG(ScuffWall, "Foley.Event.ScuffWall");
	UE_DEFINE_GAMEPLAY_TAG(RunStrafe, "Foley.Event.RunStrafe");
	UE_DEFINE_GAMEPLAY_TAG(Tumble, "Foley.Event.Tumble");
	UE_DEFINE_GAMEPLAY_TAG(WalkBackwards, "Foley.Event.WalkBackwds");
}

// Preserve the imported bank tag; looping audio is excluded from the one-shot profile.
UE_DEFINE_GAMEPLAY_TAG_STATIC(SWFoleySlideLoopTag, "Foley.Event.Slide.Loop");
