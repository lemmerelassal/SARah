// MenuBarWidget.cpp
#include "MenuBarWidget.h"
#include "PDBViewer.h"
#include "PDBStructureWidget.h"
#include "InteractionControlWidget.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Misc/FileHelper.h"

void UMenuBarWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // ── Find PDB Viewer ──────────────────────────────────────────────────────
    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APDBViewer::StaticClass(), FoundActors);
    if (FoundActors.Num() > 0)
        PDBViewerRef = Cast<APDBViewer>(FoundActors[0]);

    if (PDBViewerRef)
    {
        PDBViewerRef->OnLigandsLoaded.AddDynamic(this, &UMenuBarWidget::OnStructureLoaded);
        PDBViewerRef->OnInteractionsCalculated.AddDynamic(this, &UMenuBarWidget::OnInteractionsCalculated);

        if (PDBViewerRef->LigandMap.Num() > 0)
            OnStructureLoaded();
    }

    // ── Bar buttons ──────────────────────────────────────────────────────────
    if (FileMenuButton)
    {
        FileMenuButton->OnClicked.AddDynamic(this, &UMenuBarWidget::OnFileMenuClicked);
        FileMenuButton->SetToolTipText(FText::FromString(TEXT("File operations")));
    }
    if (ViewMenuButton)
    {
        ViewMenuButton->OnClicked.AddDynamic(this, &UMenuBarWidget::OnViewMenuClicked);
        ViewMenuButton->SetToolTipText(FText::FromString(TEXT("Show or hide panels")));
    }
    if (CalculateButton)
    {
        CalculateButton->OnClicked.AddDynamic(this, &UMenuBarWidget::OnCalculateClicked);
        CalculateButton->SetIsEnabled(false);
        CalculateButton->SetToolTipText(FText::FromString(TEXT("Calculate molecular interactions (load a structure first)")));
    }

    // ── File dropdown items ──────────────────────────────────────────────────
    if (MenuItem_Load)
    {
        MenuItem_Load->OnClicked.AddDynamic(this, &UMenuBarWidget::OnLoadClicked);
        MenuItem_Load->SetToolTipText(FText::FromString(TEXT("Open a PDB file")));
    }
    if (MenuItem_Save)
    {
        MenuItem_Save->OnClicked.AddDynamic(this, &UMenuBarWidget::OnSaveClicked);
        MenuItem_Save->SetToolTipText(FText::FromString(TEXT("Save current structure")));
    }
    if (MenuItem_Clear)
    {
        MenuItem_Clear->OnClicked.AddDynamic(this, &UMenuBarWidget::OnClearClicked);
        MenuItem_Clear->SetToolTipText(FText::FromString(TEXT("Remove the current structure from the viewport")));
    }
    if (MenuItem_LoadSDF)
    {
        MenuItem_LoadSDF->OnClicked.AddDynamic(this, &UMenuBarWidget::OnLoadSDFClicked);
        MenuItem_LoadSDF->SetToolTipText(FText::FromString(TEXT("Load a ligand from an SDF file")));
    }

    // ── View dropdown items ──────────────────────────────────────────────────
    if (MenuItem_ToggleTree)
    {
        MenuItem_ToggleTree->OnClicked.AddDynamic(this, &UMenuBarWidget::OnToggleTreeClicked);
        MenuItem_ToggleTree->SetToolTipText(FText::FromString(TEXT("Show or hide the structure tree")));
    }
    if (MenuItem_ToggleInteractions)
    {
        MenuItem_ToggleInteractions->OnClicked.AddDynamic(this, &UMenuBarWidget::OnToggleInteractionsClicked);
        MenuItem_ToggleInteractions->SetToolTipText(FText::FromString(TEXT("Show or hide the interactions panel")));
    }
    if (MenuItem_ToggleSurface)
    {
        MenuItem_ToggleSurface->OnClicked.AddDynamic(this, &UMenuBarWidget::OnToggleSurfaceClicked);
        MenuItem_ToggleSurface->SetToolTipText(FText::FromString(TEXT("Generate or toggle the molecular surface")));
        MenuItem_ToggleSurface->SetIsEnabled(false);
    }

    // ── Backdrop (dismisses open dropdowns on outside click) ─────────────────
    if (DropdownBackdrop)
    {
        DropdownBackdrop->OnClicked.AddDynamic(this, &UMenuBarWidget::OnDropdownBackdropClicked);
        DropdownBackdrop->SetVisibility(ESlateVisibility::Collapsed);
    }

    // ── Calculate options ────────────────────────────────────────────────────
    if (ProteinProteinCheckBox)
    {
        ProteinProteinCheckBox->SetIsChecked(true);
        ProteinProteinCheckBox->SetToolTipText(FText::FromString(TEXT("Include protein–protein interactions")));
    }
    if (ProteinLigandCheckBox)
    {
        ProteinLigandCheckBox->SetIsChecked(true);
        ProteinLigandCheckBox->SetToolTipText(FText::FromString(TEXT("Include protein–ligand interactions")));
    }

    // ── Collapse dropdowns on start ──────────────────────────────────────────
    if (FileDropdownPanel) FileDropdownPanel->SetVisibility(ESlateVisibility::Collapsed);
    if (ViewDropdownPanel) ViewDropdownPanel->SetVisibility(ESlateVisibility::Collapsed);
}

void UMenuBarWidget::NativeDestruct()
{
    if (PDBViewerRef)
    {
        PDBViewerRef->OnLigandsLoaded.RemoveDynamic(this, &UMenuBarWidget::OnStructureLoaded);
        PDBViewerRef->OnInteractionsCalculated.RemoveDynamic(this, &UMenuBarWidget::OnInteractionsCalculated);
    }
    Super::NativeDestruct();
}

// ── Helpers ──────────────────────────────────────────────────────────────────

void UMenuBarWidget::CloseAllDropdowns()
{
    bFileDropdownOpen = false;
    bViewDropdownOpen = false;
    if (FileDropdownPanel) FileDropdownPanel->SetVisibility(ESlateVisibility::Collapsed);
    if (ViewDropdownPanel) ViewDropdownPanel->SetVisibility(ESlateVisibility::Collapsed);
    if (DropdownBackdrop)  DropdownBackdrop->SetVisibility(ESlateVisibility::Collapsed);
}

void UMenuBarWidget::SetCalculateLabel(const FString& Label)
{
    if (CalculateButtonText)
        CalculateButtonText->SetText(FText::FromString(Label));
}

// ── Bar button handlers ───────────────────────────────────────────────────────

void UMenuBarWidget::OnFileMenuClicked()
{
    if (bFileDropdownOpen) { CloseAllDropdowns(); return; }
    CloseAllDropdowns();
    bFileDropdownOpen = true;
    if (FileDropdownPanel) FileDropdownPanel->SetVisibility(ESlateVisibility::Visible);
    if (DropdownBackdrop)  DropdownBackdrop->SetVisibility(ESlateVisibility::Visible);
}

void UMenuBarWidget::OnViewMenuClicked()
{
    if (bViewDropdownOpen) { CloseAllDropdowns(); return; }
    CloseAllDropdowns();
    bViewDropdownOpen = true;
    if (ViewDropdownPanel) ViewDropdownPanel->SetVisibility(ESlateVisibility::Visible);
    if (DropdownBackdrop)  DropdownBackdrop->SetVisibility(ESlateVisibility::Visible);
}

void UMenuBarWidget::OnDropdownBackdropClicked()
{
    CloseAllDropdowns();
}

// ── File menu ─────────────────────────────────────────────────────────────────

void UMenuBarWidget::OnLoadClicked()
{
    CloseAllDropdowns();
    if (PDBViewerRef) PDBViewerRef->OpenLoadDialog();
}

void UMenuBarWidget::OnSaveClicked()
{
    CloseAllDropdowns();
    if (PDBViewerRef) PDBViewerRef->OpenSaveDialog();
}

void UMenuBarWidget::OnClearClicked()
{
    CloseAllDropdowns();
    if (PDBViewerRef) PDBViewerRef->ClearCurrentStructure();
}

void UMenuBarWidget::OnLoadSDFClicked()
{
    CloseAllDropdowns();
    if (!PDBViewerRef) return;

    FString FilePath;
    if (PDBViewerRef->ShowSDFFileDialog(FilePath))
    {
        FString FileContent;
        if (FFileHelper::LoadFileToString(FileContent, *FilePath))
        {
            FString SelectedChain;
            if (PDBViewerRef->ShowChainSelectionDialog(SelectedChain))
                PDBViewerRef->ParseSDF(FileContent, SelectedChain);
        }
    }
}

// ── View menu ─────────────────────────────────────────────────────────────────

void UMenuBarWidget::FindPanels()
{
    if (!TreePanel)
    {
        TArray<UUserWidget*> Found;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GetWorld(), Found, UPDBStructureWidget::StaticClass(), false);
        if (Found.Num() > 0) TreePanel = Found[0];
    }
    if (!InteractionsPanel)
    {
        TArray<UUserWidget*> Found;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GetWorld(), Found, UInteractionControlWidget::StaticClass(), false);
        if (Found.Num() > 0) InteractionsPanel = Found[0];
    }
}

void UMenuBarWidget::OnToggleTreeClicked()
{
    CloseAllDropdowns();
    FindPanels();
    bTreeVisible = !bTreeVisible;
    if (TreePanel)
        TreePanel->SetVisibility(bTreeVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UMenuBarWidget::OnToggleInteractionsClicked()
{
    CloseAllDropdowns();
    FindPanels();
    bInteractionsVisible = !bInteractionsVisible;
    if (InteractionsPanel)
        InteractionsPanel->SetVisibility(bInteractionsVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UMenuBarWidget::OnToggleSurfaceClicked()
{
    CloseAllDropdowns();
    if (!PDBViewerRef || !bStructureReady) return;

    if (!bSurfaceVisible)
    {
        // Generate on first show (or after clear); subsequent toggles just show/hide
        if (!PDBViewerRef->bSurfaceGenerated)
            PDBViewerRef->GenerateMolecularSurface(PDBViewerRef->SurfaceProbeRadius, PDBViewerRef->SurfaceGridResolution);
        else
            PDBViewerRef->ToggleSurface(true);
        bSurfaceVisible = true;
    }
    else
    {
        PDBViewerRef->ToggleSurface(false);
        bSurfaceVisible = false;
    }
}

// ── Calculate ────────────────────────────────────────────────────────────────

void UMenuBarWidget::OnCalculateClicked()
{
    if (!PDBViewerRef || !bStructureReady || bIsCalculating) return;

    bIsCalculating = true;
    if (CalculateButton) CalculateButton->SetIsEnabled(false);
    SetCalculateLabel(TEXT("Calculating…"));

    const bool bPP = ProteinProteinCheckBox ? ProteinProteinCheckBox->IsChecked() : true;
    const bool bPL = ProteinLigandCheckBox  ? ProteinLigandCheckBox->IsChecked()  : true;
    PDBViewerRef->CalculateAllInteractions(bPP, bPL);
}

void UMenuBarWidget::OnStructureLoaded()
{
    bStructureReady = true;
    if (CalculateButton)
    {
        CalculateButton->SetIsEnabled(true);
        CalculateButton->SetToolTipText(FText::FromString(TEXT("Calculate molecular interactions")));
    }
    if (MenuItem_ToggleSurface)
        MenuItem_ToggleSurface->SetIsEnabled(true);
    SetCalculateLabel(TEXT("Calculate"));
}

void UMenuBarWidget::OnInteractionsCalculated()
{
    bIsCalculating = false;
    if (CalculateButton && bStructureReady)
        CalculateButton->SetIsEnabled(true);
    SetCalculateLabel(TEXT("Calculate"));
}
