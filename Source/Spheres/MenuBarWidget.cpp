// MenuBarWidget.cpp
#include "MenuBarWidget.h"
#include "PDBViewer.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"

void UMenuBarWidget::NativeConstruct()
{
    Super::NativeConstruct();

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

    if (FileMenuButton)
        FileMenuButton->OnClicked.AddDynamic(this, &UMenuBarWidget::OnFileMenuClicked);

    if (MenuItem_Load)
        MenuItem_Load->OnClicked.AddDynamic(this, &UMenuBarWidget::OnLoadClicked);
    if (MenuItem_Save)
        MenuItem_Save->OnClicked.AddDynamic(this, &UMenuBarWidget::OnSaveClicked);
    if (MenuItem_Clear)
        MenuItem_Clear->OnClicked.AddDynamic(this, &UMenuBarWidget::OnClearClicked);
    if (MenuItem_LoadSDF)
        MenuItem_LoadSDF->OnClicked.AddDynamic(this, &UMenuBarWidget::OnLoadSDFClicked);

    if (CalculateButton)
    {
        CalculateButton->OnClicked.AddDynamic(this, &UMenuBarWidget::OnCalculateClicked);
        CalculateButton->SetIsEnabled(false);
    }

    if (ProteinProteinCheckBox) ProteinProteinCheckBox->SetIsChecked(true);
    if (ProteinLigandCheckBox)  ProteinLigandCheckBox->SetIsChecked(true);

    if (FileDropdownPanel)
        FileDropdownPanel->SetVisibility(ESlateVisibility::Collapsed);
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

void UMenuBarWidget::CloseFileDropdown()
{
    bFileDropdownOpen = false;
    if (FileDropdownPanel)
        FileDropdownPanel->SetVisibility(ESlateVisibility::Collapsed);
}

void UMenuBarWidget::OnFileMenuClicked()
{
    if (!FileDropdownPanel) return;
    bFileDropdownOpen = !bFileDropdownOpen;
    FileDropdownPanel->SetVisibility(bFileDropdownOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UMenuBarWidget::OnLoadClicked()
{
    CloseFileDropdown();
    if (PDBViewerRef) PDBViewerRef->OpenLoadDialog();
}

void UMenuBarWidget::OnSaveClicked()
{
    CloseFileDropdown();
    if (PDBViewerRef) PDBViewerRef->OpenSaveDialog();
}

void UMenuBarWidget::OnClearClicked()
{
    CloseFileDropdown();
    if (PDBViewerRef) PDBViewerRef->ClearCurrentStructure();
}

void UMenuBarWidget::OnLoadSDFClicked()
{
    CloseFileDropdown();
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

void UMenuBarWidget::OnCalculateClicked()
{
    if (!PDBViewerRef || !bStructureReady || bIsCalculating) return;

    bIsCalculating = true;
    if (CalculateButton) CalculateButton->SetIsEnabled(false);

    const bool bPP = ProteinProteinCheckBox ? ProteinProteinCheckBox->IsChecked() : true;
    const bool bPL = ProteinLigandCheckBox  ? ProteinLigandCheckBox->IsChecked()  : true;
    PDBViewerRef->CalculateAllInteractions(bPP, bPL);
}

void UMenuBarWidget::OnStructureLoaded()
{
    bStructureReady = true;
    if (CalculateButton) CalculateButton->SetIsEnabled(true);
}

void UMenuBarWidget::OnInteractionsCalculated()
{
    bIsCalculating = false;
    if (CalculateButton && bStructureReady)
        CalculateButton->SetIsEnabled(true);
}
