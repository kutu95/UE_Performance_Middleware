using UnrealBuildTool;

public class UnrealPerformerEditor : ModuleRules
{
	public UnrealPerformerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"EditorSubsystem",
			"Blutility",
			"UMG",
			"UMGEditor",
			"Projects",
			"AssetRegistry",
			"UnrealPerformer",
			"ACERuntime",
			"ACECore",
		});
	}
}
