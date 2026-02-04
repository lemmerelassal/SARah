// PDBViewer_Hydrogen.cpp - Hydrogen atom generation and visibility management

#include "PDBViewer.h"
#include "HydrogenGenerator.h"
#include "Components/StaticMeshComponent.h"

namespace PDB
{
    constexpr float SCALE = 50.0f;
    constexpr float SPHERE_SIZE = 0.5f;
    constexpr float CYLINDER_SIZE = 0.1f;
    constexpr float BOND_OFFSET = 8.0f;
}

void APDBViewer::GenerateHydrogensForResidue(FResidueInfo *ResInfo)
{
    if (!ResInfo || ResInfo->AtomPositions.Num() == 0)
        return;

    // Check if hydrogens already exist
    bool bHasHydrogens = ResInfo->AtomElements.Contains(TEXT("H"));
    if (bHasHydrogens)
    {
        UE_LOG(LogTemp, Log, TEXT("Residue %s already has hydrogens"), *ResInfo->ResidueName);
        return;
    }

    if (ResInfo->BondPairs.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Residue %s has no bonds - cannot generate hydrogens"),
               *ResInfo->ResidueName);
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("Generating hydrogens for residue: %s %s"),
           *ResInfo->ResidueName, *ResInfo->ResidueSeq);

    TArray<TPair<FVector, int32>> Hydrogens = FHydrogenGenerator::GenerateHydrogens(
        ResInfo->AtomPositions, ResInfo->AtomElements, ResInfo->BondPairs, ResInfo->BondOrders);

    UE_LOG(LogTemp, Log, TEXT("  Generated %d hydrogens"), Hydrogens.Num());

    for (const auto &HPair : Hydrogens)
    {
        int32 ParentIdx = HPair.Value;

        // Skip hydrogens on backbone atoms that have inter-residue bonds
        if (ResInfo->AtomNames.IsValidIndex(ParentIdx))
        {
            FString AtomName = ResInfo->AtomNames[ParentIdx];

            // Skip backbone carbonyl carbon (C) - bonds to next residue's N
            if (AtomName == TEXT("C"))
            {
                continue;
            }

            // Skip backbone nitrogen (N) - bonds to previous residue's C
            // It should only have 1 H, which might already be in the structure
            if (AtomName == TEXT("N"))
            {
                continue;
            }
        }

        // Store UNSCALED hydrogen position
        int32 HIdx = ResInfo->AtomPositions.Add(HPair.Key);
        ResInfo->AtomElements.Add(TEXT("H"));
        // OPTIMIZATION #9: Use Appendf for integer formatting
        ResInfo->AtomNames.Add(TStringBuilder<16>().Appendf(TEXT("H%d"), HIdx).ToString()); // Add atom name

        // Apply scaling only when drawing
        FVector ScaledHPos = HPair.Key * PDB::SCALE;
        // Scale both positions for drawing the bond
        FVector ScaledParent = ResInfo->AtomPositions[ParentIdx] * PDB::SCALE;
        DrawBond(ScaledParent, ScaledHPos, 1,
                 ResInfo->AtomElements[ParentIdx], TEXT("H"),
                 GetRootComponent(), ResInfo->BondMeshes);
        ResInfo->BondMeshes.Last()->SetVisibility(bHydrogensVisible && ResInfo->bIsVisible);

        ResInfo->BondPairs.Add(TPair<int32, int32>(ParentIdx, HIdx));
        ResInfo->BondOrders.Add(1);
    }
}

// ===== OPTIMIZATION: Consolidated Hydrogen Visibility Helpers =====
void APDBViewer::UpdateLigandHydrogenVisibility(FLigandInfo* LigInfo, bool bVisible)
{
    UpdateStructureHydrogenVisibility(LigInfo, bVisible);
}

void APDBViewer::UpdateResidueHydrogenVisibility(FResidueInfo* ResInfo, bool bVisible)
{
    UpdateStructureHydrogenVisibility(ResInfo, bVisible);
}

void APDBViewer::AddExplicitHydrogens()
{
    if (bHydrogensVisible)
        return;

    UpdateAllHydrogenVisibility(true);
    bHydrogensVisible = true;
    UE_LOG(LogTemp, Log, TEXT("Showed %d hydrogens"), GetHydrogenCount());
}

void APDBViewer::RemoveExplicitHydrogens()
{
    if (!bHydrogensVisible)
        return;

    UpdateAllHydrogenVisibility(false);
    bHydrogensVisible = false;
    UE_LOG(LogTemp, Log, TEXT("Hid %d hydrogens"), GetHydrogenCount());
}

void APDBViewer::ToggleHydrogens()
{
    bHydrogensVisible ? RemoveExplicitHydrogens() : AddExplicitHydrogens();
}

int32 APDBViewer::AddHydrogensToLigand(FLigandInfo *LigInfo) { return 0; } // Deprecated

int32 APDBViewer::GetHydrogenCount() const
{
    auto CountHydrogens = [](const auto& InfoMap) {
        int32 Count = 0;
        for (const auto& Pair : InfoMap)
            if (Pair.Value)
                for (const FString& Elem : Pair.Value->AtomElements)
                    if (Elem == TEXT("H"))
                        Count++;
        return Count;
    };

    return CountHydrogens(LigandMap) + CountHydrogens(ResidueMap);
}
