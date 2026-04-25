// PDBTreeEntryWidget.cpp
#include "PDBTreeEntryWidget.h"
#include "PDBViewer.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/TreeView.h"
#include "Components/PanelWidget.h"
#include "Kismet/GameplayStatics.h"

void UPDBTreeEntryWidget::NativeOnListItemObjectSet(UObject* ListItemObject)
{
    IUserObjectListEntry::NativeOnListItemObjectSet(ListItemObject);

    // CRITICAL: Reset expansion state - widgets are recycled!
    bIsExpanded = false;

    // Validate object before casting - prevents crash on invalid/garbage collected objects
    if (!IsValid(ListItemObject))
    {
        CurrentNode = nullptr;
        return;
    }

    // Cast to our node type
    CurrentNode = Cast<UPDBTreeNode>(ListItemObject);
    if (!CurrentNode)
        return;
    
    UE_LOG(LogTemp, Warning, TEXT("NativeOnListItemObjectSet: Setting up widget for %s (Type: %d)"), 
           *CurrentNode->DisplayName, (int32)CurrentNode->NodeType);
    
    // Find PDBViewer if we don't have it
    if (!PDBViewerRef)
    {
        TArray<AActor*> FoundActors;
        UGameplayStatics::GetAllActorsOfClass(GetWorld(), APDBViewer::StaticClass(), FoundActors);
        if (FoundActors.Num() > 0)
        {
            PDBViewerRef = Cast<APDBViewer>(FoundActors[0]);
        }
    }
    
    // Set display name
    if (DisplayNameText)
    {
        // Add icon prefix for category nodes
        FString DisplayText = GetNodeIcon() + CurrentNode->DisplayName;
        DisplayNameText->SetText(FText::FromString(DisplayText));
    }
    
    // Set visibility checkbox
    if (VisibilityCheckbox)
    {
        // Get the actual visibility state from the underlying data
        bool bActualVisibility = GetActualVisibilityForNode(CurrentNode);
        VisibilityCheckbox->SetIsChecked(bActualVisibility);

        // Bind checkbox event
        if (!VisibilityCheckbox->OnCheckStateChanged.IsBound())
        {
            VisibilityCheckbox->OnCheckStateChanged.AddDynamic(this, &UPDBTreeEntryWidget::OnVisibilityChanged);
        }
    }
    
    // Show/hide expander button based on whether node can have children
    if (ExpanderButton)
    {
        // Show expander for nodes that can expand (chains and category nodes)
        bool bShowExpander = CurrentNode->bCanExpand;
        ExpanderButton->SetVisibility(bShowExpander ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        
        // Note: We can't sync with TreeView's expansion state in UE 5.6
        // The widget will track its own state with bIsExpanded

        // Set initial arrow based on expansion state (always starts collapsed)
        UPanelWidget* ButtonContent = Cast<UPanelWidget>(ExpanderButton->GetChildAt(0));
        if (ButtonContent && ButtonContent->GetChildrenCount() > 0)
        {
            if (UTextBlock* ButtonText = Cast<UTextBlock>(ButtonContent->GetChildAt(0)))
            {
                ButtonText->SetText(FText::FromString(bIsExpanded ? TEXT("▼") : TEXT("▶")));
            }
        }

        // Bind expander button event
        if (!ExpanderButton->OnClicked.IsBound())
        {
            ExpanderButton->OnClicked.AddDynamic(this, &UPDBTreeEntryWidget::OnExpanderClicked);
        }
    }
}

bool UPDBTreeEntryWidget::GetActualVisibilityForNode(UPDBTreeNode* Node) const
{
    if (!Node || !PDBViewerRef)
        return true; // Default to visible
    
    // For category nodes, check the visibility of the first child item
    switch (Node->NodeType)
    {
        case EPDBNodeType::ResiduesCategory:
        {
            // Check first residue in this chain
            for (const auto& Pair : PDBViewerRef->ResidueMap)
            {
                if (Pair.Value && Pair.Value->Chain == Node->ChainID)
                {
                    return Pair.Value->bIsVisible;
                }
            }
            return true;
        }
        
        case EPDBNodeType::HeteroatomsCategory:
        {
            // Check first heteroatom (any water or ligand) in this chain
            for (const auto& Pair : PDBViewerRef->LigandMap)
            {
                TArray<FString> Parts;
                Pair.Key.ParseIntoArray(Parts, TEXT("_"));
                if (Parts.Num() >= 3 && Parts[Parts.Num() - 1] == Node->ChainID && Pair.Value)
                {
                    return Pair.Value->bIsVisible;
                }
            }
            return true;
        }
        
        case EPDBNodeType::WaterCategory:
        {
            // Check first WATER molecule in this chain
            for (const auto& Pair : PDBViewerRef->LigandMap)
            {
                TArray<FString> Parts;
                Pair.Key.ParseIntoArray(Parts, TEXT("_"));
                if (Parts.Num() >= 3 && Parts[Parts.Num() - 1] == Node->ChainID && Pair.Value)
                {
                    // Check if this is a water molecule
                    if (PDBViewerRef->IsWaterKey(Pair.Key))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("GetActualVisibility for WaterCategory: Found water %s, visible=%s"),
                               *Pair.Key, Pair.Value->bIsVisible ? TEXT("true") : TEXT("false"));
                        return Pair.Value->bIsVisible;
                    }
                }
            }
            UE_LOG(LogTemp, Warning, TEXT("GetActualVisibility for WaterCategory: No waters found, returning true"));
            return true;
        }
        
        case EPDBNodeType::LigandsCategory:
        {
            // Check first LIGAND (non-water) in this chain
            for (const auto& Pair : PDBViewerRef->LigandMap)
            {
                TArray<FString> Parts;
                Pair.Key.ParseIntoArray(Parts, TEXT("_"));
                if (Parts.Num() >= 3 && Parts[Parts.Num() - 1] == Node->ChainID && Pair.Value)
                {
                    // Check if this is NOT a water molecule
                    if (!PDBViewerRef->IsWaterKey(Pair.Key))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("GetActualVisibility for LigandsCategory: Found ligand %s, visible=%s"),
                               *Pair.Key, Pair.Value->bIsVisible ? TEXT("true") : TEXT("false"));
                        return Pair.Value->bIsVisible;
                    }
                }
            }
            UE_LOG(LogTemp, Warning, TEXT("GetActualVisibility for LigandsCategory: No ligands found, returning true"));
            return true;
        }
        
        case EPDBNodeType::Residue:
        {
            // Check this specific residue
            if (const FResidueInfo* ResInfo = PDBViewerRef->ResidueMap.FindRef(Node->NodeKey))
            {
                return ResInfo->bIsVisible;
            }
            return true;
        }
        
        case EPDBNodeType::Water:
        case EPDBNodeType::Ligand:
        {
            // Check this specific ligand/water
            if (const FLigandInfo* LigInfo = PDBViewerRef->LigandMap.FindRef(Node->NodeKey))
            {
                return LigInfo->bIsVisible;
            }
            return true;
        }
        
        default:
            return Node->bIsVisible;
    }
}

FString UPDBTreeEntryWidget::GetNodeIcon() const
{
    if (!CurrentNode)
        return TEXT("");
    
    switch (CurrentNode->NodeType)
    {
        case EPDBNodeType::Chain:
            return TEXT("🔗 ");  // Chain link
            
        case EPDBNodeType::ResiduesCategory:
            return TEXT("📦 ");  // Box for residues
            
        case EPDBNodeType::HeteroatomsCategory:
            return TEXT("⚛️ ");  // Atom symbol
            
        case EPDBNodeType::WaterCategory:
            return TEXT("💧 ");  // Water drop
            
        case EPDBNodeType::LigandsCategory:
            return TEXT("💊 ");  // Pill for ligands
            
        case EPDBNodeType::Residue:
            return TEXT("  ");   // Indentation for residues
            
        case EPDBNodeType::Water:
            return TEXT("  ");   // Indentation for water
            
        case EPDBNodeType::Ligand:
            return TEXT("  ");   // Indentation for ligands
            
        default:
            return TEXT("");
    }
}

void UPDBTreeEntryWidget::OnExpanderClicked()
{
    UE_LOG(LogTemp, Warning, TEXT("OnExpanderClicked called!"));
    
    if (!CurrentNode)
    {
        UE_LOG(LogTemp, Error, TEXT("OnExpanderClicked: CurrentNode is NULL!"));
        return;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("OnExpanderClicked: Node=%s, Type=%d, bCanExpand=%s"), 
           *CurrentNode->DisplayName, (int32)CurrentNode->NodeType, 
           CurrentNode->bCanExpand ? TEXT("true") : TEXT("false"));
    
    // Get the owning tree view
    UTreeView* TreeView = Cast<UTreeView>(GetOwningListView());
    if (!TreeView)
    {
        UE_LOG(LogTemp, Error, TEXT("OnExpanderClicked: Failed to get TreeView!"));
        return;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("OnExpanderClicked: Got TreeView, toggling expansion from %s to %s"),
           bIsExpanded ? TEXT("expanded") : TEXT("collapsed"),
           !bIsExpanded ? TEXT("expanded") : TEXT("collapsed"));
    
    // Toggle the expansion state
    bIsExpanded = !bIsExpanded;
    TreeView->SetItemExpansion(CurrentNode, bIsExpanded);
    
    UE_LOG(LogTemp, Warning, TEXT("OnExpanderClicked: SetItemExpansion(%s) called"), 
           bIsExpanded ? TEXT("true") : TEXT("false"));
    
    // FIX: Update button text/icon - navigate through panel to find the TextBlock
    if (ExpanderButton)
    {
        UPanelWidget* ButtonContent = Cast<UPanelWidget>(ExpanderButton->GetChildAt(0));
        if (ButtonContent && ButtonContent->GetChildrenCount() > 0)
        {
            if (UTextBlock* ButtonText = Cast<UTextBlock>(ButtonContent->GetChildAt(0)))
            {
                ButtonText->SetText(FText::FromString(bIsExpanded ? TEXT("▼") : TEXT("▶")));
                UE_LOG(LogTemp, Warning, TEXT("OnExpanderClicked: Updated arrow to %s"), 
                       bIsExpanded ? TEXT("▼") : TEXT("▶"));
            }
        }
    }
}

void UPDBTreeEntryWidget::OnVisibilityChanged(bool bIsChecked)
{
    if (!CurrentNode || !PDBViewerRef)
        return;
    
    // Toggle visibility based on node type
    switch (CurrentNode->NodeType)
    {
        case EPDBNodeType::Chain:
            PDBViewerRef->ToggleChainVisibility(CurrentNode->ChainID);
            break;
            
        case EPDBNodeType::ResiduesCategory:
        case EPDBNodeType::HeteroatomsCategory:
        case EPDBNodeType::WaterCategory:
        case EPDBNodeType::LigandsCategory:
            // Toggle all children in this category
            PDBViewerRef->ToggleCategoryVisibility(CurrentNode);
            break;
            
        case EPDBNodeType::Residue:
            PDBViewerRef->ToggleResidueVisibility(CurrentNode->NodeKey);
            break;
            
        case EPDBNodeType::Water:
        case EPDBNodeType::Ligand:
            PDBViewerRef->ToggleLigandVisibility(CurrentNode->NodeKey);
            break;
            
        default:
            PDBViewerRef->ToggleNodeVisibility(CurrentNode);
            break;
    }
}
