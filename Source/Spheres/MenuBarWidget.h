// MenuBarWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MenuBarWidget.generated.h"

class UButton;
class UCheckBox;
class UWidget;
class APDBViewer;

UCLASS()
class SPHERES_API UMenuBarWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    UPROPERTY(meta = (BindWidget))
    UButton* FileMenuButton;

    UPROPERTY(meta = (BindWidget))
    UButton* CalculateButton;

    UPROPERTY(meta = (BindWidgetOptional))
    UWidget* FileDropdownPanel;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* MenuItem_Load;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* MenuItem_Save;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* MenuItem_Clear;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* MenuItem_LoadSDF;

    UPROPERTY(meta = (BindWidgetOptional))
    UCheckBox* ProteinProteinCheckBox;

    UPROPERTY(meta = (BindWidgetOptional))
    UCheckBox* ProteinLigandCheckBox;

protected:
    UPROPERTY()
    APDBViewer* PDBViewerRef;

    bool bFileDropdownOpen = false;
    bool bStructureReady = false;
    bool bIsCalculating = false;

    UFUNCTION()
    void OnFileMenuClicked();

    UFUNCTION()
    void OnLoadClicked();

    UFUNCTION()
    void OnSaveClicked();

    UFUNCTION()
    void OnClearClicked();

    UFUNCTION()
    void OnLoadSDFClicked();

    UFUNCTION()
    void OnCalculateClicked();

    UFUNCTION()
    void OnStructureLoaded();

    UFUNCTION()
    void OnInteractionsCalculated();

    void CloseFileDropdown();
};
