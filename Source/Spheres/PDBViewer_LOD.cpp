// PDBViewer_LOD.cpp - Level of Detail (LOD) system implementation

#include "PDBViewer.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

namespace PDB
{
    constexpr float SCALE = 50.0f;
    constexpr float SPHERE_SIZE = 0.5f;
    constexpr float CYLINDER_SIZE = 0.1f;
    constexpr float BOND_OFFSET = 8.0f;
}

void APDBViewer::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // LOD system update
    if (bEnableLODSystem && ForcedLODLevel < 0)
    {
        TimeSinceLastLODCheck += DeltaTime;

        // Only check LOD every LODCheckInterval seconds to avoid performance hit
        if (TimeSinceLastLODCheck >= LODCheckInterval)
        {
            UpdateLODLevel();
            TimeSinceLastLODCheck = 0.0f;
        }
    }

    // Molecular dynamics simulation step
    if (bMDSimulationActive)
    {
        PerformMDStep(DeltaTime);
    }
}

void APDBViewer::UpdateLODLevel()
{
    // Get camera position from player controller
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (!PC || !PC->PlayerCameraManager)
        return;

    FVector CameraLocation = PC->PlayerCameraManager->GetCameraLocation();

    // Calculate structure center if not cached
    if (!bStructureCenterCached)
    {
        StructureCenter = CalculateStructureCenter();
        bStructureCenterCached = true;
    }

    // Calculate distance from camera to structure center
    float Distance = FVector::Dist(CameraLocation, StructureCenter);

    // Determine LOD level based on distance
    int32 NewLODLevel = 0;
    if (Distance > LOD2Distance)
        NewLODLevel = 3;  // Very far - center of mass only
    else if (Distance > LOD1Distance)
        NewLODLevel = 2;  // Far - backbone atoms only
    else if (Distance > LOD0Distance)
        NewLODLevel = 1;  // Medium - all atoms, backbone bonds
    else
        NewLODLevel = 0;  // Close - full detail

    // Apply LOD if it changed
    if (NewLODLevel != CurrentLODLevel)
    {
        ApplyLODLevel(NewLODLevel);
        CurrentLODLevel = NewLODLevel;
    }
}

FVector APDBViewer::CalculateStructureCenter()
{
    FVector SumPosition = FVector::ZeroVector;
    int32 TotalAtoms = 0;

    // Lambda to accumulate positions from any info map
    auto AccumulateVisiblePositions = [&](const auto& InfoMap) {
        for (const auto& Pair : InfoMap) {
            if (Pair.Value && Pair.Value->bIsVisible) {
                for (const FVector& Pos : Pair.Value->AtomPositions) {
                    SumPosition += Pos * PDB::SCALE;
                    TotalAtoms++;
                }
            }
        }
    };

    AccumulateVisiblePositions(ResidueMap);
    AccumulateVisiblePositions(LigandMap);

    if (TotalAtoms > 0)
        return GetActorLocation() + SumPosition / TotalAtoms;

    return GetActorLocation();
}

void APDBViewer::ApplyLODLevel(int32 NewLODLevel)
{
    // Early out if no data loaded
    if (ResidueMap.Num() == 0 && LigandMap.Num() == 0)
        return;

    UE_LOG(LogTemp, Log, TEXT("Applying LOD Level %d (was %d)"), NewLODLevel, CurrentLODLevel);

    // Apply LOD to all residues
    for (const auto& Pair : ResidueMap)
    {
        FResidueInfo* ResInfo = Pair.Value;
        if (!ResInfo || !ResInfo->bIsVisible)
            continue;

        // Update atom visibility based on LOD
        for (int32 i = 0; i < ResInfo->AtomMeshes.Num(); i++)
        {
            UStaticMeshComponent* AtomMesh = ResInfo->AtomMeshes[i];
            if (!IsValid(AtomMesh))
                continue;

            const FString& AtomName = (i < ResInfo->AtomNames.Num()) ? ResInfo->AtomNames[i] : TEXT("");
            bool bShouldBeVisible = ShouldAtomBeVisibleAtLOD(AtomName, NewLODLevel);

            AtomMesh->SetVisibility(bShouldBeVisible, false);
        }

        // Update bond visibility based on LOD
        for (int32 i = 0; i < ResInfo->BondMeshes.Num(); i++)
        {
            UStaticMeshComponent* BondMesh = ResInfo->BondMeshes[i];
            if (!IsValid(BondMesh))
                continue;

            // Get atom names for this bond
            if (i < ResInfo->BondPairs.Num())
            {
                const TPair<int32, int32>& BondPair = ResInfo->BondPairs[i];
                const FString& Atom1Name = (BondPair.Key < ResInfo->AtomNames.Num()) ? ResInfo->AtomNames[BondPair.Key] : TEXT("");
                const FString& Atom2Name = (BondPair.Value < ResInfo->AtomNames.Num()) ? ResInfo->AtomNames[BondPair.Value] : TEXT("");

                bool bShouldBeVisible = ShouldBondBeVisibleAtLOD(Atom1Name, Atom2Name, NewLODLevel);
                BondMesh->SetVisibility(bShouldBeVisible, false);
            }
        }
    }

    // Apply LOD to all ligands
    for (const auto& Pair : LigandMap)
    {
        FLigandInfo* LigInfo = Pair.Value;
        if (!LigInfo || !LigInfo->bIsVisible)
            continue;

        // For ligands, show at full detail at LOD 0 and 1, hide at LOD 2+
        bool bShowLigand = (NewLODLevel <= 1);
        SetMeshArrayVisibility(LigInfo->AtomMeshes, bShowLigand, false);
        SetMeshArrayVisibility(LigInfo->BondMeshes, bShowLigand, false);
    }
}

bool APDBViewer::ShouldAtomBeVisibleAtLOD(const FString& AtomName, int32 LODLevel) const
{
    if (LODLevel == 0)
        return true;  // Full detail - show everything

    if (LODLevel == 1)
        return true;  // Medium detail - show all atoms

    if (LODLevel == 2)
    {
        // Low detail - backbone atoms only (CA, C, N, O)
        return (AtomName == TEXT("CA") ||
                AtomName == TEXT("C") ||
                AtomName == TEXT("N") ||
                AtomName == TEXT("O"));
    }

    // LOD 3 - hide all individual atoms (will show center of mass sphere instead)
    return false;
}

bool APDBViewer::ShouldBondBeVisibleAtLOD(const FString& Atom1Name, const FString& Atom2Name, int32 LODLevel) const
{
    if (LODLevel == 0)
        return true;  // Full detail - show all bonds

    if (LODLevel == 1)
    {
        // Medium detail - backbone bonds only (uses static set for performance)
        return BackboneAtoms.Contains(Atom1Name) && BackboneAtoms.Contains(Atom2Name);
    }

    // LOD 2 and 3 - no bonds
    return false;
}

void APDBViewer::SetForcedLODLevel(int32 LODLevel)
{
    ForcedLODLevel = LODLevel;

    if (LODLevel >= 0 && LODLevel <= 3)
    {
        ApplyLODLevel(LODLevel);
        CurrentLODLevel = LODLevel;
        UE_LOG(LogTemp, Log, TEXT("Forced LOD level to %d"), LODLevel);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("LOD level set to automatic"));
    }
}

// ===== MOLECULAR DYNAMICS SIMULATION =====

void APDBViewer::PerformMDStep(float DeltaTime)
{
    const float TimeStep = MDTimeStep;
    const int32 NumSteps = FMath::Max(1, MDStepsPerFrame);

    // Run multiple MD steps per frame for faster simulation
    for (int32 Step = 0; Step < NumSteps; ++Step)
    {
        // PHASE 1: Only simulate ligands (protein is frozen)
        // For each visible ligand, integrate its atoms
        for (auto& Pair : LigandMap)
        {
            if (Pair.Value && Pair.Value->bIsVisible && Pair.Value->Velocities.Num() > 0)
            {
                IntegrateMolecule(Pair.Value, TimeStep);

                // Check for badly stretched bonds (diagnostic)
                CheckBondIntegrity(Pair.Value);
            }
        }
    }

    // Update mesh positions once after all steps (only ligands will have moved)
    UpdateMeshPositions();
}

template<typename TMolInfo>
void APDBViewer::CheckBondIntegrity(TMolInfo* Info)
{
    // Check if any bonds are stretched beyond reasonable limits
    const float MaxBondStretch = 3.0f; // 3x equilibrium length

    for (int32 i = 0; i < Info->BondPairs.Num(); ++i)
    {
        int32 Atom1 = Info->BondPairs[i].Key;
        int32 Atom2 = Info->BondPairs[i].Value;

        if (!Info->AtomPositions.IsValidIndex(Atom1) || !Info->AtomPositions.IsValidIndex(Atom2))
            continue;

        int32 BondOrder = Info->BondOrders.IsValidIndex(i) ? Info->BondOrders[i] : 1;
        float R0 = FMDForceCalculator::GetEquilibriumBondLength(BondOrder,
                                                                 Info->AtomElements[Atom1],
                                                                 Info->AtomElements[Atom2]);

        float CurrentDist = FVector::Dist(Info->AtomPositions[Atom1], Info->AtomPositions[Atom2]);

        if (CurrentDist > R0 * MaxBondStretch)
        {
            UE_LOG(LogTemp, Warning, TEXT("Bond %d stretched to %.2f Å (equilibrium: %.2f Å, %.1fx)"),
                   i, CurrentDist, R0, CurrentDist / R0);

            // Optional: Reset bond to equilibrium length
            FVector Direction = (Info->AtomPositions[Atom2] - Info->AtomPositions[Atom1]).GetSafeNormal();
            FVector MidPoint = (Info->AtomPositions[Atom1] + Info->AtomPositions[Atom2]) * 0.5f;
            Info->AtomPositions[Atom1] = MidPoint - Direction * (R0 * 0.5f);
            Info->AtomPositions[Atom2] = MidPoint + Direction * (R0 * 0.5f);

            // Zero velocities to prevent further stretching
            Info->Velocities[Atom1] = FVector::ZeroVector;
            Info->Velocities[Atom2] = FVector::ZeroVector;
        }
    }
}

template<typename TMolInfo>
void APDBViewer::IntegrateMolecule(TMolInfo* Info, float TimeStep)
{
    // Calculate bonded forces
    FMDForceCalculator::CalculateBondedForces(
        Info->AtomPositions,
        Info->AtomElements,
        Info->BondPairs,
        Info->BondOrders,
        Info->Forces
    );

    // TODO Phase 2: Calculate non-bonded forces with spatial partitioning
    // For now, skip non-bonded forces to keep it simple

    // Unit conversion: kcal/(mol*Å*amu) to Å/fs²
    // 1 kcal/(mol*Å*amu) = 418.4 Å/ps² = 0.0004184 Å/fs²
    const float UnitConversion = 0.0004184f;

    // Maximum force magnitude to prevent explosions (in kcal/mol/Å)
    // Higher cap allows stretched bonds to restore properly while preventing VdW explosions
    const float MaxForce = 500.0f;

    // For bonded forces only, we can allow even higher forces for badly stretched bonds
    const float MaxBondedForce = 2000.0f;

    // Velocity Verlet integration - only for visible atoms
    for (int32 i = 0; i < Info->AtomPositions.Num(); ++i)
    {
        // Skip atoms that don't have valid masses
        if (!Info->Masses.IsValidIndex(i) || Info->Masses[i] <= 0.0f)
            continue;

        // Skip atoms whose meshes are hidden (e.g., hydrogens when bShowHydrogens is off)
        if (Info->AtomMeshes.IsValidIndex(i) && IsValid(Info->AtomMeshes[i]))
        {
            if (!Info->AtomMeshes[i]->IsVisible())
                continue;
        }
        else
        {
            // If no mesh exists, skip this atom
            continue;
        }

        FVector& Pos = Info->AtomPositions[i];
        FVector& Vel = Info->Velocities[i];
        FVector& Force = Info->Forces[i];
        float Mass = Info->Masses[i];

        // Cap force magnitude to prevent explosions
        // Use higher cap for bonded-only forces (Phase 1), lower when non-bonded forces added (Phase 2)
        float ForceMag = Force.Size();
        if (ForceMag > MaxBondedForce)
        {
            Force = Force.GetSafeNormal() * MaxBondedForce;
        }

        // Convert force to acceleration (Å/fs²)
        FVector Acceleration = (Force / Mass) * UnitConversion;

        // Velocity Verlet integration
        // v(t + dt/2) = v(t) + a(t) * dt/2
        Vel += Acceleration * (TimeStep * 0.5f);

        // x(t + dt) = x(t) + v(t + dt/2) * dt
        Pos += Vel * TimeStep;

        // v(t + dt) = v(t + dt/2) + a(t) * dt/2 (simplified - should recalculate forces)
        Vel += Acceleration * (TimeStep * 0.5f);

        // Apply velocity damping to dissipate energy (once per timestep)
        Vel *= MDDamping;
    }
}

// Explicit template instantiations
template void APDBViewer::IntegrateMolecule<FResidueInfo>(FResidueInfo*, float);
template void APDBViewer::IntegrateMolecule<FLigandInfo>(FLigandInfo*, float);
template void APDBViewer::CheckBondIntegrity<FResidueInfo>(FResidueInfo*);
template void APDBViewer::CheckBondIntegrity<FLigandInfo>(FLigandInfo*);

void APDBViewer::UpdateMeshPositions()
{
    // Update residue atom meshes
    for (auto& Pair : ResidueMap)
    {
        if (!Pair.Value)
            continue;

        for (int32 i = 0; i < Pair.Value->AtomPositions.Num(); ++i)
        {
            if (Pair.Value->AtomMeshes.IsValidIndex(i) && IsValid(Pair.Value->AtomMeshes[i]))
            {
                FVector ScaledPos = Pair.Value->AtomPositions[i] * PDB::SCALE;
                Pair.Value->AtomMeshes[i]->SetWorldLocation(ScaledPos);
            }
        }

        // Update bond meshes
        for (int32 i = 0; i < Pair.Value->BondPairs.Num(); ++i)
        {
            if (!Pair.Value->BondMeshes.IsValidIndex(i) || !IsValid(Pair.Value->BondMeshes[i]))
                continue;

            const TPair<int32, int32>& Bond = Pair.Value->BondPairs[i];
            if (!Pair.Value->AtomPositions.IsValidIndex(Bond.Key) || !Pair.Value->AtomPositions.IsValidIndex(Bond.Value))
                continue;

            FVector Pos1 = Pair.Value->AtomPositions[Bond.Key] * PDB::SCALE;
            FVector Pos2 = Pair.Value->AtomPositions[Bond.Value] * PDB::SCALE;
            FVector MidPoint = (Pos1 + Pos2) * 0.5f;
            FVector Direction = Pos2 - Pos1;
            float Distance = Direction.Size();

            if (Distance > 0.01f)
            {
                FRotator Rotation = Direction.Rotation();
                Pair.Value->BondMeshes[i]->SetWorldLocation(MidPoint);
                Pair.Value->BondMeshes[i]->SetWorldRotation(Rotation);
                Pair.Value->BondMeshes[i]->SetWorldScale3D(FVector(PDB::CYLINDER_SIZE, PDB::CYLINDER_SIZE, Distance / 100.0f));
            }
        }
    }

    // Update ligand atom meshes
    for (auto& Pair : LigandMap)
    {
        if (!Pair.Value)
            continue;

        for (int32 i = 0; i < Pair.Value->AtomPositions.Num(); ++i)
        {
            if (Pair.Value->AtomMeshes.IsValidIndex(i) && IsValid(Pair.Value->AtomMeshes[i]))
            {
                FVector ScaledPos = Pair.Value->AtomPositions[i] * PDB::SCALE;
                Pair.Value->AtomMeshes[i]->SetWorldLocation(ScaledPos);
            }
        }

        // Update bond meshes
        for (int32 i = 0; i < Pair.Value->BondPairs.Num(); ++i)
        {
            if (!Pair.Value->BondMeshes.IsValidIndex(i) || !IsValid(Pair.Value->BondMeshes[i]))
                continue;

            const TPair<int32, int32>& Bond = Pair.Value->BondPairs[i];
            if (!Pair.Value->AtomPositions.IsValidIndex(Bond.Key) || !Pair.Value->AtomPositions.IsValidIndex(Bond.Value))
                continue;

            FVector Pos1 = Pair.Value->AtomPositions[Bond.Key] * PDB::SCALE;
            FVector Pos2 = Pair.Value->AtomPositions[Bond.Value] * PDB::SCALE;
            FVector MidPoint = (Pos1 + Pos2) * 0.5f;
            FVector Direction = Pos2 - Pos1;
            float Distance = Direction.Size();

            if (Distance > 0.01f)
            {
                FRotator Rotation = Direction.Rotation();
                Pair.Value->BondMeshes[i]->SetWorldLocation(MidPoint);
                Pair.Value->BondMeshes[i]->SetWorldRotation(Rotation);
                Pair.Value->BondMeshes[i]->SetWorldScale3D(FVector(PDB::CYLINDER_SIZE, PDB::CYLINDER_SIZE, Distance / 100.0f));
            }
        }
    }
}

void APDBViewer::StartMDSimulation()
{
    // Initialize velocities from Maxwell-Boltzmann distribution
    InitializeVelocitiesFromTemperature();

    bMDSimulationActive = true;
    UE_LOG(LogTemp, Log, TEXT("MD Simulation Started (TimeStep: %f fs, Temperature: %f K)"), MDTimeStep, MDTemperature);
}

void APDBViewer::InitializeVelocitiesFromTemperature()
{
    // Boltzmann constant in kcal/(mol*K)
    const float kB = 0.001987f;

    // Lambda to generate Gaussian random number using Box-Muller transform
    auto RandGaussian = []() -> float {
        float U1 = FMath::FRand();
        float U2 = FMath::FRand();
        // Avoid log(0)
        if (U1 < 1e-8f) U1 = 1e-8f;
        return FMath::Sqrt(-2.0f * FMath::Loge(U1)) * FMath::Cos(2.0f * PI * U2);
    };

    // Lambda to initialize velocities for a molecule
    auto InitMolecule = [&](auto* Info) {
        if (!Info || Info->Velocities.Num() == 0)
            return;

        for (int32 i = 0; i < Info->Velocities.Num(); ++i)
        {
            if (!Info->Masses.IsValidIndex(i) || Info->Masses[i] <= 0.0f)
                continue;

            // Maxwell-Boltzmann distribution: <v²> = kB*T/m
            // Standard deviation of velocity component: σ = sqrt(kB*T/m)
            // Units: sqrt(kcal/(mol*amu)) = sqrt(0.001987 * 300 / 12) Å/fs
            float Mass = Info->Masses[i];
            float Sigma = FMath::Sqrt(kB * MDTemperature / Mass);

            // Generate random velocity components from Gaussian distribution
            // Scale by 0.01 to convert from Å/fs to appropriate units for visible motion
            Info->Velocities[i] = FVector(
                RandGaussian() * Sigma * 0.01f,
                RandGaussian() * Sigma * 0.01f,
                RandGaussian() * Sigma * 0.01f
            );
        }
    };

    // Initialize velocities for all visible ligands
    for (auto& Pair : LigandMap)
    {
        if (Pair.Value && Pair.Value->bIsVisible)
        {
            InitMolecule(Pair.Value);
        }
    }

    // Initialize velocities for all visible residues
    for (auto& Pair : ResidueMap)
    {
        if (Pair.Value && Pair.Value->bIsVisible)
        {
            InitMolecule(Pair.Value);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Initialized velocities from temperature: %.1f K"), MDTemperature);
}

void APDBViewer::StopMDSimulation()
{
    bMDSimulationActive = false;
    UE_LOG(LogTemp, Log, TEXT("MD Simulation Stopped"));
}

void APDBViewer::ResetMDSimulation()
{
    // Reset all velocities and forces to zero
    for (auto& Pair : ResidueMap)
    {
        if (Pair.Value)
        {
            for (FVector& Vel : Pair.Value->Velocities)
                Vel = FVector::ZeroVector;
            for (FVector& Force : Pair.Value->Forces)
                Force = FVector::ZeroVector;
        }
    }

    for (auto& Pair : LigandMap)
    {
        if (Pair.Value)
        {
            for (FVector& Vel : Pair.Value->Velocities)
                Vel = FVector::ZeroVector;
            for (FVector& Force : Pair.Value->Forces)
                Force = FVector::ZeroVector;
        }
    }

    UE_LOG(LogTemp, Log, TEXT("MD Simulation Reset (all velocities and forces set to zero)"));
}
