// FEPControlWidget.h - User interface for FEP calculations
// Compatible with Unreal Engine 5.6

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FEPCalculator.h"
#include "FEPControlWidget.generated.h"

class UButton;
class UTextBlock;
class UProgressBar;
class UEditableTextBox;
class UCheckBox;
class UComboBoxString;
class APDBViewer;

/**
 * Widget for controlling FEP calculations and displaying results
 */
UCLASS()
class SPHERES_API UFEPControlWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // Called to initialize the widget
    virtual void NativeConstruct() override;
    
    // Set the FEP calculator reference
    UFUNCTION(BlueprintCallable, Category = "FEP")
    void SetFEPCalculator(AFEPCalculator* Calculator);
    
    // Set the PDB viewer reference
    UFUNCTION(BlueprintCallable, Category = "FEP")
    void SetPDBViewer(APDBViewer* Viewer);
    
    // Update display with current results
    UFUNCTION(BlueprintCallable, Category = "FEP")
    void UpdateResults(const FFEPResult& Result);

protected:
    // UI Components (bind these in UMG Designer)
    
    // Control buttons
    UPROPERTY(meta = (BindWidget))
    UButton* StartButton;
    
    UPROPERTY(meta = (BindWidget))
    UButton* StopButton;
    
    UPROPERTY(meta = (BindWidget))
    UButton* ExportButton;
    
    // Optional: Calculate visible ligands button (recommended)
    UPROPERTY(meta = (BindWidgetOptional))
    UButton* CalculateVisibleButton;
    
    // Optional: Calculate all ligands button
    UPROPERTY(meta = (BindWidgetOptional))
    UButton* CalculateAllButton;
    
    // Ligand selection
    UPROPERTY(meta = (BindWidget))
    UComboBoxString* LigandComboBox;
    
    // Progress display
    UPROPERTY(meta = (BindWidget))
    UProgressBar* CalculationProgressBar;
    
    UPROPERTY(meta = (BindWidget))
    UTextBlock* ProgressText;
    
    // Results display
    UPROPERTY(meta = (BindWidget))
    UTextBlock* DeltaGText;
    
    UPROPERTY(meta = (BindWidget))
    UTextBlock* BindingAffinityText;
    
    UPROPERTY(meta = (BindWidget))
    UTextBlock* ErrorText;
    
    UPROPERTY(meta = (BindWidget))
    UTextBlock* ElectrostaticText;
    
    UPROPERTY(meta = (BindWidget))
    UTextBlock* VdWText;
    
    UPROPERTY(meta = (BindWidget))
    UTextBlock* SolvationText;
    
    // Parameter controls
    UPROPERTY(meta = (BindWidget))
    UEditableTextBox* TemperatureTextBox;
    
    UPROPERTY(meta = (BindWidget))
    UEditableTextBox* NumLambdaWindowsTextBox;
    
    UPROPERTY(meta = (BindWidget))
    UEditableTextBox* EquilibrationStepsTextBox;
    
    UPROPERTY(meta = (BindWidget))
    UEditableTextBox* ProductionStepsTextBox;
    
    UPROPERTY(meta = (BindWidget))
    UCheckBox* UseSoftCoreCheckBox;
    
    UPROPERTY(meta = (BindWidget))
    UCheckBox* CalculateSolvationCheckBox;
    
    UPROPERTY(meta = (BindWidget))
    UCheckBox* VerboseLoggingCheckBox;
    
    // Button click handlers
    UFUNCTION()
    void OnStartButtonClicked();
    
    UFUNCTION()
    void OnStopButtonClicked();
    
    UFUNCTION()
    void OnExportButtonClicked();
    
    UFUNCTION()
    void OnCalculateVisibleButtonClicked();
    
    UFUNCTION()
    void OnCalculateAllButtonClicked();
    
    // Progress update handler
    UFUNCTION()
    void OnFEPProgress(float ProgressPercent);
    
    // Completion handler
    UFUNCTION()
    void OnFEPComplete(const FFEPResult& Result);
    
    // Helper functions
    void PopulateLigandList();
    void UpdateUIState(bool bIsCalculating);
    FFEPParameters GetParametersFromUI();
    void ExportResultsToFile(const FFEPResult& Result);
    void EnsureActorsFound();  // Re-find actors if needed

private:
    UPROPERTY()
    AFEPCalculator* FEPCalculator;
    
    UPROPERTY()
    APDBViewer* PDBViewer;
    
    UPROPERTY()
    FFEPResult CurrentResult;
};
