// InteractionControlWidget.cpp
#include "InteractionControlWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Components/TextBlock.h"
#include "Components/CheckBox.h"
#include "Components/ScrollBox.h"

UInteractionControlWidget::UInteractionControlWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bShowDetailedList = true;
}

void UInteractionControlWidget::NativeConstruct()
{
    Super::NativeConstruct();

    PDBViewerRef = FindPDBViewer();

    if (!PDBViewerRef)
    {
        UpdateStatusText(TEXT("ERROR: PDB Viewer not found"));
        return;
    }

    PDBViewerRef->OnLigandsLoaded.AddDynamic(this, &UInteractionControlWidget::OnStructureLoaded);
    PDBViewerRef->OnInteractionsCalculated.AddDynamic(this, &UInteractionControlWidget::OnInteractionsCalculated);

    if (HBondCheckBox)
        HBondCheckBox->OnCheckStateChanged.AddDynamic(this, &UInteractionControlWidget::OnHBondCheckBoxChanged);
    if (SaltBridgeCheckBox)
        SaltBridgeCheckBox->OnCheckStateChanged.AddDynamic(this, &UInteractionControlWidget::OnSaltBridgeCheckBoxChanged);
    if (PiStackCheckBox)
        PiStackCheckBox->OnCheckStateChanged.AddDynamic(this, &UInteractionControlWidget::OnPiStackCheckBoxChanged);
    if (HydrophobicCheckBox)
        HydrophobicCheckBox->OnCheckStateChanged.AddDynamic(this, &UInteractionControlWidget::OnHydrophobicCheckBoxChanged);

    if (HBondCheckBox)       HBondCheckBox->SetIsChecked(true);
    if (SaltBridgeCheckBox)  SaltBridgeCheckBox->SetIsChecked(true);
    if (PiStackCheckBox)     PiStackCheckBox->SetIsChecked(true);
    if (HydrophobicCheckBox) HydrophobicCheckBox->SetIsChecked(true);

    if (PDBViewerRef->LigandMap.Num() > 0)
        OnStructureLoaded();
    else
        UpdateStatusText(TEXT("Waiting for structure to load..."));
}

void UInteractionControlWidget::NativeDestruct()
{
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
    return FoundActors.Num() > 0 ? Cast<APDBViewer>(FoundActors[0]) : nullptr;
}

void UInteractionControlWidget::SetPDBViewer(APDBViewer* Viewer)
{
    if (!Viewer) return;

    if (PDBViewerRef)
    {
        PDBViewerRef->OnLigandsLoaded.RemoveAll(this);
        PDBViewerRef->OnInteractionsCalculated.RemoveAll(this);
    }

    PDBViewerRef = Viewer;
    PDBViewerRef->OnLigandsLoaded.AddDynamic(this, &UInteractionControlWidget::OnStructureLoaded);
    PDBViewerRef->OnInteractionsCalculated.AddDynamic(this, &UInteractionControlWidget::OnInteractionsCalculated);
}

void UInteractionControlWidget::OnStructureLoaded()
{
    UpdateStatusText(TEXT("Structure loaded - Ready to calculate"));
}

void UInteractionControlWidget::OnInteractionsCalculated()
{
    SetCheckBoxesEnabled(true);
    UpdateAllCounts();
    UpdateInteractionList();

    int32 TotalCount = PDBViewerRef ? PDBViewerRef->GetAllInteractions().Num() : 0;
    UpdateStatusText(FString::Printf(TEXT("Found %d total interactions"), TotalCount));
}

void UInteractionControlWidget::OnHBondCheckBoxChanged(bool bIsChecked)
{
    if (PDBViewerRef)
        PDBViewerRef->ToggleInteractionType(EInteractionType::HydrogenBond, bIsChecked);
}

void UInteractionControlWidget::OnSaltBridgeCheckBoxChanged(bool bIsChecked)
{
    if (PDBViewerRef)
        PDBViewerRef->ToggleInteractionType(EInteractionType::SaltBridge, bIsChecked);
}

void UInteractionControlWidget::OnPiStackCheckBoxChanged(bool bIsChecked)
{
    if (PDBViewerRef)
        PDBViewerRef->ToggleInteractionType(EInteractionType::PiStacking, bIsChecked);
}

void UInteractionControlWidget::OnHydrophobicCheckBoxChanged(bool bIsChecked)
{
    if (PDBViewerRef)
        PDBViewerRef->ToggleInteractionType(EInteractionType::Hydrophobic, bIsChecked);
}

void UInteractionControlWidget::UpdateAllCounts()
{
    if (!PDBViewerRef) return;

    int32 HBondCount      = GetInteractionCountByType(EInteractionType::HydrogenBond);
    int32 SaltBridgeCount = GetInteractionCountByType(EInteractionType::SaltBridge);
    int32 PiStackCount    = GetInteractionCountByType(EInteractionType::PiStacking);
    int32 HydrophobicCount= GetInteractionCountByType(EInteractionType::Hydrophobic);
    int32 TotalCount      = PDBViewerRef->GetAllInteractions().Num();

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
}

void UInteractionControlWidget::UpdateStatusText(const FString& Status)
{
    if (StatusText)
        StatusText->SetText(FText::FromString(Status));
}

void UInteractionControlWidget::UpdateInteractionList()
{
    if (!bShowDetailedList || !InteractionListBox || !PDBViewerRef) return;
    InteractionListBox->ClearChildren();
    PopulateDetailedList();
}

void UInteractionControlWidget::PopulateDetailedList()
{
    if (!InteractionListBox || !PDBViewerRef) return;

    TArray<FMolecularInteraction> Interactions = PDBViewerRef->GetAllInteractions();
    int32 MaxDisplay = FMath::Min(Interactions.Num(), 100);

    for (int32 i = 0; i < MaxDisplay; ++i)
    {
        const FMolecularInteraction& Interaction = Interactions[i];

        FString TypeStr;
        switch (Interaction.Type)
        {
            case EInteractionType::HydrogenBond: TypeStr = TEXT("H-Bond");     break;
            case EInteractionType::SaltBridge:   TypeStr = TEXT("Salt Bridge"); break;
            case EInteractionType::PiStacking:   TypeStr = TEXT("Pi-Stack");    break;
            case EInteractionType::Hydrophobic:  TypeStr = TEXT("Hydrophobic"); break;
            default:                              TypeStr = TEXT("Other");       break;
        }

        FString EntryStr = FString::Printf(TEXT("%s: %s(%s) <-> %s(%s) [%.2f Å]"),
            *TypeStr,
            *Interaction.Residue1, *Interaction.Atom1,
            *Interaction.Residue2, *Interaction.Atom2,
            Interaction.Distance);

        UTextBlock* EntryText = NewObject<UTextBlock>(InteractionListBox);
        EntryText->SetText(FText::FromString(EntryStr));
        InteractionListBox->AddChild(EntryText);
    }

    if (Interactions.Num() > MaxDisplay)
    {
        UTextBlock* MoreText = NewObject<UTextBlock>(InteractionListBox);
        MoreText->SetText(FText::FromString(FString::Printf(TEXT("... and %d more"), Interactions.Num() - MaxDisplay)));
        InteractionListBox->AddChild(MoreText);
    }
}

void UInteractionControlWidget::SetCheckBoxesEnabled(bool bEnabled)
{
    if (HBondCheckBox)       HBondCheckBox->SetIsEnabled(bEnabled);
    if (SaltBridgeCheckBox)  SaltBridgeCheckBox->SetIsEnabled(bEnabled);
    if (PiStackCheckBox)     PiStackCheckBox->SetIsEnabled(bEnabled);
    if (HydrophobicCheckBox) HydrophobicCheckBox->SetIsEnabled(bEnabled);
}

int32 UInteractionControlWidget::GetInteractionCountByType(EInteractionType Type) const
{
    if (!PDBViewerRef) return 0;
    return PDBViewerRef->GetInteractionsByType(Type).Num();
}

void UInteractionControlWidget::RefreshDisplay()
{
    UpdateAllCounts();
    UpdateInteractionList();
}
