#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimCurveTypes.h"

/**
 * Baked viseme / jaw / tongue curves on performance AnimSequences (ARKit + MetaHuman
 * CTRL_expressions_*). GreetingWelcome_03 and other solved takes still contain speech
 * mouth motion. Body overlays must not drive those — ACE owns lips while speaking;
 * when silent they read as errant lipsync.
 *
 * Smile / dimple / corner-pull expression curves are kept.
 */
inline bool GodfreyIsBakedSpeechLipCurve(const FName CurveName)
{
	const FString Name = CurveName.ToString();
	if (Name.Equals(TEXT("CTRL_expressions_mouthCornerPullL"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("CTRL_expressions_mouthCornerPullR"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("CTRL_expressions_mouthCornerDepressL"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("CTRL_expressions_mouthCornerDepressR"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("CTRL_expressions_mouthDimpleL"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("CTRL_expressions_mouthDimpleR"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("CTRL_expressions_mouthUpperLipRaiseL"), ESearchCase::IgnoreCase)
		|| Name.Equals(TEXT("CTRL_expressions_mouthUpperLipRaiseR"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	FString Rest = Name;
	if (Rest.StartsWith(TEXT("CTRL_expressions_"), ESearchCase::IgnoreCase))
	{
		Rest.RightChopInline(17);
	}
	else if (Rest.StartsWith(TEXT("CTRL_C_"), ESearchCase::IgnoreCase))
	{
		Rest.RightChopInline(7);
	}

	static const TCHAR* Prefixes[] = {
		TEXT("jaw"),
		TEXT("tongue"),
		TEXT("teeth"),
		TEXT("mouthClose"),
		TEXT("mouthFunnel"),
		TEXT("mouthPucker"),
		TEXT("mouthStretch"),
		TEXT("mouthPress"),
		TEXT("mouthShrug"),
		TEXT("mouthRoll"),
		TEXT("mouthUpperUp"),
		TEXT("mouthLowerDown"),
		TEXT("mouthLeft"),
		TEXT("mouthRight"),
		TEXT("mouthSmile"),
		TEXT("mouthFrown"),
		TEXT("mouthWiden"),
	};
	for (const TCHAR* Prefix : Prefixes)
	{
		if (Rest.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

inline void GodfreySuppressBakedSpeechLipCurves(UAnimInstance* Anim, bool* bInOutLoggedNonZero = nullptr)
{
	if (!Anim)
	{
		return;
	}

	auto SuppressList = [Anim, bInOutLoggedNonZero](EAnimCurveType Type)
	{
		TMap<FName, float> Curves;
		Anim->AppendAnimationCurveList(Type, Curves);
		for (const TPair<FName, float>& Pair : Curves)
		{
			if (!GodfreyIsBakedSpeechLipCurve(Pair.Key) || FMath::Abs(Pair.Value) <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			Anim->OverrideCurveValue(Pair.Key, 0.f);
			if (bInOutLoggedNonZero && !*bInOutLoggedNonZero)
			{
				*bInOutLoggedNonZero = true;
			}
		}
	};

	SuppressList(EAnimCurveType::AttributeCurve);
	SuppressList(EAnimCurveType::MorphTargetCurve);
	SuppressList(EAnimCurveType::MaterialCurve);
}
