// MDForceCalculator.cpp - Implementation of molecular dynamics force calculations

#include "MDForceCalculator.h"
#include "PDBViewer.h"

FMDForceCalculator::FMDForceCalculator()
{
}

FMDForceCalculator::~FMDForceCalculator()
{
}

// ==== Atomic Parameters ====

float FMDForceCalculator::GetAtomMass(const FString& Element)
{
    // Atomic masses in amu (atomic mass units)
    static TMap<FString, float> Masses = {
        {TEXT("H"),  1.008f},
        {TEXT("C"),  12.011f},
        {TEXT("N"),  14.007f},
        {TEXT("O"),  15.999f},
        {TEXT("S"),  32.06f},
        {TEXT("P"),  30.974f},
        {TEXT("F"),  18.998f},
        {TEXT("Cl"), 35.45f},
        {TEXT("Br"), 79.904f},
        {TEXT("I"),  126.90f},
        {TEXT("Fe"), 55.845f},
        {TEXT("Mg"), 24.305f},
        {TEXT("Zn"), 65.38f},
        {TEXT("Ca"), 40.078f},
        {TEXT("Na"), 22.990f},
        {TEXT("K"),  39.098f},
        {TEXT("Cu"), 63.546f},
        {TEXT("B"),  10.81f}
    };

    const float* Mass = Masses.Find(Element);
    return Mass ? *Mass : 12.011f; // Default to carbon if unknown
}

float FMDForceCalculator::GetVdWRadius(const FString& Element)
{
    // van der Waals radii in Angstroms
    static TMap<FString, float> Radii = {
        {TEXT("H"),  1.2f},
        {TEXT("C"),  1.7f},
        {TEXT("N"),  1.55f},
        {TEXT("O"),  1.52f},
        {TEXT("S"),  1.8f},
        {TEXT("P"),  1.8f},
        {TEXT("F"),  1.47f},
        {TEXT("Cl"), 1.75f},
        {TEXT("Br"), 1.85f},
        {TEXT("I"),  1.98f},
        {TEXT("Fe"), 1.4f},
        {TEXT("Mg"), 1.73f},
        {TEXT("Zn"), 1.39f},
        {TEXT("Ca"), 1.97f},
        {TEXT("Na"), 2.27f},
        {TEXT("K"),  2.75f},
        {TEXT("Cu"), 1.4f},
        {TEXT("B"),  1.92f}
    };

    const float* Radius = Radii.Find(Element);
    return Radius ? *Radius : 1.7f; // Default to carbon
}

float FMDForceCalculator::GetVdWEpsilon(const FString& Element)
{
    // van der Waals well depths in kcal/mol
    static TMap<FString, float> Epsilons = {
        {TEXT("H"),  0.02f},
        {TEXT("C"),  0.08f},
        {TEXT("N"),  0.17f},
        {TEXT("O"),  0.21f},
        {TEXT("S"),  0.25f},
        {TEXT("P"),  0.20f},
        {TEXT("F"),  0.061f},
        {TEXT("Cl"), 0.27f},
        {TEXT("Br"), 0.32f},
        {TEXT("I"),  0.40f},
        {TEXT("Fe"), 0.013f},
        {TEXT("Mg"), 0.087f},
        {TEXT("Zn"), 0.012f},
        {TEXT("Ca"), 0.12f},
        {TEXT("Na"), 0.047f},
        {TEXT("K"),  0.087f},
        {TEXT("Cu"), 0.005f},
        {TEXT("B"),  0.18f}
    };

    const float* Epsilon = Epsilons.Find(Element);
    return Epsilon ? *Epsilon : 0.08f; // Default to carbon
}

float FMDForceCalculator::GetBondSpringConstant(int32 BondOrder)
{
    // Spring constants in kcal/(mol*Ų) - reduced for stability
    switch (BondOrder)
    {
        case 1:  return 50.0f;   // Single bond
        case 2:  return 100.0f;  // Double bond
        case 3:  return 150.0f;  // Triple bond
        case 4:  return 75.0f;   // Aromatic (between single and double)
        default: return 50.0f;
    }
}

float FMDForceCalculator::GetEquilibriumBondLength(int32 BondOrder, const FString& Element1, const FString& Element2)
{
    // Simplified equilibrium bond lengths in Angstroms
    // In reality, these depend on both elements and bond order

    // C-C bonds
    if ((Element1 == TEXT("C") && Element2 == TEXT("C")) ||
        (Element1 == TEXT("C") && Element2 == TEXT("C")))
    {
        switch (BondOrder)
        {
            case 1:  return 1.54f;  // C-C single
            case 2:  return 1.34f;  // C=C double
            case 3:  return 1.20f;  // C≡C triple
            case 4:  return 1.40f;  // Aromatic
            default: return 1.54f;
        }
    }

    // C-H bonds
    if ((Element1 == TEXT("C") && Element2 == TEXT("H")) ||
        (Element1 == TEXT("H") && Element2 == TEXT("C")))
    {
        return 1.09f;
    }

    // C-N bonds
    if ((Element1 == TEXT("C") && Element2 == TEXT("N")) ||
        (Element1 == TEXT("N") && Element2 == TEXT("C")))
    {
        return BondOrder == 1 ? 1.47f : 1.25f;
    }

    // C-O bonds
    if ((Element1 == TEXT("C") && Element2 == TEXT("O")) ||
        (Element1 == TEXT("O") && Element2 == TEXT("C")))
    {
        return BondOrder == 1 ? 1.43f : 1.20f;
    }

    // N-H bonds
    if ((Element1 == TEXT("N") && Element2 == TEXT("H")) ||
        (Element1 == TEXT("H") && Element2 == TEXT("N")))
    {
        return 1.01f;
    }

    // O-H bonds
    if ((Element1 == TEXT("O") && Element2 == TEXT("H")) ||
        (Element1 == TEXT("H") && Element2 == TEXT("O")))
    {
        return 0.96f;
    }

    // Default: sum of covalent radii
    float r1 = GetVdWRadius(Element1) * 0.6f;  // Approximate covalent radius
    float r2 = GetVdWRadius(Element2) * 0.6f;
    return r1 + r2;
}

// ==== Force Calculations ====

void FMDForceCalculator::CalculateBondedForces(
    const TArray<FVector>& AtomPositions,
    const TArray<FString>& AtomElements,
    const TArray<TPair<int32, int32>>& BondPairs,
    const TArray<int32>& BondOrders,
    TArray<FVector>& OutForces)
{
    // Ensure forces array is sized correctly
    if (OutForces.Num() != AtomPositions.Num())
    {
        OutForces.SetNum(AtomPositions.Num());
    }

    // Zero all forces
    for (FVector& Force : OutForces)
    {
        Force = FVector::ZeroVector;
    }

    // Calculate harmonic bond forces
    for (int32 i = 0; i < BondPairs.Num(); ++i)
    {
        int32 Atom1 = BondPairs[i].Key;
        int32 Atom2 = BondPairs[i].Value;

        if (!AtomPositions.IsValidIndex(Atom1) || !AtomPositions.IsValidIndex(Atom2))
            continue;

        int32 BondOrder = BondOrders.IsValidIndex(i) ? BondOrders[i] : 1;

        float K = GetBondSpringConstant(BondOrder);
        float R0 = GetEquilibriumBondLength(BondOrder, AtomElements[Atom1], AtomElements[Atom2]);

        FVector Force = CalculateBondForce(
            AtomPositions[Atom1],
            AtomPositions[Atom2],
            K, R0
        );

        // Apply equal and opposite forces
        OutForces[Atom1] += Force;
        OutForces[Atom2] -= Force;
    }
}

void FMDForceCalculator::CalculateNonBondedForces(
    const TArray<FVector>& Positions1,
    const TArray<FString>& Elements1,
    const TArray<FVector>& Positions2,
    const TArray<FString>& Elements2,
    TArray<FVector>& Forces1,
    TArray<FVector>& Forces2,
    float Cutoff)
{
    const float CutoffSquared = Cutoff * Cutoff;

    // Ensure force arrays are sized correctly
    if (Forces1.Num() != Positions1.Num())
        Forces1.SetNum(Positions1.Num());
    if (Forces2.Num() != Positions2.Num())
        Forces2.SetNum(Positions2.Num());

    // Calculate Lennard-Jones forces between all pairs
    for (int32 i = 0; i < Positions1.Num(); ++i)
    {
        for (int32 j = 0; j < Positions2.Num(); ++j)
        {
            // Skip if same atom (when calculating within same molecule)
            if (&Positions1 == &Positions2 && i == j)
                continue;

            const FVector& Pos1 = Positions1[i];
            const FVector& Pos2 = Positions2[j];

            // Distance cutoff check
            float DistSquared = FVector::DistSquared(Pos1, Pos2);
            if (DistSquared > CutoffSquared)
                continue;

            // Combining rules for LJ parameters (Lorentz-Berthelot)
            float Sigma1 = GetVdWRadius(Elements1[i]);
            float Sigma2 = GetVdWRadius(Elements2[j]);
            float Sigma = (Sigma1 + Sigma2) * 0.5f;

            float Epsilon1 = GetVdWEpsilon(Elements1[i]);
            float Epsilon2 = GetVdWEpsilon(Elements2[j]);
            float Epsilon = FMath::Sqrt(Epsilon1 * Epsilon2);

            FVector Force = CalculateLJForce(Pos1, Pos2, Sigma, Epsilon);

            Forces1[i] += Force;
            Forces2[j] -= Force;
        }
    }
}

// ==== Private Helper Functions ====

FVector FMDForceCalculator::CalculateLJForce(
    const FVector& Pos1,
    const FVector& Pos2,
    float Sigma,
    float Epsilon)
{
    FVector R = Pos2 - Pos1;
    float Distance = R.Size();

    if (Distance < 0.01f) // Avoid division by zero
        return FVector::ZeroVector;

    // Lennard-Jones 12-6 potential: V(r) = 4*ε*[(σ/r)¹² - (σ/r)⁶]
    // Force: F(r) = -dV/dr = 24*ε/r * [2*(σ/r)¹² - (σ/r)⁶]

    float SigmaOverR = Sigma / Distance;
    float SigmaOverR6 = FMath::Pow(SigmaOverR, 6.0f);
    float SigmaOverR12 = SigmaOverR6 * SigmaOverR6;

    float ForceMagnitude = 24.0f * Epsilon / Distance * (2.0f * SigmaOverR12 - SigmaOverR6);

    return R.GetSafeNormal() * ForceMagnitude;
}

FVector FMDForceCalculator::CalculateBondForce(
    const FVector& Pos1,
    const FVector& Pos2,
    float K,
    float R0)
{
    FVector R = Pos2 - Pos1;
    float Distance = R.Size();

    if (Distance < 0.01f) // Avoid division by zero
        return FVector::ZeroVector;

    // Harmonic potential: V(r) = K/2 * (r - r0)²
    // Force: F(r) = -dV/dr = -K * (r - r0)

    float ForceMagnitude = -K * (Distance - R0);

    return R.GetSafeNormal() * ForceMagnitude;
}
