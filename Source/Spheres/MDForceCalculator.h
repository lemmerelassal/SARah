// MDForceCalculator.h - Molecular dynamics force field calculations
// Phase 1: Basic Lennard-Jones and harmonic bond forces

#pragma once

#include "CoreMinimal.h"

// Forward declarations
struct FResidueInfo;
struct FLigandInfo;

/**
 * Simple force field calculator for molecular dynamics
 * Phase 1: Implements Lennard-Jones non-bonded and harmonic bonded forces
 */
class SPHERES_API FMDForceCalculator
{
public:
    FMDForceCalculator();
    ~FMDForceCalculator();

    // ==== Atomic Parameters ====

    /**
     * Get atomic mass in amu (atomic mass units)
     * @param Element - Element symbol (H, C, N, O, etc.)
     * @return Atomic mass in amu
     */
    static float GetAtomMass(const FString& Element);

    /**
     * Get van der Waals radius (sigma parameter for LJ potential)
     * @param Element - Element symbol
     * @return vdW radius in Angstroms
     */
    static float GetVdWRadius(const FString& Element);

    /**
     * Get van der Waals well depth (epsilon parameter for LJ potential)
     * @param Element - Element symbol
     * @return vdW well depth in kcal/mol
     */
    static float GetVdWEpsilon(const FString& Element);

    /**
     * Get bond spring constant based on bond order
     * @param BondOrder - 1 (single), 2 (double), 3 (triple), 4 (aromatic)
     * @return Spring constant in kcal/(mol*Ų)
     */
    static float GetBondSpringConstant(int32 BondOrder);

    /**
     * Get equilibrium bond length based on bond order and elements
     * @param BondOrder - Bond order
     * @param Element1, Element2 - Element symbols
     * @return Equilibrium bond length in Angstroms
     */
    static float GetEquilibriumBondLength(int32 BondOrder, const FString& Element1, const FString& Element2);

    // ==== Force Calculations ====

    /**
     * Calculate bonded forces (harmonic springs) for a molecule
     * @param AtomPositions - Array of atom positions (Angstroms)
     * @param AtomElements - Array of element symbols
     * @param BondPairs - Array of bonded atom pairs (indices)
     * @param BondOrders - Array of bond orders
     * @param OutForces - Output array of forces (will be resized if needed)
     */
    static void CalculateBondedForces(
        const TArray<FVector>& AtomPositions,
        const TArray<FString>& AtomElements,
        const TArray<TPair<int32, int32>>& BondPairs,
        const TArray<int32>& BondOrders,
        TArray<FVector>& OutForces
    );

    /**
     * Calculate non-bonded Lennard-Jones forces between two sets of atoms
     * @param Positions1, Positions2 - Atom positions
     * @param Elements1, Elements2 - Element symbols
     * @param Forces1, Forces2 - Output force arrays
     * @param Cutoff - Distance cutoff in Angstroms (default 12.0)
     */
    static void CalculateNonBondedForces(
        const TArray<FVector>& Positions1,
        const TArray<FString>& Elements1,
        const TArray<FVector>& Positions2,
        const TArray<FString>& Elements2,
        TArray<FVector>& Forces1,
        TArray<FVector>& Forces2,
        float Cutoff = 12.0f
    );

private:
    /**
     * Calculate Lennard-Jones force between two atoms
     * @param Pos1, Pos2 - Atom positions
     * @param Sigma - Combined vdW radius (Angstroms)
     * @param Epsilon - Combined vdW well depth (kcal/mol)
     * @return Force vector on atom 1 (kcal/mol/Angstrom)
     */
    static FVector CalculateLJForce(
        const FVector& Pos1,
        const FVector& Pos2,
        float Sigma,
        float Epsilon
    );

    /**
     * Calculate harmonic bond force
     * @param Pos1, Pos2 - Atom positions
     * @param K - Spring constant (kcal/mol/Ų)
     * @param R0 - Equilibrium bond length (Angstroms)
     * @return Force vector on atom 1 (kcal/mol/Angstrom)
     */
    static FVector CalculateBondForce(
        const FVector& Pos1,
        const FVector& Pos2,
        float K,
        float R0
    );
};
