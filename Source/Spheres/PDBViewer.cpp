// PDBViewer.cpp – UE 5.6 compatible version with TreeView, SDF support, and Ligand Atom Lighting
// Modified to include point light components on each ligand atom

#include "Materials/MaterialInstanceDynamic.h"

#include "PDBViewer.h"
#include "PDBCameraComponent.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "Interfaces/IMainFrameModule.h"
#include "Components/TreeView.h"
#include "Components/ListView.h"
#include "Kismet/GameplayStatics.h"
#include "HydrogenGenerator.h"

namespace PDB
{
    constexpr float SCALE = 50.0f;
    constexpr float SPHERE_SIZE = 0.5f;
    constexpr float CYLINDER_SIZE = 0.1f;
    constexpr float BOND_OFFSET = 8.0f;
}

APDBViewer::APDBViewer()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
    static ConstructorHelpers::FObjectFinder<UMaterial> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial"));

    SphereMeshAsset = Sphere.Object;
    CylinderMeshAsset = Cylinder.Object;
    SphereMaterialAsset = Mat.Object;
}

void APDBViewer::BeginPlay()
{
    Super::BeginPlay();

    // Bind to the ligands loaded event
    OnLigandsLoaded.AddDynamic(this, &APDBViewer::OnLigandsLoadedHandler);

    FetchAndDisplayStructure(TEXT("5ENB"));

    if (auto *Cam = GetWorld()->SpawnActor<APDBCameraComponent>(APDBCameraComponent::StaticClass(), GetActorLocation(), FRotator::ZeroRotator))
        Cam->SetTargetActor(this);

        
    // Show FPS using built-in stat command
    if (GEngine && GEngine->GameViewport)
    {
        GEngine->GameViewport->ConsoleCommand(TEXT("stat fps"));
    }
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

    // Just update hydrogen visibility for ligands
    for (auto &Pair : LigandMap)
    {
        if (!Pair.Value)
            continue;

        FLigandInfo *Info = Pair.Value;

        // Update visibility for hydrogen atoms
        for (int32 i = 0; i < Info->AtomElements.Num(); ++i)
        {
            if (Info->AtomElements[i] == TEXT("H") && Info->AtomMeshes.IsValidIndex(i))
                Info->AtomMeshes[i]->SetVisibility(bHydrogensVisible && Info->bIsVisible);
        }

        // Update visibility for bonds involving hydrogens
        for (int32 i = 0; i < Info->BondPairs.Num(); ++i)
        {
            int32 A1 = Info->BondPairs[i].Key;
            int32 A2 = Info->BondPairs[i].Value;

            bool bHasH = (Info->AtomElements.IsValidIndex(A1) && Info->AtomElements[A1] == TEXT("H")) ||
                         (Info->AtomElements.IsValidIndex(A2) && Info->AtomElements[A2] == TEXT("H"));

            if (bHasH && Info->BondMeshes.IsValidIndex(i))
                Info->BondMeshes[i]->SetVisibility(bHydrogensVisible && Info->bIsVisible);
        }
        
        // ===== NEW: CREATE LIGAND ATOM LIGHTS =====
        CreateLigandAtomLights(Info);
        // ==========================================
    }

    int32 TotalH = GetHydrogenCount();
    int32 TotalLights = GetLigandAtomLightCount();
    UE_LOG(LogTemp, Warning, TEXT("Total hydrogens: %d (Visible: %s)"),
           TotalH, bHydrogensVisible ? TEXT("YES") : TEXT("NO"));
    UE_LOG(LogTemp, Warning, TEXT("Total ligand atom lights: %d"), TotalLights);
}

void APDBViewer::FetchAndDisplayStructure(const FString &ID)
{
    CurrentStructureID = ID;
    FString URL = FString::Printf(TEXT("https://files.rcsb.org/download/%s.pdb"), *ID);
    FetchFileAsync(URL, [this, ID](bool bOK, const FString &Content)
                   {
        if (bOK) ParsePDB(Content);
        else FetchFileAsync(FString::Printf(TEXT("https://files.rcsb.org/download/%s.cif"), *ID),
            [this](bool bOK2, const FString& C) { if (bOK2) ParseMMCIF(C); }); });
}

void APDBViewer::FetchFileAsync(const FString &URL, TFunction<void(bool, const FString &)> CB)
{
    auto Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(URL);
    Req->SetVerb(TEXT("GET"));
    Req->OnProcessRequestComplete().BindLambda([CB](FHttpRequestPtr R, FHttpResponsePtr Resp, bool bOK)
                                               { CB(bOK && Resp.IsValid() && Resp->GetResponseCode() == 200, bOK ? Resp->GetContentAsString() : TEXT("")); });
    Req->ProcessRequest();
}

void APDBViewer::ParsePDB(const FString &Content)
{
    CurrentPDBContent = Content;
    ClearResidueMap();
    ClearLigandMap();
    ChainIDs.Empty();

    TArray<FString> Lines;
    Content.ParseIntoArrayLines(Lines);

    TMap<FString, TMap<FString, FVector>> ResAtoms;
    TMap<FString, FResidueMetadata> ResMeta;

    for (const auto &L : Lines)
    {
        FString RecordType = L.Mid(0, 6);
        if (L.Len() < 80 || !(RecordType.StartsWith("ATOM") || RecordType.StartsWith("HETATM")))
            continue;

        FString Chain = L.Mid(21, 1).TrimStartAndEnd();
        if (Chain.IsEmpty())
            Chain = TEXT("_");

        ChainIDs.Add(Chain);

        FString Key = FString::Printf(TEXT("%s_%s_%s"),
                                      *L.Mid(17, 3).TrimStartAndEnd(),
                                      *L.Mid(22, 4).TrimStartAndEnd(),
                                      *Chain);

        FString AtomName = L.Mid(12, 4).TrimStartAndEnd();

        // Store UNSCALED coordinates with atom name as key
        ResAtoms.FindOrAdd(Key).Add(AtomName,
                                    FVector(FCString::Atof(*L.Mid(30, 8)),
                                            FCString::Atof(*L.Mid(38, 8)),
                                            FCString::Atof(*L.Mid(46, 8))));

        if (!ResMeta.Contains(Key))
        {
            auto &M = ResMeta.Add(Key);
            M.ResidueName = L.Mid(17, 3).TrimStartAndEnd();
            M.ResidueSeq = L.Mid(22, 4).TrimStartAndEnd();
            M.Chain = Chain;
            M.RecordType = RecordType.TrimStartAndEnd();
        }
    }

    CreateResiduesFromAtomData(ResAtoms, ResMeta);

    // Fetch bonds from the structure's mmCIF file
    if (!CurrentStructureID.IsEmpty())
    {
        FetchStructureBondsFromCIF(CurrentStructureID);
    }
    else
    {
        OnResiduesLoaded.Broadcast();
    }
}

void APDBViewer::ParseMMCIF(const FString &Content)
{
    CurrentPDBContent = Content;
    ClearResidueMap();
    ClearLigandMap();
    ChainIDs.Empty();

    TArray<FString> Lines;
    Content.ParseIntoArrayLines(Lines);

    TArray<FString> Hdrs;
    TArray<TArray<FString>> AtomTab;
    int32 XI = -1, YI = -1, ZI = -1, RI = -1, AI = -1, GI = -1, CI = -1, SI = -1;
    bool bLoop = false;

    for (const auto &L : Lines)
    {
        if (L.StartsWith(TEXT("loop_")))
        {
            bLoop = true;
            Hdrs.Empty();
            continue;
        }
        if (bLoop && L.StartsWith(TEXT("_atom_site.")))
        {
            int32 I = Hdrs.Add(L);
            if (L.Contains(TEXT("Cartn_x")))
                XI = I;
            else if (L.Contains(TEXT("Cartn_y")))
                YI = I;
            else if (L.Contains(TEXT("Cartn_z")))
                ZI = I;
            else if (L.Contains(TEXT("label_comp_id")))
                RI = I;
            else if (L.Contains(TEXT("label_atom_id")))
                AI = I;
            else if (L.Contains(TEXT("group_PDB")))
                GI = I;
            else if (L.Contains(TEXT("label_asym_id")))
                CI = I;
            else if (L.Contains(TEXT("label_seq_id")) || L.Contains(TEXT("auth_seq_id")))
                SI = I;
            continue;
        }
        if (bLoop && !L.StartsWith(TEXT("_")))
        {
            TArray<FString> T;
            L.ParseIntoArrayWS(T);
            if (T.Num() > FMath::Max3(XI, YI, ZI))
                AtomTab.Add(MoveTemp(T));
        }
    }

    TMap<FString, TMap<FString, FVector>> ResAtoms;
    TMap<FString, FResidueMetadata> ResMeta;

    for (const auto &R : AtomTab)
    {
        if (XI < 0 || YI < 0 || ZI < 0 || RI < 0 || AI < 0)
            continue;

        FString GroupPDB = (GI >= 0 && R.IsValidIndex(GI)) ? R[GI] : TEXT("ATOM");
        FString Chain = (CI >= 0 && R.IsValidIndex(CI)) ? R[CI] : TEXT("_");
        if (Chain.IsEmpty())
            Chain = TEXT("_");

        ChainIDs.Add(Chain);

        FString Seq = (SI >= 0 && R.IsValidIndex(SI)) ? R[SI] : TEXT("0");
        FString Key = FString::Printf(TEXT("%s_%s_%s"), *R[RI], *Seq, *Chain);

        FString AtomName = R[AI];

        ResAtoms.FindOrAdd(Key).Add(AtomName,
                                    FVector(FCString::Atof(*R[XI]),
                                            FCString::Atof(*R[YI]),
                                            FCString::Atof(*R[ZI])));

        if (!ResMeta.Contains(Key))
        {
            auto &M = ResMeta.Add(Key);
            M.ResidueName = R[RI];
            M.ResidueSeq = Seq;
            M.Chain = Chain;
            M.RecordType = GroupPDB;
        }
    }

    CreateResiduesFromAtomData(ResAtoms, ResMeta);

    // Fetch bonds from the structure's mmCIF file (or parse from this file if it's the same)
    if (!CurrentStructureID.IsEmpty())
    {
        FetchStructureBondsFromCIF(CurrentStructureID);
    }
    else
    {
        // We're already parsing a CIF file, parse bonds from it directly
        ParseStructureBondsFromCIF(Content);
    }
}

void APDBViewer::FetchStructureBondsFromCIF(const FString &StructureID)
{
    FString URL = FString::Printf(TEXT("https://files.rcsb.org/download/%s.cif"), *StructureID);

    UE_LOG(LogTemp, Log, TEXT("Fetching bond information from mmCIF: %s"), *URL);

    FetchFileAsync(URL, [this](bool bOK, const FString &Content)
                   {
        if (bOK)
        {
            UE_LOG(LogTemp, Log, TEXT("Successfully fetched mmCIF bond data"));
            ParseStructureBondsFromCIF(Content);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to fetch mmCIF bond data"));
            OnResiduesLoaded.Broadcast();
        } });
}

void APDBViewer::ParseStructureBondsFromCIF(const FString &Content)
{
    TArray<FString> Lines;
    Content.ParseIntoArrayLines(Lines);

    bool bInChemCompBond = false;
    TArray<FString> BondHeaders;
    int32 CompIdIdx = -1, Atom1Idx = -1, Atom2Idx = -1, OrderIdx = -1;

    // Maps to store bonds by component type (residue name like "ALA", "GLY", "ATP", etc.)
    TMap<FString, TArray<TPair<TPair<FString, FString>, int32>>> ComponentBonds;

    for (const FString &Line : Lines)
    {
        if (Line.StartsWith(TEXT("loop_")))
        {
            bInChemCompBond = false;
            BondHeaders.Empty();
            CompIdIdx = Atom1Idx = Atom2Idx = OrderIdx = -1;
            continue;
        }

        if (Line.StartsWith(TEXT("_chem_comp_bond.")))
        {
            bInChemCompBond = true;
            int32 Idx = BondHeaders.Add(Line);

            if (Line.Contains(TEXT("comp_id")))
                CompIdIdx = Idx;
            else if (Line.Contains(TEXT("atom_id_1")))
                Atom1Idx = Idx;
            else if (Line.Contains(TEXT("atom_id_2")))
                Atom2Idx = Idx;
            else if (Line.Contains(TEXT("value_order")))
                OrderIdx = Idx;

            continue;
        }

        if (bInChemCompBond && !Line.StartsWith(TEXT("_")) && !Line.StartsWith(TEXT("#")) && !Line.IsEmpty())
        {
            if (CompIdIdx < 0 || Atom1Idx < 0 || Atom2Idx < 0)
                continue;

            TArray<FString> Tokens;
            Line.ParseIntoArrayWS(Tokens);

            if (Tokens.Num() <= FMath::Max3(CompIdIdx, Atom1Idx, Atom2Idx))
                continue;

            FString CompId = Tokens[CompIdIdx].TrimStartAndEnd();
            FString Atom1 = Tokens[Atom1Idx].TrimStartAndEnd();
            FString Atom2 = Tokens[Atom2Idx].TrimStartAndEnd();
            int32 Order = (OrderIdx >= 0 && Tokens.IsValidIndex(OrderIdx))
                              ? ParseBondOrder(Tokens[OrderIdx])
                              : 1;

            // Store bond for this component type
            ComponentBonds.FindOrAdd(CompId).Add(TPair<TPair<FString, FString>, int32>(
                TPair<FString, FString>(Atom1, Atom2), Order));
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Parsed bond data for %d component types from mmCIF"), ComponentBonds.Num());

    // Apply bonds to all residues and ligands
    ApplyBondsToResidues(ComponentBonds);

    // Now broadcast events after bonds are applied
    OnResiduesLoaded.Broadcast();
    OnLigandsLoaded.Broadcast();
    
    // Optionally calculate interactions automatically if enabled
    if (bAutoCalculateInteractions)
    {
        UE_LOG(LogTemp, Log, TEXT("Auto-calculating interactions..."));
        CalculateAllInteractions(true, true);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("Auto-calculate interactions is disabled. Call CalculateAllInteractions() manually if needed."));
    }
}

void APDBViewer::ApplyBondsToResidues(const TMap<FString, TArray<TPair<TPair<FString, FString>, int32>>> &ComponentBonds)
{
    // Apply to regular residues (ATOM records)
    for (auto &Pair : ResidueMap)
    {
        FResidueInfo *ResInfo = Pair.Value;
        if (!ResInfo)
            continue;

        const TArray<TPair<TPair<FString, FString>, int32>> *BondData = ComponentBonds.Find(ResInfo->ResidueName);
        if (!BondData)
        {
            UE_LOG(LogTemp, Warning, TEXT("No bond data found for residue: %s"), *ResInfo->ResidueName);
            continue;
        }

        // Create atom name to index map
        TMap<FString, int32> AtomNameToIdx;
        for (int32 i = 0; i < ResInfo->AtomNames.Num(); ++i)
        {
            AtomNameToIdx.Add(ResInfo->AtomNames[i], i);
        }

        UE_LOG(LogTemp, Log, TEXT("Applying %d bonds to residue %s %s"),
               BondData->Num(), *ResInfo->ResidueName, *ResInfo->ResidueSeq);

        // Apply bonds
        for (const auto &BondInfo : *BondData)
        {
            const FString &Atom1Name = BondInfo.Key.Key;
            const FString &Atom2Name = BondInfo.Key.Value;
            int32 Order = BondInfo.Value;

            const int32 *Idx1Ptr = AtomNameToIdx.Find(Atom1Name);
            const int32 *Idx2Ptr = AtomNameToIdx.Find(Atom2Name);

            if (Idx1Ptr && Idx2Ptr)
            {
                int32 Idx1 = *Idx1Ptr;
                int32 Idx2 = *Idx2Ptr;

                // Store bond connectivity
                ResInfo->BondPairs.Add(TPair<int32, int32>(Idx1, Idx2));
                ResInfo->BondOrders.Add(Order);

                // Draw the bond
                FVector ScaledPos1 = ResInfo->AtomPositions[Idx1] * PDB::SCALE;
                FVector ScaledPos2 = ResInfo->AtomPositions[Idx2] * PDB::SCALE;
                DrawBond(ScaledPos1, ScaledPos2, Order,
                         ResInfo->AtomElements[Idx1], ResInfo->AtomElements[Idx2],
                         GetRootComponent(), ResInfo->BondMeshes);
            }
        }

        // After bonds are added, generate hydrogens
        if (bAutoGenerateHydrogens && ResInfo->BondPairs.Num() > 0)
        {
            GenerateHydrogensForResidue(ResInfo);
        }
    }

    // Apply to ligands (HETATM records)
    for (auto &Pair : LigandMap)
    {
        FLigandInfo *LigInfo = Pair.Value;
        if (!LigInfo)
            continue;

        // Extract the residue name from the ligand key
        FString ResName = LigInfo->LigandName;
        int32 DashPos;
        if (ResName.FindChar('-', DashPos))
            ResName = ResName.Left(DashPos);

        const TArray<TPair<TPair<FString, FString>, int32>> *BondData = ComponentBonds.Find(ResName);
        if (!BondData)
        {
            UE_LOG(LogTemp, Warning, TEXT("No bond data found for ligand: %s (ResName: %s)"),
                   *LigInfo->LigandName, *ResName);
            continue;
        }

        // Create atom name to index map
        TMap<FString, int32> AtomNameToIdx;
        for (int32 i = 0; i < LigInfo->AtomNames.Num(); ++i)
        {
            AtomNameToIdx.Add(LigInfo->AtomNames[i], i);
        }

        UE_LOG(LogTemp, Log, TEXT("Applying %d bonds to ligand %s"),
               BondData->Num(), *LigInfo->LigandName);

        // Apply bonds
        for (const auto &BondInfo : *BondData)
        {
            const FString &Atom1Name = BondInfo.Key.Key;
            const FString &Atom2Name = BondInfo.Key.Value;
            int32 Order = BondInfo.Value;

            const int32 *Idx1Ptr = AtomNameToIdx.Find(Atom1Name);
            const int32 *Idx2Ptr = AtomNameToIdx.Find(Atom2Name);

            if (Idx1Ptr && Idx2Ptr)
            {
                int32 Idx1 = *Idx1Ptr;
                int32 Idx2 = *Idx2Ptr;

                // Store bond connectivity
                LigInfo->BondPairs.Add(TPair<int32, int32>(Idx1, Idx2));
                LigInfo->BondOrders.Add(Order);

                // Draw the bond
                FVector ScaledPos1 = LigInfo->AtomPositions[Idx1] * PDB::SCALE;
                FVector ScaledPos2 = LigInfo->AtomPositions[Idx2] * PDB::SCALE;
                DrawBond(ScaledPos1, ScaledPos2, Order,
                         LigInfo->AtomElements[Idx1], LigInfo->AtomElements[Idx2],
                         GetRootComponent(), LigInfo->BondMeshes);

                if (LigInfo->BondMeshes.Num() > 0)
                {
                    LigInfo->BondMeshes.Last()->SetVisibility(LigInfo->bIsVisible);
                }
            }
        }

        // After bonds are added, generate hydrogens for ligands too!
        if (bAutoGenerateHydrogens && LigInfo->BondPairs.Num() > 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("Generating hydrogens for ligand: %s"), *LigInfo->LigandName);

            // Skip single atoms (water, ions)
            if (LigInfo->AtomPositions.Num() <= 1)
            {
                UE_LOG(LogTemp, Log, TEXT("Skipping %s (single atom)"), *LigInfo->LigandName);
                continue;
            }

            // Check if hydrogens already exist
            bool bHasHydrogens = LigInfo->AtomElements.Contains(TEXT("H"));
            if (bHasHydrogens)
            {
                UE_LOG(LogTemp, Log, TEXT("Ligand %s already has hydrogens"), *LigInfo->LigandName);
                continue;
            }

            TArray<TPair<FVector, int32>> Hydrogens = FHydrogenGenerator::GenerateHydrogens(
                LigInfo->AtomPositions, LigInfo->AtomElements, LigInfo->BondPairs, LigInfo->BondOrders);

            UE_LOG(LogTemp, Warning, TEXT("  Generated %d hydrogens for ligand"), Hydrogens.Num());

            for (const auto &HPair : Hydrogens)
            {
                int32 ParentIdx = HPair.Value;

                // Store UNSCALED hydrogen position
                int32 HIdx = LigInfo->AtomPositions.Add(HPair.Key);
                LigInfo->AtomElements.Add(TEXT("H"));
                LigInfo->AtomNames.Add(FString::Printf(TEXT("H%d"), HIdx));

                // Apply scaling only when drawing
                FVector ScaledHPos = HPair.Key * PDB::SCALE;
                DrawSphere(ScaledHPos.X, ScaledHPos.Y, ScaledHPos.Z, FLinearColor::White,
                           GetRootComponent(), LigInfo->AtomMeshes);
                LigInfo->AtomMeshes.Last()->SetWorldScale3D(FVector(0.3f));
                LigInfo->AtomMeshes.Last()->SetVisibility(bHydrogensVisible && LigInfo->bIsVisible);

                // Scale both positions for drawing the bond
                FVector ScaledParent = LigInfo->AtomPositions[ParentIdx] * PDB::SCALE;
                DrawBond(ScaledParent, ScaledHPos, 1,
                         LigInfo->AtomElements[ParentIdx], TEXT("H"),
                         GetRootComponent(), LigInfo->BondMeshes);
                LigInfo->BondMeshes.Last()->SetVisibility(bHydrogensVisible && LigInfo->bIsVisible);

                LigInfo->BondPairs.Add(TPair<int32, int32>(ParentIdx, HIdx));
                LigInfo->BondOrders.Add(1);
            }
        }
    }
}

void APDBViewer::ParseSDF(const FString &Content)
{
    CurrentPDBContent = Content;
    ClearLigandMap();

    TArray<FString> Lines;
    Content.ParseIntoArrayLines(Lines);

    int32 MoleculeIndex = 0;
    int32 LineIndex = 0;

    while (LineIndex < Lines.Num())
    {
        if (LineIndex + 3 >= Lines.Num())
            break;

        FString MoleculeName = Lines[LineIndex].TrimStartAndEnd();
        if (MoleculeName.IsEmpty())
            MoleculeName = FString::Printf(TEXT("MOL%d"), MoleculeIndex + 1);

        int32 CountsLineIndex = -1;
        for (int32 i = LineIndex + 1; i < FMath::Min(LineIndex + 5, Lines.Num()); ++i)
        {
            if (Lines[i].Contains(TEXT("V2000")) || Lines[i].Contains(TEXT("V3000")))
            {
                CountsLineIndex = i;
                break;
            }
        }

        if (CountsLineIndex == -1)
        {
            LineIndex++;
            continue;
        }

        FString CountsLine = Lines[CountsLineIndex];

        if (CountsLine.Len() < 6)
        {
            LineIndex++;
            continue;
        }

        int32 NumAtoms = FCString::Atoi(*CountsLine.Mid(0, 3).TrimStartAndEnd());
        int32 NumBonds = FCString::Atoi(*CountsLine.Mid(3, 3).TrimStartAndEnd());

        TArray<FVector> AtomPositions;
        TArray<FString> AtomElements;
        int32 AtomStartLine = CountsLineIndex + 1;

        for (int32 i = 0; i < NumAtoms; ++i)
        {
            int32 CurrentLine = AtomStartLine + i;
            if (CurrentLine >= Lines.Num())
                break;

            FString Line = Lines[CurrentLine];
            if (Line.Len() < 34)
                continue;

            float X = FCString::Atof(*Line.Mid(0, 10).TrimStartAndEnd());
            float Y = FCString::Atof(*Line.Mid(10, 10).TrimStartAndEnd());
            float Z = FCString::Atof(*Line.Mid(20, 10).TrimStartAndEnd());

            FString Element = Line.Mid(31, 3).TrimStartAndEnd();
            // Element symbols can be 1-3 characters: C, Cl, Uup
            // First character is always uppercase, rest are lowercase if present
            if (Element.Len() > 0)
            {
                // Standard approach: keep first char uppercase, and consecutive lowercase chars
                // This handles: C (1), Cl (2), Uup (3), etc.
                FString Normalized;
                Normalized += FChar::ToUpper(Element[0]);
                for (int32 j = 1; j < Element.Len(); ++j)
                {
                    if (FChar::IsAlpha(Element[j]))
                        Normalized += FChar::ToLower(Element[j]);
                }
                Element = Normalized;
            }
            // Store UNSCALED coordinates
            FVector Pos(X, Y, Z);
            // Do NOT multiply by PDB::SCALE here

            AtomPositions.Add(Pos);
            AtomElements.Add(Element);
        }

        int32 BondStartLine = AtomStartLine + NumAtoms;
        TArray<TPair<int32, int32>> BondPairs;
        TArray<int32> BondOrders;

        for (int32 i = 0; i < NumBonds && (BondStartLine + i) < Lines.Num(); ++i)
        {
            FString Line = Lines[BondStartLine + i];
            if (Line.Len() < 9)
                continue;

            int32 Atom1 = FCString::Atoi(*Line.Mid(0, 3).TrimStartAndEnd()) - 1;
            int32 Atom2 = FCString::Atoi(*Line.Mid(3, 3).TrimStartAndEnd()) - 1;
            int32 BondType = FCString::Atoi(*Line.Mid(6, 3).TrimStartAndEnd());

            if (Atom1 >= 0 && Atom1 < AtomPositions.Num() &&
                Atom2 >= 0 && Atom2 < AtomPositions.Num())
            {
                BondPairs.Add(TPair<int32, int32>(Atom1, Atom2));
                BondOrders.Add(BondType);
            }
        }

        FString Key = FString::Printf(TEXT("%s"), *MoleculeName);

        auto *Info = new FLigandInfo();
        Info->LigandName = MoleculeName;
        Info->bIsVisible = false;
        Info->AtomPositions = AtomPositions;
        Info->AtomElements = AtomElements;

        for (int32 i = 0; i < BondPairs.Num(); ++i)
        {
            Info->BondPairs.Add(BondPairs[i]);
            Info->BondOrders.Add(BondOrders[i]);
        }

        // Draw heavy atoms with SCALED positions
        for (int32 i = 0; i < Info->AtomPositions.Num(); ++i)
        {
            FLinearColor Color = GetElementColor(Info->AtomElements[i]);
            FVector ScaledPos = Info->AtomPositions[i] * PDB::SCALE;
            DrawSphere(ScaledPos.X, ScaledPos.Y, ScaledPos.Z,
                       Color, GetRootComponent(), Info->AtomMeshes);
        }

        // Draw bonds with SCALED positions
        for (int32 i = 0; i < Info->BondPairs.Num(); ++i)
        {
            int32 A1 = Info->BondPairs[i].Key, A2 = Info->BondPairs[i].Value;
            int32 Order = Info->BondOrders[i] == 4 ? 1 : Info->BondOrders[i];
            FVector ScaledPos1 = Info->AtomPositions[A1] * PDB::SCALE;
            FVector ScaledPos2 = Info->AtomPositions[A2] * PDB::SCALE;
            DrawBond(ScaledPos1, ScaledPos2, Order,
                     Info->AtomElements[A1], Info->AtomElements[A2], GetRootComponent(), Info->BondMeshes);
        }

        for (auto *Mesh : Info->AtomMeshes)
            if (Mesh)
                Mesh->SetVisibility(Info->bIsVisible);
        for (auto *Mesh : Info->BondMeshes)
            if (Mesh)
                Mesh->SetVisibility(Info->bIsVisible);

        LigandMap.Add(FString::Printf(TEXT("%s"), *MoleculeName), Info);

        LineIndex = BondStartLine + NumBonds;
        while (LineIndex < Lines.Num())
        {
            if (Lines[LineIndex].StartsWith(TEXT("$$")))
            {
                LineIndex++;
                break;
            }
            LineIndex++;
        }

        MoleculeIndex++;
    }

    OnLigandsLoaded.Broadcast();
}

void APDBViewer::CreateResiduesFromAtomData(const TMap<FString, TMap<FString, FVector>> &ResAtoms, const TMap<FString, FResidueMetadata> &Meta)
{
    for (const auto &P : ResAtoms)
    {
        const auto *M = Meta.Find(P.Key);
        if (!M)
            continue;

        if (M->RecordType.StartsWith(TEXT("HETATM")))
        {
            auto *LigInfo = new FLigandInfo();
            LigInfo->LigandName = FString::Printf(TEXT("%s-%s-%s"), *M->ResidueName, *M->ResidueSeq, *M->Chain);

            // Make water molecules visible by default, hide others
            bool bIsWater = (M->ResidueName == TEXT("HOH") ||
                             M->ResidueName == TEXT("H2O") ||
                             M->ResidueName == TEXT("WAT"));
            LigInfo->bIsVisible = bIsWater;
            LigInfo->bIsWater = bIsWater;

            for (const auto &A : P.Value)
            {
                FString AtomName = A.Key;
                FString Element = AtomName.TrimStartAndEnd().Left(1);
                if (AtomName.Len() > 1 && FChar::IsLower(AtomName[1]))
                    Element = AtomName.Left(2);

                // Store UNSCALED position
                LigInfo->AtomPositions.Add(A.Value);
                LigInfo->AtomElements.Add(Element);
                LigInfo->AtomNames.Add(AtomName);

                // Apply scaling only when rendering
                FVector ScaledPos = A.Value * PDB::SCALE;
                DrawSphere(ScaledPos.X, ScaledPos.Y, ScaledPos.Z, GetElementColor(Element), GetRootComponent(), LigInfo->AtomMeshes);
            }

            for (auto *Mesh : LigInfo->AtomMeshes)
            {
                if (Mesh)
                    Mesh->SetVisibility(LigInfo->bIsVisible);
            }

            LigandMap.Add(P.Key, LigInfo);
            // Don't fetch individual ligand CIFs anymore - we'll get bonds from the main CIF
        }
        else
        {
            auto *Info = new FResidueInfo();
            Info->ResidueName = M->ResidueName;
            Info->ResidueSeq = M->ResidueSeq;
            Info->Chain = M->Chain;
            Info->RecordType = M->RecordType;
            Info->bIsVisible = true;

            // Store UNSCALED positions and atom names
            for (const auto &A : P.Value)
            {
                FString AtomName = A.Key;
                FString Element = AtomName.TrimStartAndEnd().Left(1);
                if (AtomName.Len() > 1 && FChar::IsLower(AtomName[1]))
                    Element = AtomName.Left(2);

                Info->AtomPositions.Add(A.Value);
                Info->AtomElements.Add(Element);
                Info->AtomNames.Add(AtomName);
            }

            // Don't draw bonds yet - wait for CIF bond data
            ResidueMap.Add(P.Key, Info);
        }
    }
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

        // Store UNSCALED hydrogen position
        int32 HIdx = ResInfo->AtomPositions.Add(HPair.Key);
        ResInfo->AtomElements.Add(TEXT("H"));
        ResInfo->AtomNames.Add(FString::Printf(TEXT("H%d"), HIdx)); // Add atom name

        // Apply scaling only when drawing
        FVector ScaledHPos = HPair.Key * PDB::SCALE;
/*         DrawSphere(ScaledHPos.X, ScaledHPos.Y, ScaledHPos.Z, FLinearColor::White,
                   GetRootComponent(), ResInfo->AtomMeshes); 
        ResInfo->AtomMeshes.Last()->SetWorldScale3D(FVector(0.3f));
        ResInfo->AtomMeshes.Last()->SetVisibility(bHydrogensVisible && ResInfo->bIsVisible);*/

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

void APDBViewer::DrawProteinBondsAndConnectivity(const TMap<FString, FVector> &AtomPositions, FResidueInfo *ResInfo)
{
    if (!ResInfo)
        return;

    const float BondThreshold = 2.0f; // Use angstrom units now (was 100.0f scaled)

    TArray<TPair<FString, FVector>> Atoms;
    TMap<FString, int32> AtomNameToIndex;

    for (const auto &Pair : AtomPositions)
    {
        int32 Idx = Atoms.Add(TPair<FString, FVector>(Pair.Key, Pair.Value));
        AtomNameToIndex.Add(Pair.Key, Idx);
    }

    for (int32 i = 0; i < Atoms.Num(); ++i)
    {
        for (int32 j = i + 1; j < Atoms.Num(); ++j)
        {
            float Distance = FVector::Dist(Atoms[i].Value, Atoms[j].Value);

            if (Distance < BondThreshold)
            {
                FString Element1 = Atoms[i].Key.TrimStartAndEnd().Left(1);
                if (Atoms[i].Key.Len() > 1 && FChar::IsLower(Atoms[i].Key[1]))
                    Element1 = Atoms[i].Key.Left(2);

                FString Element2 = Atoms[j].Key.TrimStartAndEnd().Left(1);
                if (Atoms[j].Key.Len() > 1 && FChar::IsLower(Atoms[j].Key[1]))
                    Element2 = Atoms[j].Key.Left(2);

                // Store bond connectivity
                ResInfo->BondPairs.Add(TPair<int32, int32>(i, j));
                ResInfo->BondOrders.Add(1); // Assume single bonds for protein backbone

                // Apply scaling when drawing
                FVector ScaledPos1 = Atoms[i].Value * PDB::SCALE;
                FVector ScaledPos2 = Atoms[j].Value * PDB::SCALE;
                DrawBond(ScaledPos1, ScaledPos2, 1, Element1, Element2, GetRootComponent(), ResInfo->BondMeshes);
            }
        }
    }
}

void APDBViewer::ParseLigandCIFForLigand(const FString &Content, const TMap<FString, FVector> &Pos, FLigandInfo *Info)
{
    if (!Info)
        return;

    TMap<FString, FVector> NormPos;
    TMap<FString, FString> AtomElements;

    for (const auto &P : Pos)
    {
        FString K;
        for (const TCHAR C : P.Key)
            if (FChar::IsAlnum(C))
                K.AppendChar(FChar::ToUpper(C));
        if (!K.IsEmpty())
        {
            NormPos.Add(K, P.Value);
            FString Element = P.Key.TrimStartAndEnd().Left(1);
            if (P.Key.Len() > 1 && FChar::IsLower(P.Key[1]))
                Element = P.Key.Left(2);
            AtomElements.Add(K, Element);
        }
    }

    TArray<FString> Lines;
    Content.ParseIntoArrayLines(Lines);

    TArray<FString> Hdrs;
    int32 A1 = -1, A2 = -1, BO = -1;
    bool bLoop = false;

    for (const auto &L : Lines)
    {
        if (L.StartsWith(TEXT("loop_")))
        {
            bLoop = true;
            Hdrs.Empty();
            A1 = A2 = BO = -1;
            continue;
        }
        if (bLoop && L.StartsWith(TEXT("_")))
        {
            int32 I = Hdrs.Add(L);
            FString Lo = L.ToLower();
            if (Lo.Contains(TEXT("atom_id_1")) || Lo.Contains(TEXT("atom_1")))
                A1 = I;
            else if (Lo.Contains(TEXT("atom_id_2")) || Lo.Contains(TEXT("atom_2")))
                A2 = I;
            else if (Lo.Contains(TEXT("value_order")) || Lo.Contains(TEXT("bond_order")))
                BO = I;
            continue;
        }
        if (bLoop && !L.StartsWith(TEXT("_")) && !L.StartsWith(TEXT("data_")))
        {
            if (A1 < 0 || A2 < 0)
                continue;
            TArray<FString> T;
            L.ParseIntoArrayWS(T);
            if (T.Num() <= FMath::Max(A1, A2))
                continue;

            FString ID1 = NormalizeAtomID(T[A1]);
            FString ID2 = NormalizeAtomID(T[A2]);
            int32 Ord = ParseBondOrder(BO >= 0 && T.IsValidIndex(BO) ? T[BO] : TEXT("1"));

            const auto *P1 = NormPos.Find(ID1);
            const auto *P2 = NormPos.Find(ID2);
            if (P1 && P2)
            {
                // Find atom indices in the stored positions
                int32 Idx1 = -1, Idx2 = -1;
                for (int32 i = 0; i < Info->AtomPositions.Num(); ++i)
                {
                    if (Info->AtomPositions[i].Equals(*P1, 0.1f))
                        Idx1 = i;
                    if (Info->AtomPositions[i].Equals(*P2, 0.1f))
                        Idx2 = i;
                }

                // Store bond connectivity
                if (Idx1 >= 0 && Idx2 >= 0)
                {
                    Info->BondPairs.Add(TPair<int32, int32>(Idx1, Idx2));
                    Info->BondOrders.Add(Ord);
                }

                // Extract element symbols from atom IDs (first 1-2 characters)
                FString Elem1, Elem2;
                if (ID1.Len() > 0)
                {
                    Elem1 = ID1.Left(1);
                    if (ID1.Len() > 1 && FChar::IsLower(ID1[1]))
                        Elem1 = ID1.Left(2);
                }
                if (ID2.Len() > 0)
                {
                    Elem2 = ID2.Left(1);
                    if (ID2.Len() > 1 && FChar::IsLower(ID2[1]))
                        Elem2 = ID2.Left(2);
                }

                // Apply scaling when drawing
                FVector ScaledPos1 = *P1 * PDB::SCALE;
                FVector ScaledPos2 = *P2 * PDB::SCALE;
                DrawBond(ScaledPos1, ScaledPos2, Ord, Elem1, Elem2, GetRootComponent(), Info->BondMeshes);

                if (Info->BondMeshes.Num() > 0)
                {
                    Info->BondMeshes.Last()->SetVisibility(Info->bIsVisible);
                }
            }
        }
        else if (bLoop && L.StartsWith(TEXT("data_")))
            break;
    }

    // IMPORTANT: Broadcast after bonds are loaded so hydrogens can be generated
    OnLigandsLoaded.Broadcast();
}

FString APDBViewer::NormalizeAtomID(const FString &In) const
{
    FString Out;
    for (const TCHAR C : In)
        if (FChar::IsAlnum(C))
            Out.AppendChar(FChar::ToUpper(C));
    return Out;
}

int32 APDBViewer::ParseBondOrder(const FString &S) const
{
    if (S.Len() == 1 && S[0] >= '1' && S[0] <= '3')
        return S[0] - '0';
    FString L = S.ToLower();
    if (L.Contains(TEXT("ar")))
        return 1;
    if (L.Contains(TEXT("doub")) || L.Equals(TEXT("d")))
        return 2;
    if (L.Contains(TEXT("trip")) || L.Equals(TEXT("t")))
        return 3;
    return FMath::Clamp(FCString::Atoi(*S), 1, 3);
}

void APDBViewer::DrawSphere(float X, float Y, float Z, const FLinearColor &Col, USceneComponent *Par, TArray<UStaticMeshComponent *> &Out)
{
    if (!SphereMeshAsset || !SphereMaterialAsset || !Par)
        return;

    auto *Sph = NewObject<UStaticMeshComponent>(this);
    Sph->SetStaticMesh(SphereMeshAsset);
    Sph->SetWorldScale3D(FVector(PDB::SPHERE_SIZE));
    Sph->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    auto *Mat = UMaterialInstanceDynamic::Create(SphereMaterialAsset, Sph);
    Mat->SetVectorParameterValue(TEXT("Color"), Col);
    Mat->SetScalarParameterValue(TEXT("EmissiveIntensity"), 5.0f);
    Sph->SetMaterial(0, Mat);

    Sph->AttachToComponent(Par, FAttachmentTransformRules::KeepWorldTransform);
    Sph->SetRelativeLocation(Par->GetComponentTransform().InverseTransformPosition(FVector(X, Y, Z)));
    Sph->RegisterComponent();

    Out.Add(Sph);
    AllAtomMeshes.Add(Sph);
}

void APDBViewer::DrawBond(const FVector &S, const FVector &E, int32 Ord, const FString &Element1, const FString &Element2, USceneComponent *Par, TArray<UStaticMeshComponent *> &Out)
{
    if (!CylinderMeshAsset || !SphereMaterialAsset || !Par)
        return;

    FVector V = E - S;
    float Len = V.Size();
    if (Len < KINDA_SMALL_NUMBER)
        return;

    FRotator Rot = FRotationMatrix::MakeFromZ(V).Rotator();
    float Scale = Len / 100.0f;

    FLinearColor Color1 = GetElementColor(Element1);
    FLinearColor Color2 = GetElementColor(Element2);

    auto MakeCyl = [&](const FVector &Pos, float ScaleZ, const FLinearColor &Color)
    {
        auto *Cyl = NewObject<UStaticMeshComponent>(this);
        Cyl->SetStaticMesh(CylinderMeshAsset);
        Cyl->SetWorldLocation(Pos);
        Cyl->SetWorldRotation(Rot);
        Cyl->SetWorldScale3D(FVector(PDB::CYLINDER_SIZE, PDB::CYLINDER_SIZE, ScaleZ));
        Cyl->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        auto *Mat = UMaterialInstanceDynamic::Create(SphereMaterialAsset, Cyl);
        Mat->SetVectorParameterValue(TEXT("Color"), Color);
        Mat->SetScalarParameterValue(TEXT("EmissiveIntensity"), 2.0f);
        Cyl->SetMaterial(0, Mat);
        Cyl->AttachToComponent(Par, FAttachmentTransformRules::KeepWorldTransform);
        Cyl->RegisterComponent();
        Out.Add(Cyl);
        AllBondMeshes.Add(Cyl);
    };

    FVector HalfDir = V * 0.5f;
    FVector FirstMid = S + V * 0.25f;
    FVector SecondMid = S + V * 0.75f;
    float HalfScale = Scale * 0.5f;

    if (Ord <= 1)
    {
        MakeCyl(FirstMid, HalfScale, Color1);
        MakeCyl(SecondMid, HalfScale, Color2);
        return;
    }

    FVector Perp = FVector::CrossProduct(V.GetSafeNormal(), FVector::UpVector);
    if (Perp.SizeSquared() < KINDA_SMALL_NUMBER)
        Perp = FVector::CrossProduct(V.GetSafeNormal(), FVector::RightVector);
    Perp.Normalize();
    FVector Off = Perp * PDB::BOND_OFFSET;

    if (Ord == 2)
    {
        MakeCyl(FirstMid + Off, HalfScale, Color1);
        MakeCyl(SecondMid + Off, HalfScale, Color2);
        MakeCyl(FirstMid - Off, HalfScale, Color1);
        MakeCyl(SecondMid - Off, HalfScale, Color2);
    }
    else if (Ord == 3)
    {
        MakeCyl(FirstMid, HalfScale, Color1);
        MakeCyl(SecondMid, HalfScale, Color2);
        MakeCyl(FirstMid + Off, HalfScale, Color1);
        MakeCyl(SecondMid + Off, HalfScale, Color2);
        MakeCyl(FirstMid - Off, HalfScale, Color1);
        MakeCyl(SecondMid - Off, HalfScale, Color2);
    }
    else
    {
        MakeCyl(FirstMid, HalfScale, Color1);
        MakeCyl(SecondMid, HalfScale, Color2);
    }
}

void APDBViewer::ClearOverlapMarkers()
{
    for (auto *M : OverlapMarkers)
    {
        if (M && IsValid(M))
            M->DestroyComponent();
    }
    OverlapMarkers.Empty();
}

/* void APDBViewer::HighlightOverlapAtoms(const TArray<FMMOverlapInfo> &Overlaps)
{
    ClearOverlapMarkers();
    if (!SphereMeshAsset || !SphereMaterialAsset || !CylinderMeshAsset)
        return;

    APlayerController *PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
    FVector FirstPos = FVector::ZeroVector;
    bool bHaveFirst = false;

    for (const FMMOverlapInfo &O : Overlaps)
    {
        // Create small red sphere at receptor position
        auto *S1 = NewObject<UStaticMeshComponent>(this);
        S1->SetStaticMesh(SphereMeshAsset);
        S1->SetWorldScale3D(FVector(PDB::SPHERE_SIZE * 0.6f));
        S1->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        auto *Mat1 = UMaterialInstanceDynamic::Create(SphereMaterialAsset, S1);
        Mat1->SetVectorParameterValue(TEXT("Color"), FLinearColor::Red);
        Mat1->SetScalarParameterValue(TEXT("EmissiveIntensity"), 8.0f);
        S1->SetMaterial(0, Mat1);
        S1->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
        S1->SetWorldLocation(O.ReceptorPosition);
        S1->RegisterComponent();
        OverlapMarkers.Add(S1);

        // Create small red sphere at ligand position
        auto *S2 = NewObject<UStaticMeshComponent>(this);
        S2->SetStaticMesh(SphereMeshAsset);
        S2->SetWorldScale3D(FVector(PDB::SPHERE_SIZE * 0.6f));
        S2->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        auto *Mat2 = UMaterialInstanceDynamic::Create(SphereMaterialAsset, S2);
        Mat2->SetVectorParameterValue(TEXT("Color"), FLinearColor::Red);
        Mat2->SetScalarParameterValue(TEXT("EmissiveIntensity"), 8.0f);
        S2->SetMaterial(0, Mat2);
        S2->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
        S2->SetWorldLocation(O.LigandPosition);
        S2->RegisterComponent();
        OverlapMarkers.Add(S2);

        // Create a thin cylinder connecting the two
        FVector Dir = O.LigandPosition - O.ReceptorPosition;
        float Len = Dir.Size();
        if (Len > KINDA_SMALL_NUMBER)
        {
            FRotator Rot = FRotationMatrix::MakeFromZ(Dir).Rotator();
            FVector Mid = O.ReceptorPosition + 0.5f * Dir;
            auto *Cyl = NewObject<UStaticMeshComponent>(this);
            Cyl->SetStaticMesh(CylinderMeshAsset);
            Cyl->SetWorldLocation(Mid);
            Cyl->SetWorldRotation(Rot);
            Cyl->SetWorldScale3D(FVector(PDB::CYLINDER_SIZE * 0.4f, PDB::CYLINDER_SIZE * 0.4f, Len / 100.0f));
            Cyl->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            auto *MatC = UMaterialInstanceDynamic::Create(SphereMaterialAsset, Cyl);
            MatC->SetVectorParameterValue(TEXT("Color"), FLinearColor::Red);
            MatC->SetScalarParameterValue(TEXT("EmissiveIntensity"), 6.0f);
            Cyl->SetMaterial(0, MatC);
            Cyl->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
            Cyl->RegisterComponent();
            OverlapMarkers.Add(Cyl);
        }

        if (!bHaveFirst)
        {
            FirstPos = O.LigandPosition;
            bHaveFirst = true;
        }
    }

    // Center camera on first overlap (optional)
    if (bHaveFirst && PC)
    {
        PC->SetViewTarget(this);
    }
}
 */
FLinearColor APDBViewer::GetElementColor(const FString &E) const
{
    static const TMap<FString, FLinearColor> Colors = {
        {TEXT("C"), FLinearColor(0.1f, 0.1f, 0.1f)}, {TEXT("O"), FLinearColor::Red}, {TEXT("H"), FLinearColor::White}, {TEXT("D"), FLinearColor::White}, {TEXT("N"), FLinearColor::Blue}, {TEXT("S"), FLinearColor::Yellow}, {TEXT("CL"), FLinearColor(0, 1, 0)}, {TEXT("P"), FLinearColor(1, 0.5f, 0)}, {TEXT("F"), FLinearColor(0, 1, 0)}, {TEXT("BR"), FLinearColor(0.6f, 0.2f, 0.2f)}, {TEXT("I"), FLinearColor(0.4f, 0, 0.8f)}, {TEXT("FE"), FLinearColor(0.8f, 0.4f, 0)}, {TEXT("MG"), FLinearColor(0, 0.8f, 0)}, {TEXT("ZN"), FLinearColor(0.5f, 0.5f, 0.5f)}, {TEXT("CA"), FLinearColor(0.2f, 0.6f, 1)}, {TEXT("NA"), FLinearColor(0, 0, 1)}, {TEXT("K"), FLinearColor(0.5f, 0, 1)}, {TEXT("CU"), FLinearColor(1, 0.5f, 0)}, {TEXT("B"), FLinearColor(1, 0.7f, 0.7f)}};
    const auto *C = Colors.Find(E.ToUpper());
    return C ? *C : FLinearColor::Gray;
}

void APDBViewer::ClearResidueMap()
{
    for (auto &P : ResidueMap)
    {
        if (!P.Value)
            continue;
        
        FResidueInfo* Info = P.Value;
        
        // Clear existing mesh components
        for (auto* M : Info->AtomMeshes)
        {
            if (M && IsValid(M))
                M->DestroyComponent();
        }
        
        for (auto* M : Info->BondMeshes)
        {
            if (M && IsValid(M))
                M->DestroyComponent();
        }
        
        delete P.Value;
    }
    ResidueMap.Empty();
    ChainIDs.Empty();
}


void APDBViewer::ClearLigandMap()
{
    for (auto &P : LigandMap)
    {
        if (!P.Value)
            continue;
        
        FLigandInfo* Info = P.Value;
        
        // Clear existing mesh components
        for (auto* M : Info->AtomMeshes)
        {
            if (M && IsValid(M))
                M->DestroyComponent();
        }
        
        for (auto* M : Info->BondMeshes)
        {
            if (M && IsValid(M))
                M->DestroyComponent();
        }
        
        // ===== NEW: CLEAR ATOM LIGHTS =====
        ClearLigandAtomLights(Info);
        // ==================================
        
        delete P.Value;
    }
    LigandMap.Empty();
}

void APDBViewer::ToggleResidueVisibility(const FString &Key)
{
    auto **InfoPtr = ResidueMap.Find(Key);
    if (!InfoPtr || !*InfoPtr)
        return;
    auto *Info = *InfoPtr;
    Info->bIsVisible = !Info->bIsVisible;
    for (auto *M : Info->AtomMeshes)
        if (M)
            M->SetVisibility(Info->bIsVisible);
    for (auto *M : Info->BondMeshes)
        if (M)
            M->SetVisibility(Info->bIsVisible);
}

TArray<UPDBTreeNode*> APDBViewer::GetChainNodes()
{
    TArray<UPDBTreeNode*> Nodes;
    TArray<FString> SortedChains = ChainIDs.Array();
    SortedChains.Sort();

    for (const FString& ChainID : SortedChains)
    {
        FString DisplayName = ChainID == TEXT("_")
                                  ? TEXT("Chain (No ID)")
                                  : FString::Printf(TEXT("Chain %s"), *ChainID);

        UPDBTreeNode* Node = NewObject<UPDBTreeNode>(this);
        Node->InitializeWithType(DisplayName, ChainID, EPDBNodeType::Chain, ChainID);
        Nodes.Add(Node);
    }

    return Nodes;
}

TArray<UPDBTreeNode*> APDBViewer::GetResidueNodesForChain(const FString& ChainID)
{
    TArray<UPDBTreeNode*> Nodes;
    TArray<FString> Keys;
    ResidueMap.GetKeys(Keys);

    // Sort by residue sequence number
    Keys.Sort([](const FString& A, const FString& B)
    {
        int32 U1, U2;
        if (!A.FindChar('_', U1) || !B.FindChar('_', U2))
            return A < B;

        int32 U3 = A.Find(TEXT("_"), ESearchCase::IgnoreCase, ESearchDir::FromStart, U1 + 1);
        int32 U4 = B.Find(TEXT("_"), ESearchCase::IgnoreCase, ESearchDir::FromStart, U2 + 1);

        if (U3 != INDEX_NONE && U4 != INDEX_NONE)
        {
            int32 NumA = FCString::Atoi(*A.Mid(U1 + 1, U3 - U1 - 1));
            int32 NumB = FCString::Atoi(*B.Mid(U2 + 1, U4 - U2 - 1));
            return NumA < NumB;
        }
        return A < B;
    });

    for (const FString& Key : Keys)
    {
        const auto* InfoPtr = ResidueMap.Find(Key);
        if (!InfoPtr || !*InfoPtr)
            continue;

        const FResidueInfo* Info = *InfoPtr;
        if (Info->Chain != ChainID)
            continue;

        FString DisplayName = FString::Printf(TEXT("%s %s"), *Info->ResidueName, *Info->ResidueSeq);

        UPDBTreeNode* Node = NewObject<UPDBTreeNode>(this);
        Node->InitializeWithType(DisplayName, Key, EPDBNodeType::Residue, ChainID);
        Node->bIsVisible = Info->bIsVisible;

        Nodes.Add(Node);
    }

    return Nodes;
}


TArray<UPDBTreeNode*> APDBViewer::GetWaterNodesForChain(const FString& ChainID)
{
    TArray<UPDBTreeNode*> Nodes;
    TArray<FString> Keys;
    LigandMap.GetKeys(Keys);

    // Sort by sequence number
    Keys.Sort([](const FString& A, const FString& B)
    {
        int32 U1, U2;
        if (!A.FindChar('_', U1) || !B.FindChar('_', U2))
            return A < B;

        int32 U3 = A.Find(TEXT("_"), ESearchCase::IgnoreCase, ESearchDir::FromStart, U1 + 1);
        int32 U4 = B.Find(TEXT("_"), ESearchCase::IgnoreCase, ESearchDir::FromStart, U2 + 1);

        if (U3 != INDEX_NONE && U4 != INDEX_NONE)
        {
            int32 NumA = FCString::Atoi(*A.Mid(U1 + 1, U3 - U1 - 1));
            int32 NumB = FCString::Atoi(*B.Mid(U2 + 1, U4 - U2 - 1));
            return NumA < NumB;
        }
        return A < B;
    });

    for (const FString& Key : Keys)
    {
        // Check if this is a water molecule
        if (!IsWaterKey(Key))
            continue;

        const auto* InfoPtr = LigandMap.Find(Key);
        if (!InfoPtr || !*InfoPtr)
            continue;

        const FLigandInfo* Info = *InfoPtr;
        
        // Extract chain from key (format: "HOH_101_A")
        TArray<FString> Parts;
        Key.ParseIntoArray(Parts, TEXT("_"));
        FString KeyChain = Parts.Num() >= 3 ? Parts[2] : TEXT("_");
        
        if (KeyChain != ChainID)
            continue;

        UPDBTreeNode* Node = NewObject<UPDBTreeNode>(this);
        Node->InitializeWithType(Info->LigandName, Key, EPDBNodeType::Water, ChainID);
        Node->bIsVisible = Info->bIsVisible;

        Nodes.Add(Node);
    }

    return Nodes;
}



TArray<UPDBTreeNode*> APDBViewer::GetLigandNodesForChain(const FString& ChainID)
{
    TArray<UPDBTreeNode*> Nodes;
    TArray<FString> Keys;
    LigandMap.GetKeys(Keys);

    // Sort by name then sequence number
    Keys.Sort([](const FString& A, const FString& B)
    {
        int32 U1, U2;
        if (!A.FindChar('_', U1) || !B.FindChar('_', U2))
            return A < B;

        // First compare residue names
        FString NameA = A.Left(U1);
        FString NameB = B.Left(U2);
        if (NameA != NameB)
            return NameA < NameB;

        // Then compare sequence numbers
        int32 U3 = A.Find(TEXT("_"), ESearchCase::IgnoreCase, ESearchDir::FromStart, U1 + 1);
        int32 U4 = B.Find(TEXT("_"), ESearchCase::IgnoreCase, ESearchDir::FromStart, U2 + 1);

        if (U3 != INDEX_NONE && U4 != INDEX_NONE)
        {
            int32 NumA = FCString::Atoi(*A.Mid(U1 + 1, U3 - U1 - 1));
            int32 NumB = FCString::Atoi(*B.Mid(U2 + 1, U4 - U2 - 1));
            return NumA < NumB;
        }
        return A < B;
    });

    for (const FString& Key : Keys)
    {
        // Skip water molecules - they go in the Water category
        if (IsWaterKey(Key))
            continue;

        const auto* InfoPtr = LigandMap.Find(Key);
        if (!InfoPtr || !*InfoPtr)
            continue;

        const FLigandInfo* Info = *InfoPtr;
        
        // Extract chain from key (format: "ATP_501_A")
        TArray<FString> Parts;
        Key.ParseIntoArray(Parts, TEXT("_"));
        FString KeyChain = Parts.Num() >= 3 ? Parts[2] : TEXT("_");
        
        if (KeyChain != ChainID)
            continue;

        UPDBTreeNode* Node = NewObject<UPDBTreeNode>(this);
        Node->InitializeWithType(Info->LigandName, Key, EPDBNodeType::Ligand, ChainID);
        Node->bIsVisible = Info->bIsVisible;

        Nodes.Add(Node);
    }

    return Nodes;
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
            for (auto *M : Pair.Value->AtomMeshes)
                if (M)
                    M->SetVisibility(bNewVisibility);
            for (auto *M : Pair.Value->BondMeshes)
                if (M)
                    M->SetVisibility(bNewVisibility);
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
    
    // Determine new visibility by checking first item in category
    bool bNewVisibility = true;
    
    switch (Node->NodeType)
    {
        case EPDBNodeType::ResiduesCategory:
        {
            // Toggle all residues in this chain
            for (auto& Pair : ResidueMap)
            {
                if (Pair.Value && Pair.Value->Chain == Node->ChainID)
                {
                    bNewVisibility = !Pair.Value->bIsVisible;
                    break;
                }
            }
            
            for (auto& Pair : ResidueMap)
            {
                if (Pair.Value && Pair.Value->Chain == Node->ChainID)
                {
                    Pair.Value->bIsVisible = bNewVisibility;
                    for (auto* M : Pair.Value->AtomMeshes)
                        if (M) M->SetVisibility(bNewVisibility);
                    for (auto* M : Pair.Value->BondMeshes)
                        if (M) M->SetVisibility(bNewVisibility);
                }
            }
            break;
        }
        
        case EPDBNodeType::HeteroatomsCategory:
        {
            // Toggle all heteroatoms (water + ligands) in this chain
            for (auto& Pair : LigandMap)
            {
                TArray<FString> Parts;
                Pair.Key.ParseIntoArray(Parts, TEXT("_"));
                if (Parts.Num() >= 3 && Parts[2] == Node->ChainID && Pair.Value)
                {
                    bNewVisibility = !Pair.Value->bIsVisible;
                    break;
                }
            }
            
            for (auto& Pair : LigandMap)
            {
                TArray<FString> Parts;
                Pair.Key.ParseIntoArray(Parts, TEXT("_"));
                if (Parts.Num() >= 3 && Parts[2] == Node->ChainID && Pair.Value)
                {
                    Pair.Value->bIsVisible = bNewVisibility;
                    for (auto* M : Pair.Value->AtomMeshes)
                        if (M) M->SetVisibility(bNewVisibility);
                    for (auto* M : Pair.Value->BondMeshes)
                        if (M) M->SetVisibility(bNewVisibility);
                    UpdateLigandAtomLights(Pair.Value);
                }
            }
            break;
        }
        
        case EPDBNodeType::WaterCategory:
        {
            // Toggle all water molecules in this chain
            for (auto& Pair : LigandMap)
            {
                if (!IsWaterKey(Pair.Key))
                    continue;
                    
                TArray<FString> Parts;
                Pair.Key.ParseIntoArray(Parts, TEXT("_"));
                if (Parts.Num() >= 3 && Parts[2] == Node->ChainID && Pair.Value)
                {
                    bNewVisibility = !Pair.Value->bIsVisible;
                    break;
                }
            }
            
            for (auto& Pair : LigandMap)
            {
                if (!IsWaterKey(Pair.Key))
                    continue;
                    
                TArray<FString> Parts;
                Pair.Key.ParseIntoArray(Parts, TEXT("_"));
                if (Parts.Num() >= 3 && Parts[2] == Node->ChainID && Pair.Value)
                {
                    Pair.Value->bIsVisible = bNewVisibility;
                    for (auto* M : Pair.Value->AtomMeshes)
                        if (M) M->SetVisibility(bNewVisibility);
                    for (auto* M : Pair.Value->BondMeshes)
                        if (M) M->SetVisibility(bNewVisibility);
                    UpdateLigandAtomLights(Pair.Value);
                }
            }
            break;
        }
        
        case EPDBNodeType::LigandsCategory:
        {
            // Toggle all ligands (non-water) in this chain
            for (auto& Pair : LigandMap)
            {
                if (IsWaterKey(Pair.Key))
                    continue;
                    
                TArray<FString> Parts;
                Pair.Key.ParseIntoArray(Parts, TEXT("_"));
                if (Parts.Num() >= 3 && Parts[2] == Node->ChainID && Pair.Value)
                {
                    bNewVisibility = !Pair.Value->bIsVisible;
                    break;
                }
            }
            
            for (auto& Pair : LigandMap)
            {
                if (IsWaterKey(Pair.Key))
                    continue;
                    
                TArray<FString> Parts;
                Pair.Key.ParseIntoArray(Parts, TEXT("_"));
                if (Parts.Num() >= 3 && Parts[2] == Node->ChainID && Pair.Value)
                {
                    Pair.Value->bIsVisible = bNewVisibility;
                    for (auto* M : Pair.Value->AtomMeshes)
                        if (M) M->SetVisibility(bNewVisibility);
                    for (auto* M : Pair.Value->BondMeshes)
                        if (M) M->SetVisibility(bNewVisibility);
                    UpdateLigandAtomLights(Pair.Value);
                }
            }
            break;
        }
        
        default:
            break;
    }
    
    // Update the node's visibility state
    Node->bIsVisible = bNewVisibility;
}


void APDBViewer::PopulateTreeView(UTreeView *TreeView)
{
    if (!TreeView)
    {
        UE_LOG(LogTemp, Error, TEXT("PopulateTreeView called with NULL TreeView!"));
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("====== PopulateTreeView starting ======"));

    TreeView->ClearListItems();
    
    // Force clear any selection
    TreeView->ClearSelection();

    // CRITICAL FIX: Bind the GetChildren delegate in C++
    // This tells the TreeView how to get children for each node
    // UE 5.6 syntax: SetOnGetItemChildren(ObjectPointer, MethodPointer)
    TreeView->SetOnGetItemChildren(this, &APDBViewer::GetChildrenForNodeInternal);
    
    UE_LOG(LogTemp, Warning, TEXT("SetOnGetItemChildren bound to GetChildrenForNodeInternal"));

    TArray<UPDBTreeNode *> ChainNodes = GetChainNodes();
    
    UE_LOG(LogTemp, Warning, TEXT("Found %d chain nodes"), ChainNodes.Num());

    for (UPDBTreeNode *ChainNode : ChainNodes)
    {
        TreeView->AddItem(ChainNode);
        
        // Make sure items start collapsed
        TreeView->SetItemExpansion(ChainNode, false);
        
        UE_LOG(LogTemp, Warning, TEXT("Added chain node: %s (bCanExpand: %s)"), 
               *ChainNode->DisplayName, 
               ChainNode->bCanExpand ? TEXT("true") : TEXT("false"));
    }

    TreeView->RequestRefresh();
    
    UE_LOG(LogTemp, Warning, TEXT("====== PopulateTreeView complete ======"));
}

// NEW: Internal callback for UE 5.6 TreeView - uses reference parameter
void APDBViewer::GetChildrenForNodeInternal(UObject* Item, TArray<UObject*>& OutChildren)
{
    UE_LOG(LogTemp, Warning, TEXT("GetChildrenForNodeInternal called with Item: %s"), Item ? *Item->GetName() : TEXT("NULL"));
    
    UPDBTreeNode* Node = Cast<UPDBTreeNode>(Item);
    if (!Node)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to cast Item to UPDBTreeNode!"));
        return;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("Node type: %d, DisplayName: %s, NodeKey: %s"), 
           (int32)Node->NodeType, *Node->DisplayName, *Node->NodeKey);
    
    // Call the existing function and copy results to the output array
    OutChildren = GetChildrenForNode(Node);
    
    UE_LOG(LogTemp, Warning, TEXT("GetChildrenForNode returned %d children"), OutChildren.Num());
    for (int32 i = 0; i < OutChildren.Num(); ++i)
    {
        if (UPDBTreeNode* ChildNode = Cast<UPDBTreeNode>(OutChildren[i]))
        {
            UE_LOG(LogTemp, Warning, TEXT("  Child %d: %s (Type: %d)"), 
                   i, *ChildNode->DisplayName, (int32)ChildNode->NodeType);
        }
    }
}

TArray<UObject*> APDBViewer::GetChildrenForNode(UPDBTreeNode* Node)
{
    TArray<UObject*> ChildNodes;

    if (!Node)
        return ChildNodes;

    switch (Node->NodeType)
    {
        case EPDBNodeType::Chain:
        {
            // Chain has two category children: Residues and Heteroatoms
            
            // Get or create "Residues" category node
            FString ResiduesKey = FString::Printf(TEXT("RESIDUES_%s"), *Node->ChainID);
            UPDBTreeNode** ResiduesCategoryPtr = TreeNodeCache.Find(ResiduesKey);
            UPDBTreeNode* ResiduesCategory = nullptr;
            
            if (ResiduesCategoryPtr && *ResiduesCategoryPtr)
            {
                ResiduesCategory = *ResiduesCategoryPtr;
            }
            else
            {
                ResiduesCategory = NewObject<UPDBTreeNode>(this);
                ResiduesCategory->InitializeWithType(TEXT("Residues"), ResiduesKey,
                    EPDBNodeType::ResiduesCategory, Node->ChainID);
                TreeNodeCache.Add(ResiduesKey, ResiduesCategory);
            }
            ChildNodes.Add(ResiduesCategory);
            
            // Get or create "Heteroatoms" category node (only if there are any)
            bool bHasHeteroatoms = false;
            for (const auto& Pair : LigandMap)
            {
                TArray<FString> Parts;
                Pair.Key.ParseIntoArray(Parts, TEXT("_"));
                if (Parts.Num() >= 3 && Parts[2] == Node->ChainID)
                {
                    bHasHeteroatoms = true;
                    break;
                }
            }
            
            if (bHasHeteroatoms)
            {
                FString HeteroatomsKey = FString::Printf(TEXT("HETEROATOMS_%s"), *Node->ChainID);
                UPDBTreeNode** HeteroatomsCategoryPtr = TreeNodeCache.Find(HeteroatomsKey);
                UPDBTreeNode* HeteroatomsCategory = nullptr;
                
                if (HeteroatomsCategoryPtr && *HeteroatomsCategoryPtr)
                {
                    HeteroatomsCategory = *HeteroatomsCategoryPtr;
                }
                else
                {
                    HeteroatomsCategory = NewObject<UPDBTreeNode>(this);
                    HeteroatomsCategory->InitializeWithType(TEXT("Heteroatoms"), HeteroatomsKey,
                        EPDBNodeType::HeteroatomsCategory, Node->ChainID);
                    TreeNodeCache.Add(HeteroatomsKey, HeteroatomsCategory);
                }
                ChildNodes.Add(HeteroatomsCategory);
            }
            break;
        }
        
        case EPDBNodeType::ResiduesCategory:
        {
            // Residues category contains individual residues
            TArray<UPDBTreeNode*> Residues = GetResidueNodesForChain(Node->ChainID);
            for (UPDBTreeNode* Residue : Residues)
            {
                ChildNodes.Add(Residue);
            }
            break;
        }
        
        case EPDBNodeType::HeteroatomsCategory:
        {
            // Heteroatoms category contains Water and Ligands subcategories
            
            // Check if there are any water molecules for this chain
            TArray<UPDBTreeNode*> Waters = GetWaterNodesForChain(Node->ChainID);
            if (Waters.Num() > 0)
            {
                FString WaterKey = FString::Printf(TEXT("WATER_%s"), *Node->ChainID);
                UPDBTreeNode** WaterCategoryPtr = TreeNodeCache.Find(WaterKey);
                UPDBTreeNode* WaterCategory = nullptr;
                
                if (WaterCategoryPtr && *WaterCategoryPtr)
                {
                    WaterCategory = *WaterCategoryPtr;
                }
                else
                {
                    WaterCategory = NewObject<UPDBTreeNode>(this);
                    WaterCategory->InitializeWithType(
                        FString::Printf(TEXT("Water (%d)"), Waters.Num()),
                        WaterKey,
                        EPDBNodeType::WaterCategory, Node->ChainID);
                    TreeNodeCache.Add(WaterKey, WaterCategory);
                }
                ChildNodes.Add(WaterCategory);
            }
            
            // Check if there are any ligands for this chain
            TArray<UPDBTreeNode*> Ligands = GetLigandNodesForChain(Node->ChainID);
            if (Ligands.Num() > 0)
            {
                FString LigandsKey = FString::Printf(TEXT("LIGANDS_%s"), *Node->ChainID);
                UPDBTreeNode** LigandsCategoryPtr = TreeNodeCache.Find(LigandsKey);
                UPDBTreeNode* LigandsCategory = nullptr;
                
                if (LigandsCategoryPtr && *LigandsCategoryPtr)
                {
                    LigandsCategory = *LigandsCategoryPtr;
                }
                else
                {
                    LigandsCategory = NewObject<UPDBTreeNode>(this);
                    LigandsCategory->InitializeWithType(
                        FString::Printf(TEXT("Ligands (%d)"), Ligands.Num()),
                        LigandsKey,
                        EPDBNodeType::LigandsCategory, Node->ChainID);
                    TreeNodeCache.Add(LigandsKey, LigandsCategory);
                }
                ChildNodes.Add(LigandsCategory);
            }
            break;
        }
        
        case EPDBNodeType::WaterCategory:
        {
            // Water category contains individual water molecules
            TArray<UPDBTreeNode*> Waters = GetWaterNodesForChain(Node->ChainID);
            for (UPDBTreeNode* Water : Waters)
            {
                ChildNodes.Add(Water);
            }
            break;
        }
        
        case EPDBNodeType::LigandsCategory:
        {
            // Ligands category contains individual ligands
            TArray<UPDBTreeNode*> Ligands = GetLigandNodesForChain(Node->ChainID);
            for (UPDBTreeNode* Ligand : Ligands)
            {
                ChildNodes.Add(Ligand);
            }
            break;
        }
        
        default:
            // Leaf nodes have no children
            break;
    }

    return ChildNodes;
}

TArray<FString> APDBViewer::GetResidueList() const
{
    TArray<FString> List;
    ResidueMap.GetKeys(List);
    List.Sort([](const FString &A, const FString &B)
              {
        int32 U1, U2;
        if (!A.FindChar('_', U1) || !B.FindChar('_', U2))
            return A < B;

        int32 U3 = A.Find(TEXT("_"), ESearchCase::IgnoreCase, ESearchDir::FromStart, U1 + 1);
        int32 U4 = B.Find(TEXT("_"), ESearchCase::IgnoreCase, ESearchDir::FromStart, U2 + 1);

        if (U3 != INDEX_NONE && U4 != INDEX_NONE)
        {
            int32 NumA = FCString::Atoi(*A.Mid(U1 + 1, U3 - U1 - 1));
            int32 NumB = FCString::Atoi(*B.Mid(U2 + 1, U4 - U2 - 1));
            return NumA < NumB;
        }
        return A < B; });
    return List;
}

TArray<FString> APDBViewer::GetLigandList() const { return GetResidueList(); }

FString APDBViewer::GetResidueDisplayName(const FString &Key) const
{
    const auto *P = ResidueMap.Find(Key);
    return (P && *P)
               ? FString::Printf(TEXT("%s %s"), *(*P)->ResidueName, *(*P)->ResidueSeq)
               : Key;
}

FString APDBViewer::GetLigandDisplayName(const FString &Key) const
{
    const auto *P = LigandMap.Find(Key);
    return (P && *P)
               ? FString::Printf(TEXT("%s"), *(*P)->LigandName)
               : Key;
}

FLigandInfo *APDBViewer::GetVisibleLigandInfo() const
{
    FLigandInfo *BestInfo = nullptr;
    int32 BestCount = 0;

    for (auto &Pair : LigandMap)
    {
        FLigandInfo *Info = Pair.Value;
        if (!Info || !Info->bIsVisible)
            continue;
        int32 Count = Info->AtomMeshes.Num() + Info->BondMeshes.Num();
        if (Count > BestCount)
        {
            BestCount = Count;
            BestInfo = Info;
        }
    }

    return BestInfo;
}

void APDBViewer::SaveStructureToFile(const FString &Path)
{
    if (CurrentPDBContent.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("No data to save"));
        return;
    }

    if (FFileHelper::SaveStringToFile(CurrentPDBContent, *Path))
    {
        UE_LOG(LogTemp, Log, TEXT("Saved: %s"), *Path);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed: %s"), *Path);
    }
}

void APDBViewer::LoadStructureFromFile(const FString &Path)
{
    FString Content;
    if (!FFileHelper::LoadFileToString(Content, *Path))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load: %s"), *Path);
        return;
    }

    FString Ext = FPaths::GetExtension(Path).ToLower();

    // Only clear structure ID and set content for PDB/CIF
    if (Ext == TEXT("pdb") || Ext == TEXT("cif"))
    {
        // Don't call ClearCurrentStructure here - let individual parsers handle it
        CurrentStructureID = FPaths::GetBaseFilename(Path);
    }

    CurrentPDBContent = Content;

    if (Ext == TEXT("pdb"))
        ParsePDB(CurrentPDBContent);
    else if (Ext == TEXT("cif"))
        ParseMMCIF(CurrentPDBContent);
    else if (Ext == TEXT("sdf") || Ext == TEXT("mol"))
        ParseSDF(CurrentPDBContent);
    else
        UE_LOG(LogTemp, Error, TEXT("Unsupported format: %s"), *Ext);
}

bool APDBViewer::ShowFileDialog(bool bSave, FString &Out)
{
    auto *DP = FDesktopPlatformModule::Get();
    if (!DP)
        return false;

    void *Handle = nullptr;
    if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
        if (auto P = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame").GetParentWindow())
            if (P.IsValid() && P->GetNativeWindow().IsValid())
                Handle = P->GetNativeWindow()->GetOSWindowHandle();

    TArray<FString> Files;
    FString FileTypes = TEXT("All Supported (*.pdb;*.cif;*.sdf;*.mol)|*.pdb;*.cif;*.sdf;*.mol|")
        TEXT("PDB Files (*.pdb)|*.pdb|")
            TEXT("mmCIF Files (*.cif)|*.cif|")
                TEXT("SDF Files (*.sdf)|*.sdf|")
                    TEXT("MOL Files (*.mol)|*.mol");

    FString DefaultFile = bSave ? TEXT("structure.pdb") : TEXT("");
    FString DialogTitle = bSave ? TEXT("Save Structure") : TEXT("Load Structure");

    bool bOK = bSave ? DP->SaveFileDialog(Handle, DialogTitle, FPaths::ProjectSavedDir(), DefaultFile,
                                          FileTypes, EFileDialogFlags::None, Files)
                     : DP->OpenFileDialog(Handle, DialogTitle, FPaths::ProjectSavedDir(), TEXT(""),
                                          FileTypes, EFileDialogFlags::None, Files);

    if (bOK && Files.Num() > 0)
    {
        Out = MoveTemp(Files[0]);
        return true;
    }
    return false;
}

void APDBViewer::OpenSaveDialog()
{
    FString P;
    if (ShowFileDialog(true, P))
        SaveStructureToFile(P);
}
void APDBViewer::OpenLoadDialog()
{
    FString P;
    if (ShowFileDialog(false, P))
        LoadStructureFromFile(P);
}

TArray<UPDBMoleculeNode *> APDBViewer::GetMoleculeNodes()
{
    TArray<UPDBMoleculeNode *> Nodes;
    TArray<FString> Keys;
    LigandMap.GetKeys(Keys);

    Keys.Sort();

    for (const FString &Key : Keys)
    {
        const auto *InfoPtr = LigandMap.Find(Key);
        if (!InfoPtr || !*InfoPtr)
            continue;

        const FLigandInfo *Info = *InfoPtr;

        UPDBMoleculeNode *Node = NewObject<UPDBMoleculeNode>(this);
        Node->Initialize(
            Info->LigandName,
            Key,
            Info->bIsVisible,
            Info->AtomMeshes.Num(),
            Info->BondMeshes.Num());

        Nodes.Add(Node);
    }

    return Nodes;
}

void APDBViewer::PopulateMoleculeListView(UListView *ListView)
{
    if (!ListView)
        return;

    ListView->ClearListItems();

    TArray<UPDBMoleculeNode *> MoleculeNodes = GetMoleculeNodes();

    for (UPDBMoleculeNode *MoleculeNode : MoleculeNodes)
    {
        ListView->AddItem(MoleculeNode);
    }

    ListView->RegenerateAllEntries();
}


void APDBViewer::ToggleMoleculeVisibility(const FString &MoleculeKey)
{
    auto **InfoPtr = LigandMap.Find(MoleculeKey);
    if (!InfoPtr || !*InfoPtr)
        return;

    auto *Info = *InfoPtr;
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
                    for (auto *M : P.Value->AtomMeshes)
                        if (M)
                            M->SetVisibility(false);
                    for (auto *M : P.Value->BondMeshes)
                        if (M)
                            M->SetVisibility(false);
                    
                    // ===== NEW: UPDATE LIGHTS =====
                    UpdateLigandAtomLights(P.Value);
                    // ==============================
                }
            }
        }
    }

    Info->bIsVisible = bNewVisible;

    for (auto *M : Info->AtomMeshes)
        if (M)
            M->SetVisibility(Info->bIsVisible);

    for (auto *M : Info->BondMeshes)
        if (M)
            M->SetVisibility(Info->bIsVisible);

    // ===== NEW: UPDATE LIGHTS =====
    UpdateLigandAtomLights(Info);
    // ==============================
    
    // Note: We don't broadcast OnLigandsLoaded here because that event is only for
    // when ligands are initially loaded from a file, not for visibility changes.
    // If you need to respond to visibility changes, use a separate event or callback.
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

void APDBViewer::DebugPrintLigandInfo()
{
    UE_LOG(LogTemp, Log, TEXT("=== Ligand Debug Info ==="));
    UE_LOG(LogTemp, Log, TEXT("Total ligands: %d"), LigandMap.Num());

    for (auto &Pair : LigandMap)
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
        for (auto &ElemPair : ElementCounts)
            UE_LOG(LogTemp, Log, TEXT("    %s: %d"), *ElemPair.Key, ElemPair.Value);

        if (Info->BondOrders.Num() > 0)
        {
            int32 Single = 0, Double = 0, Triple = 0, Aromatic = 0;
            for (int32 Order : Info->BondOrders)
            {
                if (Order == 1)
                    Single++;
                else if (Order == 2)
                    Double++;
                else if (Order == 3)
                    Triple++;
                else
                    Aromatic++;
            }
            UE_LOG(LogTemp, Log, TEXT("  Bond Types: Single=%d, Double=%d, Triple=%d, Other=%d"),
                   Single, Double, Triple, Aromatic);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Total hydrogens: %d"), GetHydrogenCount());
    UE_LOG(LogTemp, Log, TEXT("Hydrogens visible: %s"), bHydrogensVisible ? TEXT("YES") : TEXT("NO"));
    UE_LOG(LogTemp, Log, TEXT("======================"));
}

void APDBViewer::AddExplicitHydrogens()
{
    if (bHydrogensVisible)
        return;

    // Show hydrogens for ligands
    for (auto &Pair : LigandMap)
    {
        if (!Pair.Value)
            continue;

        for (int32 i = 0; i < Pair.Value->AtomElements.Num(); ++i)
        {
            if (Pair.Value->AtomElements[i] == TEXT("H") && Pair.Value->AtomMeshes.IsValidIndex(i))
                Pair.Value->AtomMeshes[i]->SetVisibility(Pair.Value->bIsVisible);
        }

        for (int32 i = 0; i < Pair.Value->BondPairs.Num(); ++i)
        {
            int32 A1 = Pair.Value->BondPairs[i].Key;
            int32 A2 = Pair.Value->BondPairs[i].Value;

            bool bHasBond = (Pair.Value->AtomElements.IsValidIndex(A1) && Pair.Value->AtomElements[A1] == TEXT("H")) ||
                            (Pair.Value->AtomElements.IsValidIndex(A2) && Pair.Value->AtomElements[A2] == TEXT("H"));

            if (bHasBond && Pair.Value->BondMeshes.IsValidIndex(i))
                Pair.Value->BondMeshes[i]->SetVisibility(Pair.Value->bIsVisible);
        }
    }

    // Show hydrogens for residues
    for (auto &Pair : ResidueMap)
    {
        if (!Pair.Value)
            continue;

        for (int32 i = 0; i < Pair.Value->AtomElements.Num(); ++i)
        {
            if (Pair.Value->AtomElements[i] == TEXT("H") && Pair.Value->AtomMeshes.IsValidIndex(i))
                Pair.Value->AtomMeshes[i]->SetVisibility(Pair.Value->bIsVisible);
        }

        for (int32 i = 0; i < Pair.Value->BondPairs.Num(); ++i)
        {
            int32 A1 = Pair.Value->BondPairs[i].Key;
            int32 A2 = Pair.Value->BondPairs[i].Value;

            bool bHasBond = (Pair.Value->AtomElements.IsValidIndex(A1) && Pair.Value->AtomElements[A1] == TEXT("H")) ||
                            (Pair.Value->AtomElements.IsValidIndex(A2) && Pair.Value->AtomElements[A2] == TEXT("H"));

            if (bHasBond && Pair.Value->BondMeshes.IsValidIndex(i))
                Pair.Value->BondMeshes[i]->SetVisibility(Pair.Value->bIsVisible);
        }
    }

    bHydrogensVisible = true;
    UE_LOG(LogTemp, Log, TEXT("Showed %d hydrogens"), GetHydrogenCount());
}

void APDBViewer::RemoveExplicitHydrogens()
{
    if (!bHydrogensVisible)
        return;

    // Hide hydrogens for ligands
    for (auto &Pair : LigandMap)
    {
        if (!Pair.Value)
            continue;

        for (int32 i = 0; i < Pair.Value->AtomElements.Num(); ++i)
        {
            if (Pair.Value->AtomElements[i] == TEXT("H") && Pair.Value->AtomMeshes.IsValidIndex(i))
                Pair.Value->AtomMeshes[i]->SetVisibility(false);
        }

        for (int32 i = 0; i < Pair.Value->BondPairs.Num(); ++i)
        {
            int32 A1 = Pair.Value->BondPairs[i].Key;
            int32 A2 = Pair.Value->BondPairs[i].Value;

            bool bHasBond = (Pair.Value->AtomElements.IsValidIndex(A1) && Pair.Value->AtomElements[A1] == TEXT("H")) ||
                            (Pair.Value->AtomElements.IsValidIndex(A2) && Pair.Value->AtomElements[A2] == TEXT("H"));

            if (bHasBond && Pair.Value->BondMeshes.IsValidIndex(i))
                Pair.Value->BondMeshes[i]->SetVisibility(false);
        }
    }

    // Hide hydrogens for residues
    for (auto &Pair : ResidueMap)
    {
        if (!Pair.Value)
            continue;

        for (int32 i = 0; i < Pair.Value->AtomElements.Num(); ++i)
        {
            if (Pair.Value->AtomElements[i] == TEXT("H") && Pair.Value->AtomMeshes.IsValidIndex(i))
                Pair.Value->AtomMeshes[i]->SetVisibility(false);
        }

        for (int32 i = 0; i < Pair.Value->BondPairs.Num(); ++i)
        {
            int32 A1 = Pair.Value->BondPairs[i].Key;
            int32 A2 = Pair.Value->BondPairs[i].Value;

            bool bHasBond = (Pair.Value->AtomElements.IsValidIndex(A1) && Pair.Value->AtomElements[A1] == TEXT("H")) ||
                            (Pair.Value->AtomElements.IsValidIndex(A2) && Pair.Value->AtomElements[A2] == TEXT("H"));

            if (bHasBond && Pair.Value->BondMeshes.IsValidIndex(i))
                Pair.Value->BondMeshes[i]->SetVisibility(false);
        }
    }

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
    int32 Count = 0;

    // Count ligand hydrogens
    for (const auto &Pair : LigandMap)
        if (Pair.Value)
            for (const FString &Elem : Pair.Value->AtomElements)
                if (Elem == TEXT("H"))
                    Count++;

    // Count residue hydrogens
    for (const auto &Pair : ResidueMap)
        if (Pair.Value)
            for (const FString &Elem : Pair.Value->AtomElements)
                if (Elem == TEXT("H"))
                    Count++;

    return Count;
}

// Update ClearCurrentStructure - remove ClearHydrogens() call:
void APDBViewer::ClearCurrentStructure()
{
    ClearResidueMap();
    ClearLigandMap();
    
    // Clear tree node cache
    TreeNodeCache.Empty();
    
    // Clear interaction data and meshes
    for (auto *M : InteractionMeshes)
        if (M && IsValid(M))
            M->DestroyComponent();
    InteractionMeshes.Empty();
    DetectedInteractions.Empty();
    
    // Clear general atom/bond meshes
    for (auto *M : AllAtomMeshes)
        if (M && IsValid(M))
            M->DestroyComponent();
    for (auto *M : AllBondMeshes)
        if (M && IsValid(M))
            M->DestroyComponent();
    AllAtomMeshes.Empty();
    AllBondMeshes.Empty();
}
// ===== LIGAND ATOM LIGHTING IMPLEMENTATION =====

void APDBViewer::CreateLigandAtomLights(FLigandInfo* LigInfo)
{
    if (!LigInfo)
        return;
    
    // Clear any existing lights
    ClearLigandAtomLights(LigInfo);
    
    // Create a light for each atom
    for (int32 i = 0; i < LigInfo->AtomPositions.Num(); ++i)
    {
        UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
        if (!Light)
            continue;
        
        Light->RegisterComponent();
        Light->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
        
        // Set position (scaled to match atom position)
        FVector ScaledPos = LigInfo->AtomPositions[i] * PDB::SCALE;
        Light->SetRelativeLocation(ScaledPos);
        
        // Set light color based on element
        FLinearColor LightColor = FLinearColor::White;
        if (bUseDynamicAtomColors && LigInfo->AtomElements.IsValidIndex(i))
        {
            LightColor = GetLightColorForElement(LigInfo->AtomElements[i]);
        }
        Light->SetLightColor(LightColor);
        
        // Set light properties
        Light->SetIntensity(LigandAtomLightIntensity);
        Light->SetAttenuationRadius(LigandAtomLightRadius);
        Light->SetCastShadows(false); // Disable shadows for performance
        Light->SetMobility(EComponentMobility::Movable);
        
        // Set initial visibility based on ligand visibility and global light setting
        Light->SetVisibility(bLigandAtomLightsEnabled && LigInfo->bIsVisible);
        
        LigInfo->AtomLights.Add(Light);
    }
    
    UE_LOG(LogTemp, Log, TEXT("Created %d atom lights for ligand %s"), 
           LigInfo->AtomLights.Num(), *LigInfo->LigandName);
}

void APDBViewer::UpdateLigandAtomLights(FLigandInfo* LigInfo)
{
    if (!LigInfo)
        return;
    
    for (UPointLightComponent* Light : LigInfo->AtomLights)
    {
        if (!Light || !IsValid(Light))
            continue;
        
        // Update visibility
        Light->SetVisibility(bLigandAtomLightsEnabled && LigInfo->bIsVisible);
        
        // Update intensity and radius
        Light->SetIntensity(LigandAtomLightIntensity);
        Light->SetAttenuationRadius(LigandAtomLightRadius);
    }
}

void APDBViewer::ClearLigandAtomLights(FLigandInfo* LigInfo)
{
    if (!LigInfo)
        return;
    
    for (UPointLightComponent* Light : LigInfo->AtomLights)
    {
        if (Light && IsValid(Light))
        {
            Light->DestroyComponent();
        }
    }
    
    LigInfo->AtomLights.Empty();
}

FLinearColor APDBViewer::GetLightColorForElement(const FString& Element) const
{
    // Use the same color scheme as atoms
    return GetElementColor(Element);
}

void APDBViewer::SetLigandAtomLightsEnabled(bool bEnabled)
{
    bLigandAtomLightsEnabled = bEnabled;
    
    // Update all ligand lights
    for (auto& Pair : LigandMap)
    {
        if (Pair.Value)
        {
            UpdateLigandAtomLights(Pair.Value);
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("Ligand atom lights %s"), 
           bEnabled ? TEXT("enabled") : TEXT("disabled"));
}

void APDBViewer::ToggleLigandAtomLights()
{
    SetLigandAtomLightsEnabled(!bLigandAtomLightsEnabled);
}

void APDBViewer::SetLigandAtomLightIntensity(float Intensity)
{
    LigandAtomLightIntensity = Intensity;
    
    // Update all existing lights
    for (auto& Pair : LigandMap)
    {
        if (Pair.Value)
        {
            for (UPointLightComponent* Light : Pair.Value->AtomLights)
            {
                if (Light && IsValid(Light))
                {
                    Light->SetIntensity(Intensity);
                }
            }
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("Set ligand atom light intensity to %.1f"), Intensity);
}

void APDBViewer::SetLigandAtomLightRadius(float Radius)
{
    LigandAtomLightRadius = Radius;
    
    // Update all existing lights
    for (auto& Pair : LigandMap)
    {
        if (Pair.Value)
        {
            for (UPointLightComponent* Light : Pair.Value->AtomLights)
            {
                if (Light && IsValid(Light))
                {
                    Light->SetAttenuationRadius(Radius);
                }
            }
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("Set ligand atom light radius to %.1f"), Radius);
}

int32 APDBViewer::GetLigandAtomLightCount() const
{
    int32 Count = 0;
    
    for (const auto& Pair : LigandMap)
    {
        if (Pair.Value)
        {
            Count += Pair.Value->AtomLights.Num();
        }
    }
    
    return Count;
}

// ===== MISSING FUNCTION IMPLEMENTATIONS =====

void APDBViewer::LoadPDBFromString(const FString& PDBContent)
{
    CurrentPDBContent = PDBContent;
    ParsePDB(PDBContent);
}

void APDBViewer::LoadPDB(const FString& PDB_ID)
{
    FetchAndDisplayStructure(PDB_ID);
}

void APDBViewer::ToggleLigandVisibility(const FString& LigandKey)
{
    // This is an alias for ToggleMoleculeVisibility
    ToggleMoleculeVisibility(LigandKey);
}

void APDBViewer::LoadSDFFromString(const FString& SDFContent)
{
    CurrentPDBContent = SDFContent;
    ParseSDF(SDFContent);
}


bool APDBViewer::IsWaterKey(const FString& LigandKey) const
{
    // Key format is "ResidueName_ResidueSeq_Chain" e.g., "HOH_101_A"
    return LigandKey.StartsWith(TEXT("HOH_")) ||
           LigandKey.StartsWith(TEXT("H2O_")) ||
           LigandKey.StartsWith(TEXT("WAT_"));
}
