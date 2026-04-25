// PDBStructureWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PDBStructureWidget.generated.h"

class UTreeView;
class APDBViewer;
class UPDBTreeNode;

UCLASS()
class SPHERES_API UPDBStructureWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UTreeView* StructureTreeView;

protected:
    UPROPERTY()
    APDBViewer* PDBViewerRef;

    UFUNCTION()
    void OnStructureLoaded();

    UFUNCTION()
    void OnLigandsLoadedEvent();

    UFUNCTION()
    void OnStructureClearedEvent();

    void OnGetItemChildren(UObject* Item, TArray<UObject*>& OutChildren);
};
