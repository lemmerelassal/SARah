// PDBStructureWidget.cpp
#include "PDBStructureWidget.h"
#include "PDBViewer.h"
#include "Components/TreeView.h"
#include "Kismet/GameplayStatics.h"

void UPDBStructureWidget::NativeConstruct()
{
    Super::NativeConstruct();

    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APDBViewer::StaticClass(), FoundActors);
    if (FoundActors.Num() > 0)
        PDBViewerRef = Cast<APDBViewer>(FoundActors[0]);

    if (PDBViewerRef)
    {
        PDBViewerRef->OnResiduesLoaded.AddDynamic(this, &UPDBStructureWidget::OnStructureLoaded);
        PDBViewerRef->OnLigandsLoaded.AddDynamic(this, &UPDBStructureWidget::OnLigandsLoadedEvent);
        PDBViewerRef->OnStructureCleared.AddDynamic(this, &UPDBStructureWidget::OnStructureClearedEvent);

        if (StructureTreeView)
        {
            StructureTreeView->SetOnGetItemChildren(this, &UPDBStructureWidget::OnGetItemChildren);
            OnStructureLoaded();
        }
    }
}

void UPDBStructureWidget::NativeDestruct()
{
    if (PDBViewerRef)
    {
        PDBViewerRef->OnResiduesLoaded.RemoveDynamic(this, &UPDBStructureWidget::OnStructureLoaded);
        PDBViewerRef->OnLigandsLoaded.RemoveDynamic(this, &UPDBStructureWidget::OnLigandsLoadedEvent);
        PDBViewerRef->OnStructureCleared.RemoveDynamic(this, &UPDBStructureWidget::OnStructureClearedEvent);
    }
    Super::NativeDestruct();
}

void UPDBStructureWidget::OnStructureLoaded()
{
    if (!StructureTreeView || !PDBViewerRef) return;
    PDBViewerRef->PopulateTreeView(StructureTreeView);
}

void UPDBStructureWidget::OnLigandsLoadedEvent()
{
    if (!StructureTreeView || !PDBViewerRef) return;
    PDBViewerRef->ClearTreeNodeCache();
    PDBViewerRef->PopulateTreeView(StructureTreeView);
}

void UPDBStructureWidget::OnStructureClearedEvent()
{
    if (StructureTreeView)
        StructureTreeView->ClearListItems();
}

void UPDBStructureWidget::OnGetItemChildren(UObject* Item, TArray<UObject*>& OutChildren)
{
    if (!PDBViewerRef) return;
    UPDBTreeNode* Node = Cast<UPDBTreeNode>(Item);
    if (Node)
        OutChildren = PDBViewerRef->GetChildrenForNode(Node);
}
