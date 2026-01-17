// FEPControlWidget.cpp - Implementation of FEP control widget
// Compatible with Unreal Engine 5.6

#include "FEPControlWidget.h"
#include "FEPCalculator.h"
#include "FEPCalculatorOptimized.h"
#include "PDBViewer.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/EditableTextBox.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"

void UFEPControlWidget::NativeConstruct()
{
    Super::NativeConstruct();
    
    // Bind button events
    if (StartButton)
    {
        StartButton->OnClicked.AddDynamic(this, &UFEPControlWidget::OnStartButtonClicked);
    }
    
    if (StopButton)
    {
        StopButton->OnClicked.AddDynamic(this, &UFEPControlWidget::OnStopButtonClicked);
        StopButton->SetIsEnabled(false);
    }
    
    if (ExportButton)
    {
        ExportButton->OnClicked.AddDynamic(this, &UFEPControlWidget::OnExportButtonClicked);
    }
    
    if (CalculateVisibleButton)
    {
        CalculateVisibleButton->OnClicked.AddDynamic(this, &UFEPControlWidget::OnCalculateVisibleButtonClicked);
    }
    
    if (CalculateAllButton)
    {
        CalculateAllButton->OnClicked.AddDynamic(this, &UFEPControlWidget::OnCalculateAllButtonClicked);
    }
    
    // Initialize progress bar
    if (CalculationProgressBar)
    {
        CalculationProgressBar->SetPercent(0.0f);
    }
    
    // Set default parameter values
    if (TemperatureTextBox)
    {
        TemperatureTextBox->SetText(FText::FromString(TEXT("300.0")));
    }
    
    if (NumLambdaWindowsTextBox)
    {
        NumLambdaWindowsTextBox->SetText(FText::FromString(TEXT("20")));
    }
    
    if (EquilibrationStepsTextBox)
    {
        EquilibrationStepsTextBox->SetText(FText::FromString(TEXT("10000")));
    }
    
    if (ProductionStepsTextBox)
    {
        ProductionStepsTextBox->SetText(FText::FromString(TEXT("50000")));
    }
    
    if (UseSoftCoreCheckBox)
    {
        UseSoftCoreCheckBox->SetIsChecked(true);
    }
    
    if (CalculateSolvationCheckBox)
    {
        CalculateSolvationCheckBox->SetIsChecked(true);
    }
    
    // Auto-find FEPCalculator and PDBViewer if not set
    if (!FEPCalculator)
    {
        // Try to find optimized calculator first
        AFEPCalculator* FoundCalc = Cast<AFEPCalculator>(
            UGameplayStatics::GetActorOfClass(GetWorld(), AFEPCalculatorOptimized::StaticClass())
        );
        
        // If not found, try base calculator
        if (!FoundCalc)
        {
            FoundCalc = Cast<AFEPCalculator>(
                UGameplayStatics::GetActorOfClass(GetWorld(), AFEPCalculator::StaticClass())
            );
        }
        
        if (FoundCalc)
        {
            SetFEPCalculator(FoundCalc);
            UE_LOG(LogTemp, Log, TEXT("FEPControlWidget: Auto-found FEPCalculator"));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("FEPControlWidget: Could not find FEPCalculator in level"));
        }
    }
    
    if (!PDBViewer)
    {
        APDBViewer* FoundViewer = Cast<APDBViewer>(
            UGameplayStatics::GetActorOfClass(GetWorld(), APDBViewer::StaticClass())
        );
        if (FoundViewer)
        {
            SetPDBViewer(FoundViewer);
            UE_LOG(LogTemp, Log, TEXT("FEPControlWidget: Auto-found PDBViewer"));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("FEPControlWidget: Could not find PDBViewer in level"));
        }
    }
    
    // Final check and warning
    if (!FEPCalculator || !PDBViewer)
    {
        UE_LOG(LogTemp, Error, TEXT("FEPControlWidget: Calculator or Viewer not set!"));
        UE_LOG(LogTemp, Error, TEXT("  - FEPCalculator found: %s"), FEPCalculator ? TEXT("YES") : TEXT("NO"));
        UE_LOG(LogTemp, Error, TEXT("  - PDBViewer found: %s"), PDBViewer ? TEXT("YES") : TEXT("NO"));
    }
}

void UFEPControlWidget::SetFEPCalculator(AFEPCalculator* Calculator)
{
    FEPCalculator = Calculator;
    
    if (FEPCalculator)
    {
        // Bind to calculator delegates
        FEPCalculator->OnFEPProgress.AddDynamic(this, &UFEPControlWidget::OnFEPProgress);
        FEPCalculator->OnFEPComplete.AddDynamic(this, &UFEPControlWidget::OnFEPComplete);
        
        UE_LOG(LogTemp, Log, TEXT("FEPControlWidget: Bound to FEP calculator"));
    }
}

void UFEPControlWidget::SetPDBViewer(APDBViewer* Viewer)
{
    PDBViewer = Viewer;
    
    if (PDBViewer)
    {
        PopulateLigandList();
        UE_LOG(LogTemp, Log, TEXT("FEPControlWidget: Bound to PDB viewer"));
    }
}

void UFEPControlWidget::PopulateLigandList()
{
    if (!LigandComboBox || !PDBViewer)
        return;
    
    LigandComboBox->ClearOptions();
    
    TArray<FString> LigandKeys;
    PDBViewer->LigandMap.GetKeys(LigandKeys);
    
    for (const FString& Key : LigandKeys)
    {
        if (PDBViewer->LigandMap[Key])
        {
            FString DisplayName = PDBViewer->LigandMap[Key]->LigandName;
            LigandComboBox->AddOption(Key + TEXT(" - ") + DisplayName);
        }
    }
    
    if (LigandKeys.Num() > 0)
    {
        LigandComboBox->SetSelectedIndex(0);
    }
    
    UE_LOG(LogTemp, Log, TEXT("FEPControlWidget: Populated %d ligands"), LigandKeys.Num());
}

FFEPParameters UFEPControlWidget::GetParametersFromUI()
{
    FFEPParameters Params;
    
    if (TemperatureTextBox)
    {
        Params.Temperature = FCString::Atof(*TemperatureTextBox->GetText().ToString());
    }
    
    if (NumLambdaWindowsTextBox)
    {
        Params.NumLambdaWindows = FCString::Atoi(*NumLambdaWindowsTextBox->GetText().ToString());
    }
    
    if (EquilibrationStepsTextBox)
    {
        Params.EquilibrationSteps = FCString::Atoi(*EquilibrationStepsTextBox->GetText().ToString());
    }
    
    if (ProductionStepsTextBox)
    {
        Params.ProductionSteps = FCString::Atoi(*ProductionStepsTextBox->GetText().ToString());
    }
    
    if (UseSoftCoreCheckBox)
    {
        Params.bUseSoftCore = UseSoftCoreCheckBox->IsChecked();
    }
    
    if (CalculateSolvationCheckBox)
    {
        Params.bCalculateSolvation = CalculateSolvationCheckBox->IsChecked();
    }
    
    if (VerboseLoggingCheckBox)
    {
        if (FEPCalculator)
        {
            FEPCalculator->bVerboseLogging = VerboseLoggingCheckBox->IsChecked();
        }
    }
    
    return Params;
}

void UFEPControlWidget::OnStartButtonClicked()
{
    // Ensure actors are found
    EnsureActorsFound();
    
    if (!FEPCalculator || !PDBViewer)
    {
        UE_LOG(LogTemp, Error, TEXT("FEPControlWidget: Calculator or Viewer not set"));
        UE_LOG(LogTemp, Error, TEXT("  Make sure both FEPCalculatorOptimized and PDBViewer are in your level!"));
        UE_LOG(LogTemp, Error, TEXT("  FEPCalculator: %s"), FEPCalculator ? TEXT("Found") : TEXT("NOT FOUND"));
        UE_LOG(LogTemp, Error, TEXT("  PDBViewer: %s"), PDBViewer ? TEXT("Found") : TEXT("NOT FOUND"));
        return;
    }
    
    if (!LigandComboBox)
    {
        UE_LOG(LogTemp, Error, TEXT("FEPControlWidget: Ligand combo box not found"));
        return;
    }
    
    // Get selected ligand
    FString Selected = LigandComboBox->GetSelectedOption();
    if (Selected.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("FEPControlWidget: No ligand selected"));
        return;
    }
    
    // Extract ligand key (before the " - ")
    FString LigandKey;
    Selected.Split(TEXT(" - "), &LigandKey, nullptr);
    
    UE_LOG(LogTemp, Log, TEXT("FEPControlWidget: Starting calculation for ligand: %s"), *LigandKey);
    
    // Update parameters from UI
    FEPCalculator->Parameters = GetParametersFromUI();
    
    // Start calculation
    FEPCalculator->CalculateBindingFreeEnergy(LigandKey);
    
    // Update UI state
    UpdateUIState(true);
}

void UFEPControlWidget::OnStopButtonClicked()
{
    if (FEPCalculator)
    {
        FEPCalculator->StopCalculation();
        UpdateUIState(false);
    }
}

void UFEPControlWidget::OnExportButtonClicked()
{
    ExportResultsToFile(CurrentResult);
}

void UFEPControlWidget::OnCalculateVisibleButtonClicked()
{
    // Ensure actors are found
    EnsureActorsFound();
    
    if (!FEPCalculator || !PDBViewer)
    {
        UE_LOG(LogTemp, Error, TEXT("FEPControlWidget: Calculator or Viewer not set"));
        UE_LOG(LogTemp, Error, TEXT("  Make sure both FEPCalculatorOptimized and PDBViewer are in your level!"));
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("FEPControlWidget: Starting calculation for visible ligands"));
    
    // Update parameters from UI
    FEPCalculator->Parameters = GetParametersFromUI();
    
    // Start calculation for visible ligands
    FEPCalculator->CalculateVisibleLigands();
    
    // Update UI state
    UpdateUIState(true);
}

void UFEPControlWidget::OnCalculateAllButtonClicked()
{
    // Ensure actors are found
    EnsureActorsFound();
    
    if (!FEPCalculator || !PDBViewer)
    {
        UE_LOG(LogTemp, Error, TEXT("FEPControlWidget: Calculator or Viewer not set"));
        UE_LOG(LogTemp, Error, TEXT("  Make sure both FEPCalculatorOptimized and PDBViewer are in your level!"));
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("FEPControlWidget: Starting calculation for all ligands"));
    
    // Update parameters from UI
    FEPCalculator->Parameters = GetParametersFromUI();
    
    // Start calculation for all ligands
    FEPCalculator->CalculateAllLigands();
    
    // Update UI state
    UpdateUIState(true);
}

void UFEPControlWidget::EnsureActorsFound()
{
    // Try to find FEPCalculator if not set
    if (!FEPCalculator)
    {
        AFEPCalculator* FoundCalc = Cast<AFEPCalculator>(
            UGameplayStatics::GetActorOfClass(GetWorld(), AFEPCalculatorOptimized::StaticClass())
        );
        if (!FoundCalc)
        {
            FoundCalc = Cast<AFEPCalculator>(
                UGameplayStatics::GetActorOfClass(GetWorld(), AFEPCalculator::StaticClass())
            );
        }
        if (FoundCalc)
        {
            SetFEPCalculator(FoundCalc);
            UE_LOG(LogTemp, Log, TEXT("FEPControlWidget: Found FEPCalculator"));
        }
    }
    
    // Try to find PDBViewer if not set
    if (!PDBViewer)
    {
        APDBViewer* FoundViewer = Cast<APDBViewer>(
            UGameplayStatics::GetActorOfClass(GetWorld(), APDBViewer::StaticClass())
        );
        if (FoundViewer)
        {
            SetPDBViewer(FoundViewer);
            UE_LOG(LogTemp, Log, TEXT("FEPControlWidget: Found PDBViewer"));
        }
    }
}

void UFEPControlWidget::OnFEPProgress(float ProgressPercent)
{
    if (CalculationProgressBar)
    {
        CalculationProgressBar->SetPercent(ProgressPercent / 100.0f);
    }
    
    if (ProgressText)
    {
        FString Text = FString::Printf(TEXT("Progress: %.1f%%"), ProgressPercent);
        ProgressText->SetText(FText::FromString(Text));
    }
}

void UFEPControlWidget::OnFEPComplete(const FFEPResult& Result)
{
    CurrentResult = Result;
    UpdateResults(Result);
    UpdateUIState(false);
    
    UE_LOG(LogTemp, Log, TEXT("FEPControlWidget: Calculation complete"));
}

void UFEPControlWidget::UpdateResults(const FFEPResult& Result)
{
    if (!Result.bCalculationSuccessful)
    {
        if (ErrorText)
        {
            ErrorText->SetText(FText::FromString(TEXT("Calculation failed: ") + Result.ErrorMessage));
        }
        return;
    }
    
    // Update Delta G
    if (DeltaGText)
    {
        FString Text = FString::Printf(TEXT("ΔG = %.2f ± %.2f kcal/mol"), 
                                      Result.DeltaG, Result.StandardError);
        DeltaGText->SetText(FText::FromString(Text));
    }
    
    // Update binding affinity
    if (BindingAffinityText)
    {
        FString Text;
        if (Result.BindingAffinity < 1000.0f)
        {
            Text = FString::Printf(TEXT("Kd = %.1f nM"), Result.BindingAffinity);
        }
        else if (Result.BindingAffinity < 1000000.0f)
        {
            Text = FString::Printf(TEXT("Kd = %.1f μM"), Result.BindingAffinity / 1000.0f);
        }
        else
        {
            Text = FString::Printf(TEXT("Kd = %.1f mM"), Result.BindingAffinity / 1000000.0f);
        }
        BindingAffinityText->SetText(FText::FromString(Text));
    }
    
    // Update energy components
    if (ElectrostaticText)
    {
        FString Text = FString::Printf(TEXT("Electrostatic: %.2f kcal/mol"), 
                                      Result.ElectrostaticContribution);
        ElectrostaticText->SetText(FText::FromString(Text));
    }
    
    if (VdWText)
    {
        FString Text = FString::Printf(TEXT("Van der Waals: %.2f kcal/mol"), 
                                      Result.VdWContribution);
        VdWText->SetText(FText::FromString(Text));
    }
    
    if (SolvationText)
    {
        FString Text = FString::Printf(TEXT("Solvation: %.2f kcal/mol"), 
                                      Result.SolvationContribution);
        SolvationText->SetText(FText::FromString(Text));
    }
    
    // Clear error text
    if (ErrorText)
    {
        ErrorText->SetText(FText::GetEmpty());
    }
}

void UFEPControlWidget::UpdateUIState(bool bIsCalculating)
{
    if (StartButton)
    {
        StartButton->SetIsEnabled(!bIsCalculating);
    }
    
    if (StopButton)
    {
        StopButton->SetIsEnabled(bIsCalculating);
    }
    
    if (CalculateVisibleButton)
    {
        CalculateVisibleButton->SetIsEnabled(!bIsCalculating);
    }
    
    if (CalculateAllButton)
    {
        CalculateAllButton->SetIsEnabled(!bIsCalculating);
    }
    
    if (LigandComboBox)
    {
        LigandComboBox->SetIsEnabled(!bIsCalculating);
    }
    
    if (!bIsCalculating)
    {
        if (CalculationProgressBar)
        {
            CalculationProgressBar->SetPercent(1.0f);
        }
        
        if (ProgressText)
        {
            ProgressText->SetText(FText::FromString(TEXT("Complete")));
        }
    }
}

void UFEPControlWidget::ExportResultsToFile(const FFEPResult& Result)
{
    if (!Result.bCalculationSuccessful)
    {
        UE_LOG(LogTemp, Warning, TEXT("FEPControlWidget: No results to export"));
        return;
    }
    
    // Create output directory
    FString OutputDir = FPaths::ProjectSavedDir() + TEXT("FEP/");
    FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    FString Filename = FString::Printf(TEXT("FEP_Results_%s.txt"), *Timestamp);
    FString FilePath = OutputDir + Filename;
    
    // Create directory if it doesn't exist
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*OutputDir))
    {
        PlatformFile.CreateDirectory(*OutputDir);
    }
    
    // Build output text
    FString Output;
    Output += TEXT("===========================================\n");
    Output += TEXT("Free Energy Perturbation (FEP) Results\n");
    Output += TEXT("===========================================\n\n");
    Output += FString::Printf(TEXT("Timestamp: %s\n\n"), *FDateTime::Now().ToString());
    
    Output += TEXT("BINDING FREE ENERGY:\n");
    Output += FString::Printf(TEXT("  ΔG_bind = %.3f ± %.3f kcal/mol\n"), 
                             Result.DeltaG, Result.StandardError);
    Output += FString::Printf(TEXT("  K_d = %.2f nM\n\n"), Result.BindingAffinity);
    
    Output += TEXT("ENERGY COMPONENTS:\n");
    Output += FString::Printf(TEXT("  Electrostatic: %.3f kcal/mol\n"), 
                             Result.ElectrostaticContribution);
    Output += FString::Printf(TEXT("  Van der Waals: %.3f kcal/mol\n"), 
                             Result.VdWContribution);
    Output += FString::Printf(TEXT("  Solvation: %.3f kcal/mol\n\n"), 
                             Result.SolvationContribution);
    
    Output += TEXT("LAMBDA WINDOWS:\n");
    Output += TEXT("  λ        <E>         <dH/dλ>      ±Error     Samples\n");
    Output += TEXT("  -------------------------------------------------------\n");
    
    for (const FFEPLambdaWindow& Window : Result.LambdaWindows)
    {
        Output += FString::Printf(TEXT("  %.3f    %8.2f    %8.2f    %6.2f    %6d\n"),
                                 Window.Lambda,
                                 Window.Energy,
                                 Window.dHdLambda,
                                 Window.StandardError,
                                 Window.SampleCount);
    }
    
    Output += TEXT("\n===========================================\n");
    
    // Write to file
    if (FFileHelper::SaveStringToFile(Output, *FilePath))
    {
        UE_LOG(LogTemp, Log, TEXT("FEPControlWidget: Results exported to %s"), *FilePath);
        
        if (ProgressText)
        {
            ProgressText->SetText(FText::FromString(TEXT("Results exported to Saved/FEP/")));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("FEPControlWidget: Failed to export results"));
    }
}
