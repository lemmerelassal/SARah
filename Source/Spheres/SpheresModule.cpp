#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "SlateBasics.h"
#include "SlateExtras.h"
#include "Framework/Docking/TabManager.h"
#include "Editor/LevelEditor/Public/LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

class FSpheresModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UE_LOG(LogTemp, Warning, TEXT("Spheres Module Startup!"));

        // Register the toolbar widget
        FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
        
        LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(GetToolbarExtender());
    }

    virtual void ShutdownModule() override
    {
        UE_LOG(LogTemp, Warning, TEXT("Spheres Module Shutdown!"));

        // Unregister any editor-specific resources here
    }

private:
    TSharedRef<FExtender> GetToolbarExtender()
    {
        TSharedRef<FExtender> Extender = MakeShared<FExtender>();

        Extender->AddToolBarExtension(
            "LevelEditor",
            EExtensionHook::Before,
            nullptr,
            FToolBarExtensionDelegate::CreateLambda([](FToolBarBuilder& Builder)
            {
                Builder.AddToolBarButton(
                    FUIAction(FExecuteAction::CreateLambda([](){ 
                        UE_LOG(LogTemp, Warning, TEXT("Toolbar Button 1 clicked"));
                    })),
                    NAME_None,
                    FText::FromString("My Toolbar Button 1")
                );
                
                Builder.AddToolBarButton(
                    FUIAction(FExecuteAction::CreateLambda([](){ 
                        UE_LOG(LogTemp, Warning, TEXT("Toolbar Button 2 clicked"));
                    })),
                    NAME_None,
                    FText::FromString("My Toolbar Button 2")
                );
                
                Builder.AddToolBarButton(
                    FUIAction(FExecuteAction::CreateLambda([](){ 
                        UE_LOG(LogTemp, Warning, TEXT("Toolbar Button 3 clicked"));
                    })),
                    NAME_None,
                    FText::FromString("My Toolbar Button 3")
                );
            })
        );

        return Extender;
    }
};

#endif // WITH_EDITOR

// No IMPLEMENT_MODULE in SpheresModule.cpp
