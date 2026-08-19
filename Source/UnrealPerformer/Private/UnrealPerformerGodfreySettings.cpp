#include "UnrealPerformerGodfreySettings.h"

UUnrealPerformerGodfreySettings::UUnrealPerformerGodfreySettings()
{
	GodfreyApplyRootMotionActions = {
		TEXT("MHP_DuckUnderBanner_01"),
		TEXT("CrouchUnderBanner_01"),
	};
}

FName UUnrealPerformerGodfreySettings::GetCategoryName() const
{
	return FName(TEXT("Plugins"));
}
