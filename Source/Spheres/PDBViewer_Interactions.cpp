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
    
    // Analyze H-bond networks
    AnalyzeHBondNetworks();
    
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
            bool bIsDonor = IsHBondDonor(Elements1[i], AtomNames1[i]);
            
            // DEBUG: Log donor checks for ligands
            if (bIsProteinLigand && Elements1[i] == TEXT("N"))
            {
                UE_LOG(LogTemp, Log, TEXT("  Checking donor: %s atom %s (element %s) -> IsDonor: %s"),
                       *Key1, *AtomNames1[i], *Elements1[i], bIsDonor ? TEXT("YES") : TEXT("NO"));
            }
            
            if (!bIsDonor)
                continue;
            
            for (int32 j = 0; j < Positions2.Num(); ++j)
            {
                // Check if acceptor
                bool bIsAcceptor = IsHBondAcceptor(Elements2[j], AtomNames2[j]);
                
                if (!bIsAcceptor)
                    continue;
                
                float Distance = FVector::Dist(Positions1[i], Positions2[j]);
                
                // DEBUG: Log close pairs
                if (bIsProteinLigand && Distance <= HBondMaxDistance + 1.0f)
                {
                    UE_LOG(LogTemp, Log, TEXT("  Potential H-bond: %s:%s (%s) -> %s:%s (%s) | Dist: %.2f | Max: %.2f"),
                           *Key1, *AtomNames1[i], *Elements1[i],
                           *Key2, *AtomNames2[j], *Elements2[j],
                           Distance, HBondMaxDistance);
                }
                
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
                    
                    UE_LOG(LogTemp, Warning, TEXT("  *** H-BOND DETECTED: %s:%s -> %s:%s | %.2f A ***"),
                           *Key1, *AtomNames1[i], *Key2, *AtomNames2[j], Distance);
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
        
        UE_LOG(LogTemp, Warning, TEXT("=== Checking Protein-Ligand H-bonds ==="));
        UE_LOG(LogTemp, Warning, TEXT("Protein residues: %d, Ligands: %d"), ResKeys.Num(), LigKeys.Num());
        
        for (const FString& LigKey : LigKeys)
        {
            FLigandInfo* Lig = LigandMap[LigKey];
            if (!Lig || !Lig->bIsVisible) continue;
            
            UE_LOG(LogTemp, Warning, TEXT("Ligand %s: %d atoms"), *LigKey, Lig->AtomNames.Num());
            
            // Log all ligand atoms
            for (int32 i = 0; i < Lig->AtomNames.Num(); ++i)
            {
                UE_LOG(LogTemp, Log, TEXT("  Ligand atom %d: Name='%s' Element='%s'"),
                       i, *Lig->AtomNames[i], *Lig->AtomElements[i]);
            }
            
            for (const FString& ResKey : ResKeys)
            {
                FResidueInfo* Res = ResidueMap[ResKey];
                if (!Res || !Res->bIsVisible) continue;
                
                // Check both directions (protein donor, ligand acceptor AND vice versa)
                UE_LOG(LogTemp, Log, TEXT("Checking: Residue %s as donor -> Ligand %s as acceptor"), *ResKey, *LigKey);
                CheckHBonds(
                    Res->AtomPositions, Res->AtomElements, Res->AtomNames, ResKey,
                    Lig->AtomPositions, Lig->AtomElements, Lig->AtomNames, LigKey,
                    true
                );
                
                UE_LOG(LogTemp, Log, TEXT("Checking: Ligand %s as donor -> Residue %s as acceptor"), *LigKey, *ResKey);
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

    // Protein-Ligand hydrophobic interactions
    if (bProteinLigand)
    {
        TArray<FString> ResKeys, LigKeys;
        ResidueMap.GetKeys(ResKeys);
        LigandMap.GetKeys(LigKeys);

        for (const FString& ResKey : ResKeys)
        {
            FResidueInfo* Res = ResidueMap[ResKey];
            if (!Res || !Res->bIsVisible || !IsHydrophobic(Res->ResidueName)) continue;

            // Get hydrophobic atoms from residue (sidechain carbons)
            TArray<FVector> ResHydrophobicAtoms;
            for (int32 i = 0; i < Res->AtomElements.Num(); ++i)
            {
                // Carbon atoms in hydrophobic sidechains
                if (Res->AtomElements[i] == TEXT("C") && i < Res->AtomNames.Num())
                {
                    const FString& AtomName = Res->AtomNames[i];
                    // Skip backbone atoms (CA, C)
                    if (AtomName != TEXT("CA") && AtomName != TEXT("C"))
                    {
                        ResHydrophobicAtoms.Add(Res->AtomPositions[i]);
                    }
                }
            }

            if (ResHydrophobicAtoms.Num() == 0)
                continue;

            for (const FString& LigKey : LigKeys)
            {
                FLigandInfo* Lig = LigandMap[LigKey];
                if (!Lig || !Lig->bIsVisible || Lig->bIsWater) continue;

                // Get hydrophobic atoms from ligand (carbon atoms not bonded to heteroatoms)
                TArray<TPair<int32, FVector>> LigHydrophobicAtoms;
                for (int32 i = 0; i < Lig->AtomElements.Num(); ++i)
                {
                    if (Lig->AtomElements[i] == TEXT("C"))
                    {
                        // Check if this carbon is bonded to N, O, S (not purely hydrophobic)
                        bool bBondedToHeteroatom = false;
                        for (const auto& Bond : Lig->BondPairs)
                        {
                            int32 OtherAtom = -1;
                            if (Bond.Key == i) OtherAtom = Bond.Value;
                            else if (Bond.Value == i) OtherAtom = Bond.Key;

                            if (OtherAtom >= 0 && OtherAtom < Lig->AtomElements.Num())
                            {
                                const FString& OtherElem = Lig->AtomElements[OtherAtom];
                                if (OtherElem == TEXT("N") || OtherElem == TEXT("O") || OtherElem == TEXT("S"))
                                {
                                    bBondedToHeteroatom = true;
                                    break;
                                }
                            }
                        }

                        // Only include carbons bonded only to C/H (truly hydrophobic)
                        if (!bBondedToHeteroatom)
                        {
                            LigHydrophobicAtoms.Add(TPair<int32, FVector>(i, Lig->AtomPositions[i]));
                        }
                    }
                }

                if (LigHydrophobicAtoms.Num() == 0)
                    continue;

                // Check distances between hydrophobic atoms
                for (const FVector& ResAtom : ResHydrophobicAtoms)
                {
                    for (const auto& LigAtom : LigHydrophobicAtoms)
                    {
                        float Distance = FVector::Dist(ResAtom, LigAtom.Value);

                        if (Distance <= HydrophobicMaxDistance)
                        {
                            FMolecularInteraction Interaction;
                            Interaction.Type = EInteractionType::Hydrophobic;
                            Interaction.Residue1 = ResKey;
                            Interaction.Residue2 = LigKey;
                            Interaction.Atom1 = TEXT("C");
                            Interaction.Atom2 = (LigAtom.Key < Lig->AtomNames.Num()) ? Lig->AtomNames[LigAtom.Key] : TEXT("C");
                            Interaction.Position1 = ResAtom;
                            Interaction.Position2 = LigAtom.Value;
                            Interaction.Distance = Distance;
                            Interaction.Energy = -0.3f - (HydrophobicMaxDistance - Distance) * 0.5f;
                            Interaction.bIsProteinLigand = true;

                            DetectedInteractions.Add(Interaction);

                            UE_LOG(LogTemp, Log, TEXT("Hydrophobic (Prot-Lig): %s <-> %s | Dist: %.2f A"),
                                   *ResKey, *LigKey, Distance);

                            // Only report one interaction per residue-ligand pair to avoid spam
                            goto NextLigand;
                        }
                    }
                }
                NextLigand:;
            }
        }
    }

    int32 HydrophobicFound = DetectedInteractions.Num() - InitialCount;
    UE_LOG(LogTemp, Log, TEXT("Found %d hydrophobic interactions"), HydrophobicFound);
}

// ===== HELPER FUNCTIONS =====

bool APDBViewer::IsHBondDonor(const FString& Element, const FString& AtomName) const
{
    // H-bond donors must have hydrogen atoms attached
    // This is a simplified check - ideally you'd verify actual H atoms in the structure
    
    // Nitrogen donors (with H attached)
    if (Element == TEXT("N"))
    {
        // Backbone amide nitrogen (always has H)
        if (AtomName == TEXT("N"))
            return true;
        
        // Side chain nitrogens with H
        // Lysine NZ (NH3+)
        if (AtomName == TEXT("NZ"))
            return true;
        
        // Arginine NH1, NH2, NE
        if (AtomName == TEXT("NH1") || AtomName == TEXT("NH2") || AtomName == TEXT("NE"))
            return true;
        
        // Tryptophan NE1 (indole NH)
        if (AtomName == TEXT("NE1"))
            return true;
        
        // Histidine ND1, NE2 (can be protonated)
        if (AtomName == TEXT("ND1") || AtomName == TEXT("NE2"))
            return true;
        
        // Glutamine, Asparagine ND2, NE2
        if (AtomName == TEXT("NE2") || AtomName == TEXT("ND2"))
            return true;
        
        // LIGAND ATOMS: Check for common ligand nitrogen patterns
        // Primary amines: NH2, N1H2, etc.
        if (AtomName.Contains(TEXT("NH2")) || AtomName == TEXT("NH2"))
            return true;
        
        // Secondary amines: NH, N1H, etc.
        if (AtomName == TEXT("NH") || AtomName.EndsWith(TEXT("NH")))
            return true;
        
        // Numbered amines: N1, N2, N3 with implicit hydrogens
        // For safety, assume any N in a ligand could be a donor
        // (This is more permissive but catches ligand cases)
        if (AtomName.StartsWith(TEXT("N")) && AtomName.Len() <= 3)
            return true;
    }
    
    // Oxygen donors (with H attached) - mainly hydroxyl groups
    if (Element == TEXT("O"))
    {
        // Serine OG, Threonine OG1 (hydroxyl)
        if (AtomName == TEXT("OG") || AtomName == TEXT("OG1"))
            return true;
        
        // Tyrosine OH (phenolic hydroxyl)
        if (AtomName == TEXT("OH"))
            return true;
        
        // LIGAND ATOMS: Hydroxyl oxygens
        // Common patterns: OH, O1H, etc.
        if (AtomName.Contains(TEXT("OH")) || AtomName.EndsWith(TEXT("H")))
            return true;
        
        // Don't include backbone carbonyl O - it's an acceptor, not donor
    }
    
    // Sulfur donors (rare, but cysteine SH can weakly donate)
    if (Element == TEXT("S"))
    {
        if (AtomName == TEXT("SG"))
            return true;
        
        // LIGAND ATOMS: Thiol groups SH
        if (AtomName.Contains(TEXT("SH")) || AtomName.EndsWith(TEXT("H")))
            return true;
    }
    
    return false;
}

bool APDBViewer::IsHBondAcceptor(const FString& Element, const FString& AtomName) const
{
    // H-bond acceptors have lone pairs to accept hydrogen bonds
    
    // Oxygen acceptors
    if (Element == TEXT("O"))
    {
        // Backbone carbonyl oxygen (strong acceptor)
        if (AtomName == TEXT("O") || AtomName == TEXT("OXT"))
            return true;
        
        // Side chain carbonyls
        // Aspartate, Glutamate OD1, OD2, OE1, OE2
        if (AtomName == TEXT("OD1") || AtomName == TEXT("OD2") || 
            AtomName == TEXT("OE1") || AtomName == TEXT("OE2"))
            return true;
        
        // Asparagine, Glutamine OD1, OE1
        if (AtomName == TEXT("OD1") || AtomName == TEXT("OE1"))
            return true;
        
        // Serine OG, Threonine OG1 (can also accept)
        if (AtomName == TEXT("OG") || AtomName == TEXT("OG1"))
            return true;
        
        // Tyrosine OH (can accept and donate)
        if (AtomName == TEXT("OH"))
            return true;
        
        // LIGAND ATOMS: All oxygens can potentially accept
        // Carbonyl, ether, hydroxyl, etc.
        return true;
    }
    
    // Nitrogen acceptors (with lone pairs)
    if (Element == TEXT("N"))
    {
        // Histidine ND1, NE2 (when not protonated, can accept)
        if (AtomName == TEXT("ND1") || AtomName == TEXT("NE2"))
            return true;
        
        // LIGAND ATOMS: Aromatic/sp2 nitrogens, tertiary amines
        // More permissive for ligands - most N can accept
        if (AtomName.StartsWith(TEXT("N")))
            return true;
    }
    
    // Sulfur acceptors (weak)
    if (Element == TEXT("S"))
    {
        // Methionine SD, Cysteine SG can weakly accept
        return true;
    }
    
    // Fluorine acceptors (moderate strength)
    if (Element == TEXT("F"))
    {
        return true;
    }
    
    // Chlorine acceptors (weak)
    if (Element == TEXT("Cl") || Element == TEXT("CL"))
    {
        return true;
    }
    
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
    if (!LigInfo || LigInfo->AtomPositions.Num() < 5)
        return false;

    // Check cache first
    if (LigInfo->bAromaticCenterCached)
    {
        // Cache stores false result as zero normal
        if (LigInfo->CachedAromaticNormal.IsNearlyZero())
            return false;
        OutCenter = LigInfo->CachedAromaticCenter;
        OutNormal = LigInfo->CachedAromaticNormal;
        return true;
    }

    // Build adjacency list from bond pairs
    TMap<int32, TArray<int32>> Adjacency;
    TSet<int32> AromaticAtoms;

    for (int32 i = 0; i < LigInfo->BondPairs.Num(); ++i)
    {
        int32 A1 = LigInfo->BondPairs[i].Key;
        int32 A2 = LigInfo->BondPairs[i].Value;
        Adjacency.FindOrAdd(A1).Add(A2);
        Adjacency.FindOrAdd(A2).Add(A1);

        // Bond order 4 = aromatic bond
        if (i < LigInfo->BondOrders.Num() && LigInfo->BondOrders[i] == 4)
        {
            AromaticAtoms.Add(A1);
            AromaticAtoms.Add(A2);
        }
    }

    // If we have explicit aromatic bonds, use those atoms
    if (AromaticAtoms.Num() >= 5)
    {
        TArray<FVector> RingPositions;
        for (int32 Idx : AromaticAtoms)
        {
            if (Idx < LigInfo->AtomPositions.Num())
                RingPositions.Add(LigInfo->AtomPositions[Idx]);
        }

        if (RingPositions.Num() >= 5)
        {
            // Calculate center
            OutCenter = FVector::ZeroVector;
            for (const FVector& Pos : RingPositions)
                OutCenter += Pos;
            OutCenter /= RingPositions.Num();

            // Calculate normal using SVD-like approach (find best-fit plane)
            // Simplified: use first 3 non-collinear points
            for (int32 i = 0; i < RingPositions.Num() - 2; ++i)
            {
                FVector V1 = RingPositions[i + 1] - RingPositions[i];
                FVector V2 = RingPositions[i + 2] - RingPositions[i];
                FVector N = FVector::CrossProduct(V1, V2);
                if (N.SizeSquared() > 0.01f)
                {
                    OutNormal = N.GetSafeNormal();
                    break;
                }
            }

            if (OutNormal.IsNearlyZero())
                OutNormal = FVector::UpVector;

            // Cache and return
            LigInfo->CachedAromaticCenter = OutCenter;
            LigInfo->CachedAromaticNormal = OutNormal;
            LigInfo->bAromaticCenterCached = true;
            return true;
        }
    }

    // No aromatic bonds found - try to find 5 or 6 membered rings of C/N atoms
    // Use simple DFS to find rings
    TArray<int32> BestRing;

    for (const auto& Pair : Adjacency)
    {
        int32 StartAtom = Pair.Key;

        // Only start from C or N atoms
        if (StartAtom >= LigInfo->AtomElements.Num())
            continue;
        const FString& Elem = LigInfo->AtomElements[StartAtom];
        if (Elem != TEXT("C") && Elem != TEXT("N"))
            continue;

        // DFS to find rings of size 5 or 6
        TArray<int32> Path;
        Path.Add(StartAtom);
        TSet<int32> Visited;
        Visited.Add(StartAtom);

        TFunction<bool(int32, int32)> FindRing = [&](int32 Current, int32 Depth) -> bool
        {
            if (Depth > 6)
                return false;

            const TArray<int32>* Neighbors = Adjacency.Find(Current);
            if (!Neighbors)
                return false;

            for (int32 Next : *Neighbors)
            {
                if (Next == StartAtom && Depth >= 5)
                {
                    // Found a ring!
                    if (BestRing.Num() == 0 || (Path.Num() == 6 && BestRing.Num() != 6))
                        BestRing = Path;
                    return true;
                }

                if (!Visited.Contains(Next) && Next < LigInfo->AtomElements.Num())
                {
                    const FString& NextElem = LigInfo->AtomElements[Next];
                    if (NextElem == TEXT("C") || NextElem == TEXT("N"))
                    {
                        Path.Add(Next);
                        Visited.Add(Next);
                        FindRing(Next, Depth + 1);
                        Path.Pop();
                        Visited.Remove(Next);
                    }
                }
            }
            return false;
        };

        FindRing(StartAtom, 1);

        // If we found a 6-membered ring, that's ideal for aromatics
        if (BestRing.Num() == 6)
            break;
    }

    // Check if we found a valid ring
    if (BestRing.Num() < 5)
    {
        // Cache negative result
        LigInfo->CachedAromaticCenter = FVector::ZeroVector;
        LigInfo->CachedAromaticNormal = FVector::ZeroVector;
        LigInfo->bAromaticCenterCached = true;
        return false;
    }

    // Verify the ring is roughly planar
    TArray<FVector> RingPositions;
    for (int32 Idx : BestRing)
    {
        if (Idx < LigInfo->AtomPositions.Num())
            RingPositions.Add(LigInfo->AtomPositions[Idx]);
    }

    if (RingPositions.Num() < 5)
    {
        LigInfo->CachedAromaticCenter = FVector::ZeroVector;
        LigInfo->CachedAromaticNormal = FVector::ZeroVector;
        LigInfo->bAromaticCenterCached = true;
        return false;
    }

    // Calculate center
    OutCenter = FVector::ZeroVector;
    for (const FVector& Pos : RingPositions)
        OutCenter += Pos;
    OutCenter /= RingPositions.Num();

    // Calculate normal
    FVector V1 = RingPositions[1] - RingPositions[0];
    FVector V2 = RingPositions[2] - RingPositions[0];
    OutNormal = FVector::CrossProduct(V1, V2).GetSafeNormal();

    if (OutNormal.IsNearlyZero())
    {
        LigInfo->CachedAromaticCenter = FVector::ZeroVector;
        LigInfo->CachedAromaticNormal = FVector::ZeroVector;
        LigInfo->bAromaticCenterCached = true;
        return false;
    }

    // Check planarity - all atoms should be within ~0.5 Angstrom of the plane
    float MaxDeviation = 0.0f;
    for (const FVector& Pos : RingPositions)
    {
        float Deviation = FMath::Abs(FVector::DotProduct(Pos - OutCenter, OutNormal));
        MaxDeviation = FMath::Max(MaxDeviation, Deviation);
    }

    // If not planar enough, not aromatic
    if (MaxDeviation > 0.5f)
    {
        LigInfo->CachedAromaticCenter = FVector::ZeroVector;
        LigInfo->CachedAromaticNormal = FVector::ZeroVector;
        LigInfo->bAromaticCenterCached = true;
        return false;
    }

    // Cache and return
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
    
    // Determine sphere size and brightness based on network membership
    float SphereScale = 0.15f;
    float BrightnessMult = 1.0f;
    
    if (Interaction.Type == EInteractionType::HydrogenBond && Interaction.bIsPartOfNetwork)
    {
        SphereScale = 0.25f; // Larger spheres for network H-bonds
        BrightnessMult = 1.5f; // Brighter for network H-bonds
    }
    
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
        Sphere->SetWorldScale3D(FVector(SphereScale));
        
        // Create material and set color
        UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(SphereMaterialAsset, Sphere);
        FLinearColor Color = GetInteractionColor(Interaction.Type);
        
        // Brighten color for network H-bonds
        if (BrightnessMult > 1.0f)
        {
            Color.R = FMath::Min(Color.R * BrightnessMult, 1.0f);
            Color.G = FMath::Min(Color.G * BrightnessMult, 1.0f);
            Color.B = FMath::Min(Color.B * BrightnessMult, 1.0f);
        }
        
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

void APDBViewer::AnalyzeHBondNetworks()
{
    UE_LOG(LogTemp, Log, TEXT("=== Analyzing H-Bond Networks ==="));
    
    // Build adjacency list of residues connected by H-bonds
    TMap<FString, TArray<FString>> HBondGraph;
    TMap<FString, TArray<FMolecularInteraction*>> HBondConnections;
    
    for (FMolecularInteraction& Interaction : DetectedInteractions)
    {
        if (Interaction.Type == EInteractionType::HydrogenBond)
        {
            // Add edges in both directions
            HBondGraph.FindOrAdd(Interaction.Residue1).Add(Interaction.Residue2);
            HBondGraph.FindOrAdd(Interaction.Residue2).Add(Interaction.Residue1);
            
            HBondConnections.FindOrAdd(Interaction.Residue1).Add(&Interaction);
            HBondConnections.FindOrAdd(Interaction.Residue2).Add(&Interaction);
        }
    }
    
    // Find networks (connected components with 3+ residues)
    TSet<FString> Visited;
    TArray<TArray<FString>> Networks;
    
    for (const auto& Pair : HBondGraph)
    {
        if (Visited.Contains(Pair.Key))
            continue;
        
        // BFS to find connected component
        TArray<FString> Network;
        TArray<FString> Queue;
        Queue.Add(Pair.Key);
        Visited.Add(Pair.Key);
        
        while (Queue.Num() > 0)
        {
            FString Current = Queue[0];
            Queue.RemoveAt(0);
            Network.Add(Current);
            
            if (HBondGraph.Contains(Current))
            {
                for (const FString& Neighbor : HBondGraph[Current])
                {
                    if (!Visited.Contains(Neighbor))
                    {
                        Visited.Add(Neighbor);
                        Queue.Add(Neighbor);
                    }
                }
            }
        }
        
        // Only consider networks with 3+ residues
        if (Network.Num() >= 3)
        {
            Networks.Add(Network);
        }
    }
    
    // Log network information
    UE_LOG(LogTemp, Log, TEXT("Found %d H-bond networks (3+ residues):"), Networks.Num());
    
    for (int32 i = 0; i < Networks.Num(); ++i)
    {
        const TArray<FString>& Network = Networks[i];
        UE_LOG(LogTemp, Log, TEXT("Network %d: %d residues"), i + 1, Network.Num());
        
        // Count H-bonds in this network
        int32 HBondCount = 0;
        TSet<FMolecularInteraction*> NetworkBonds;
        
        for (const FString& Residue : Network)
        {
            if (HBondConnections.Contains(Residue))
            {
                for (FMolecularInteraction* Bond : HBondConnections[Residue])
                {
                    if (Network.Contains(Bond->Residue1) && Network.Contains(Bond->Residue2))
                    {
                        NetworkBonds.Add(Bond);
                    }
                }
            }
        }
        
        UE_LOG(LogTemp, Log, TEXT("  %d H-bonds connecting:"), NetworkBonds.Num());
        
        // Print residues in network
        FString ResidueList;
        for (int32 j = 0; j < Network.Num(); ++j)
        {
            ResidueList += Network[j];
            if (j < Network.Num() - 1)
                ResidueList += TEXT(", ");
        }
        UE_LOG(LogTemp, Log, TEXT("  %s"), *ResidueList);
        
        // Mark these interactions as part of a network
        for (FMolecularInteraction* Bond : NetworkBonds)
        {
            Bond->bIsPartOfNetwork = true;
            Bond->NetworkID = i;
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("==========================="));
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
