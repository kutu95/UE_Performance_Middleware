using UnrealBuildTool;

public class UnrealPerformer : ModuleRules
{
	public UnrealPerformer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AnimGraphRuntime",
			"ControlRig",
			"IKRig",
			"InputCore",
			"HTTP",
			"Json",
			"JsonUtilities",
			"WebSockets",
			"AudioMixer",
			"AudioCaptureCore",
			"ACERuntime",
			"ACECore",
			"DeveloperSettings",
			"MediaAssets",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"MetaHumanSDKRuntime",
			"Sockets",
			"Networking",
			"Media",
			"MediaUtils",
			"MediaPlate",
			"Slate",
			"SlateCore",
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",
				"TakeRecorder",
				"TakeRecorderSources",
				"TakesCore",
				"LevelSequence",
			});
		}
	}
}
