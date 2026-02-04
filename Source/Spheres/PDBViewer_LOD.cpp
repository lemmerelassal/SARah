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

    if (!bEnableLODSystem || ForcedLODLevel >= 0)
        return;

    TimeSinceLastLODCheck += DeltaTime;

    // Only check LOD every LODCheckInterval seconds to avoid performance hit
    if (TimeSinceLastLODCheck >= LODCheckInterval)
    {
        UpdateLODLevel();
        TimeSinceLastLODCheck = 0.0f;
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
        // Medium detail - backbone bonds only
        TSet<FString> BackboneAtoms = {TEXT("CA"), TEXT("C"), TEXT("N"), TEXT("O")};
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
