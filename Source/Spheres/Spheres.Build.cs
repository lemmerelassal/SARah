using UnrealBuildTool;

public class Spheres : ModuleRules
{
	public Spheres(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Slate and SlateCore are already included in the PublicDependencyModuleNames
		// No need to add them again in PrivateDependencyModuleNames

		if (Target.Type == TargetType.Editor)
		{
			// Add Editor dependencies if you need editor-specific features
			PrivateDependencyModuleNames.AddRange(new string[] { "LevelEditor", "EditorStyle", "MainFrame" });
		}

		// Public dependencies for the game (and editor)
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Json",
			"JsonUtilities",
			"Slate",       // Slate module for UI elements like SButton
			"SlateCore",   // Core functionality for Slate widgets
			"UMG",         // If you're using UMG, keep it
			"HTTP",
			"RHI",              // Required for GPU
		    "RHICore",          // Required for GPU
    		"RenderCore",       // Required for GPU
    		"Renderer",         // Required for GPU
			"ProceduralMeshComponent"
		});

		// Private dependencies for your custom modules (like Eigen)
		PrivateDependencyModuleNames.AddRange(new string[] { "Eigen", "DesktopPlatform", "Slate", "SlateCore" });
	}
}
