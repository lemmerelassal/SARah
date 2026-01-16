// PDBViewer_Interactions.cpp - Molecular Interaction Detection Implementation
// Add this to your existing PDBViewer.cpp or compile as separate file

#include "PDBViewer.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

// Define the PDB namespace constants if not already defined
namespace PDB
{
    constexpr float SCALE = 50.0f;
    constexpr float SPHERE_SIZE = 0.5f;
    constexpr float CYLINDER_SIZE = 0.1f;
    constexpr float BOND_OFFSET = 8.0f;
}

// ===== MAIN INTERACTION CALCULATION FUNCTION =====

void APDBViewer::CalculateAllInteractions(bool bProteinProtein, bool bProteinLigand)
{
    // Clear existing interactions
    ClearAllInteractions();
    
    UE_LOG(LogTemp, Log, TEXT("=== Calculating Molecular Interactions ==="));
    UE_LOG(LogTemp, Log, TEXT("Protein-Protein: %s, Protein-Ligand: %s"),
           bProteinProtein ? TEXT("YES") : TEXT("NO"),
           bProteinLigand ? TEXT("YES") : TEXT("NO"));
    
    // Detect different types of interactions
    DetectHydrogenBonds(bProteinProtein, bProteinLigand);
    DetectSaltBridges(bProteinProtein, bProteinLigand);
    DetectPiStacking(bProteinProtein, bProteinLigand);
    DetectHydrophobicInteractions(bProteinProtein, bProteinLigand);
    
    // Visualize all detected interactions
    for (const FMolecularInteraction& Interaction : DetectedInteractions)
    {
        DrawInteraction(Interaction);
    }
    
    UE_LOG(LogTemp, Log, TEXT("Total interactions detected: %d"), DetectedInteractions.Num());
    DebugPrintInteractions();
    
    // Initialize visibility for all interaction types
    InteractionVisibility.Empty();
    InteractionVisibility.Add(EInteractionType::HydrogenBond, true);
    InteractionVisibility.Add(EInteractionType::SaltBridge, true);
    InteractionVisibility.Add(EInteractionType::PiStacking, true);
    InteractionVisibility.Add(EInteractionType::Hydrophobic, true);
    InteractionVisibility.Add(EInteractionType::VanDerWaals, true);
    InteractionVisibility.Add(EInteractionType::Cation_Pi, true);
    
    OnInteractionsCalculated.Broadcast();
}

// ===== HYDROGEN BOND DETECTION =====

void APDBViewer::DetectHydrogenBonds(bool bProteinProtein, bool bProteinLigand)
{
    UE_LOG(LogTemp, Log, TEXT("Detecting hydrogen bonds..."));
    int32 InitialCount = DetectedInteractions.Num();
    
    // Lambda to check H-bonds between two sets of atoms
    auto CheckHBonds = [this](
        const TArray<FVector>& Positions1, const TArray<FString>& Elements1, 
        const TArray<FString>& AtomNames1, const FString& Key1,
        const TArray<FVector>& Positions2, const TArray<FString>& Elements2,
        const TArray<FString>& AtomNames2, const FString& Key2,
        bool bIsProteinLigand)
    {
        for (int32 i = 0; i < Positions1.Num(); ++i)
        {
            // Check if donor (with hydrogen)
            if (!IsHBondDonor(Elements1[i], AtomNames1[i]))
                continue;
            
            for (int32 j = 0; j < Positions2.Num(); ++j)
            {
                // Check if acceptor
                if (!IsHBondAcceptor(Elements2[j], AtomNames2[j]))
                    continue;
                
                float Distance = FVector::Dist(Positions1[i], Positions2[j]);
                
                if (Distance <= HBondMaxDistance)
                {
                    // For simplicity, we'll assume ideal geometry if distance is good
                    // In a full implementation, you'd want to find the actual H atom and check D-H-A angle
                    
                    FMolecularInteraction Interaction;
                    Interaction.Type = EInteractionType::HydrogenBond;
                    Interaction.Residue1 = Key1;
                    Interaction.Residue2 = Key2;
                    Interaction.Atom1 = AtomNames1[i];
                    Interaction.Atom2 = AtomNames2[j];
                    Interaction.Position1 = Positions1[i];
                    Interaction.Position2 = Positions2[j];
                    Interaction.Distance = Distance;
                    Interaction.Angle = 180.0f; // Ideal - should calculate from actual H
                    Interaction.Energy = -1.0f - (HBondMaxDistance - Distance) * 2.0f; // Simple approximation
                    Interaction.bIsProteinLigand = bIsProteinLigand;
                    
                    DetectedInteractions.Add(Interaction);
                }
            }
        }
    };
    
    // Protein-Protein H-bonds
    if (bProteinProtein)
    {
        TArray<FString> ResKeys;
        ResidueMap.GetKeys(ResKeys);
        
        for (int32 i = 0; i < ResKeys.Num(); ++i)
        {
            FResidueInfo* Res1 = ResidueMap[ResKeys[i]];
            if (!Res1 || !Res1->bIsVisible) continue;
            
            for (int32 j = i + 1; j < ResKeys.Num(); ++j)
            {
                FResidueInfo* Res2 = ResidueMap[ResKeys[j]];
                if (!Res2 || !Res2->bIsVisible) continue;
                
                // Skip if same residue
                if (ResKeys[i] == ResKeys[j]) continue;
                
                CheckHBonds(
                    Res1->AtomPositions, Res1->AtomElements, Res1->AtomNames, ResKeys[i],
                    Res2->AtomPositions, Res2->AtomElements, Res2->AtomNames, ResKeys[j],
                    false
                );
            }
        }
    }
    
    // Protein-Ligand H-bonds
    if (bProteinLigand)
    {
        TArray<FString> ResKeys, LigKeys;
        ResidueMap.GetKeys(ResKeys);
        LigandMap.GetKeys(LigKeys);
        
        for (const FString& ResKey : ResKeys)
        {
            FResidueInfo* Res = ResidueMap[ResKey];
            if (!Res || !Res->bIsVisible) continue;
            
            for (const FString& LigKey : LigKeys)
            {
                FLigandInfo* Lig = LigandMap[LigKey];
                if (!Lig || !Lig->bIsVisible) continue;
                
                // Check both directions (protein donor, ligand acceptor AND vice versa)
                CheckHBonds(
                    Res->AtomPositions, Res->AtomElements, Res->AtomNames, ResKey,
                    Lig->AtomPositions, Lig->AtomElements, Lig->AtomNames, LigKey,
                    true
                );
                
                CheckHBonds(
                    Lig->AtomPositions, Lig->AtomElements, Lig->AtomNames, LigKey,
                    Res->AtomPositions, Res->AtomElements, Res->AtomNames, ResKey,
                    true
                );
            }
        }
    }
    
    int32 HBondsFound = DetectedInteractions.Num() - InitialCount;
    UE_LOG(LogTemp, Log, TEXT("Found %d hydrogen bonds"), HBondsFound);
}

// ===== SALT BRIDGE DETECTION =====

void APDBViewer::DetectSaltBridges(bool bProteinProtein, bool bProteinLigand)
{
    UE_LOG(LogTemp, Log, TEXT("Detecting salt bridges..."));
    int32 InitialCount = DetectedInteractions.Num();
    
    auto CheckSaltBridge = [this](
        FResidueInfo* Res1, const FString& Key1,
        FResidueInfo* Res2, const FString& Key2,
        bool bIsProteinLigand)
    {
        bool bRes1Positive = IsPositivelyCharged(Res1->ResidueName);
        bool bRes1Negative = IsNegativelyCharged(Res1->ResidueName);
        bool bRes2Positive = IsPositivelyCharged(Res2->ResidueName);
        bool bRes2Negative = IsNegativelyCharged(Res2->ResidueName);
        
        // Need opposite charges
        if (!((bRes1Positive && bRes2Negative) || (bRes1Negative && bRes2Positive)))
            return;
        
        FVector Center1 = GetResidueCenterOfMass(Res1);
        FVector Center2 = GetResidueCenterOfMass(Res2);
        float Distance = FVector::Dist(Center1, Center2);
        
        if (Distance <= SaltBridgeMaxDistance)
        {
            FMolecularInteraction Interaction;
            Interaction.Type = EInteractionType::SaltBridge;
            Interaction.Residue1 = Key1;
            Interaction.Residue2 = Key2;
            Interaction.Atom1 = TEXT("CENTER");
            Interaction.Atom2 = TEXT("CENTER");
            Interaction.Position1 = Center1;
            Interaction.Position2 = Center2;
            Interaction.Distance = Distance;
            Interaction.Energy = -3.0f - (SaltBridgeMaxDistance - Distance) * 5.0f; // Strong interaction
            Interaction.bIsProteinLigand = bIsProteinLigand;
            
            DetectedInteractions.Add(Interaction);
        }
    };
    
    // Protein-Protein salt bridges
    if (bProteinProtein)
    {
        TArray<FString> ResKeys;
        ResidueMap.GetKeys(ResKeys);
        
        for (int32 i = 0; i < ResKeys.Num(); ++i)
        {
            FResidueInfo* Res1 = ResidueMap[ResKeys[i]];
            if (!Res1 || !Res1->bIsVisible) continue;
            
            for (int32 j = i + 1; j < ResKeys.Num(); ++j)
            {
                FResidueInfo* Res2 = ResidueMap[ResKeys[j]];
                if (!Res2 || !Res2->bIsVisible) continue;
                
                CheckSaltBridge(Res1, ResKeys[i], Res2, ResKeys[j], false);
            }
        }
    }
    
    int32 SaltBridgesFound = DetectedInteractions.Num() - InitialCount;
    UE_LOG(LogTemp, Log, TEXT("Found %d salt bridges"), SaltBridgesFound);
}

// ===== PI-STACKING DETECTION =====

void APDBViewer::DetectPiStacking(bool bProteinProtein, bool bProteinLigand)
{
    UE_LOG(LogTemp, Log, TEXT("Detecting pi-stacking interactions..."));
    int32 InitialCount = DetectedInteractions.Num();
    
    // Lambda to check pi-stacking between two aromatic systems
    auto CheckPiStacking = [this](
        const FVector& Center1, const FVector& Normal1, const FString& Key1,
        const FVector& Center2, const FVector& Normal2, const FString& Key2,
        bool bIsProteinLigand)
    {
        float Distance = FVector::Dist(Center1, Center2);
        
        if (Distance <= PiStackingMaxDistance)
        {
            // Calculate angle between ring normals
            float CosAngle = FMath::Abs(FVector::DotProduct(Normal1, Normal2));
            float Angle = FMath::Acos(FMath::Clamp(CosAngle, 0.0f, 1.0f)) * 180.0f / PI;
            
            // Face-to-face stacking: normals are parallel (angle ~0° or ~180°)
            // T-shaped stacking: normals are perpendicular (angle ~90°)
            bool bIsFaceToFace = (Angle < 30.0f || Angle > 150.0f);
            bool bIsTShaped = (Angle > 60.0f && Angle < 120.0f);
            
            if (bIsFaceToFace || bIsTShaped)
            {
                FMolecularInteraction Interaction;
                Interaction.Type = EInteractionType::PiStacking;
                Interaction.Residue1 = Key1;
                Interaction.Residue2 = Key2;
                Interaction.Atom1 = TEXT("RING");
                Interaction.Atom2 = TEXT("RING");
                Interaction.Position1 = Center1;
                Interaction.Position2 = Center2;
                Interaction.Distance = Distance;
                Interaction.Angle = Angle;
                Interaction.Energy = -2.0f - (PiStackingMaxDistance - Distance) * 3.0f;
                Interaction.bIsProteinLigand = bIsProteinLigand;
                
                DetectedInteractions.Add(Interaction);
                
                UE_LOG(LogTemp, Log, TEXT("Pi-Stacking (%s): %s <-> %s | Dist: %.2f A | Angle: %.1f° | Type: %s"),
                       bIsProteinLigand ? TEXT("Prot-Lig") : TEXT("Prot-Prot"),
                       *Key1, *Key2, Distance, Angle,
                       bIsTShaped ? TEXT("T-shaped") : TEXT("Face-to-face"));
            }
        }
    };
    
    // Protein-Protein pi-stacking
    if (bProteinProtein)
    {
        TArray<FString> ResKeys;
        ResidueMap.GetKeys(ResKeys);
        
        for (int32 i = 0; i < ResKeys.Num(); ++i)
        {
            FResidueInfo* Res1 = ResidueMap[ResKeys[i]];
            if (!Res1 || !Res1->bIsVisible || !IsAromatic(Res1->ResidueName)) continue;
            
            FVector Center1, Normal1;
            if (!GetAromaticRingCenter(Res1, Center1, Normal1)) continue;
            
            for (int32 j = i + 1; j < ResKeys.Num(); ++j)
            {
                FResidueInfo* Res2 = ResidueMap[ResKeys[j]];
                if (!Res2 || !Res2->bIsVisible || !IsAromatic(Res2->ResidueName)) continue;
                
                FVector Center2, Normal2;
                if (!GetAromaticRingCenter(Res2, Center2, Normal2)) continue;
                
                CheckPiStacking(Center1, Normal1, ResKeys[i], Center2, Normal2, ResKeys[j], false);
            }
        }
    }
    
    // Protein-Ligand pi-stacking
    if (bProteinLigand)
    {
        TArray<FString> ResKeys, LigKeys;
        ResidueMap.GetKeys(ResKeys);
        LigandMap.GetKeys(LigKeys);
        
        for (const FString& ResKey : ResKeys)
        {
            FResidueInfo* Res = ResidueMap[ResKey];
            if (!Res || !Res->bIsVisible || !IsAromatic(Res->ResidueName)) continue;
            
            FVector ResCenter, ResNormal;
            if (!GetAromaticRingCenter(Res, ResCenter, ResNormal)) continue;
            
            for (const FString& LigKey : LigKeys)
            {
                FLigandInfo* Lig = LigandMap[LigKey];
                if (!Lig || !Lig->bIsVisible) continue;
                
                FVector LigCenter, LigNormal;
                if (!GetAromaticRingCenter(Lig, LigCenter, LigNormal)) continue;
                
                CheckPiStacking(ResCenter, ResNormal, ResKey, LigCenter, LigNormal, LigKey, true);
            }
        }
    }
    
    int32 PiStackingFound = DetectedInteractions.Num() - InitialCount;
    UE_LOG(LogTemp, Log, TEXT("Found %d pi-stacking interactions"), PiStackingFound);
}

// ===== HYDROPHOBIC INTERACTIONS =====

void APDBViewer::DetectHydrophobicInteractions(bool bProteinProtein, bool bProteinLigand)
{
    UE_LOG(LogTemp, Log, TEXT("Detecting hydrophobic interactions..."));
    int32 InitialCount = DetectedInteractions.Num();
    
    // Protein-Protein hydrophobic interactions
    if (bProteinProtein)
    {
        TArray<FString> ResKeys;
        ResidueMap.GetKeys(ResKeys);
        
        for (int32 i = 0; i < ResKeys.Num(); ++i)
        {
            FResidueInfo* Res1 = ResidueMap[ResKeys[i]];
            if (!Res1 || !Res1->bIsVisible || !IsHydrophobic(Res1->ResidueName)) continue;
            
            FVector Center1 = GetResidueCenterOfMass(Res1);
            
            for (int32 j = i + 1; j < ResKeys.Num(); ++j)
            {
                FResidueInfo* Res2 = ResidueMap[ResKeys[j]];
                if (!Res2 || !Res2->bIsVisible || !IsHydrophobic(Res2->ResidueName)) continue;
                
                FVector Center2 = GetResidueCenterOfMass(Res2);
                float Distance = FVector::Dist(Center1, Center2);
                
                if (Distance <= HydrophobicMaxDistance)
                {
                    FMolecularInteraction Interaction;
                    Interaction.Type = EInteractionType::Hydrophobic;
                    Interaction.Residue1 = ResKeys[i];
                    Interaction.Residue2 = ResKeys[j];
                    Interaction.Atom1 = TEXT("CENTER");
                    Interaction.Atom2 = TEXT("CENTER");
                    Interaction.Position1 = Center1;
                    Interaction.Position2 = Center2;
                    Interaction.Distance = Distance;
                    Interaction.Energy = -0.5f - (HydrophobicMaxDistance - Distance) * 1.0f;
                    Interaction.bIsProteinLigand = false;
                    
                    DetectedInteractions.Add(Interaction);
                }
            }
        }
    }
    
    int32 HydrophobicFound = DetectedInteractions.Num() - InitialCount;
    UE_LOG(LogTemp, Log, TEXT("Found %d hydrophobic interactions"), HydrophobicFound);
}

// ===== HELPER FUNCTIONS =====

bool APDBViewer::IsHBondDonor(const FString& Element, const FString& AtomName) const
{
    // Nitrogen and Oxygen with hydrogens can be donors
    if (Element == TEXT("N") || Element == TEXT("O"))
        return true;
    
    // Also check for specific backbone atoms
    if (AtomName == TEXT("N") || AtomName == TEXT("NH1") || AtomName == TEXT("NH2") ||
        AtomName == TEXT("NE") || AtomName == TEXT("NE1") || AtomName == TEXT("NE2") ||
        AtomName == TEXT("ND1") || AtomName == TEXT("ND2") || AtomName == TEXT("NZ"))
        return true;
    
    if (AtomName == TEXT("OH") || AtomName == TEXT("OG") || AtomName == TEXT("OG1"))
        return true;
    
    return false;
}

bool APDBViewer::IsHBondAcceptor(const FString& Element, const FString& AtomName) const
{
    // Oxygen, Nitrogen, and sometimes Sulfur can be acceptors
    if (Element == TEXT("O") || Element == TEXT("N") || Element == TEXT("S"))
        return true;
    
    // Backbone carbonyl oxygen
    if (AtomName == TEXT("O") || AtomName == TEXT("OXT"))
        return true;
    
    // Side chain oxygens
    if (AtomName.StartsWith(TEXT("O")))
        return true;
    
    return false;
}

bool APDBViewer::IsPositivelyCharged(const FString& ResidueName) const
{
    // Lysine, Arginine, Histidine
    return (ResidueName == TEXT("LYS") || ResidueName == TEXT("ARG") || 
            ResidueName == TEXT("HIS") || ResidueName == TEXT("HSD") || 
            ResidueName == TEXT("HSE") || ResidueName == TEXT("HSP"));
}

bool APDBViewer::IsNegativelyCharged(const FString& ResidueName) const
{
    // Aspartate, Glutamate
    return (ResidueName == TEXT("ASP") || ResidueName == TEXT("GLU"));
}

bool APDBViewer::IsAromatic(const FString& ResidueName) const
{
    // Phenylalanine, Tyrosine, Tryptophan, Histidine
    return (ResidueName == TEXT("PHE") || ResidueName == TEXT("TYR") || 
            ResidueName == TEXT("TRP") || ResidueName == TEXT("HIS") ||
            ResidueName == TEXT("HSD") || ResidueName == TEXT("HSE"));
}

bool APDBViewer::IsHydrophobic(const FString& ResidueName) const
{
    // Aliphatic and aromatic hydrophobic residues
    return (ResidueName == TEXT("ALA") || ResidueName == TEXT("VAL") || 
            ResidueName == TEXT("ILE") || ResidueName == TEXT("LEU") ||
            ResidueName == TEXT("MET") || ResidueName == TEXT("PHE") ||
            ResidueName == TEXT("TRP") || ResidueName == TEXT("PRO"));
}


// OPTIMIZED: Non-const to allow caching
FVector APDBViewer::GetResidueCenterOfMass(FResidueInfo* ResInfo)
{
    if (!ResInfo || ResInfo->AtomPositions.Num() == 0)
        return FVector::ZeroVector;
    
    // Check cache first
    if (ResInfo->bCenterOfMassCached)
        return ResInfo->CachedCenterOfMass;
    
    // Calculate and cache
    FVector Sum = FVector::ZeroVector;
    for (const FVector& Pos : ResInfo->AtomPositions)
        Sum += Pos;
    
    ResInfo->CachedCenterOfMass = Sum / ResInfo->AtomPositions.Num();
    ResInfo->bCenterOfMassCached = true;
    
    return ResInfo->CachedCenterOfMass;
}

// OPTIMIZED: Non-const to allow caching
FVector APDBViewer::GetLigandCenterOfMass(FLigandInfo* LigInfo)
{
    if (!LigInfo || LigInfo->AtomPositions.Num() == 0)
        return FVector::ZeroVector;
    
    // Check cache first
    if (LigInfo->bCenterOfMassCached)
        return LigInfo->CachedCenterOfMass;
    
    // Calculate and cache
    FVector Sum = FVector::ZeroVector;
    for (const FVector& Pos : LigInfo->AtomPositions)
        Sum += Pos;
    
    LigInfo->CachedCenterOfMass = Sum / LigInfo->AtomPositions.Num();
    LigInfo->bCenterOfMassCached = true;
    
    return LigInfo->CachedCenterOfMass;
}

// OPTIMIZED: Non-const to allow caching
bool APDBViewer::GetAromaticRingCenter(FResidueInfo* ResInfo, FVector& OutCenter, FVector& OutNormal)
{
    if (!ResInfo)
        return false;
    
    // Check cache first
    if (ResInfo->bAromaticCenterCached)
    {
        OutCenter = ResInfo->CachedAromaticCenter;
        OutNormal = ResInfo->CachedAromaticNormal;
        return true;
    }
    
    // Find aromatic ring atoms based on residue type
    TArray<FVector> RingAtoms;
    
    if (ResInfo->ResidueName == TEXT("PHE"))
    {
        // Phenylalanine: CG, CD1, CD2, CE1, CE2, CZ
        for (int32 i = 0; i < ResInfo->AtomNames.Num(); ++i)
        {
            const FString& Name = ResInfo->AtomNames[i];
            if (Name == TEXT("CG") || Name == TEXT("CD1") || Name == TEXT("CD2") ||
                Name == TEXT("CE1") || Name == TEXT("CE2") || Name == TEXT("CZ"))
            {
                RingAtoms.Add(ResInfo->AtomPositions[i]);
            }
        }
    }
    else if (ResInfo->ResidueName == TEXT("TYR"))
    {
        // Tyrosine: CG, CD1, CD2, CE1, CE2, CZ
        for (int32 i = 0; i < ResInfo->AtomNames.Num(); ++i)
        {
            const FString& Name = ResInfo->AtomNames[i];
            if (Name == TEXT("CG") || Name == TEXT("CD1") || Name == TEXT("CD2") ||
                Name == TEXT("CE1") || Name == TEXT("CE2") || Name == TEXT("CZ"))
            {
                RingAtoms.Add(ResInfo->AtomPositions[i]);
            }
        }
    }
    else if (ResInfo->ResidueName == TEXT("TRP"))
    {
        // Tryptophan: use 5-membered ring CG, CD1, NE1, CE2, CD2
        for (int32 i = 0; i < ResInfo->AtomNames.Num(); ++i)
        {
            const FString& Name = ResInfo->AtomNames[i];
            if (Name == TEXT("CG") || Name == TEXT("CD1") || Name == TEXT("NE1") ||
                Name == TEXT("CE2") || Name == TEXT("CD2"))
            {
                RingAtoms.Add(ResInfo->AtomPositions[i]);
            }
        }
    }
    else if (ResInfo->ResidueName == TEXT("HIS") || ResInfo->ResidueName.StartsWith(TEXT("HS")))
    {
        // Histidine: CG, ND1, CE1, NE2, CD2
        for (int32 i = 0; i < ResInfo->AtomNames.Num(); ++i)
        {
            const FString& Name = ResInfo->AtomNames[i];
            if (Name == TEXT("CG") || Name == TEXT("ND1") || Name == TEXT("CE1") ||
                Name == TEXT("NE2") || Name == TEXT("CD2"))
            {
                RingAtoms.Add(ResInfo->AtomPositions[i]);
            }
        }
    }
    
    if (RingAtoms.Num() < 3)
        return false;
    
    // Calculate center
    OutCenter = FVector::ZeroVector;
    for (const FVector& Pos : RingAtoms)
        OutCenter += Pos;
    OutCenter /= RingAtoms.Num();
    
    // Calculate normal (simplified - use first 3 atoms)
    if (RingAtoms.Num() >= 3)
    {
        FVector V1 = RingAtoms[1] - RingAtoms[0];
        FVector V2 = RingAtoms[2] - RingAtoms[0];
        OutNormal = FVector::CrossProduct(V1, V2).GetSafeNormal();
    }
    else
    {
        OutNormal = FVector::UpVector;
    }
    
    // Cache the results
    ResInfo->CachedAromaticCenter = OutCenter;
    ResInfo->CachedAromaticNormal = OutNormal;
    ResInfo->bAromaticCenterCached = true;
    
    return true;
}

// OPTIMIZED: Non-const to allow caching
bool APDBViewer::GetAromaticRingCenter(FLigandInfo* LigInfo, FVector& OutCenter, FVector& OutNormal)
{
    if (!LigInfo || LigInfo->AtomPositions.Num() < 3)
        return false;
    
    // Check cache first
    if (LigInfo->bAromaticCenterCached)
    {
        OutCenter = LigInfo->CachedAromaticCenter;
        OutNormal = LigInfo->CachedAromaticNormal;
        return true;
    }
    
    // Simplified: Use all carbon atoms as potential ring atoms
    TArray<FVector> RingAtoms;
    for (int32 i = 0; i < LigInfo->AtomElements.Num(); ++i)
    {
        if (LigInfo->AtomElements[i] == TEXT("C"))
            RingAtoms.Add(LigInfo->AtomPositions[i]);
    }
    
    if (RingAtoms.Num() < 3)
        return false;
    
    OutCenter = FVector::ZeroVector;
    for (const FVector& Pos : RingAtoms)
        OutCenter += Pos;
    OutCenter /= RingAtoms.Num();
    
    if (RingAtoms.Num() >= 3)
    {
        FVector V1 = RingAtoms[1] - RingAtoms[0];
        FVector V2 = RingAtoms[2] - RingAtoms[0];
        OutNormal = FVector::CrossProduct(V1, V2).GetSafeNormal();
    }
    else
    {
        OutNormal = FVector::UpVector;
    }
    
    // Cache the results
    LigInfo->CachedAromaticCenter = OutCenter;
    LigInfo->CachedAromaticNormal = OutNormal;
    LigInfo->bAromaticCenterCached = true;
    
    return true;
}

float APDBViewer::CalculateAngle(const FVector& A, const FVector& B, const FVector& C) const
{
    FVector BA = (A - B).GetSafeNormal();
    FVector BC = (C - B).GetSafeNormal();
    float CosAngle = FVector::DotProduct(BA, BC);
    return FMath::Acos(FMath::Clamp(CosAngle, -1.0f, 1.0f)) * 180.0f / PI;
}

// ===== VISUALIZATION FUNCTIONS =====

void APDBViewer::DrawInteraction(const FMolecularInteraction& Interaction)
{
    if (!SphereMeshAsset || !SphereMaterialAsset)
        return;
    
    // Scale positions for rendering
    FVector Start = Interaction.Position1 * PDB::SCALE;
    FVector End = Interaction.Position2 * PDB::SCALE;
    
    FVector Direction = End - Start;
    float Length = Direction.Size();
    
    if (Length < 0.1f)
        return;
    
    // Create dashed line effect using spheres (5 spheres with gaps)
    int32 NumSpheres = 5;
    float SegmentLength = Length / (NumSpheres * 2 - 1);
    
    for (int32 i = 0; i < NumSpheres; ++i)
    {
        // Calculate sphere position along the interaction line
        FVector SpherePos = Start + Direction.GetSafeNormal() * (i * 2 * SegmentLength + SegmentLength * 0.5f);
        
        // Create sphere component
        UStaticMeshComponent* Sphere = NewObject<UStaticMeshComponent>(this);
        Sphere->SetStaticMesh(SphereMeshAsset);
        Sphere->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
        Sphere->RegisterComponent();
        
        // Position and scale the sphere
        Sphere->SetWorldLocation(SpherePos);
        Sphere->SetWorldScale3D(FVector(0.15f)); // Small sphere size
        
        // Create material and set color
        UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(SphereMaterialAsset, Sphere);
        FLinearColor Color = GetInteractionColor(Interaction.Type);
        Mat->SetVectorParameterValue(TEXT("Color"), Color);
        Sphere->SetMaterial(0, Mat);
        
        InteractionMeshes.Add(Sphere);
    }
}

FLinearColor APDBViewer::GetInteractionColor(EInteractionType Type) const
{
    switch (Type)
    {
        case EInteractionType::HydrogenBond:
            return FLinearColor(0.0f, 0.8f, 1.0f); // Cyan
        case EInteractionType::SaltBridge:
            return FLinearColor(1.0f, 1.0f, 0.0f); // Yellow
        case EInteractionType::PiStacking:
            return FLinearColor(1.0f, 0.5f, 0.0f); // Orange
        case EInteractionType::Hydrophobic:
            return FLinearColor(0.5f, 0.5f, 0.5f); // Gray
        case EInteractionType::Cation_Pi:
            return FLinearColor(1.0f, 0.0f, 1.0f); // Magenta
        default:
            return FLinearColor(1.0f, 1.0f, 1.0f); // White
    }
}

// ===== INTERACTION MANAGEMENT FUNCTIONS =====

void APDBViewer::ClearAllInteractions()
{
    DetectedInteractions.Empty();
    
    for (UStaticMeshComponent* Mesh : InteractionMeshes)
    {
        if (Mesh && IsValid(Mesh))
            Mesh->DestroyComponent();
    }
    InteractionMeshes.Empty();
}

void APDBViewer::ToggleInteractionType(EInteractionType Type, bool bVisible)
{
    InteractionVisibility.Add(Type, bVisible);
    
    // Update visibility of interaction meshes
    int32 MeshIndex = 0;
    for (const FMolecularInteraction& Interaction : DetectedInteractions)
    {
        if (Interaction.Type == Type)
        {
            // Each interaction has 5 mesh segments
            for (int32 i = 0; i < 5 && MeshIndex < InteractionMeshes.Num(); ++i, ++MeshIndex)
            {
                if (InteractionMeshes[MeshIndex])
                    InteractionMeshes[MeshIndex]->SetVisibility(bVisible);
            }
        }
        else
        {
            MeshIndex += 5; // Skip meshes for other interaction types
        }
    }
}

void APDBViewer::ShowAllInteractions(bool bVisible)
{
    for (UStaticMeshComponent* Mesh : InteractionMeshes)
    {
        if (Mesh)
            Mesh->SetVisibility(bVisible);
    }
    
    for (auto& Pair : InteractionVisibility)
        Pair.Value = bVisible;
}

TArray<FMolecularInteraction> APDBViewer::GetInteractionsByType(EInteractionType Type) const
{
    TArray<FMolecularInteraction> Result;
    for (const FMolecularInteraction& Interaction : DetectedInteractions)
    {
        if (Interaction.Type == Type)
            Result.Add(Interaction);
    }
    return Result;
}

TArray<FMolecularInteraction> APDBViewer::GetInteractionsForResidue(const FString& ResidueKey) const
{
    TArray<FMolecularInteraction> Result;
    for (const FMolecularInteraction& Interaction : DetectedInteractions)
    {
        if (Interaction.Residue1 == ResidueKey || Interaction.Residue2 == ResidueKey)
            Result.Add(Interaction);
    }
    return Result;
}

void APDBViewer::DebugPrintInteractions()
{
    UE_LOG(LogTemp, Log, TEXT("=== Interaction Summary ==="));
    
    TMap<EInteractionType, int32> TypeCounts;
    for (const FMolecularInteraction& Interaction : DetectedInteractions)
    {
        int32& Count = TypeCounts.FindOrAdd(Interaction.Type, 0);
        Count++;
    }
    
    UE_LOG(LogTemp, Log, TEXT("Hydrogen Bonds: %d"), TypeCounts.FindRef(EInteractionType::HydrogenBond));
    UE_LOG(LogTemp, Log, TEXT("Salt Bridges: %d"), TypeCounts.FindRef(EInteractionType::SaltBridge));
    UE_LOG(LogTemp, Log, TEXT("Pi-Stacking: %d"), TypeCounts.FindRef(EInteractionType::PiStacking));
    UE_LOG(LogTemp, Log, TEXT("Hydrophobic: %d"), TypeCounts.FindRef(EInteractionType::Hydrophobic));
    UE_LOG(LogTemp, Log, TEXT("Cation-Pi: %d"), TypeCounts.FindRef(EInteractionType::Cation_Pi));
    
    // Print detailed info for protein-ligand interactions
    UE_LOG(LogTemp, Log, TEXT("=== Protein-Ligand Interactions ==="));
    for (const FMolecularInteraction& Interaction : DetectedInteractions)
    {
        if (Interaction.bIsProteinLigand)
        {
            FString TypeStr;
            switch (Interaction.Type)
            {
                case EInteractionType::HydrogenBond: TypeStr = TEXT("H-Bond"); break;
                case EInteractionType::SaltBridge: TypeStr = TEXT("Salt Bridge"); break;
                case EInteractionType::PiStacking: TypeStr = TEXT("Pi-Stack"); break;
                case EInteractionType::Hydrophobic: TypeStr = TEXT("Hydrophobic"); break;
                default: TypeStr = TEXT("Other"); break;
            }
            
            UE_LOG(LogTemp, Log, TEXT("%s: %s (%s) <-> %s (%s) | Dist: %.2f A | Energy: %.2f kcal/mol"),
                   *TypeStr,
                   *Interaction.Residue1, *Interaction.Atom1,
                   *Interaction.Residue2, *Interaction.Atom2,
                   Interaction.Distance,
                   Interaction.Energy);
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("==========================="));
}
