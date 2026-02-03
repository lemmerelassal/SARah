// PDBStructureWidget.cpp
#include "PDBStructureWidget.h"
#include "PDBViewer.h"
#include "Components/TreeView.h"
#include "Kismet/GameplayStatics.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Blueprint/WidgetTree.h"
#include "Misc/FileHelper.h"

void UPDBStructureWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // Find the PDBViewer actor in the level
    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APDBViewer::StaticClass(), FoundActors);
    
    if (FoundActors.Num() > 0)
    {
        PDBViewerRef = Cast<APDBViewer>(FoundActors[0]);
        
        if (PDBViewerRef)
        {
            // Bind to the OnResiduesLoaded event
            PDBViewerRef->OnResiduesLoaded.AddDynamic(this, &UPDBStructureWidget::OnStructureLoaded);

            // Bind to the OnLigandsLoaded event (covers SDF loading via any path)
            PDBViewerRef->OnLigandsLoaded.AddDynamic(this, &UPDBStructureWidget::OnLigandsLoadedEvent);
            
            // If TreeView is bound, set up delegates
            if (StructureTreeView)
            {
                // Bind the "get children" function - UE 5.6 syntax
                StructureTreeView->SetOnGetItemChildren(this, &UPDBStructureWidget::OnGetItemChildren);
                
                // Initial population (if structure already loaded)
                OnStructureLoaded();
            }
        }
    }
    
    // Auto-style all text blocks in this widget
    ApplyTextStyles();
    
    // Auto-style buttons
    ApplyButtonStyles();
}

void UPDBStructureWidget::ApplyTextStyles()
{
    // Define colors
    FLinearColor PrimaryTextColor = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("1F2937")));
    FLinearColor SecondaryTextColor = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("6B7280")));
    
    // Find and style all text blocks
    TArray<UWidget*> AllWidgets;
    WidgetTree->GetAllWidgets(AllWidgets);
    
    for (UWidget* Widget : AllWidgets)
    {
        if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
        {
            // Check if text block is inside a button - if so, make it white
            UWidget* Parent = TextBlock->GetParent();
            if (Parent && Parent->IsA<UButton>())
            {
                TextBlock->SetColorAndOpacity(FSlateColor(FLinearColor::White));
                continue;
            }

            FString WidgetName = TextBlock->GetName();
            
            // Apply colors based on widget naming convention
            if (WidgetName.Contains(TEXT("Title")) || WidgetName.Contains(TEXT("Primary")))
            {
                TextBlock->SetColorAndOpacity(FSlateColor(PrimaryTextColor));
            }
            else if (WidgetName.Contains(TEXT("Subtitle")) || WidgetName.Contains(TEXT("Secondary")))
            {
                TextBlock->SetColorAndOpacity(FSlateColor(SecondaryTextColor));
            }
            else
            {
                // Default to primary text color
                TextBlock->SetColorAndOpacity(FSlateColor(PrimaryTextColor));
            }
        }
    }
}

void UPDBStructureWidget::ApplyButtonStyles()
{
    // Define colors
    FLinearColor PinkNormal = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("EC4899")));
    FLinearColor PinkHover = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("DB2777")));
    FLinearColor PinkPressed = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("BE185D")));
    
    FLinearColor GrayNormal = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("F3F4F6")));
    FLinearColor GrayHover = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("E5E7EB")));
    FLinearColor GrayPressed = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("D1D5DB")));
    
    // Style Load button (primary - pink)
    if (Button_Load)
    {
        // Set background colors via SetBackgroundColor (simpler approach)
        Button_Load->SetBackgroundColor(PinkNormal);
        
        // Bind click event
        if (!Button_Load->OnClicked.IsBound())
        {
            Button_Load->OnClicked.AddDynamic(this, &UPDBStructureWidget::OnLoadClicked);
        }
    }
    
    // Style Save button (secondary - gray)
    if (Button_Save)
    {
        Button_Save->SetBackgroundColor(GrayNormal);
        
        if (!Button_Save->OnClicked.IsBound())
        {
            Button_Save->OnClicked.AddDynamic(this, &UPDBStructureWidget::OnSaveClicked);
        }
    }
    
    // Style Clear button (secondary - gray)
    if (Button_Clear)
    {
        Button_Clear->SetBackgroundColor(GrayNormal);
        
        if (!Button_Clear->OnClicked.IsBound())
        {
            Button_Clear->OnClicked.AddDynamic(this, &UPDBStructureWidget::OnClearClicked);
        }
    }
    
    // Style Load SDF button (primary - pink)
    if (Button_LoadSDF)
    {
        Button_LoadSDF->SetBackgroundColor(PinkNormal);
        
        if (!Button_LoadSDF->OnClicked.IsBound())
        {
            Button_LoadSDF->OnClicked.AddDynamic(this, &UPDBStructureWidget::OnLoadSDFClicked);
        }
    }
}

void UPDBStructureWidget::OnLoadClicked()
{
    if (PDBViewerRef)
    {
        PDBViewerRef->OpenLoadDialog();
    }
}

void UPDBStructureWidget::OnSaveClicked()
{
    if (PDBViewerRef)
    {
        PDBViewerRef->OpenSaveDialog();
    }
}

void UPDBStructureWidget::OnClearClicked()
{
    if (PDBViewerRef)
    {
        PDBViewerRef->ClearCurrentStructure();
        
        // Clear the tree view
        if (StructureTreeView)
        {
            StructureTreeView->ClearListItems();
        }
    }
}

void UPDBStructureWidget::NativeDestruct()
{
    // Unbind from events
    if (PDBViewerRef)
    {
        PDBViewerRef->OnResiduesLoaded.RemoveDynamic(this, &UPDBStructureWidget::OnStructureLoaded);
        PDBViewerRef->OnLigandsLoaded.RemoveDynamic(this, &UPDBStructureWidget::OnLigandsLoadedEvent);
    }
    
    Super::NativeDestruct();
}

void UPDBStructureWidget::OnStructureLoaded()
{
    if (!StructureTreeView || !PDBViewerRef)
        return;
    
    // Populate the tree view
    PDBViewerRef->PopulateTreeView(StructureTreeView);
}

void UPDBStructureWidget::OnGetItemChildren(UObject* Item, TArray<UObject*>& OutChildren)
{
    if (!PDBViewerRef)
        return;
    
    // Cast to our node type
    UPDBTreeNode* Node = Cast<UPDBTreeNode>(Item);
    if (!Node)
        return;
    
    // Get children from PDBViewer
    OutChildren = PDBViewerRef->GetChildrenForNode(Node);
}

void UPDBStructureWidget::OnLigandsLoadedEvent()
{
    if (!StructureTreeView || !PDBViewerRef)
        return;

    PDBViewerRef->ClearTreeNodeCache();
    PDBViewerRef->PopulateTreeView(StructureTreeView);
}

void UPDBStructureWidget::OnLoadSDFClicked()
{
    if (!PDBViewerRef)
        return;

    FString FilePath;
    if (PDBViewerRef->ShowSDFFileDialog(FilePath))
    {
        FString FileContent;
        if (FFileHelper::LoadFileToString(FileContent, *FilePath))
        {
            PDBViewerRef->ParseSDF(FileContent);

            // Explicitly refresh the tree after SDF load
            if (StructureTreeView)
            {
                PDBViewerRef->ClearTreeNodeCache();
                PDBViewerRef->PopulateTreeView(StructureTreeView);
            }
        }
    }
}
