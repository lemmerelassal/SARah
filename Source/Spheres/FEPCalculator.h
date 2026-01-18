// FEPCalculator.h - Free Energy Perturbation for Binding Affinity Calculations
// Compatible with Unreal Engine 5.6

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FEPCalculator.generated.h"

// Forward declarations
class APDBViewer;
struct FResidueInfo;
struct FLigandInfo;

// Lambda window state for thermodynamic integration
USTRUCT(BlueprintType)
struct FFEPLambdaWindow
{
    GENERATED_BODY()
    
    UPROPERTY(BlueprintReadOnly, Category = "FEP")
    float Lambda; // Coupling parameter (0 = state A, 1 = state B)
    
    UPROPERTY(BlueprintReadOnly, Category = "FEP")
    float Energy; // Average energy at this lambda
    
    UPROPERTY(BlueprintReadOnly, Category = "FEP")
    float dHdLambda; // Derivative of Hamiltonian with respect to lambda
    
    UPROPERTY(BlueprintReadOnly, Category = "FEP")
    float StandardError; // Statistical error
    
    UPROPERTY(BlueprintReadOnly, Category = "FEP")
    int32 SampleCount; // Number of samples collected
    
    FFEPLambdaWindow()
        : Lambda(0.0f)
        , Energy(0.0f)
        , dHdLambda(0.0f)
        , StandardError(0.0f)
        , SampleCount(0)
    {}
};

// FEP calculation results
USTRUCT(BlueprintType)
struct FFEPResult
{
    GENERATED_BODY()
    
    UPROPERTY(BlueprintReadOnly, Category = "FEP")
    float DeltaG; // Free energy change (kcal/mol)
    
    UPROPERTY(BlueprintReadOnly, Category = "FEP")
    float StandardError; // Statistical error
    
    UPROPERTY(BlueprintReadOnly, Category = "FEP")
    float BindingAffinity; // Ki or Kd in nM
    
    UPROPERTY(BlueprintReadOnly, Category = "FEP")
    TArray<FFEPLambdaWindow> LambdaWindows;
    
    UPROPERTY(BlueprintReadOnly, Category = "FEP")
    float ElectrostaticContribution; // Electrostatic energy component
    
    UPROPERTY(BlueprintReadOnly, Category = "FEP")
    float VdWContribution; // Van der Waals energy component
    
    UPROPERTY(BlueprintReadOnly, Category = "FEP")
    float SolvationContribution; // Solvation free energy
    
    UPROPERTY(BlueprintReadOnly, Category = "FEP")
    bool bCalculationSuccessful;
    
    UPROPERTY(BlueprintReadOnly, Category = "FEP")
    FString ErrorMessage;
    
    FFEPResult()
        : DeltaG(0.0f)
        , StandardError(0.0f)
        , BindingAffinity(0.0f)
        , ElectrostaticContribution(0.0f)
        , VdWContribution(0.0f)
        , SolvationContribution(0.0f)
        , bCalculationSuccessful(false)
    {}
};

// Atom state for MD simulation
USTRUCT()
struct FAtomState
{
    GENERATED_BODY()
    
    FVector Position;
    FVector Velocity;
    FVector Force;
    float Mass;
    float Charge;
    float VdWRadius;
    float VdWEpsilon;
    FString Element;
    bool bIsLigandAtom; // True if part of ligand, false if part of protein
    
    FAtomState()
        : Position(FVector::ZeroVector)
        , Velocity(FVector::ZeroVector)
        , Force(FVector::ZeroVector)
        , Mass(12.0f)
        , Charge(0.0f)
        , VdWRadius(1.7f)
        , VdWEpsilon(0.1f)
        , bIsLigandAtom(false)
    {}
};

// System state for simulation
USTRUCT()
struct FSystemState
{
    GENERATED_BODY()
    
    TArray<FAtomState> Atoms;
    float PotentialEnergy;
    float KineticEnergy;
    float Temperature;
    float Lambda; // Current coupling parameter
    
    FSystemState()
        : PotentialEnergy(0.0f)
        , KineticEnergy(0.0f)
        , Temperature(300.0f)
        , Lambda(0.0f)
    {}
};

// FEP calculation parameters
USTRUCT(BlueprintType)
struct FFEPParameters
{
    GENERATED_BODY()
    
    // Simulation parameters
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Simulation")
    float Temperature = 300.0f; // Kelvin
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Simulation")
    float TimeStep = 0.001f; // picoseconds
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Simulation")
    int32 EquilibrationSteps = 10000; // Steps for equilibration
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Simulation")
    int32 ProductionSteps = 50000; // Steps for production
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Simulation")
    int32 SamplingInterval = 100; // Collect data every N steps
    
    // Lambda schedule
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Lambda")
    int32 NumLambdaWindows = 20; // Number of lambda values
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Lambda")
    bool bUseSoftCore = true; // Use soft-core potentials
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Lambda")
    float SoftCoreAlpha = 0.5f; // Soft-core parameter
    
    // Force field parameters
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|ForceField")
    float DielectricConstant = 1.0f; // For protein interior
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|ForceField")
    float CutoffDistance = 12.0f; // Angstroms
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|ForceField")
    bool bUseReactionField = false; // Reaction field correction (simple)
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|ForceField")
    bool bUseParticleMeshEwald = true; // PME for electrostatics (accurate)
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|ForceField", meta = (EditCondition = "bUseParticleMeshEwald"))
    int32 PMEGridSize = 32; // Grid points per dimension (32x32x32)
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|ForceField", meta = (EditCondition = "bUseParticleMeshEwald"))
    float PMEAlpha = 0.3f; // Ewald splitting parameter (1/Angstroms)
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|ForceField", meta = (EditCondition = "bUseParticleMeshEwald"))
    int32 PMESplineOrder = 4; // B-spline interpolation order
    
    // Restraints
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Restraints")
    bool bRestrainProtein = true; // Keep protein atoms fixed/restrained
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Restraints")
    float ProteinRestraintForce = 10.0f; // kcal/mol/Å²
    
    // Analysis
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Analysis")
    bool bCalculateComponents = true; // Break down energy components
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Analysis")
    bool bCalculateSolvation = true; // Include solvation effects
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFEPProgress, float, ProgressPercent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFEPComplete, const FFEPResult&, Result);

UCLASS()
class SPHERES_API AFEPCalculator : public AActor
{
    GENERATED_BODY()

public:
    AFEPCalculator();

    // Auto-find and attach to PDBViewer in the level
    UFUNCTION(BlueprintCallable, Category = "FEP")
    void InitializeWithPDBViewer();
    
    // Main FEP calculation function - simplified (auto-uses attached PDBViewer)
    UFUNCTION(BlueprintCallable, Category = "FEP")
    void CalculateBindingFreeEnergy(const FString& LigandKey);
    
    // Calculate for all currently visible ligands (most common use case)
    UFUNCTION(BlueprintCallable, Category = "FEP")
    void CalculateVisibleLigands();
    
    // Calculate for all ligands automatically
    UFUNCTION(BlueprintCallable, Category = "FEP")
    void CalculateAllLigands();
    
    // Calculate relative binding free energy (comparing two ligands)
    UFUNCTION(BlueprintCallable, Category = "FEP")
    void CalculateRelativeBindingFreeEnergy(const FString& LigandKeyA, 
                                           const FString& LigandKeyB);
    
    // Stop ongoing calculation
    UFUNCTION(BlueprintCallable, Category = "FEP")
    void StopCalculation();
    
    // Get current calculation progress
    UFUNCTION(BlueprintCallable, Category = "FEP")
    float GetCalculationProgress() const { return CurrentProgress; }
    
    // Check if calculation is running
    UFUNCTION(BlueprintCallable, Category = "FEP")
    bool IsCalculating() const { return bIsCalculating; }
    
    // Get last result
    UFUNCTION(BlueprintCallable, Category = "FEP")
    FFEPResult GetLastResult() const { return LastResult; }
    
    // Get results for all calculated ligands
    UFUNCTION(BlueprintCallable, Category = "FEP")
    TMap<FString, FFEPResult> GetAllResults() const { return AllResults; }
    
    // Visualization
    UFUNCTION(BlueprintCallable, Category = "FEP|Visualization")
    void VisualizeEnergyLandscape(const FFEPResult& Result);
    
    UFUNCTION(BlueprintCallable, Category = "FEP|Visualization")
    void ClearVisualization();
    
    // Parameters
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP")
    FFEPParameters Parameters;
    
    // Delegates
    UPROPERTY(BlueprintAssignable, Category = "FEP")
    FOnFEPProgress OnFEPProgress;
    
    UPROPERTY(BlueprintAssignable, Category = "FEP")
    FOnFEPComplete OnFEPComplete;
    
    // Debug output
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Debug")
    bool bVerboseLogging = false;
    
    // Auto-spawn UI widget on BeginPlay
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|UI")
    bool bAutoSpawnUI = true;
    
    // UI widget class to spawn
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|UI")
    TSubclassOf<class UFEPControlWidget> ControlWidgetClass;
    
    // Reference to spawned UI widget
    UPROPERTY(BlueprintReadOnly, Category = "FEP|UI")
    class UFEPControlWidget* ControlWidget;
    
    // Auto-calculate on BeginPlay (for testing/automation)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Automation")
    bool bAutoCalculateOnBeginPlay = false;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Automation", meta = (EditCondition = "bAutoCalculateOnBeginPlay"))
    bool bAutoCalculateVisibleOnly = true;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Automation", meta = (EditCondition = "bAutoCalculateOnBeginPlay && !bAutoCalculateVisibleOnly"))
    FString AutoCalculateLigandKey;
    
    // Auto-export results when complete
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Automation")
    bool bAutoExportResults = true;
    
    // Energy minimization before FEP
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Minimization")
    bool bPerformEnergyMinimization = true;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Minimization", meta = (EditCondition = "bPerformEnergyMinimization"))
    int32 MinimizationMaxSteps = 5000;  // Increased from 1000
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Minimization", meta = (EditCondition = "bPerformEnergyMinimization"))
    float MinimizationForceThreshold = 50.0f;  // kcal/mol/Å - increased tolerance
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Minimization", meta = (EditCondition = "bPerformEnergyMinimization"))
    float MinimizationStepSize = 0.001f;  // Angstroms - reduced for stability
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEP|Minimization", meta = (EditCondition = "bPerformEnergyMinimization"))
    bool bMoveProteinDuringMinimization = false;  // Keep protein fixed by default

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

protected:  // All members protected to allow derived classes to access
    // Calculation state
    bool bIsCalculating;
    float CurrentProgress;
    FFEPResult LastResult;
    
    // Store results for all ligands
    TMap<FString, FFEPResult> AllResults;
    
    // Auto-found PDBViewer reference
    UPROPERTY()
    APDBViewer* PDBViewer;
    
    // System state
    UPROPERTY()
    FSystemState CurrentState;
    
    TArray<FVector> InitialProteinPositions; // For restraints
    
    // Timer for async calculation
    FTimerHandle CalculationTimerHandle;
    int32 CurrentLambdaIndex;
    int32 CurrentStep;
    TArray<float> CollectedEnergies;
    TArray<float> CollecteddHdL;
    
    // Queue for batch calculations
    TArray<FString> CalculationQueue;
    int32 CurrentQueueIndex;
    
    // Core FEP methods - now virtual for overriding
    virtual void InitializeSystem(const FString& LigandKey);
    virtual void RunLambdaWindow(float Lambda);
    virtual void PerformMDStep(float Lambda);
    virtual void CalculateForces(float Lambda);
    virtual void UpdatePositions();
    virtual void ApplyThermostat();
    
    // Auto-export helper
    void ExportResult(const FString& LigandKey, const FFEPResult& Result);
    
    // Queue processing
    void ProcessNextInQueue();
    
    // Energy minimization
    void MinimizeEnergy();
    float CalculateMaxForce();
    
    // Energy calculations - now virtual for overriding
    virtual float CalculateTotalEnergy(float Lambda);
    virtual float CalculateElectrostaticEnergy(float Lambda);
    virtual float CalculateVanDerWaalsEnergy(float Lambda);
    virtual float CalculateBondEnergy();
    virtual float CalculateAngleEnergy();
    virtual float CalculateDihedralEnergy();
    virtual float CalculateSolvationEnergy(float Lambda);
    virtual float CalculatedHdLambda(float Lambda);
    
    // Force calculations
    void CalculateElectrostaticForces(float Lambda);
    void CalculateVanDerWaalsForces(float Lambda);
    void CalculateBondForces();
    void ApplyRestraints();
    
    // Soft-core potentials
    float SoftCoreLJ(float r, float sigma, float epsilon, float lambda);
    float SoftCoreCoulomb(float r, float q1, float q2, float lambda);
    
    // Thermodynamic integration
    float IntegrateFreeEnergy(const TArray<FFEPLambdaWindow>& Windows);
    float CalculateBindingAffinityFromDeltaG(float DeltaG);
    
    // Utility functions
    void SetAtomParameters(FAtomState& Atom, const FString& Element);
    float GetAtomicMass(const FString& Element);
    float GetVdWRadius(const FString& Element);
    float GetVdWEpsilon(const FString& Element);
    
    // Error estimation
    float CalculateBlockAverageError(const TArray<float>& Data);
    
    // Particle Mesh Ewald (PME) for electrostatics
    void InitializePME();
    float CalculatePMEElectrostatics(float Lambda);
    void CalculatePMEForces(float Lambda);
    void SpreadChargesToGrid();
    void SolvePoisson3D();
    void InterpolateForces();
    
    // PME data structures
    TArray<float> ChargeGrid;  // 3D grid of charges
    TArray<FVector> ForceGrid; // 3D grid of electric field
    FVector GridOrigin;
    FVector GridSpacing;
    int32 GridNx, GridNy, GridNz;
    bool bPMEInitialized;
    
    // Visualization meshes
    UPROPERTY()
    TArray<UStaticMeshComponent*> VisualizationMeshes;
};
