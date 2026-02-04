// PDBViewer_Debug.cpp - Debug and logging functions

#include "PDBViewer.h"

namespace PDB
{
    constexpr float SCALE = 50.0f;
    constexpr float SPHERE_SIZE = 0.5f;
    constexpr float CYLINDER_SIZE = 0.1f;
    constexpr float BOND_OFFSET = 8.0f;
}

void APDBViewer::OnLigandsLoadedHandler()
{
    UE_LOG(LogTemp, Warning, TEXT("===== Ligands Loaded - Updating Hydrogen Visibility ====="));

    if (LigandMap.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No ligands in map"));
        return;
    }

    DebugPrintLigandInfo();

    // OPTIMIZED: Use consolidated helper for hydrogen visibility
    // OPTIMIZATION #12: Use const reference (not modifying map entries)
    for (const auto &Pair : LigandMap)
    {
        if (Pair.Value)
        {
            UpdateLigandHydrogenVisibility(Pair.Value, bHydrogensVisible);

            // ===== NEW: CREATE LIGAND ATOM LIGHTS =====
            CreateLigandAtomLights(Pair.Value);
            // ==========================================
        }
    }

    int32 TotalH = GetHydrogenCount();
    int32 TotalLights = GetLigandAtomLightCount();
    UE_LOG(LogTemp, Warning, TEXT("Total hydrogens: %d (Visible: %s)"),
           TotalH, bHydrogensVisible ? TEXT("YES") : TEXT("NO"));
    UE_LOG(LogTemp, Warning, TEXT("Total ligand atom lights: %d"), TotalLights);
}

void APDBViewer::DebugPrintLigandInfo()
{
    UE_LOG(LogTemp, Log, TEXT("=== Ligand Debug Info ==="));
    UE_LOG(LogTemp, Log, TEXT("Total ligands: %d"), LigandMap.Num());

    // OPTIMIZATION #12: Use const reference (read-only logging)
    for (const auto &Pair : LigandMap)
    {
        FLigandInfo *Info = Pair.Value;
        if (!Info)
        {
            UE_LOG(LogTemp, Warning, TEXT("Ligand '%s': NULL INFO"), *Pair.Key);
            continue;
        }

        UE_LOG(LogTemp, Log, TEXT("Ligand '%s':"), *Pair.Key);
        UE_LOG(LogTemp, Log, TEXT("  Name: %s"), *Info->LigandName);
        UE_LOG(LogTemp, Log, TEXT("  Visible: %s"), Info->bIsVisible ? TEXT("YES") : TEXT("NO"));
        UE_LOG(LogTemp, Log, TEXT("  Atom Positions: %d"), Info->AtomPositions.Num());
        UE_LOG(LogTemp, Log, TEXT("  Atom Elements: %d"), Info->AtomElements.Num());
        UE_LOG(LogTemp, Log, TEXT("  Bond Pairs: %d"), Info->BondPairs.Num());
        UE_LOG(LogTemp, Log, TEXT("  Bond Orders: %d"), Info->BondOrders.Num());
        UE_LOG(LogTemp, Log, TEXT("  Atom Meshes: %d"), Info->AtomMeshes.Num());
        UE_LOG(LogTemp, Log, TEXT("  Bond Meshes: %d"), Info->BondMeshes.Num());

        TMap<FString, int32> ElementCounts;
        for (const FString &Elem : Info->AtomElements)
            ElementCounts.FindOrAdd(Elem, 0)++;

        UE_LOG(LogTemp, Log, TEXT("  Elements:"));
        // OPTIMIZATION #12: Use const reference (read-only logging)
        for (const auto &ElemPair : ElementCounts)
            UE_LOG(LogTemp, Log, TEXT("    %s: %d"), *ElemPair.Key, ElemPair.Value);

        if (Info->BondOrders.Num() > 0)
        {
            TArray<int32> BondCounts = CountBondOrdersByType(Info->BondOrders);
            UE_LOG(LogTemp, Log, TEXT("  Bond Types: Single=%d, Double=%d, Triple=%d, Other=%d"),
                   BondCounts[0], BondCounts[1], BondCounts[2], BondCounts[3]);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Total hydrogens: %d"), GetHydrogenCount());
    UE_LOG(LogTemp, Log, TEXT("Hydrogens visible: %s"), bHydrogensVisible ? TEXT("YES") : TEXT("NO"));
    UE_LOG(LogTemp, Log, TEXT("======================"));
}
