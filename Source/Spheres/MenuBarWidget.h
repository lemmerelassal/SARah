// MenuBarWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MenuBarWidget.generated.h"

class UButton;
class UCheckBox;
class UWidget;
class UTextBlock;
class APDBViewer;

UCLASS()
class SPHERES_API UMenuBarWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    // ── Bar buttons (required) ───────────────────────────────────────────────
    UPROPERTY(meta = (BindWidget))
    UButton* FileMenuButton;

    UPROPERTY(meta = (BindWidget))
    UButton* ViewMenuButton;

    UPROPERTY(meta = (BindWidget))
    UButton* CalculateButton;

    // ── File dropdown ────────────────────────────────────────────────────────
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

    // ── View dropdown ────────────────────────────────────────────────────────
    UPROPERTY(meta = (BindWidgetOptional))
    UWidget* ViewDropdownPanel;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* MenuItem_ToggleTree;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* MenuItem_ToggleInteractions;

    UPROPERTY(meta = (BindWidgetOptional))
    UButton* MenuItem_ToggleSurface;

    // ── Calculate options ────────────────────────────────────────────────────
    UPROPERTY(meta = (BindWidgetOptional))
    UCheckBox* ProteinProteinCheckBox;

    UPROPERTY(meta = (BindWidgetOptional))
    UCheckBox* ProteinLigandCheckBox;

    // Optional text block inside CalculateButton to show "Calculating…"
    UPROPERTY(meta = (BindWidgetOptional))
    UTextBlock* CalculateButtonText;

    // ── Backdrop (full-screen invisible button behind open dropdowns) ────────
    UPROPERTY(meta = (BindWidgetOptional))
    UButton* DropdownBackdrop;

    // Populated automatically on first toggle — no Blueprint wiring needed
    UPROPERTY()
    UUserWidget* TreePanel;

    UPROPERTY()
    UUserWidget* InteractionsPanel;

protected:
    UPROPERTY()
    APDBViewer* PDBViewerRef;

    bool bFileDropdownOpen       = false;
    bool bViewDropdownOpen       = false;
    bool bStructureReady         = false;
    bool bIsCalculating          = false;
    bool bTreeVisible            = true;
    bool bInteractionsVisible    = true;
    bool bSurfaceVisible         = false;

    UFUNCTION() void OnFileMenuClicked();
    UFUNCTION() void OnViewMenuClicked();
    UFUNCTION() void OnDropdownBackdropClicked();

    UFUNCTION() void OnLoadClicked();
    UFUNCTION() void OnSaveClicked();
    UFUNCTION() void OnClearClicked();
    UFUNCTION() void OnLoadSDFClicked();

    UFUNCTION() void OnToggleTreeClicked();
    UFUNCTION() void OnToggleInteractionsClicked();
    UFUNCTION() void OnToggleSurfaceClicked();

    UFUNCTION() void OnCalculateClicked();
    UFUNCTION() void OnStructureLoaded();
    UFUNCTION() void OnInteractionsCalculated();

    void CloseAllDropdowns();
    void SetCalculateLabel(const FString& Label);
    void FindPanels();
};
