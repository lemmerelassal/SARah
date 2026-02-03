// PDBStructureWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PDBStructureWidget.generated.h"

class UTreeView;
class UButton;
class APDBViewer;
class UPDBTreeNode;

UCLASS()
class SPHERES_API UPDBStructureWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    // Bind this to your TreeView in the Designer
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UTreeView* StructureTreeView;
    
    // Bind buttons (optional - use BindWidgetOptional if they might not exist)
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    UButton* Button_Load;
    
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    UButton* Button_Save;
    
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    UButton* Button_Clear;
    
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    UButton* Button_LoadSDF;

protected:
    UPROPERTY()
    APDBViewer* PDBViewerRef;

    // Called when structure loads
    UFUNCTION()
    void OnStructureLoaded();

    // Called when ligands are loaded (e.g. SDF files)
    UFUNCTION()
    void OnLigandsLoadedEvent();

    // TreeView delegate for getting children - must match UE signature
    void OnGetItemChildren(UObject* Item, TArray<UObject*>& OutChildren);
    
    // Auto-apply text styles
    void ApplyTextStyles();
    
    // Auto-apply button styles
    void ApplyButtonStyles();
    
    // Button click handlers
    UFUNCTION()
    void OnLoadClicked();
    
    UFUNCTION()
    void OnSaveClicked();
    
    UFUNCTION()
    void OnLoadSDFClicked();
    
    UFUNCTION()
    void OnClearClicked();
};