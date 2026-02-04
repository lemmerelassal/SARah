// PDBViewer_Visibility.cpp - Visibility toggle functions

#include "PDBViewer.h"
#include "Components/StaticMeshComponent.h"

namespace PDB
{
    constexpr float SCALE = 50.0f;
    constexpr float SPHERE_SIZE = 0.5f;
    constexpr float CYLINDER_SIZE = 0.1f;
    constexpr float BOND_OFFSET = 8.0f;
}

void APDBViewer::ToggleResidueVisibility(const FString &Key)
{
    auto *Info = SafeDereference(ResidueMap.Find(Key));
    if (!Info)
        return;
    Info->bIsVisible = !Info->bIsVisible;
    // OPTIMIZATION #16: Use batched visibility updates
    SetMeshVisibilityBatched(Info->AtomMeshes, Info->bIsVisible);
    SetMeshVisibilityBatched(Info->BondMeshes, Info->bIsVisible);
}

void APDBViewer::ToggleLigandVisibility(const FString& LigandKey)
{
    // This is an alias for ToggleMoleculeVisibility
    ToggleMoleculeVisibility(LigandKey);
}

void APDBViewer::ToggleChainVisibility(const FString &ChainID)
{
    bool bNewVisibility = false;
    bool bFirstResidue = true;

    for (const auto &Pair : ResidueMap)
    {
        if (Pair.Value && Pair.Value->Chain == ChainID)
        {
            if (bFirstResidue)
            {
                bNewVisibility = !Pair.Value->bIsVisible;
                bFirstResidue = false;
            }

            Pair.Value->bIsVisible = bNewVisibility;
            // OPTIMIZATION #16: Use batched visibility updates
            SetMeshVisibilityBatched(Pair.Value->AtomMeshes, bNewVisibility);
            SetMeshVisibilityBatched(Pair.Value->BondMeshes, bNewVisibility);
        }
    }
}

void APDBViewer::ToggleNodeVisibility(UPDBTreeNode* Node)
{
    if (!Node)
        return;

    switch (Node->NodeType)
    {
        case EPDBNodeType::Chain:
            ToggleChainVisibility(Node->ChainID);
            break;

        case EPDBNodeType::ResiduesCategory:
        case EPDBNodeType::HeteroatomsCategory:
        case EPDBNodeType::WaterCategory:
        case EPDBNodeType::LigandsCategory:
            ToggleCategoryVisibility(Node);
            break;

        case EPDBNodeType::Residue:
            ToggleResidueVisibility(Node->NodeKey);
            break;

        case EPDBNodeType::Water:
        case EPDBNodeType::Ligand:
            ToggleLigandVisibility(Node->NodeKey);
            break;
    }
}

void APDBViewer::ToggleCategoryVisibility(UPDBTreeNode* Node)
{
    if (!Node)
        return;

    bool bNewVisibility = true;

    // Lambda to toggle residue category
    auto ToggleResidueCategory = [this, &Node, &bNewVisibility]() {
        // Determine new visibility
        for (auto& Pair : ResidueMap) {
            if (Pair.Value && Pair.Value->Chain == Node->ChainID) {
                bNewVisibility = !Pair.Value->bIsVisible;
                break;
            }
        }
        // Apply new visibility
        for (auto& Pair : ResidueMap) {
            if (Pair.Value && Pair.Value->Chain == Node->ChainID) {
                Pair.Value->bIsVisible = bNewVisibility;
                SetMeshVisibilityBatched(Pair.Value->AtomMeshes, bNewVisibility);
                SetMeshVisibilityBatched(Pair.Value->BondMeshes, bNewVisibility);
            }
        }
    };

    // Lambda to toggle ligand category with filter
    auto ToggleLigandCategory = [this, &Node, &bNewVisibility](TFunction<bool(const FString&)> KeyFilter) {
        // Determine new visibility
        for (auto& Pair : LigandMap) {
            if (!KeyFilter(Pair.Key)) continue;
            if (GetChainFromLigandKey(Pair.Key) == Node->ChainID && Pair.Value) {
                bNewVisibility = !Pair.Value->bIsVisible;
                break;
            }
        }
        // Apply new visibility
        for (auto& Pair : LigandMap) {
            if (!KeyFilter(Pair.Key)) continue;
            if (GetChainFromLigandKey(Pair.Key) == Node->ChainID && Pair.Value) {
                Pair.Value->bIsVisible = bNewVisibility;
                SetMeshVisibilityBatched(Pair.Value->AtomMeshes, bNewVisibility);
                SetMeshVisibilityBatched(Pair.Value->BondMeshes, bNewVisibility);
                UpdateLigandAtomLights(Pair.Value);
            }
        }
    };

    switch (Node->NodeType)
    {
        case EPDBNodeType::ResiduesCategory:
            ToggleResidueCategory();
            break;

        case EPDBNodeType::HeteroatomsCategory:
            ToggleLigandCategory([](const FString&) { return true; });  // All ligands
            break;

        case EPDBNodeType::WaterCategory:
            ToggleLigandCategory([this](const FString& K) { return IsWaterKey(K); });
            break;

        case EPDBNodeType::LigandsCategory:
            ToggleLigandCategory([this](const FString& K) { return !IsWaterKey(K); });
            break;

        default:
            break;
    }

    Node->bIsVisible = bNewVisibility;
}

void APDBViewer::ToggleMoleculeVisibility(const FString &MoleculeKey)
{
    auto *Info = SafeDereference(LigandMap.Find(MoleculeKey));
    if (!Info)
        return;
    bool bNewVisible = !Info->bIsVisible;

    if (bNewVisible)
    {
        for (auto &P : LigandMap)
        {
            if (P.Key != MoleculeKey && P.Value)
            {
                // Skip water molecules - keep them visible
                bool bIsWater = (P.Key.Contains(TEXT("HOH")) ||
                                 P.Key.Contains(TEXT("H2O")) ||
                                 P.Key.Contains(TEXT("WAT")));

                if (!bIsWater)
                {
                    P.Value->bIsVisible = false;
                    // OPTIMIZATION #16: Use batched visibility updates
                    SetMeshVisibilityBatched(P.Value->AtomMeshes, false);
                    SetMeshVisibilityBatched(P.Value->BondMeshes, false);

                    // ===== NEW: UPDATE LIGHTS =====
                    UpdateLigandAtomLights(P.Value);
                    // ==============================
                }
            }
        }
    }

    Info->bIsVisible = bNewVisible;

    // OPTIMIZATION #16: Use batched visibility updates
    SetMeshVisibilityBatched(Info->AtomMeshes, Info->bIsVisible);
    SetMeshVisibilityBatched(Info->BondMeshes, Info->bIsVisible);

    // ===== NEW: UPDATE LIGHTS =====
    UpdateLigandAtomLights(Info);
    // ==============================
}

void APDBViewer::ToggleMoleculeNodeVisibility(UPDBMoleculeNode *Node)
{
    if (!Node)
        return;

    ToggleMoleculeVisibility(Node->MoleculeKey);

    auto **InfoPtr = LigandMap.Find(Node->MoleculeKey);
    if (InfoPtr && *InfoPtr)
    {
        Node->bIsVisible = (*InfoPtr)->bIsVisible;
    }
}

// OPTIMIZATION #16: Batched visibility update helper
void APDBViewer::SetMeshVisibilityBatched(TArray<UStaticMeshComponent*>& Meshes, bool bVisible)
{
    // Batch visibility updates to avoid redundant state changes
    for (UStaticMeshComponent* Mesh : Meshes)
    {
        if (IsValid(Mesh) && Mesh->IsVisible() != bVisible)
        {
            Mesh->SetVisibility(bVisible);
        }
    }
}

bool APDBViewer::IsWaterKey(const FString& LigandKey) const
{
    // Key format is "ResidueName_ResidueSeq_Chain" e.g., "HOH_101_A"
    // FIX: Check the actual bIsWater flag instead of just the name prefix
    const FLigandInfo* Info = LigandMap.FindRef(LigandKey);
    if (Info)
    {
        // Use the actual bIsWater flag - this is more reliable
        return Info->bIsWater;
    }

    // Fallback for ligands without Info: check name prefix
    return LigandKey.StartsWith(TEXT("HOH_")) ||
           LigandKey.StartsWith(TEXT("H2O_")) ||
           LigandKey.StartsWith(TEXT("WAT_"));
}
