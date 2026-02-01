// InteractionControlWidget.cpp - Complete C++ Implementation
#include "InteractionControlWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ScrollBox.h"
#include "Components/VerticalBox.h"

UInteractionControlWidget::UInteractionControlWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bAutoCalculateOnLoad = false;
    bShowDetailedList = true;
}

void UInteractionControlWidget::NativeConstruct()
{
    Super::NativeConstruct();
    
    UE_LOG(LogTemp, Log, TEXT("InteractionControlWidget: Initializing..."));
    
    // Find PDB Viewer in the world
    PDBViewerRef = FindPDBViewer();
    
    if (!PDBViewerRef)
    {
        UE_LOG(LogTemp, Error, TEXT("InteractionControlWidget: PDB Viewer not found in world!"));
        UpdateStatusText(TEXT("ERROR: PDB Viewer not found"), FLinearColor::Red);
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("InteractionControlWidget: Found PDB Viewer"));
    
    // Bind to PDB Viewer events
    PDBViewerRef->OnLigandsLoaded.AddDynamic(this, &UInteractionControlWidget::OnStructureLoaded);
    PDBViewerRef->OnInteractionsCalculated.AddDynamic(this, &UInteractionControlWidget::OnInteractionsCalculated);
    
    // Bind button click
    if (CalculateButton)
    {
        CalculateButton->OnClicked.AddDynamic(this, &UInteractionControlWidget::OnCalculateButtonClicked);
        CalculateButton->SetIsEnabled(false);
    }
    
    // Bind checkboxes
    if (HBondCheckBox)
        HBondCheckBox->OnCheckStateChanged.AddDynamic(this, &UInteractionControlWidget::OnHBondCheckBoxChanged);
    
    if (SaltBridgeCheckBox)
        SaltBridgeCheckBox->OnCheckStateChanged.AddDynamic(this, &UInteractionControlWidget::OnSaltBridgeCheckBoxChanged);
    
    if (PiStackCheckBox)
        PiStackCheckBox->OnCheckStateChanged.AddDynamic(this, &UInteractionControlWidget::OnPiStackCheckBoxChanged);
    
    if (HydrophobicCheckBox)
        HydrophobicCheckBox->OnCheckStateChanged.AddDynamic(this, &UInteractionControlWidget::OnHydrophobicCheckBoxChanged);
    
    // Set default checkbox states
    if (HBondCheckBox) HBondCheckBox->SetIsChecked(true);
    if (SaltBridgeCheckBox) SaltBridgeCheckBox->SetIsChecked(true);
    if (PiStackCheckBox) PiStackCheckBox->SetIsChecked(true);
    if (HydrophobicCheckBox) HydrophobicCheckBox->SetIsChecked(true);
    if (ProteinProteinCheckBox) ProteinProteinCheckBox->SetIsChecked(true);
    if (ProteinLigandCheckBox) ProteinLigandCheckBox->SetIsChecked(true);
    
    // Check if structure is already loaded
    if (PDBViewerRef->LigandMap.Num() > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("InteractionControlWidget: Structure already loaded"));
        OnStructureLoaded();
    }
    else
    {
        UpdateStatusText(TEXT("Waiting for structure to load..."), FLinearColor::Yellow);
    }
}

void UInteractionControlWidget::NativeDestruct()
{
    // Unbind all events to prevent crashes
    if (PDBViewerRef)
    {
        PDBViewerRef->OnLigandsLoaded.RemoveAll(this);
        PDBViewerRef->OnInteractionsCalculated.RemoveAll(this);
    }
    
    Super::NativeDestruct();
}

APDBViewer* UInteractionControlWidget::FindPDBViewer()
{
    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APDBViewer::StaticClass(), FoundActors);
    
    if (FoundActors.Num() > 0)
    {
        return Cast<APDBViewer>(FoundActors[0]);
    }
    
    return nullptr;
}

void UInteractionControlWidget::SetPDBViewer(APDBViewer* Viewer)
{
    if (Viewer)
    {
        // Unbind from old viewer if exists
        if (PDBViewerRef)
        {
            PDBViewerRef->OnLigandsLoaded.RemoveAll(this);
            PDBViewerRef->OnInteractionsCalculated.RemoveAll(this);
        }
        
        // Set new viewer
        PDBViewerRef = Viewer;
        
        // Bind to new viewer
        PDBViewerRef->OnLigandsLoaded.AddDynamic(this, &UInteractionControlWidget::OnStructureLoaded);
        PDBViewerRef->OnInteractionsCalculated.AddDynamic(this, &UInteractionControlWidget::OnInteractionsCalculated);
        
        UE_LOG(LogTemp, Log, TEXT("InteractionControlWidget: PDB Viewer reference updated"));
    }
}

void UInteractionControlWidget::OnStructureLoaded()
{
    UE_LOG(LogTemp, Log, TEXT("InteractionControlWidget: Structure loaded event received"));
    
    bStructureReady = true;
    
    UpdateStatusText(TEXT("Structure loaded - Ready to calculate"), FLinearColor::Green);
    
    if (CalculateButton)
    {
        CalculateButton->SetIsEnabled(true);
    }
    
    // Auto-calculate if enabled
    if (bAutoCalculateOnLoad && PDBViewerRef)
    {
        UE_LOG(LogTemp, Log, TEXT("InteractionControlWidget: Auto-calculating interactions"));
        TriggerCalculation();
    }
}

void UInteractionControlWidget::OnInteractionsCalculated()
{
    UE_LOG(LogTemp, Log, TEXT("InteractionControlWidget: Interactions calculated event received"));
    
    bIsCalculating = false;
    
    // Re-enable UI
    SetUIEnabled(true);
    
    // Update all displays
    UpdateAllCounts();
    UpdateInteractionList();
    
    int32 TotalCount = PDBViewerRef ? PDBViewerRef->GetAllInteractions().Num() : 0;
    UpdateStatusText(FString::Printf(TEXT("Found %d total interactions"), TotalCount), FLinearColor::Green);
}

void UInteractionControlWidget::OnCalculateButtonClicked()
{
    UE_LOG(LogTemp, Log, TEXT("InteractionControlWidget: Calculate button clicked"));
    TriggerCalculation();
}

void UInteractionControlWidget::TriggerCalculation()
{
    if (!PDBViewerRef)
    {
        UE_LOG(LogTemp, Warning, TEXT("InteractionControlWidget: Cannot calculate - no PDB Viewer reference"));
        UpdateStatusText(TEXT("ERROR: No PDB Viewer"), FLinearColor::Red);
        return;
    }
    
    if (!bStructureReady)
    {
        UE_LOG(LogTemp, Warning, TEXT("InteractionControlWidget: Cannot calculate - structure not ready"));
        UpdateStatusText(TEXT("Waiting for structure..."), FLinearColor::Yellow);
        return;
    }
    
    if (bIsCalculating)
    {
        UE_LOG(LogTemp, Warning, TEXT("InteractionControlWidget: Already calculating"));
        return;
    }
    
    bIsCalculating = true;
    
    // Disable UI while calculating
    SetUIEnabled(false);
    UpdateStatusText(TEXT("Calculating interactions..."), FLinearColor::Yellow);
    
    // Get settings from checkboxes
    bool bProteinProtein = ProteinProteinCheckBox ? ProteinProteinCheckBox->IsChecked() : true;
    bool bProteinLigand = ProteinLigandCheckBox ? ProteinLigandCheckBox->IsChecked() : true;
    
    UE_LOG(LogTemp, Log, TEXT("InteractionControlWidget: Starting calculation (PP=%s, PL=%s)"),
           bProteinProtein ? TEXT("true") : TEXT("false"),
           bProteinLigand ? TEXT("true") : TEXT("false"));
    
    // Trigger calculation
    PDBViewerRef->CalculateAllInteractions(bProteinProtein, bProteinLigand);
}

void UInteractionControlWidget::OnHBondCheckBoxChanged(bool bIsChecked)
{
    if (PDBViewerRef)
    {
        PDBViewerRef->ToggleInteractionType(EInteractionType::HydrogenBond, bIsChecked);
        UE_LOG(LogTemp, Log, TEXT("H-Bonds visibility: %s"), bIsChecked ? TEXT("ON") : TEXT("OFF"));
    }
}

void UInteractionControlWidget::OnSaltBridgeCheckBoxChanged(bool bIsChecked)
{
    if (PDBViewerRef)
    {
        PDBViewerRef->ToggleInteractionType(EInteractionType::SaltBridge, bIsChecked);
        UE_LOG(LogTemp, Log, TEXT("Salt Bridges visibility: %s"), bIsChecked ? TEXT("ON") : TEXT("OFF"));
    }
}

void UInteractionControlWidget::OnPiStackCheckBoxChanged(bool bIsChecked)
{
    if (PDBViewerRef)
    {
        PDBViewerRef->ToggleInteractionType(EInteractionType::PiStacking, bIsChecked);
        UE_LOG(LogTemp, Log, TEXT("Pi-Stacking visibility: %s"), bIsChecked ? TEXT("ON") : TEXT("OFF"));
    }
}

void UInteractionControlWidget::OnHydrophobicCheckBoxChanged(bool bIsChecked)
{
    if (PDBViewerRef)
    {
        PDBViewerRef->ToggleInteractionType(EInteractionType::Hydrophobic, bIsChecked);
        UE_LOG(LogTemp, Log, TEXT("Hydrophobic visibility: %s"), bIsChecked ? TEXT("ON") : TEXT("OFF"));
    }
}

void UInteractionControlWidget::UpdateAllCounts()
{
    if (!PDBViewerRef)
        return;
    
    // Get counts
    int32 HBondCount = GetInteractionCountByType(EInteractionType::HydrogenBond);
    int32 SaltBridgeCount = GetInteractionCountByType(EInteractionType::SaltBridge);
    int32 PiStackCount = GetInteractionCountByType(EInteractionType::PiStacking);
    int32 HydrophobicCount = GetInteractionCountByType(EInteractionType::Hydrophobic);
    int32 TotalCount = PDBViewerRef->GetAllInteractions().Num();
    
    // Update UI
    if (HBondCountText)
        HBondCountText->SetText(FText::FromString(FString::Printf(TEXT("H-Bonds: %d"), HBondCount)));
    
    if (SaltBridgeCountText)
        SaltBridgeCountText->SetText(FText::FromString(FString::Printf(TEXT("Salt Bridges: %d"), SaltBridgeCount)));
    
    if (PiStackCountText)
        PiStackCountText->SetText(FText::FromString(FString::Printf(TEXT("Pi-Stacking: %d"), PiStackCount)));
    
    if (HydrophobicCountText)
        HydrophobicCountText->SetText(FText::FromString(FString::Printf(TEXT("Hydrophobic: %d"), HydrophobicCount)));
    
    if (TotalCountText)
        TotalCountText->SetText(FText::FromString(FString::Printf(TEXT("Total: %d"), TotalCount)));
    
    UE_LOG(LogTemp, Log, TEXT("Updated counts - Total: %d, H-Bonds: %d, Salt: %d, Pi: %d, Hydro: %d"),
           TotalCount, HBondCount, SaltBridgeCount, PiStackCount, HydrophobicCount);
}

void UInteractionControlWidget::UpdateStatusText(const FString& Status, const FLinearColor& Color)
{
    if (StatusText)
    {
        StatusText->SetText(FText::FromString(Status));
        StatusText->SetColorAndOpacity(FSlateColor(Color));
    }
    
    UE_LOG(LogTemp, Log, TEXT("Status: %s"), *Status);
}

void UInteractionControlWidget::UpdateInteractionList()
{
    if (!bShowDetailedList || !InteractionListBox || !PDBViewerRef)
        return;
    
    // Clear existing entries
    InteractionListBox->ClearChildren();
    
    // Populate with new data
    PopulateDetailedList();
}

void UInteractionControlWidget::PopulateDetailedList()
{
    if (!InteractionListBox || !PDBViewerRef)
        return;
    
    TArray<FMolecularInteraction> Interactions = PDBViewerRef->GetAllInteractions();
    
    // Limit to first 100 for performance
    int32 MaxDisplay = FMath::Min(Interactions.Num(), 100);
    
    for (int32 i = 0; i < MaxDisplay; ++i)
    {
        const FMolecularInteraction& Interaction = Interactions[i];
        
        // Create text block for this interaction
        UTextBlock* EntryText = NewObject<UTextBlock>(InteractionListBox);
        
        // Format the interaction info
        FString TypeStr;
        switch (Interaction.Type)
        {
            case EInteractionType::HydrogenBond: TypeStr = TEXT("H-Bond"); break;
            case EInteractionType::SaltBridge: TypeStr = TEXT("Salt Bridge"); break;
            case EInteractionType::PiStacking: TypeStr = TEXT("Pi-Stack"); break;
            case EInteractionType::Hydrophobic: TypeStr = TEXT("Hydrophobic"); break;
            default: TypeStr = TEXT("Other"); break;
        }
        
        FString EntryStr = FString::Printf(TEXT("%s: %s(%s) <-> %s(%s) [%.2f Å]"),
            *TypeStr,
            *Interaction.Residue1, *Interaction.Atom1,
            *Interaction.Residue2, *Interaction.Atom2,
            Interaction.Distance);
        
        EntryText->SetText(FText::FromString(EntryStr));
        
        // Add to scroll box
        InteractionListBox->AddChild(EntryText);
    }
    
    if (Interactions.Num() > MaxDisplay)
    {
        UTextBlock* MoreText = NewObject<UTextBlock>(InteractionListBox);
        MoreText->SetText(FText::FromString(FString::Printf(TEXT("... and %d more"), Interactions.Num() - MaxDisplay)));
        InteractionListBox->AddChild(MoreText);
    }
}

void UInteractionControlWidget::SetUIEnabled(bool bEnabled)
{
    if (CalculateButton)
        CalculateButton->SetIsEnabled(bEnabled);
    
    if (HBondCheckBox)
        HBondCheckBox->SetIsEnabled(bEnabled);
    
    if (SaltBridgeCheckBox)
        SaltBridgeCheckBox->SetIsEnabled(bEnabled);
    
    if (PiStackCheckBox)
        PiStackCheckBox->SetIsEnabled(bEnabled);
    
    if (HydrophobicCheckBox)
        HydrophobicCheckBox->SetIsEnabled(bEnabled);
    
    if (ProteinProteinCheckBox)
        ProteinProteinCheckBox->SetIsEnabled(bEnabled);
    
    if (ProteinLigandCheckBox)
        ProteinLigandCheckBox->SetIsEnabled(bEnabled);
}

int32 UInteractionControlWidget::GetInteractionCountByType(EInteractionType Type) const
{
    if (!PDBViewerRef)
        return 0;
    
    return PDBViewerRef->GetInteractionsByType(Type).Num();
}

void UInteractionControlWidget::RefreshDisplay()
{
    UpdateAllCounts();
    UpdateInteractionList();
}
