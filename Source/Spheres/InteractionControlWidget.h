// InteractionControlWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PDBViewer.h"
#include "InteractionControlWidget.generated.h"

class UTextBlock;
class UCheckBox;
class UScrollBox;

UCLASS()
class SPHERES_API UInteractionControlWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UInteractionControlWidget(const FObjectInitializer& ObjectInitializer);

    // Interaction type visibility toggles
    UPROPERTY(meta = (BindWidget))
    UCheckBox* HBondCheckBox;

    UPROPERTY(meta = (BindWidget))
    UCheckBox* SaltBridgeCheckBox;

    UPROPERTY(meta = (BindWidget))
    UCheckBox* PiStackCheckBox;

    UPROPERTY(meta = (BindWidget))
    UCheckBox* HydrophobicCheckBox;

    // Status and counts
    UPROPERTY(meta = (BindWidget))
    UTextBlock* StatusText;

    UPROPERTY(meta = (BindWidget))
    UTextBlock* HBondCountText;

    UPROPERTY(meta = (BindWidget))
    UTextBlock* SaltBridgeCountText;

    UPROPERTY(meta = (BindWidget))
    UTextBlock* PiStackCountText;

    UPROPERTY(meta = (BindWidget))
    UTextBlock* HydrophobicCountText;

    UPROPERTY(meta = (BindWidget))
    UTextBlock* TotalCountText;

    UPROPERTY(meta = (BindWidget))
    UScrollBox* InteractionListBox;

    UPROPERTY(BlueprintReadWrite, Category = "Interactions")
    APDBViewer* PDBViewerRef;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interactions")
    bool bShowDetailedList;

    UFUNCTION(BlueprintCallable, Category = "Interactions")
    void SetPDBViewer(APDBViewer* Viewer);

    UFUNCTION(BlueprintCallable, Category = "Interactions")
    void OnInteractionsCalculated();

    UFUNCTION(BlueprintCallable, Category = "Interactions")
    void RefreshDisplay();

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Interactions")
    int32 GetInteractionCountByType(EInteractionType Type) const;

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    APDBViewer* FindPDBViewer();

    UFUNCTION()
    void OnStructureLoaded();

    UFUNCTION()
    void OnHBondCheckBoxChanged(bool bIsChecked);

    UFUNCTION()
    void OnSaltBridgeCheckBoxChanged(bool bIsChecked);

    UFUNCTION()
    void OnPiStackCheckBoxChanged(bool bIsChecked);

    UFUNCTION()
    void OnHydrophobicCheckBoxChanged(bool bIsChecked);

    void UpdateAllCounts();
    void UpdateStatusText(const FString& Status);
    void UpdateInteractionList();
    void PopulateDetailedList();
    void SetCheckBoxesEnabled(bool bEnabled);
};
