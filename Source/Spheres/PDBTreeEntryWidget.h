// PDBTreeEntryWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "PDBTreeEntryWidget.generated.h"

class UTextBlock;
class UButton;
class UCheckBox;
class UPDBTreeNode;
class APDBViewer;

UCLASS()
class SPHERES_API UPDBTreeEntryWidget : public UUserWidget, public IUserObjectListEntry
{
    GENERATED_BODY()

public:
    // Widgets bound from Designer (must match names exactly)
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UTextBlock* DisplayNameText;
    
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UButton* ExpanderButton;
    
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UCheckBox* VisibilityCheckbox;

protected:
    // IUserObjectListEntry interface
    virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;
    
    UPROPERTY()
    UPDBTreeNode* CurrentNode;
    
    UPROPERTY()
    APDBViewer* PDBViewerRef;
    
    // Track expansion state
    bool bIsExpanded = false;

    UFUNCTION()
    void OnExpanderClicked();
    
    UFUNCTION()
    void OnVisibilityChanged(bool bIsChecked);
    
private:
    // Helper to get icon/prefix for node type
    FString GetNodeIcon() const;

    // Helper to get the actual visibility state from the underlying data
    bool GetActualVisibilityForNode(UPDBTreeNode* Node) const;
};
