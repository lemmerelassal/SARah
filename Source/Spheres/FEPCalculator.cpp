// FEPCalculator.cpp - Free Energy Perturbation Implementation
// Compatible with Unreal Engine 5.6

#include "FEPCalculator.h"
#include "FEPControlWidget.h"
#include "PDBViewer.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "Math/UnrealMathUtility.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"

// Physical constants
namespace FEPConstants
{
    constexpr float BOLTZMANN = 0.001987204f; // kcal/(mol·K)
    constexpr float COULOMB = 332.0636f; // e²·Å·kcal/mol (for electrostatics)
    constexpr float AVOGADRO = 6.02214076e23f;
    constexpr float GAS_CONSTANT = 1.987204e-3f; // kcal/(mol·K)
}

AFEPCalculator::AFEPCalculator()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    
    bIsCalculating = false;
    CurrentProgress = 0.0f;
    CurrentLambdaIndex = 0;
    CurrentStep = 0;
    CurrentQueueIndex = 0;
    PDBViewer = nullptr;
    bPMEInitialized = false;
}

void AFEPCalculator::BeginPlay()
{
    Super::BeginPlay();
    
    // Auto-initialize with PDBViewer
    InitializeWithPDBViewer();
    
    // Auto-spawn UI widget if enabled
    if (bAutoSpawnUI && ControlWidgetClass)
    {
        APlayerController* PC = GetWorld()->GetFirstPlayerController();
        if (PC)
        {
            ControlWidget = CreateWidget<UFEPControlWidget>(PC, ControlWidgetClass);
            if (ControlWidget)
            {
                ControlWidget->SetFEPCalculator(this);
                ControlWidget->SetPDBViewer(PDBViewer);
                ControlWidget->AddToViewport();
                
                UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Auto-spawned UI widget"));
            }
        }
    }
    
    // Auto-calculate if enabled
    if (bAutoCalculateOnBeginPlay)
    {
        // Delay slightly to ensure everything is initialized
        FTimerHandle DelayHandle;
        GetWorld()->GetTimerManager().SetTimer(DelayHandle, [this]()
        {
            if (bAutoCalculateVisibleOnly)
            {
                UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Starting auto-calculation for visible ligands"));
                CalculateVisibleLigands();
            }
            else if (!AutoCalculateLigandKey.IsEmpty())
            {
                UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Starting auto-calculation for %s"), *AutoCalculateLigandKey);
                CalculateBindingFreeEnergy(AutoCalculateLigandKey);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("FEPCalculator: Auto-calculate enabled but no ligand specified"));
            }
        }, 0.5f, false);
    }
}

void AFEPCalculator::InitializeWithPDBViewer()
{
    // Find PDBViewer in the level
    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APDBViewer::StaticClass(), FoundActors);
    
    if (FoundActors.Num() > 0)
    {
        PDBViewer = Cast<APDBViewer>(FoundActors[0]);
        if (PDBViewer)
        {
            UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Found and attached to PDBViewer"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("FEPCalculator: No PDBViewer found in level. Place a PDBViewer actor first."));
    }
}

void AFEPCalculator::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

void AFEPCalculator::CalculateBindingFreeEnergy(const FString& LigandKey)
{
    if (!PDBViewer)
    {
        UE_LOG(LogTemp, Error, TEXT("FEPCalculator: PDBViewer not initialized. Call InitializeWithPDBViewer() first."));
        return;
    }
    
    if (bIsCalculating)
    {
        UE_LOG(LogTemp, Warning, TEXT("FEPCalculator: Calculation already in progress"));
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Starting binding free energy calculation for ligand: %s"), *LigandKey);
    
    // Initialize
    bIsCalculating = true;
    CurrentProgress = 0.0f;
    CurrentLambdaIndex = 0;
    CurrentStep = 0;
    CollectedEnergies.Empty();
    CollecteddHdL.Empty();
    
    // Initialize system
    InitializeSystem(LigandKey);
    
    // Setup lambda windows
    LastResult.LambdaWindows.Empty();
    for (int32 i = 0; i < Parameters.NumLambdaWindows; ++i)
    {
        FFEPLambdaWindow Window;
        Window.Lambda = float(i) / float(Parameters.NumLambdaWindows - 1);
        LastResult.LambdaWindows.Add(Window);
    }
    
    // Start calculation with timer (async-like processing)
    UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Setting up timer for %d lambda windows..."), Parameters.NumLambdaWindows);
    
    if (!GetWorld())
    {
        UE_LOG(LogTemp, Error, TEXT("FEPCalculator: World is NULL! Cannot start timer."));
        return;
    }
    
    GetWorld()->GetTimerManager().SetTimer(
        CalculationTimerHandle,
        [this, LigandKey]()
        {
            UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Timer callback fired! Lambda %d/%d"), 
                   CurrentLambdaIndex + 1, Parameters.NumLambdaWindows);
            
            if (CurrentLambdaIndex < Parameters.NumLambdaWindows)
            {
                float Lambda = LastResult.LambdaWindows[CurrentLambdaIndex].Lambda;
                RunLambdaWindow(Lambda);
            }
            else
            {
                // All windows complete - finalize
                GetWorld()->GetTimerManager().ClearTimer(CalculationTimerHandle);
                
                // Calculate free energy
                LastResult.DeltaG = IntegrateFreeEnergy(LastResult.LambdaWindows);
                LastResult.BindingAffinity = CalculateBindingAffinityFromDeltaG(LastResult.DeltaG);
                LastResult.bCalculationSuccessful = true;
                
                bIsCalculating = false;
                CurrentProgress = 1.0f;
                
                // Store result
                AllResults.Add(LigandKey, LastResult);
                
                UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Calculation complete for %s!"), *LigandKey);
                UE_LOG(LogTemp, Log, TEXT("  ΔG_bind = %.2f ± %.2f kcal/mol"), 
                       LastResult.DeltaG, LastResult.StandardError);
                UE_LOG(LogTemp, Log, TEXT("  K_d = %.2f nM"), LastResult.BindingAffinity);
                
                // Auto-export if enabled
                if (bAutoExportResults)
                {
                    ExportResult(LigandKey, LastResult);
                }
                
                OnFEPComplete.Broadcast(LastResult);
                
                // Process next in queue if any
                ProcessNextInQueue();
            }
        },
        0.016f, // ~60 FPS
        true
    );
    
    UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Timer set successfully. Waiting for callbacks..."));
}

void AFEPCalculator::CalculateAllLigands()
{
    if (!PDBViewer)
    {
        UE_LOG(LogTemp, Error, TEXT("FEPCalculator: PDBViewer not initialized"));
        return;
    }
    
    if (bIsCalculating)
    {
        UE_LOG(LogTemp, Warning, TEXT("FEPCalculator: Calculation already in progress"));
        return;
    }
    
    // Get all ligand keys
    TArray<FString> LigandKeys;
    PDBViewer->LigandMap.GetKeys(LigandKeys);
    
    if (LigandKeys.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("FEPCalculator: No ligands found in PDBViewer"));
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Starting batch calculation for %d ligands"), LigandKeys.Num());
    
    // Setup queue
    CalculationQueue = LigandKeys;
    CurrentQueueIndex = 0;
    
    // Start first calculation
    if (CalculationQueue.Num() > 0)
    {
        CalculateBindingFreeEnergy(CalculationQueue[0]);
    }
}

void AFEPCalculator::CalculateVisibleLigands()
{
    if (!PDBViewer)
    {
        UE_LOG(LogTemp, Error, TEXT("FEPCalculator: PDBViewer not initialized"));
        return;
    }
    
    if (bIsCalculating)
    {
        UE_LOG(LogTemp, Warning, TEXT("FEPCalculator: Calculation already in progress"));
        return;
    }
    
    // Get visible ligands only
    TArray<FString> VisibleLigandKeys;
    for (auto& Pair : PDBViewer->LigandMap)
    {
        if (Pair.Value && Pair.Value->bIsVisible)
        {
            VisibleLigandKeys.Add(Pair.Key);
        }
    }
    
    if (VisibleLigandKeys.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("FEPCalculator: No visible ligands found"));
        UE_LOG(LogTemp, Log, TEXT("  Make sure to show at least one ligand in PDBViewer before calculating"));
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Starting calculation for %d visible ligand(s)"), VisibleLigandKeys.Num());
    
    // Setup queue
    CalculationQueue = VisibleLigandKeys;
    CurrentQueueIndex = 0;
    
    // Start first calculation
    if (CalculationQueue.Num() > 0)
    {
        CalculateBindingFreeEnergy(CalculationQueue[0]);
    }
}

void AFEPCalculator::ProcessNextInQueue()
{
    CurrentQueueIndex++;
    
    if (CurrentQueueIndex < CalculationQueue.Num())
    {
        UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Processing queue item %d/%d"), 
               CurrentQueueIndex + 1, CalculationQueue.Num());
        
        // Small delay between calculations
        FTimerHandle DelayHandle;
        GetWorld()->GetTimerManager().SetTimer(DelayHandle, [this]()
        {
            CalculateBindingFreeEnergy(CalculationQueue[CurrentQueueIndex]);
        }, 0.5f, false);
    }
    else
    {
        // Queue complete
        if (CalculationQueue.Num() > 0)
        {
            UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Batch calculation complete!"));
            UE_LOG(LogTemp, Log, TEXT("  Calculated %d ligands"), AllResults.Num());
            
            // Sort and display results
            TArray<FString> Keys;
            AllResults.GetKeys(Keys);
            
            Keys.Sort([this](const FString& A, const FString& B)
            {
                return AllResults[A].DeltaG < AllResults[B].DeltaG;
            });
            
            UE_LOG(LogTemp, Log, TEXT("  Ranking (best to worst):"));
            int32 Rank = 1;
            for (const FString& Key : Keys)
            {
                const FFEPResult& Result = AllResults[Key];
                UE_LOG(LogTemp, Log, TEXT("    %d. %s: ΔG = %.2f kcal/mol, Kd = %.2f nM"), 
                       Rank++, *Key, Result.DeltaG, Result.BindingAffinity);
            }
        }
        
        CalculationQueue.Empty();
        CurrentQueueIndex = 0;
    }
}

void AFEPCalculator::CalculateRelativeBindingFreeEnergy(const FString& LigandKeyA, 
                                                        const FString& LigandKeyB)
{
    if (!PDBViewer)
    {
        UE_LOG(LogTemp, Error, TEXT("FEPCalculator: PDBViewer not initialized"));
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Relative binding free energy calculation"));
    UE_LOG(LogTemp, Log, TEXT("  This will calculate ΔΔG = ΔG_B - ΔG_A"));
    
    // Add both to queue
    CalculationQueue.Empty();
    CalculationQueue.Add(LigandKeyA);
    CalculationQueue.Add(LigandKeyB);
    CurrentQueueIndex = 0;
    
    // Calculate first ligand
    CalculateBindingFreeEnergy(LigandKeyA);
    
    // After both complete, the results will be in AllResults
    // User can access via GetAllResults() or check logs for ranking
}

void AFEPCalculator::StopCalculation()
{
    if (bIsCalculating)
    {
        GetWorld()->GetTimerManager().ClearTimer(CalculationTimerHandle);
        bIsCalculating = false;
        
        LastResult.bCalculationSuccessful = false;
        LastResult.ErrorMessage = TEXT("Calculation stopped by user");
        
        UE_LOG(LogTemp, Warning, TEXT("FEPCalculator: Calculation stopped"));
    }
}

void AFEPCalculator::InitializeSystem(const FString& LigandKey)
{
    if (!PDBViewer)
    {
        UE_LOG(LogTemp, Error, TEXT("FEPCalculator: PDBViewer not set"));
        return;
    }
    
    CurrentState.Atoms.Empty();
    InitialProteinPositions.Empty();
    
    // Get ligand info
    FLigandInfo* Ligand = nullptr;
    if (PDBViewer->LigandMap.Contains(LigandKey))
    {
        Ligand = PDBViewer->LigandMap[LigandKey];
    }
    
    if (!Ligand)
    {
        UE_LOG(LogTemp, Error, TEXT("FEPCalculator: Ligand not found: %s"), *LigandKey);
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Initializing system with %d ligand atoms"), 
           Ligand->AtomPositions.Num());
    
    // Add ligand atoms
    for (int32 i = 0; i < Ligand->AtomPositions.Num(); ++i)
    {
        FAtomState Atom;
        Atom.Position = Ligand->AtomPositions[i];
        Atom.Element = Ligand->AtomElements.IsValidIndex(i) ? Ligand->AtomElements[i] : TEXT("C");
        Atom.bIsLigandAtom = true;
        
        SetAtomParameters(Atom, Atom.Element);
        
        // Initialize velocity from Maxwell-Boltzmann distribution
        float StdDev = FMath::Sqrt(FEPConstants::BOLTZMANN * Parameters.Temperature / Atom.Mass);
        Atom.Velocity.X = FMath::FRandRange(-StdDev, StdDev);
        Atom.Velocity.Y = FMath::FRandRange(-StdDev, StdDev);
        Atom.Velocity.Z = FMath::FRandRange(-StdDev, StdDev);
        
        CurrentState.Atoms.Add(Atom);
    }
    
    // Add nearby protein atoms (within cutoff of ligand)
    FVector LigandCenter = FVector::ZeroVector;
    for (const FVector& Pos : Ligand->AtomPositions)
    {
        LigandCenter += Pos;
    }
    LigandCenter /= Ligand->AtomPositions.Num();
    
    int32 ProteinAtomCount = 0;
    for (auto& ResPair : PDBViewer->ResidueMap)
    {
        FResidueInfo* Residue = ResPair.Value;
        if (!Residue) continue;
        
        for (int32 i = 0; i < Residue->AtomPositions.Num(); ++i)
        {
            FVector AtomPos = Residue->AtomPositions[i];
            float Distance = FVector::Dist(AtomPos, LigandCenter);
            
            // Include atoms within cutoff distance
            if (Distance < Parameters.CutoffDistance * 2.0f)
            {
                FAtomState Atom;
                Atom.Position = AtomPos;
                Atom.Element = Residue->AtomElements.IsValidIndex(i) ? 
                               Residue->AtomElements[i] : TEXT("C");
                Atom.bIsLigandAtom = false;
                
                SetAtomParameters(Atom, Atom.Element);
                
                // Protein atoms start at rest or with minimal velocity
                Atom.Velocity = FVector::ZeroVector;
                
                CurrentState.Atoms.Add(Atom);
                InitialProteinPositions.Add(AtomPos);
                ProteinAtomCount++;
            }
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("FEPCalculator: System initialized with %d total atoms (%d protein)"), 
           CurrentState.Atoms.Num(), ProteinAtomCount);
    
    // DIAGNOSTIC: Check for overlapping atoms
    int32 OverlapCount = 0;
    for (int32 i = 0; i < CurrentState.Atoms.Num(); ++i)
    {
        for (int32 j = i + 1; j < CurrentState.Atoms.Num(); ++j)
        {
            float Dist = FVector::Dist(CurrentState.Atoms[i].Position, CurrentState.Atoms[j].Position);
            if (Dist < 1.0f)  // Atoms closer than 1 Angstrom are overlapping
            {
                OverlapCount++;
                if (OverlapCount <= 10)  // Only log first 10
                {
                    UE_LOG(LogTemp, Warning, TEXT("  Overlapping atoms: %d and %d at distance %.3f Angstroms"), 
                           i, j, Dist);
                }
            }
        }
    }
    
    if (OverlapCount > 0)
    {
        UE_LOG(LogTemp, Error, TEXT("FEPCalculator: Found %d overlapping atom pairs! This will cause huge energies."), OverlapCount);
        UE_LOG(LogTemp, Error, TEXT("  Suggestion: Check PDB structure or run energy minimization first."));
    }
    
    // Perform energy minimization to relax overlaps
    if (bPerformEnergyMinimization)
    {
        UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Performing energy minimization..."));
        MinimizeEnergy();
    }
    
    // Initialize PME if enabled
    if (Parameters.bUseParticleMeshEwald)
    {
        UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Initializing Particle Mesh Ewald..."));
        InitializePME();
    }
    
    CurrentState.Temperature = Parameters.Temperature;
    CurrentState.Lambda = 0.0f;
}

void AFEPCalculator::ExportResult(const FString& LigandKey, const FFEPResult& Result)
{
    if (!Result.bCalculationSuccessful)
    {
        return;
    }
    
    // Create output directory
    FString OutputDir = FPaths::ProjectSavedDir() + TEXT("FEP/");
    FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    FString Filename = FString::Printf(TEXT("FEP_%s_%s.txt"), *LigandKey, *Timestamp);
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
    Output += FString::Printf(TEXT("Ligand: %s\n"), *LigandKey);
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
        UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Results exported to %s"), *FilePath);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("FEPCalculator: Failed to export results"));
    }
}

void AFEPCalculator::RunLambdaWindow(float Lambda)
{
    UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Starting lambda window %.3f"), Lambda);
    
    CurrentState.Lambda = Lambda;
    CollectedEnergies.Empty();
    CollecteddHdL.Empty();
    
    // Equilibration phase
    UE_LOG(LogTemp, Log, TEXT("  Starting equilibration (%d steps)..."), Parameters.EquilibrationSteps);
    double EquilStartTime = FPlatformTime::Seconds();
    
    for (int32 Step = 0; Step < Parameters.EquilibrationSteps; ++Step)
    {
        PerformMDStep(Lambda);
        
        if (Step % 1000 == 0)
        {
            float Progress = float(CurrentLambdaIndex) / float(Parameters.NumLambdaWindows);
            Progress += (float(Step) / float(Parameters.EquilibrationSteps)) / float(Parameters.NumLambdaWindows);
            CurrentProgress = Progress * 0.5f; // Equilibration is first 50%
            OnFEPProgress.Broadcast(CurrentProgress * 100.0f);
        }
    }
    
    double EquilTime = FPlatformTime::Seconds() - EquilStartTime;
    UE_LOG(LogTemp, Log, TEXT("  Equilibration complete in %.2f seconds"), EquilTime);
    
    // Production phase - collect data
    UE_LOG(LogTemp, Log, TEXT("  Starting production (%d steps)..."), Parameters.ProductionSteps);
    double ProdStartTime = FPlatformTime::Seconds();
    
    for (int32 Step = 0; Step < Parameters.ProductionSteps; ++Step)
    {
        PerformMDStep(Lambda);
        
        if (Step % Parameters.SamplingInterval == 0)
        {
            float Energy = CalculateTotalEnergy(Lambda);
            float dHdL = CalculatedHdLambda(Lambda);
            
            CollectedEnergies.Add(Energy);
            CollecteddHdL.Add(dHdL);
        }
        
        if (Step % 1000 == 0)
        {
            float Progress = float(CurrentLambdaIndex) / float(Parameters.NumLambdaWindows);
            Progress += (float(Step) / float(Parameters.ProductionSteps)) / float(Parameters.NumLambdaWindows);
            CurrentProgress = 0.5f + Progress * 0.5f; // Production is second 50%
            OnFEPProgress.Broadcast(CurrentProgress * 100.0f);
        }
    }
    
    double ProdTime = FPlatformTime::Seconds() - ProdStartTime;
    UE_LOG(LogTemp, Log, TEXT("  Production complete in %.2f seconds"), ProdTime);
    
    // Store results for this lambda window
    FFEPLambdaWindow& Window = LastResult.LambdaWindows[CurrentLambdaIndex];
    
    // Calculate average energy
    float SumEnergy = 0.0f;
    for (float E : CollectedEnergies)
    {
        SumEnergy += E;
    }
    Window.Energy = SumEnergy / CollectedEnergies.Num();
    
    // Calculate average dH/dλ
    float SumdHdL = 0.0f;
    for (float dH : CollecteddHdL)
    {
        SumdHdL += dH;
    }
    Window.dHdLambda = SumdHdL / CollecteddHdL.Num();
    Window.SampleCount = CollecteddHdL.Num();
    
    // Calculate standard error
    Window.StandardError = CalculateBlockAverageError(CollecteddHdL);
    
    UE_LOG(LogTemp, Log, TEXT("  Lambda %.3f complete: <E> = %.2f, <dH/dλ> = %.2f ± %.2f"), 
           Lambda, Window.Energy, Window.dHdLambda, Window.StandardError);
    
    CurrentLambdaIndex++;
}

void AFEPCalculator::PerformMDStep(float Lambda)
{
    // Velocity Verlet integration
    
    // Calculate forces
    CalculateForces(Lambda);
    
    // Update positions and velocities
    UpdatePositions();
    
    // Apply thermostat
    ApplyThermostat();
}

void AFEPCalculator::CalculateForces(float Lambda)
{
    // Zero forces
    for (FAtomState& Atom : CurrentState.Atoms)
    {
        Atom.Force = FVector::ZeroVector;
    }
    
    // Calculate non-bonded forces
    CalculateElectrostaticForces(Lambda);
    CalculateVanDerWaalsForces(Lambda);
    
    // Calculate bonded forces (bonds, angles)
    CalculateBondForces();
    
    // Apply restraints to protein atoms
    if (Parameters.bRestrainProtein)
    {
        ApplyRestraints();
    }
}

void AFEPCalculator::UpdatePositions()
{
    float dt = Parameters.TimeStep;
    
    for (FAtomState& Atom : CurrentState.Atoms)
    {
        // Update velocity: v(t+dt/2) = v(t) + F/m * dt/2
        FVector Acceleration = Atom.Force / Atom.Mass;
        Atom.Velocity += Acceleration * (dt * 0.5f);
        
        // Update position: r(t+dt) = r(t) + v(t+dt/2) * dt
        Atom.Position += Atom.Velocity * dt;
    }
}

void AFEPCalculator::ApplyThermostat()
{
    // Berendsen thermostat for temperature control
    
    // Calculate current kinetic energy
    float KE = 0.0f;
    for (const FAtomState& Atom : CurrentState.Atoms)
    {
        KE += 0.5f * Atom.Mass * Atom.Velocity.SizeSquared();
    }
    
    CurrentState.KineticEnergy = KE;
    
    // Calculate current temperature
    // KE = (3/2) * N * kB * T
    int32 DegreesOfFreedom = 3 * CurrentState.Atoms.Num();
    float CurrentTemp = (2.0f * KE) / (DegreesOfFreedom * FEPConstants::BOLTZMANN);
    CurrentState.Temperature = CurrentTemp;
    
    // Apply velocity scaling (Berendsen coupling)
    float TargetTemp = Parameters.Temperature;
    float TauT = 0.1f; // Coupling time constant (ps)
    float Lambda = FMath::Sqrt(1.0f + (Parameters.TimeStep / TauT) * ((TargetTemp / CurrentTemp) - 1.0f));
    
    for (FAtomState& Atom : CurrentState.Atoms)
    {
        Atom.Velocity *= Lambda;
    }
}

float AFEPCalculator::CalculateTotalEnergy(float Lambda)
{
    float Elec = CalculateElectrostaticEnergy(Lambda);
    float VdW = CalculateVanDerWaalsEnergy(Lambda);
    float Solv = Parameters.bCalculateSolvation ? CalculateSolvationEnergy(Lambda) : 0.0f;
    
    // Add bonded terms (bonds, angles, dihedrals)
    float Bond = CalculateBondEnergy();
    float Angle = CalculateAngleEnergy();
    float Dihedral = CalculateDihedralEnergy();
    
    float PE = Elec + VdW + Solv + Bond + Angle + Dihedral;
    
    // CRITICAL: Cap total potential energy to prevent numerical overflow
    if (FMath::Abs(PE) > 500000.0f)
    {
        UE_LOG(LogTemp, Error, TEXT("Total PE = %.2e kcal/mol exceeds limits! Capping at 500k."), PE);
        UE_LOG(LogTemp, Error, TEXT("  Elec=%.2e, VdW=%.2e, Bond=%.2e, Angle=%.2e"), Elec, VdW, Bond, Angle);
        PE = FMath::Sign(PE) * 500000.0f;
    }
    
    CurrentState.PotentialEnergy = PE;
    
    float TotalE = PE + CurrentState.KineticEnergy;
    
    // Also cap total energy
    if (FMath::Abs(TotalE) > 500000.0f)
    {
        TotalE = FMath::Sign(TotalE) * 500000.0f;
    }
    
    return TotalE;
}

float AFEPCalculator::CalculateElectrostaticEnergy(float Lambda)
{
    // Use PME if enabled and initialized
    if (Parameters.bUseParticleMeshEwald && bPMEInitialized)
    {
        return CalculatePMEElectrostatics(Lambda);
    }
    
    // Otherwise use reaction field or simple cutoff
    float Energy = 0.0f;
    int32 NumAtoms = CurrentState.Atoms.Num();
    
    // Reaction field parameters
    float Cutoff = Parameters.CutoffDistance;
    float CutoffSq = Cutoff * Cutoff;
    float DielectricIn = Parameters.DielectricConstant;  // Inside cutoff
    float DielectricOut = 80.0f;  // Outside cutoff (like water)
    
    // Reaction field constant: k_rf = (ε_out - ε_in) / ((2*ε_out + ε_in) * r_c^3)
    float krf = (DielectricOut - DielectricIn) / ((2.0f * DielectricOut + DielectricIn) * Cutoff * Cutoff * Cutoff);
    
    // Reaction field shift: c_rf = (3*ε_out) / ((2*ε_out + ε_in) * r_c)
    float crf = (3.0f * DielectricOut) / ((2.0f * DielectricOut + DielectricIn) * Cutoff);
    
    int32 PairsCalculated = 0;
    int32 PairsSkipped = 0;
    
    for (int32 i = 0; i < NumAtoms; ++i)
    {
        const FAtomState& Atom1 = CurrentState.Atoms[i];
        
        for (int32 j = i + 1; j < NumAtoms; ++j)
        {
            const FAtomState& Atom2 = CurrentState.Atoms[j];
            
            // Calculate distance squared first (faster)
            FVector Delta = Atom2.Position - Atom1.Position;
            float rSq = Delta.SizeSquared();
            
            // Skip if beyond cutoff
            if (rSq >= CutoffSq)
            {
                PairsSkipped++;
                continue;
            }
            
            float r = FMath::Sqrt(rSq);
            
            // CRITICAL: Prevent division by zero
            if (r < 1.5f)
            {
                r = 1.5f;
                rSq = r * r;
            }
            
            PairsCalculated++;
            
            // Scale ligand interactions by lambda
            float ScaleFactor = 1.0f;
            if (Atom1.bIsLigandAtom || Atom2.bIsLigandAtom)
            {
                if (Parameters.bUseSoftCore)
                {
                    Energy += SoftCoreCoulomb(r, Atom1.Charge, Atom2.Charge, Lambda);
                    continue;
                }
                else
                {
                    ScaleFactor = Lambda;
                }
            }
            
            // Reaction field corrected Coulomb energy
            // E = k * q1 * q2 * [1/r + k_rf * r^2 - c_rf] / ε_in
            float ChargeProduct = Atom1.Charge * Atom2.Charge;
            float E = FEPConstants::COULOMB * ChargeProduct * 
                     (1.0f / r + krf * rSq - crf) / DielectricIn;
            
            // Clamp individual pair energies
            if (FMath::Abs(E) > 1000.0f)
            {
                E = FMath::Sign(E) * 1000.0f;
            }
            
            Energy += ScaleFactor * E;
            
            // Cap total energy
            if (FMath::Abs(Energy) > 100000.0f)
            {
                UE_LOG(LogTemp, Warning, TEXT("Electrostatic energy exceeding limits! Capping at 100k kcal/mol"));
                UE_LOG(LogTemp, Warning, TEXT("  Calculated %d pairs, skipped %d pairs"), PairsCalculated, PairsSkipped);
                return FMath::Sign(Energy) * 100000.0f;
            }
        }
    }
    
    if (Parameters.bCalculateComponents)
    {
        LastResult.ElectrostaticContribution = Energy;
    }
    
    return Energy;
}

float AFEPCalculator::CalculateVanDerWaalsEnergy(float Lambda)
{
    float Energy = 0.0f;
    int32 NumAtoms = CurrentState.Atoms.Num();
    
    for (int32 i = 0; i < NumAtoms; ++i)
    {
        const FAtomState& Atom1 = CurrentState.Atoms[i];
        
        for (int32 j = i + 1; j < NumAtoms; ++j)
        {
            const FAtomState& Atom2 = CurrentState.Atoms[j];
            
            float r = FVector::Dist(Atom1.Position, Atom2.Position);
            
            // CRITICAL: Prevent division by zero or near-zero distances
            if (r < 1.5f)  // Minimum 1.5 Angstroms
            {
                r = 1.5f;
            }
            
            if (r < Parameters.CutoffDistance)
            {
                // Lennard-Jones parameters
                float Sigma = (Atom1.VdWRadius + Atom2.VdWRadius) * 0.5f;
                float Epsilon = FMath::Sqrt(Atom1.VdWEpsilon * Atom2.VdWEpsilon);
                
                // Scale ligand interactions by lambda
                float ScaleFactor = 1.0f;
                if (Atom1.bIsLigandAtom || Atom2.bIsLigandAtom)
                {
                    if (Parameters.bUseSoftCore)
                    {
                        Energy += SoftCoreLJ(r, Sigma, Epsilon, Lambda);
                        continue;
                    }
                    else
                    {
                        ScaleFactor = Lambda;
                    }
                }
                
                // Lennard-Jones 12-6 potential
                float Ratio = Sigma / r;
                float R6 = FMath::Pow(Ratio, 6.0f);
                float R12 = R6 * R6;
                float E = 4.0f * Epsilon * (R12 - R6);
                
                // Clamp individual pair energies
                if (FMath::Abs(E) > 1000.0f)
                {
                    E = FMath::Sign(E) * 1000.0f;
                }
                
                Energy += ScaleFactor * E;
                
                // Cap total energy
                if (FMath::Abs(Energy) > 100000.0f)
                {
                    UE_LOG(LogTemp, Warning, TEXT("VdW energy exceeding limits! Capping at 100k kcal/mol"));
                    return FMath::Sign(Energy) * 100000.0f;
                }
            }
        }
    }
    
    if (Parameters.bCalculateComponents)
    {
        LastResult.VdWContribution = Energy;
    }
    
    return Energy;
}

float AFEPCalculator::CalculateSolvationEnergy(float Lambda)
{
    // Simplified generalized Born solvation model
    float Energy = 0.0f;
    
    // For each ligand atom, estimate solvation penalty
    for (const FAtomState& Atom : CurrentState.Atoms)
    {
        if (!Atom.bIsLigandAtom) continue;
        
        // Estimate Born radius (simplified)
        float BornRadius = Atom.VdWRadius * 1.2f;
        
        // Self-energy term
        float SelfEnergy = -(FEPConstants::COULOMB * Atom.Charge * Atom.Charge) / 
                           (2.0f * BornRadius) * (1.0f / Parameters.DielectricConstant - 1.0f / 78.5f);
        
        Energy += Lambda * SelfEnergy;
    }
    
    if (Parameters.bCalculateComponents)
    {
        LastResult.SolvationContribution = Energy;
    }
    
    return Energy;
}

float AFEPCalculator::CalculatedHdLambda(float Lambda)
{
    // Numerical derivative: dH/dλ ≈ [H(λ+δ) - H(λ-δ)] / (2δ)
    float Delta = 0.001f;
    
    float EPlus = CalculateElectrostaticEnergy(Lambda + Delta) + 
                  CalculateVanDerWaalsEnergy(Lambda + Delta);
    float EMinus = CalculateElectrostaticEnergy(Lambda - Delta) + 
                   CalculateVanDerWaalsEnergy(Lambda - Delta);
    
    if (Parameters.bCalculateSolvation)
    {
        EPlus += CalculateSolvationEnergy(Lambda + Delta);
        EMinus += CalculateSolvationEnergy(Lambda - Delta);
    }
    
    return (EPlus - EMinus) / (2.0f * Delta);
}

void AFEPCalculator::CalculateElectrostaticForces(float Lambda)
{
    // Use PME if enabled and initialized
    if (Parameters.bUseParticleMeshEwald && bPMEInitialized)
    {
        CalculatePMEForces(Lambda);
        return;
    }
    
    // Otherwise use reaction field
    int32 NumAtoms = CurrentState.Atoms.Num();
    
    // Reaction field parameters (same as energy calculation)
    float Cutoff = Parameters.CutoffDistance;
    float CutoffSq = Cutoff * Cutoff;
    float DielectricIn = Parameters.DielectricConstant;
    float DielectricOut = 80.0f;
    
    float krf = (DielectricOut - DielectricIn) / ((2.0f * DielectricOut + DielectricIn) * Cutoff * Cutoff * Cutoff);
    
    for (int32 i = 0; i < NumAtoms; ++i)
    {
        FAtomState& Atom1 = CurrentState.Atoms[i];
        
        for (int32 j = i + 1; j < NumAtoms; ++j)
        {
            FAtomState& Atom2 = CurrentState.Atoms[j];
            
            FVector Delta = Atom2.Position - Atom1.Position;
            float rSq = Delta.SizeSquared();
            
            // Skip if beyond cutoff
            if (rSq >= CutoffSq)
            {
                continue;
            }
            
            float r = FMath::Sqrt(rSq);
            
            // Prevent numerical issues
            if (r < 1.5f)
            {
                r = 1.5f;
            }
            
            float ScaleFactor = 1.0f;
            if (Atom1.bIsLigandAtom || Atom2.bIsLigandAtom)
            {
                ScaleFactor = Lambda;
            }
            
            // Reaction field corrected force
            // F = -dE/dr = k * q1 * q2 * [-1/r^2 + 2*k_rf*r] / ε_in
            float ChargeProduct = Atom1.Charge * Atom2.Charge;
            float ForceMag = ScaleFactor * FEPConstants::COULOMB * ChargeProduct * 
                           (-1.0f / (r * r) + 2.0f * krf * r) / DielectricIn;
            
            // Cap force magnitude
            if (FMath::Abs(ForceMag) > 100.0f)
            {
                ForceMag = FMath::Sign(ForceMag) * 100.0f;
            }
            
            FVector Force = (Delta / r) * ForceMag;
            
            Atom1.Force -= Force;
            Atom2.Force += Force;
        }
    }
}

void AFEPCalculator::CalculateVanDerWaalsForces(float Lambda)
{
    int32 NumAtoms = CurrentState.Atoms.Num();
    
    for (int32 i = 0; i < NumAtoms; ++i)
    {
        FAtomState& Atom1 = CurrentState.Atoms[i];
        
        for (int32 j = i + 1; j < NumAtoms; ++j)
        {
            FAtomState& Atom2 = CurrentState.Atoms[j];
            
            FVector Delta = Atom2.Position - Atom1.Position;
            float r = Delta.Size();
            
            if (r < Parameters.CutoffDistance && r > 0.01f)
            {
                float Sigma = (Atom1.VdWRadius + Atom2.VdWRadius) * 0.5f;
                float Epsilon = FMath::Sqrt(Atom1.VdWEpsilon * Atom2.VdWEpsilon);
                
                float ScaleFactor = 1.0f;
                if (Atom1.bIsLigandAtom || Atom2.bIsLigandAtom)
                {
                    ScaleFactor = Lambda;
                }
                
                // F = -dE/dr for LJ potential
                float Ratio = Sigma / r;
                float R6 = FMath::Pow(Ratio, 6.0f);
                float R12 = R6 * R6;
                
                float ForceMag = ScaleFactor * 24.0f * Epsilon * (2.0f * R12 - R6) / r;
                
                FVector Force = (Delta / r) * ForceMag;
                
                Atom1.Force -= Force;
                Atom2.Force += Force;
            }
        }
    }
}

void AFEPCalculator::ApplyRestraints()
{
    int32 LigandAtomCount = 0;
    for (const FAtomState& Atom : CurrentState.Atoms)
    {
        if (Atom.bIsLigandAtom) LigandAtomCount++;
    }
    
    int32 ProteinIndex = 0;
    for (int32 i = LigandAtomCount; i < CurrentState.Atoms.Num(); ++i)
    {
        FAtomState& Atom = CurrentState.Atoms[i];
        
        if (ProteinIndex < InitialProteinPositions.Num())
        {
            // Harmonic restraint: F = -k * (r - r0)
            FVector Displacement = Atom.Position - InitialProteinPositions[ProteinIndex];
            FVector RestraintForce = -Parameters.ProteinRestraintForce * Displacement;
            
            Atom.Force += RestraintForce;
            ProteinIndex++;
        }
    }
}

float AFEPCalculator::SoftCoreLJ(float r, float sigma, float epsilon, float lambda)
{
    float Alpha = Parameters.SoftCoreAlpha;
    float SigmaSq = sigma * sigma;
    float rSoft = FMath::Pow(Alpha * SigmaSq * (1.0f - lambda) + r * r, 0.5f);
    
    float Ratio = sigma / rSoft;
    float R6 = FMath::Pow(Ratio, 6.0f);
    float R12 = R6 * R6;
    
    return lambda * 4.0f * epsilon * (R12 - R6);
}

float AFEPCalculator::SoftCoreCoulomb(float r, float q1, float q2, float lambda)
{
    float Alpha = Parameters.SoftCoreAlpha;
    float rSoft = FMath::Pow(Alpha * (1.0f - lambda) + r * r, 0.5f);
    
    return lambda * (FEPConstants::COULOMB * q1 * q2) / (Parameters.DielectricConstant * rSoft);
}

float AFEPCalculator::IntegrateFreeEnergy(const TArray<FFEPLambdaWindow>& Windows)
{
    // Trapezoidal rule integration: ΔG = ∫(dH/dλ)dλ
    float DeltaG = 0.0f;
    float ErrorSum = 0.0f;
    
    for (int32 i = 0; i < Windows.Num() - 1; ++i)
    {
        float dLambda = Windows[i + 1].Lambda - Windows[i].Lambda;
        float AvgdHdL = (Windows[i].dHdLambda + Windows[i + 1].dHdLambda) * 0.5f;
        
        DeltaG += AvgdHdL * dLambda;
        
        // Propagate errors
        float AvgError = (Windows[i].StandardError + Windows[i + 1].StandardError) * 0.5f;
        ErrorSum += (AvgError * dLambda) * (AvgError * dLambda);
    }
    
    LastResult.StandardError = FMath::Sqrt(ErrorSum);
    
    return DeltaG;
}

float AFEPCalculator::CalculateBindingAffinityFromDeltaG(float DeltaG)
{
    // ΔG = -RT ln(K_a) = RT ln(K_d)
    // K_d = exp(ΔG / RT)
    
    float RT = FEPConstants::GAS_CONSTANT * Parameters.Temperature; // kcal/mol
    float Kd = FMath::Exp(DeltaG / RT); // Dissociation constant in M
    
    // Convert to nM
    float KdNanoMolar = Kd * 1.0e9f;
    
    return KdNanoMolar;
}

void AFEPCalculator::SetAtomParameters(FAtomState& Atom, const FString& Element)
{
    Atom.Mass = GetAtomicMass(Element);
    Atom.VdWRadius = GetVdWRadius(Element);
    Atom.VdWEpsilon = GetVdWEpsilon(Element);
    
    // Simplified charge assignment (should use force field)
    if (Element == TEXT("N") || Element == TEXT("O"))
    {
        Atom.Charge = -0.5f;
    }
    else if (Element == TEXT("H"))
    {
        Atom.Charge = 0.3f;
    }
    else if (Element == TEXT("C"))
    {
        Atom.Charge = 0.1f;
    }
    else
    {
        Atom.Charge = 0.0f;
    }
}

float AFEPCalculator::GetAtomicMass(const FString& Element)
{
    static TMap<FString, float> Masses = {
        {TEXT("H"), 1.008f},
        {TEXT("C"), 12.011f},
        {TEXT("N"), 14.007f},
        {TEXT("O"), 15.999f},
        {TEXT("F"), 18.998f},
        {TEXT("P"), 30.974f},
        {TEXT("S"), 32.065f},
        {TEXT("Cl"), 35.453f},
        {TEXT("Br"), 79.904f},
        {TEXT("I"), 126.904f}
    };
    
    return Masses.Contains(Element) ? Masses[Element] : 12.0f;
}

float AFEPCalculator::GetVdWRadius(const FString& Element)
{
    static TMap<FString, float> Radii = {
        {TEXT("H"), 1.2f},
        {TEXT("C"), 1.7f},
        {TEXT("N"), 1.55f},
        {TEXT("O"), 1.52f},
        {TEXT("F"), 1.47f},
        {TEXT("P"), 1.8f},
        {TEXT("S"), 1.8f},
        {TEXT("Cl"), 1.75f},
        {TEXT("Br"), 1.85f}
    };
    
    return Radii.Contains(Element) ? Radii[Element] : 1.7f;
}

float AFEPCalculator::GetVdWEpsilon(const FString& Element)
{
    // OPLS-AA epsilon values (kcal/mol)
    static TMap<FString, float> Epsilons = {
        {TEXT("H"), 0.03f},
        {TEXT("C"), 0.07f},
        {TEXT("N"), 0.17f},
        {TEXT("O"), 0.21f},
        {TEXT("F"), 0.061f},
        {TEXT("P"), 0.2f},
        {TEXT("S"), 0.25f},
        {TEXT("Cl"), 0.265f}
    };
    
    return Epsilons.Contains(Element) ? Epsilons[Element] : 0.1f;
}

float AFEPCalculator::CalculateBlockAverageError(const TArray<float>& Data)
{
    if (Data.Num() < 10) return 0.0f;
    
    // Block averaging for error estimation
    int32 BlockSize = FMath::Max(1, Data.Num() / 10);
    TArray<float> BlockAverages;
    
    for (int32 i = 0; i < Data.Num(); i += BlockSize)
    {
        float Sum = 0.0f;
        int32 Count = 0;
        
        for (int32 j = i; j < FMath::Min(i + BlockSize, Data.Num()); ++j)
        {
            Sum += Data[j];
            Count++;
        }
        
        if (Count > 0)
        {
            BlockAverages.Add(Sum / Count);
        }
    }
    
    // Calculate standard deviation of block averages
    float Mean = 0.0f;
    for (float Val : BlockAverages)
    {
        Mean += Val;
    }
    Mean /= BlockAverages.Num();
    
    float Variance = 0.0f;
    for (float Val : BlockAverages)
    {
        float Diff = Val - Mean;
        Variance += Diff * Diff;
    }
    Variance /= (BlockAverages.Num() - 1);
    
    // Standard error
    return FMath::Sqrt(Variance / BlockAverages.Num());
}

// Visualization methods
void AFEPCalculator::VisualizeEnergyLandscape(const FFEPResult& Result)
{
    ClearVisualization();
    
    UE_LOG(LogTemp, Log, TEXT("FEPCalculator: Visualizing energy landscape..."));
    
    // Create visualization showing lambda windows
    // This would create meshes showing the free energy profile
    // Implementation depends on your visualization preferences
}

void AFEPCalculator::ClearVisualization()
{
    for (UStaticMeshComponent* Mesh : VisualizationMeshes)
    {
        if (Mesh)
        {
            Mesh->DestroyComponent();
        }
    }
    VisualizationMeshes.Empty();
}

float AFEPCalculator::CalculateBondEnergy()
{
    // Harmonic bond stretching energy: E = k * (r - r0)^2
    // This requires bond topology which we don't have from PDB
    // For now, we'll use a simple distance-based approach for nearby atoms
    
    float Energy = 0.0f;
    
    // We need bond topology data which isn't available from PDB parsing
    // This is a limitation - proper implementation would require:
    // 1. Bond list from topology file (PSF, PRMTOP, etc.)
    // 2. Force constants and equilibrium distances from force field
    
    // For a quick approximation, assume atoms within 1.0-2.0 Å are bonded
    // This is NOT accurate and should be replaced with proper topology
    
    int32 NumAtoms = CurrentState.Atoms.Num();
    for (int32 i = 0; i < NumAtoms; ++i)
    {
        const FAtomState& Atom1 = CurrentState.Atoms[i];
        
        for (int32 j = i + 1; j < NumAtoms; ++j)
        {
            const FAtomState& Atom2 = CurrentState.Atoms[j];
            
            float r = FVector::Dist(Atom1.Position, Atom2.Position);
            
            // Approximate bond detection: 1.0 - 2.0 Angstroms
            if (r > 1.0f && r < 2.0f)
            {
                // Typical values:
                // k_bond ~= 300-500 kcal/mol/Å²
                // r0 depends on bond type (C-C: 1.54Å, C=C: 1.34Å, C-N: 1.47Å, etc.)
                
                float k_bond = 400.0f;  // kcal/mol/Å²
                float r0 = 1.5f;  // Approximate equilibrium bond length
                
                // If both atoms are from same residue/ligand, likely bonded
                if (Atom1.bIsLigandAtom == Atom2.bIsLigandAtom)
                {
                    float dr = r - r0;
                    Energy += 0.5f * k_bond * dr * dr;
                }
            }
        }
    }
    
    // Cap bond energy
    if (FMath::Abs(Energy) > 50000.0f)
    {
        Energy = FMath::Sign(Energy) * 50000.0f;
    }
    
    return Energy;
}

float AFEPCalculator::CalculateAngleEnergy()
{
    // Harmonic angle bending energy: E = k * (θ - θ0)^2
    // Requires topology data for angle triplets (atom i-j-k)
    
    float Energy = 0.0f;
    
    // Similar to bonds, we need topology data
    // For approximation, find triplets where atoms are bonded
    
    int32 NumAtoms = CurrentState.Atoms.Num();
    
    for (int32 i = 0; i < NumAtoms; ++i)
    {
        const FAtomState& Atom1 = CurrentState.Atoms[i];
        
        for (int32 j = i + 1; j < NumAtoms; ++j)
        {
            const FAtomState& Atom2 = CurrentState.Atoms[j];
            
            float r_ij = FVector::Dist(Atom1.Position, Atom2.Position);
            
            // If i-j are bonded (approximate)
            if (r_ij > 1.0f && r_ij < 2.0f)
            {
                // Look for third atom k bonded to j
                for (int32 k = j + 1; k < NumAtoms; ++k)
                {
                    const FAtomState& Atom3 = CurrentState.Atoms[k];
                    
                    float r_jk = FVector::Dist(Atom2.Position, Atom3.Position);
                    
                    // If j-k are also bonded, we have angle i-j-k
                    if (r_jk > 1.0f && r_jk < 2.0f)
                    {
                        // Same residue/ligand check
                        if (Atom1.bIsLigandAtom == Atom2.bIsLigandAtom && 
                            Atom2.bIsLigandAtom == Atom3.bIsLigandAtom)
                        {
                            // Calculate angle using dot product
                            FVector v1 = (Atom1.Position - Atom2.Position).GetSafeNormal();
                            FVector v2 = (Atom3.Position - Atom2.Position).GetSafeNormal();
                            
                            float CosTheta = FVector::DotProduct(v1, v2);
                            CosTheta = FMath::Clamp(CosTheta, -1.0f, 1.0f);
                            float Theta = FMath::Acos(CosTheta);  // radians
                            
                            // Typical values:
                            // k_angle ~= 50-100 kcal/mol/rad²
                            // θ0 depends on hybridization (sp3: 109.5°, sp2: 120°, sp: 180°)
                            
                            float k_angle = 70.0f;  // kcal/mol/rad²
                            float Theta0 = 1.911f;  // ~109.5° in radians (tetrahedral)
                            
                            float dTheta = Theta - Theta0;
                            Energy += 0.5f * k_angle * dTheta * dTheta;
                        }
                    }
                }
            }
        }
    }
    
    // Cap angle energy
    if (FMath::Abs(Energy) > 50000.0f)
    {
        Energy = FMath::Sign(Energy) * 50000.0f;
    }
    
    return Energy;
}

float AFEPCalculator::CalculateDihedralEnergy()
{
    // Dihedral torsion energy: E = sum[Vn/2 * (1 + cos(n*φ - γ))]
    // Requires topology data for dihedral quartets (atoms i-j-k-l)
    
    float Energy = 0.0f;
    
    int32 NumAtoms = CurrentState.Atoms.Num();
    
    // Find dihedral quartets (i-j-k-l where i-j, j-k, k-l are bonded)
    for (int32 i = 0; i < NumAtoms; ++i)
    {
        const FAtomState& Atom1 = CurrentState.Atoms[i];
        
        for (int32 j = i + 1; j < NumAtoms; ++j)
        {
            const FAtomState& Atom2 = CurrentState.Atoms[j];
            
            float r_ij = FVector::Dist(Atom1.Position, Atom2.Position);
            if (r_ij < 1.0f || r_ij > 2.0f) continue;  // Not bonded
            
            for (int32 k = 0; k < NumAtoms; ++k)
            {
                if (k == i || k == j) continue;
                
                const FAtomState& Atom3 = CurrentState.Atoms[k];
                
                float r_jk = FVector::Dist(Atom2.Position, Atom3.Position);
                if (r_jk < 1.0f || r_jk > 2.0f) continue;  // Not bonded
                
                for (int32 l = k + 1; l < NumAtoms; ++l)
                {
                    if (l == i || l == j) continue;
                    
                    const FAtomState& Atom4 = CurrentState.Atoms[l];
                    
                    float r_kl = FVector::Dist(Atom3.Position, Atom4.Position);
                    if (r_kl < 1.0f || r_kl > 2.0f) continue;  // Not bonded
                    
                    // Check same residue/ligand
                    if (Atom1.bIsLigandAtom == Atom2.bIsLigandAtom && 
                        Atom2.bIsLigandAtom == Atom3.bIsLigandAtom &&
                        Atom3.bIsLigandAtom == Atom4.bIsLigandAtom)
                    {
                        // Calculate dihedral angle using cross products
                        FVector b1 = Atom2.Position - Atom1.Position;
                        FVector b2 = Atom3.Position - Atom2.Position;
                        FVector b3 = Atom4.Position - Atom3.Position;
                        
                        FVector n1 = FVector::CrossProduct(b1, b2).GetSafeNormal();
                        FVector n2 = FVector::CrossProduct(b2, b3).GetSafeNormal();
                        
                        float CosPhi = FVector::DotProduct(n1, n2);
                        CosPhi = FMath::Clamp(CosPhi, -1.0f, 1.0f);
                        
                        // Get sign of dihedral
                        FVector m1 = FVector::CrossProduct(n1, b2.GetSafeNormal());
                        float Sign = FVector::DotProduct(m1, n2) < 0.0f ? -1.0f : 1.0f;
                        
                        float Phi = Sign * FMath::Acos(CosPhi);  // Dihedral angle in radians
                        
                        // Typical dihedral parameters (simplified)
                        // V1, V2, V3 are barrier heights for n=1,2,3 periodicity
                        // γ is phase angle
                        
                        // For simplicity, use a 3-fold barrier (common for C-C rotation)
                        float V3 = 1.5f;  // kcal/mol (barrier height)
                        float n = 3.0f;   // 3-fold periodicity
                        float gamma = 0.0f;  // phase angle
                        
                        Energy += (V3 / 2.0f) * (1.0f + FMath::Cos(n * Phi - gamma));
                    }
                }
            }
        }
    }
    
    // Cap dihedral energy
    if (FMath::Abs(Energy) > 50000.0f)
    {
        Energy = FMath::Sign(Energy) * 50000.0f;
    }
    
    return Energy;
}

void AFEPCalculator::CalculateBondForces()
{
    // Calculate forces from bonded interactions
    // F = -dE/dr for bonds, angles, dihedrals
    
    int32 NumAtoms = CurrentState.Atoms.Num();
    
    // Bond forces: F = -k * (r - r0) * (r_vec / r)
    for (int32 i = 0; i < NumAtoms; ++i)
    {
        FAtomState& Atom1 = CurrentState.Atoms[i];
        
        for (int32 j = i + 1; j < NumAtoms; ++j)
        {
            FAtomState& Atom2 = CurrentState.Atoms[j];
            
            FVector Delta = Atom2.Position - Atom1.Position;
            float r = Delta.Size();
            
            // Bond detection (approximate)
            if (r > 1.0f && r < 2.0f)
            {
                if (Atom1.bIsLigandAtom == Atom2.bIsLigandAtom)
                {
                    float k_bond = 400.0f;  // kcal/mol/Å²
                    float r0 = 1.5f;
                    
                    // Force magnitude: F = -k * (r - r0)
                    float ForceMag = -k_bond * (r - r0);
                    
                    // Force direction: along bond
                    FVector ForceDir = Delta / r;  // Normalized
                    FVector Force = ForceDir * ForceMag;
                    
                    // Apply equal and opposite forces
                    Atom1.Force -= Force;
                    Atom2.Force += Force;
                }
            }
        }
    }
    
    // Angle forces: F = -k * (θ - θ0) * dθ/dr
    for (int32 i = 0; i < NumAtoms; ++i)
    {
        FAtomState& Atom1 = CurrentState.Atoms[i];
        
        for (int32 j = i + 1; j < NumAtoms; ++j)
        {
            FAtomState& Atom2 = CurrentState.Atoms[j];
            
            float r_ij = FVector::Dist(Atom1.Position, Atom2.Position);
            
            if (r_ij > 1.0f && r_ij < 2.0f)
            {
                for (int32 k = j + 1; k < NumAtoms; ++k)
                {
                    FAtomState& Atom3 = CurrentState.Atoms[k];
                    
                    float r_jk = FVector::Dist(Atom2.Position, Atom3.Position);
                    
                    if (r_jk > 1.0f && r_jk < 2.0f)
                    {
                        if (Atom1.bIsLigandAtom == Atom2.bIsLigandAtom && 
                            Atom2.bIsLigandAtom == Atom3.bIsLigandAtom)
                        {
                            FVector v1 = Atom1.Position - Atom2.Position;
                            FVector v2 = Atom3.Position - Atom2.Position;
                            
                            float r1 = v1.Size();
                            float r2 = v2.Size();
                            
                            if (r1 > 0.01f && r2 > 0.01f)
                            {
                                FVector u1 = v1 / r1;
                                FVector u2 = v2 / r2;
                                
                                float CosTheta = FVector::DotProduct(u1, u2);
                                CosTheta = FMath::Clamp(CosTheta, -1.0f, 1.0f);
                                float Theta = FMath::Acos(CosTheta);
                                
                                float k_angle = 70.0f;
                                float Theta0 = 1.911f;
                                float dTheta = Theta - Theta0;
                                
                                if (FMath::Abs(dTheta) > 0.001f)
                                {
                                    // Simplified angle force
                                    float ForceMag = -k_angle * dTheta;
                                    
                                    // Force perpendicular to bonds
                                    FVector F1 = (u2 - u1 * CosTheta) * (ForceMag / r1);
                                    FVector F3 = (u1 - u2 * CosTheta) * (ForceMag / r2);
                                    
                                    Atom1.Force += F1;
                                    Atom3.Force += F3;
                                    Atom2.Force -= (F1 + F3);  // Newton's 3rd law
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Note: Dihedral forces are complex - skipping for now
    // They require careful chain rule application and cross product derivatives
}

void AFEPCalculator::MinimizeEnergy()
{
    // Steepest descent energy minimization with adaptive step size
    // Designed to handle severe atomic overlaps
    
    double StartTime = FPlatformTime::Seconds();
    
    // Calculate initial energy (might be capped)
    float InitialEnergy = CalculateTotalEnergy(0.0f);
    
    UE_LOG(LogTemp, Log, TEXT("  Starting energy minimization:"));
    UE_LOG(LogTemp, Log, TEXT("    Initial energy: %.2f kcal/mol"), InitialEnergy);
    UE_LOG(LogTemp, Log, TEXT("    Max steps: %d"), MinimizationMaxSteps);
    UE_LOG(LogTemp, Log, TEXT("    Force threshold: %.2f kcal/mol/Å"), MinimizationForceThreshold);
    UE_LOG(LogTemp, Log, TEXT("    Initial step size: %.4f Å"), MinimizationStepSize);
    
    int32 Step = 0;
    float MaxForce = 0.0f;
    float CurrentStepSize = MinimizationStepSize;
    float PrevEnergy = InitialEnergy;
    int32 EnergyIncreaseCount = 0;
    
    for (Step = 0; Step < MinimizationMaxSteps; ++Step)
    {
        // Calculate forces at current positions
        CalculateForces(0.0f);  // Use lambda = 0 for minimization
        
        // Calculate maximum force magnitude
        MaxForce = CalculateMaxForce();
        
        // Check convergence
        if (MaxForce < MinimizationForceThreshold)
        {
            UE_LOG(LogTemp, Log, TEXT("  Minimization converged at step %d"), Step);
            break;
        }
        
        // Cap forces to prevent atoms flying away
        float MaxAllowedForce = 1000.0f;  // kcal/mol/Å
        bool ForcesWereCapped = false;
        
        for (FAtomState& Atom : CurrentState.Atoms)
        {
            float ForceMag = Atom.Force.Size();
            if (ForceMag > MaxAllowedForce)
            {
                Atom.Force = (Atom.Force / ForceMag) * MaxAllowedForce;
                ForcesWereCapped = true;
            }
        }
        
        if (ForcesWereCapped && Step % 100 == 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("    Step %d: Forces capped (severe overlaps detected)"), Step);
        }
        
        // Store old positions for rollback
        TArray<FVector> OldPositions;
        OldPositions.Reserve(CurrentState.Atoms.Num());
        for (const FAtomState& Atom : CurrentState.Atoms)
        {
            OldPositions.Add(Atom.Position);
        }
        
        // Steepest descent: move in direction of force
        for (int32 i = 0; i < CurrentState.Atoms.Num(); ++i)
        {
            FAtomState& Atom = CurrentState.Atoms[i];
            
            // Only move ligand atoms unless explicitly allowed to move protein
            if (Atom.bIsLigandAtom || bMoveProteinDuringMinimization)
            {
                float ForceMag = Atom.Force.Size();
                if (ForceMag > 0.01f)  // Avoid division by zero
                {
                    // Normalize force and scale by step size
                    FVector StepVector = (Atom.Force / ForceMag) * CurrentStepSize;
                    Atom.Position += StepVector;
                }
            }
        }
        
        // Calculate new energy
        float NewEnergy = CalculateTotalEnergy(0.0f);
        
        // Line search: if energy increased, reduce step size and rollback
        if (NewEnergy > PrevEnergy * 1.01f)  // Allow 1% increase for numerical noise
        {
            // Rollback positions
            for (int32 i = 0; i < CurrentState.Atoms.Num(); ++i)
            {
                CurrentState.Atoms[i].Position = OldPositions[i];
            }
            
            // Reduce step size significantly
            CurrentStepSize *= 0.5f;
            EnergyIncreaseCount++;
            
            if (CurrentStepSize < 0.00001f)
            {
                UE_LOG(LogTemp, Warning, TEXT("  Minimization step size too small (%.2e Å), stopping"), CurrentStepSize);
                break;
            }
            
            if (EnergyIncreaseCount > 20 && Step < 100)
            {
                UE_LOG(LogTemp, Error, TEXT("  Minimization failing to make progress (step %d)"), Step);
                UE_LOG(LogTemp, Error, TEXT("  Structure may be too badly distorted - try different PDB"));
                break;
            }
        }
        else
        {
            // Energy decreased - good!
            PrevEnergy = NewEnergy;
            EnergyIncreaseCount = 0;
            
            // Gradually increase step size if making good progress
            float EnergyChange = PrevEnergy - NewEnergy;
            if (EnergyChange > 1000.0f)  // Significant improvement
            {
                CurrentStepSize = FMath::Min(CurrentStepSize * 1.1f, 0.1f);  // Cap at 0.1 Å
            }
        }
        
        // Log progress every 100 steps or if energy is improving rapidly
        if (Step % 100 == 0 || (Step < 500 && Step % 50 == 0))
        {
            UE_LOG(LogTemp, Log, TEXT("    Step %d: E = %.2f kcal/mol, MaxF = %.2f kcal/mol/Å, StepSize = %.4f Å"), 
                   Step, NewEnergy, MaxForce, CurrentStepSize);
        }
    }
    
    float FinalEnergy = CalculateTotalEnergy(0.0f);
    double MinTime = FPlatformTime::Seconds() - StartTime;
    
    UE_LOG(LogTemp, Log, TEXT("  Minimization complete in %.2f seconds"), MinTime);
    UE_LOG(LogTemp, Log, TEXT("    Final energy: %.2f kcal/mol (was %.2f)"), FinalEnergy, InitialEnergy);
    
    if (FMath::Abs(InitialEnergy) > 400000.0f || FMath::Abs(FinalEnergy) > 400000.0f)
    {
        UE_LOG(LogTemp, Warning, TEXT("    WARNING: Energies are capped - structure still has severe overlaps!"));
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("    Energy change: %.2f kcal/mol"), FinalEnergy - InitialEnergy);
    }
    
    UE_LOG(LogTemp, Log, TEXT("    Final max force: %.2f kcal/mol/Å"), MaxForce);
    UE_LOG(LogTemp, Log, TEXT("    Completed %d steps"), Step);
    
    // Check for overlaps after minimization
    int32 OverlapCount = 0;
    for (int32 i = 0; i < CurrentState.Atoms.Num(); ++i)
    {
        for (int32 j = i + 1; j < CurrentState.Atoms.Num(); ++j)
        {
            float Dist = FVector::Dist(CurrentState.Atoms[i].Position, CurrentState.Atoms[j].Position);
            if (Dist < 1.0f)
            {
                OverlapCount++;
            }
        }
    }
    
    if (OverlapCount > 0)
    {
        UE_LOG(LogTemp, Error, TEXT("  Still have %d overlapping atoms after minimization!"), OverlapCount);
        UE_LOG(LogTemp, Error, TEXT("  This PDB structure is severely distorted."));
        UE_LOG(LogTemp, Error, TEXT("  Try: 1) Different PDB, 2) Increase MaxSteps to 10000, 3) Use external minimizer"));
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("  ✓ No overlapping atoms after minimization!"));
    }
}

float AFEPCalculator::CalculateMaxForce()
{
    // Find the maximum force magnitude on any atom
    float MaxForce = 0.0f;
    
    for (const FAtomState& Atom : CurrentState.Atoms)
    {
        float ForceMag = Atom.Force.Size();
        if (ForceMag > MaxForce)
        {
            MaxForce = ForceMag;
        }
    }
    
    return MaxForce;
}

void AFEPCalculator::InitializePME()
{
    // Set up PME grid based on system size
    GridNx = Parameters.PMEGridSize;
    GridNy = Parameters.PMEGridSize;
    GridNz = Parameters.PMEGridSize;
    
    // Calculate bounding box of all atoms
    FVector MinPos = CurrentState.Atoms[0].Position;
    FVector MaxPos = CurrentState.Atoms[0].Position;
    
    for (const FAtomState& Atom : CurrentState.Atoms)
    {
        MinPos = MinPos.ComponentMin(Atom.Position);
        MaxPos = MaxPos.ComponentMax(Atom.Position);
    }
    
    // Add padding (10 Angstroms on each side)
    MinPos -= FVector(10.0f);
    MaxPos += FVector(10.0f);
    
    GridOrigin = MinPos;
    FVector GridSize = MaxPos - MinPos;
    GridSpacing = FVector(GridSize.X / GridNx, GridSize.Y / GridNy, GridSize.Z / GridNz);
    
    // Allocate grids
    int32 TotalGridPoints = GridNx * GridNy * GridNz;
    ChargeGrid.SetNumZeroed(TotalGridPoints);
    ForceGrid.SetNumZeroed(TotalGridPoints);
    
    bPMEInitialized = true;
    
    UE_LOG(LogTemp, Log, TEXT("  PME grid: %dx%dx%d = %d points"), GridNx, GridNy, GridNz, TotalGridPoints);
    UE_LOG(LogTemp, Log, TEXT("  Grid spacing: %.3f x %.3f x %.3f Angstroms"), 
           GridSpacing.X, GridSpacing.Y, GridSpacing.Z);
    UE_LOG(LogTemp, Log, TEXT("  Alpha parameter: %.3f"), Parameters.PMEAlpha);
}

float AFEPCalculator::CalculatePMEElectrostatics(float Lambda)
{
    // PME splits electrostatics into:
    // E_total = E_real (short-range, direct) + E_reciprocal (long-range, FFT)
    
    float EnergyReal = 0.0f;
    float EnergyReciprocal = 0.0f;
    float EnergySelf = 0.0f;
    
    float Alpha = Parameters.PMEAlpha;
    float CutoffSq = Parameters.CutoffDistance * Parameters.CutoffDistance;
    
    // PART 1: Real-space (short-range) contribution
    // Uses complementary error function erfc(α*r) for smooth cutoff
    int32 NumAtoms = CurrentState.Atoms.Num();
    
    for (int32 i = 0; i < NumAtoms; ++i)
    {
        const FAtomState& Atom1 = CurrentState.Atoms[i];
        
        for (int32 j = i + 1; j < NumAtoms; ++j)
        {
            const FAtomState& Atom2 = CurrentState.Atoms[j];
            
            FVector Delta = Atom2.Position - Atom1.Position;
            float rSq = Delta.SizeSquared();
            
            if (rSq >= CutoffSq) continue;
            
            float r = FMath::Sqrt(rSq);
            if (r < 0.5f) r = 0.5f;  // Safety
            
            float ScaleFactor = 1.0f;
            if (Atom1.bIsLigandAtom || Atom2.bIsLigandAtom)
            {
                ScaleFactor = Lambda;
            }
            
            // erfc(α*r) / r - complementary error function
            float AlphaR = Alpha * r;
            float Erfc = FMath::Exp(-AlphaR * AlphaR) * (1.0f - FMath::Clamp(AlphaR, 0.0f, 1.0f));  // Approximation
            
            float E = ScaleFactor * FEPConstants::COULOMB * Atom1.Charge * Atom2.Charge * Erfc / r;
            
            if (FMath::Abs(E) > 1000.0f)
            {
                E = FMath::Sign(E) * 1000.0f;
            }
            
            EnergyReal += E;
        }
    }
    
    // PART 2: Reciprocal-space (long-range) contribution
    // Normally uses FFT, but we'll use a simplified grid-based approach
    
    // Spread charges to grid
    SpreadChargesToGrid();
    
    // Solve Poisson equation (simplified - normally FFT)
    // ∇²φ = -4πρ
    // For now, use simple finite difference
    SolvePoisson3D();
    
    // Calculate reciprocal space energy
    for (int32 i = 0; i < NumAtoms; ++i)
    {
        const FAtomState& Atom = CurrentState.Atoms[i];
        
        // Find grid point
        FVector RelPos = (Atom.Position - GridOrigin) / GridSpacing;
        int32 ix = FMath::Clamp(FMath::FloorToInt(RelPos.X), 0, GridNx - 1);
        int32 iy = FMath::Clamp(FMath::FloorToInt(RelPos.Y), 0, GridNy - 1);
        int32 iz = FMath::Clamp(FMath::FloorToInt(RelPos.Z), 0, GridNz - 1);
        
        int32 Index = ix + iy * GridNx + iz * GridNx * GridNy;
        
        float ScaleFactor = 1.0f;
        if (Atom.bIsLigandAtom)
        {
            ScaleFactor = Lambda;
        }
        
        // Potential at this grid point
        float Phi = ChargeGrid[Index];
        EnergyReciprocal += 0.5f * ScaleFactor * Atom.Charge * Phi;
    }
    
    // PART 3: Self-energy correction
    // Subtracts spurious self-interaction
    for (int32 i = 0; i < NumAtoms; ++i)
    {
        const FAtomState& Atom = CurrentState.Atoms[i];
        
        float ScaleFactor = 1.0f;
        if (Atom.bIsLigandAtom)
        {
            ScaleFactor = Lambda;
        }
        
        EnergySelf -= ScaleFactor * FEPConstants::COULOMB * Alpha / FMath::Sqrt(PI) * 
                     Atom.Charge * Atom.Charge;
    }
    
    float TotalEnergy = EnergyReal + EnergyReciprocal + EnergySelf;
    
    // Cap total
    if (FMath::Abs(TotalEnergy) > 100000.0f)
    {
        TotalEnergy = FMath::Sign(TotalEnergy) * 100000.0f;
    }
    
    if (Parameters.bCalculateComponents)
    {
        LastResult.ElectrostaticContribution = TotalEnergy;
    }
    
    return TotalEnergy;
}

void AFEPCalculator::SpreadChargesToGrid()
{
    // Spread atomic charges onto grid using B-spline interpolation
    // For simplicity, using nearest-grid-point (NGP) method
    
    // Zero grid
    for (float& Charge : ChargeGrid)
    {
        Charge = 0.0f;
    }
    
    // Spread charges
    for (const FAtomState& Atom : CurrentState.Atoms)
    {
        FVector RelPos = (Atom.Position - GridOrigin) / GridSpacing;
        int32 ix = FMath::Clamp(FMath::FloorToInt(RelPos.X), 0, GridNx - 1);
        int32 iy = FMath::Clamp(FMath::FloorToInt(RelPos.Y), 0, GridNy - 1);
        int32 iz = FMath::Clamp(FMath::FloorToInt(RelPos.Z), 0, GridNz - 1);
        
        int32 Index = ix + iy * GridNx + iz * GridNx * GridNy;
        ChargeGrid[Index] += Atom.Charge;
    }
}

void AFEPCalculator::SolvePoisson3D()
{
    // Solve Poisson equation: ∇²φ = -4πρ
    // Using simple Jacobi iteration (normally would use FFT)
    
    TArray<float> TempGrid = ChargeGrid;
    int32 Iterations = 10;  // Number of iterations
    
    for (int32 Iter = 0; Iter < Iterations; ++Iter)
    {
        for (int32 iz = 1; iz < GridNz - 1; ++iz)
        {
            for (int32 iy = 1; iy < GridNy - 1; ++iy)
            {
                for (int32 ix = 1; ix < GridNx - 1; ++ix)
                {
                    int32 Index = ix + iy * GridNx + iz * GridNx * GridNy;
                    
                    // 7-point stencil for Laplacian
                    float Phi_xp = TempGrid[Index + 1];
                    float Phi_xm = TempGrid[Index - 1];
                    float Phi_yp = TempGrid[Index + GridNx];
                    float Phi_ym = TempGrid[Index - GridNx];
                    float Phi_zp = TempGrid[Index + GridNx * GridNy];
                    float Phi_zm = TempGrid[Index - GridNx * GridNy];
                    
                    float hSq = GridSpacing.X * GridSpacing.X;  // Assuming cubic grid
                    float Rho = ChargeGrid[Index];
                    
                    // Jacobi update: φ_new = (φ_neighbors - h²*source) / 6
                    TempGrid[Index] = (Phi_xp + Phi_xm + Phi_yp + Phi_ym + Phi_zp + Phi_zm + 
                                      4.0f * PI * hSq * Rho) / 6.0f;
                }
            }
        }
    }
    
    ChargeGrid = TempGrid;
}

void AFEPCalculator::CalculatePMEForces(float Lambda)
{
    // PME forces = Real-space forces + Reciprocal-space forces
    
    float Alpha = Parameters.PMEAlpha;
    float CutoffSq = Parameters.CutoffDistance * Parameters.CutoffDistance;
    int32 NumAtoms = CurrentState.Atoms.Num();
    
    // PART 1: Real-space forces (short-range)
    for (int32 i = 0; i < NumAtoms; ++i)
    {
        FAtomState& Atom1 = CurrentState.Atoms[i];
        
        for (int32 j = i + 1; j < NumAtoms; ++j)
        {
            FAtomState& Atom2 = CurrentState.Atoms[j];
            
            FVector Delta = Atom2.Position - Atom1.Position;
            float rSq = Delta.SizeSquared();
            
            if (rSq >= CutoffSq) continue;
            
            float r = FMath::Sqrt(rSq);
            if (r < 0.5f) r = 0.5f;
            
            float ScaleFactor = 1.0f;
            if (Atom1.bIsLigandAtom || Atom2.bIsLigandAtom)
            {
                ScaleFactor = Lambda;
            }
            
            // Force: F = -dE/dr
            // d/dr[erfc(α*r)/r] = -erfc(α*r)/r² - 2α/√π * exp(-α²r²)/r
            float AlphaR = Alpha * r;
            float Erfc = FMath::Exp(-AlphaR * AlphaR) * (1.0f - FMath::Clamp(AlphaR, 0.0f, 1.0f));
            float ExpTerm = 2.0f * Alpha / FMath::Sqrt(PI) * FMath::Exp(-AlphaR * AlphaR);
            
            float ForceMag = ScaleFactor * FEPConstants::COULOMB * Atom1.Charge * Atom2.Charge *
                           (Erfc / (r * r) + ExpTerm / r);
            
            // Cap force
            if (FMath::Abs(ForceMag) > 100.0f)
            {
                ForceMag = FMath::Sign(ForceMag) * 100.0f;
            }
            
            FVector Force = (Delta / r) * ForceMag;
            
            Atom1.Force -= Force;
            Atom2.Force += Force;
        }
    }
    
    // PART 2: Reciprocal-space forces
    // Calculate electric field from potential grid
    InterpolateForces();
    
    // Apply reciprocal forces to atoms
    for (int32 i = 0; i < NumAtoms; ++i)
    {
        FAtomState& Atom = CurrentState.Atoms[i];
        
        FVector RelPos = (Atom.Position - GridOrigin) / GridSpacing;
        int32 ix = FMath::Clamp(FMath::FloorToInt(RelPos.X), 0, GridNx - 1);
        int32 iy = FMath::Clamp(FMath::FloorToInt(RelPos.Y), 0, GridNy - 1);
        int32 iz = FMath::Clamp(FMath::FloorToInt(RelPos.Z), 0, GridNz - 1);
        
        int32 Index = ix + iy * GridNx + iz * GridNx * GridNy;
        
        float ScaleFactor = 1.0f;
        if (Atom.bIsLigandAtom)
        {
            ScaleFactor = Lambda;
        }
        
        // E-field at grid point
        FVector EField = ForceGrid[Index];
        
        // Force = q * E
        FVector Force = EField * (ScaleFactor * Atom.Charge);
        
        Atom.Force += Force;
    }
}

void AFEPCalculator::InterpolateForces()
{
    // Calculate electric field from potential: E = -∇φ
    // Using finite differences
    
    for (FVector& F : ForceGrid)
    {
        F = FVector::ZeroVector;
    }
    
    for (int32 iz = 1; iz < GridNz - 1; ++iz)
    {
        for (int32 iy = 1; iy < GridNy - 1; ++iy)
        {
            for (int32 ix = 1; ix < GridNx - 1; ++ix)
            {
                int32 Index = ix + iy * GridNx + iz * GridNx * GridNy;
                
                // Central differences for gradient
                float dPhi_dx = (ChargeGrid[Index + 1] - ChargeGrid[Index - 1]) / (2.0f * GridSpacing.X);
                float dPhi_dy = (ChargeGrid[Index + GridNx] - ChargeGrid[Index - GridNx]) / (2.0f * GridSpacing.Y);
                float dPhi_dz = (ChargeGrid[Index + GridNx * GridNy] - ChargeGrid[Index - GridNx * GridNy]) / (2.0f * GridSpacing.Z);
                
                // E = -∇φ
                ForceGrid[Index] = -FVector(dPhi_dx, dPhi_dy, dPhi_dz);
            }
        }
    }
}
