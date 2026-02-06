// PDBViewer_Parsing.cpp - File parsing functions (PDB, mmCIF, SDF)

#include "PDBViewer.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "HydrogenGenerator.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

namespace PDB
{
    constexpr float SCALE = 50.0f;
    constexpr float SPHERE_SIZE = 0.5f;
    constexpr float CYLINDER_SIZE = 0.1f;
    constexpr float BOND_OFFSET = 8.0f;
}

void APDBViewer::FetchAndDisplayStructure(const FString &ID)
{
    CurrentStructureID = ID;

    // Try loading from cache first
    FString CachedContent;
    if (LoadFromCache(ID, TEXT("pdb"), CachedContent))
    {
        ParsePDB(CachedContent);
        return;
    }

    if (LoadFromCache(ID, TEXT("cif"), CachedContent))
    {
        ParseMMCIF(CachedContent);
        return;
    }

    // Not in cache, fetch from web
    UE_LOG(LogTemp, Log, TEXT("Downloading %s from RCSB..."), *ID);

    // OPTIMIZATION #9: Use FStringBuilder instead of Printf
    FString URL = TStringBuilder<256>().Append(TEXT("https://files.rcsb.org/download/")).Append(ID).Append(TEXT(".pdb")).ToString();
    FetchFileAsync(URL, [this, ID](bool bOK, const FString &Content)
                   {
        if (bOK)
        {
            SaveToCache(ID, TEXT("pdb"), Content);
            ParsePDB(Content);
        }
        else
        {
            FetchFileAsync(TStringBuilder<256>().Append(TEXT("https://files.rcsb.org/download/")).Append(ID).Append(TEXT(".cif")).ToString(),
                [this, ID](bool bOK2, const FString& C)
                {
                    if (bOK2)
                    {
                        SaveToCache(ID, TEXT("cif"), C);
                        ParseMMCIF(C);
                    }
                });
        }
    });
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

FString APDBViewer::GetCacheDirectory() const
{
    return FPaths::ProjectContentDir() / TEXT("Cache") / TEXT("CIF");
}

FString APDBViewer::GetCachedFilePath(const FString& PDB_ID, const FString& Extension) const
{
    return GetCacheDirectory() / (PDB_ID + TEXT(".") + Extension);
}

bool APDBViewer::LoadFromCache(const FString& PDB_ID, const FString& Extension, FString& OutContent)
{
    if (!bEnableFileCache)
        return false;

    FString CachePath = GetCachedFilePath(PDB_ID, Extension);

    if (FFileHelper::LoadFileToString(OutContent, *CachePath))
    {
        UE_LOG(LogTemp, Log, TEXT("Loaded %s from cache: %s"), *PDB_ID, *CachePath);
        return true;
    }

    return false;
}

bool APDBViewer::SaveToCache(const FString& PDB_ID, const FString& Extension, const FString& Content)
{
    if (!bEnableFileCache)
        return false;

    FString CacheDir = GetCacheDirectory();
    FString CachePath = GetCachedFilePath(PDB_ID, Extension);

    // Create cache directory if it doesn't exist
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*CacheDir))
    {
        if (!PlatformFile.CreateDirectoryTree(*CacheDir))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create cache directory: %s"), *CacheDir);
            return false;
        }
    }

    if (FFileHelper::SaveStringToFile(Content, *CachePath))
    {
        UE_LOG(LogTemp, Log, TEXT("Saved %s to cache: %s"), *PDB_ID, *CachePath);
        return true;
    }

    UE_LOG(LogTemp, Error, TEXT("Failed to save %s to cache"), *PDB_ID);
    return false;
}

FString APDBViewer::GetComponentCIFURL(const FString& ComponentName) const
{
    if (ComponentName.IsEmpty())
        return TEXT("");

    // Component CIF files are organized by first letter in lowercase
    // URL format: https://files.wwpdb.org/pub/pdb/data/monomers/[first_letter]/[COMPONENT].cif
    FString FirstLetter = ComponentName.Left(1).ToLower();

    return TStringBuilder<256>()
        .Append(TEXT("https://files.wwpdb.org/pub/pdb/data/monomers/"))
        .Append(FirstLetter)
        .Append(TEXT("/"))
        .Append(ComponentName)
        .Append(TEXT(".cif"))
        .ToString();
}

void APDBViewer::FetchComponentCIF(const FString& ComponentName, TFunction<void(bool, const FString&)> Callback)
{
    if (ComponentName.IsEmpty())
    {
        Callback(false, TEXT(""));
        return;
    }

    // Try loading from cache first (cache in Components subdirectory)
    FString CachePath = GetCacheDirectory() / TEXT("Components") / (ComponentName + TEXT(".cif"));
    FString CachedContent;

    if (FFileHelper::LoadFileToString(CachedContent, *CachePath))
    {
        UE_LOG(LogTemp, Log, TEXT("Loaded component %s from cache"), *ComponentName);
        Callback(true, CachedContent);
        return;
    }

    // Not in cache, fetch from web
    FString URL = GetComponentCIFURL(ComponentName);
    UE_LOG(LogTemp, Log, TEXT("Fetching component CIF: %s"), *URL);

    FetchFileAsync(URL, [this, ComponentName, CachePath, Callback](bool bOK, const FString& Content)
    {
        if (bOK && !Content.IsEmpty())
        {
            // Save to cache
            FString CacheDir = FPaths::GetPath(CachePath);
            IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

            if (!PlatformFile.DirectoryExists(*CacheDir))
            {
                PlatformFile.CreateDirectoryTree(*CacheDir);
            }

            if (FFileHelper::SaveStringToFile(Content, *CachePath))
            {
                UE_LOG(LogTemp, Log, TEXT("Saved component %s to cache"), *ComponentName);
            }

            Callback(true, Content);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to fetch component CIF for %s"), *ComponentName);
            Callback(false, TEXT(""));
        }
    });
}

void APDBViewer::ParseComponentBonds(const FString& Content, const FString& ComponentName, TMap<FString, TArray<TPair<TPair<FString, FString>, int32>>>& OutComponentBonds)
{
    TArray<FString> Lines;
    Content.ParseIntoArrayLines(Lines);

    bool bInChemCompBond = false;
    TArray<FString> BondHeaders;
    int32 Atom1Idx = -1, Atom2Idx = -1, OrderIdx = -1;

    for (const FString& Line : Lines)
    {
        if (Line.StartsWith(TEXT("loop_")))
        {
            bInChemCompBond = false;
            BondHeaders.Empty();
            Atom1Idx = Atom2Idx = OrderIdx = -1;
            continue;
        }

        if (Line.StartsWith(TEXT("_chem_comp_bond.")))
        {
            bInChemCompBond = true;
            int32 Idx = BondHeaders.Add(Line);

            if (Line.Contains(TEXT("atom_id_1")))
                Atom1Idx = Idx;
            else if (Line.Contains(TEXT("atom_id_2")))
                Atom2Idx = Idx;
            else if (Line.Contains(TEXT("value_order")))
                OrderIdx = Idx;

            continue;
        }

        if (bInChemCompBond && !Line.StartsWith(TEXT("_")) && !Line.StartsWith(TEXT("#")) && !Line.IsEmpty())
        {
            if (Atom1Idx < 0 || Atom2Idx < 0)
                continue;

            TArray<FString> Tokens;
            Line.ParseIntoArrayWS(Tokens);

            if (Tokens.Num() <= FMath::Max(Atom1Idx, Atom2Idx))
                continue;

            FString Atom1 = Tokens[Atom1Idx].TrimStartAndEnd();
            FString Atom2 = Tokens[Atom2Idx].TrimStartAndEnd();
            int32 Order = (OrderIdx >= 0 && Tokens.IsValidIndex(OrderIdx))
                              ? ParseBondOrder(Tokens[OrderIdx])
                              : 1;

            // Store bond for this component type
            OutComponentBonds.FindOrAdd(ComponentName).Add(TPair<TPair<FString, FString>, int32>(
                TPair<FString, FString>(Atom1, Atom2), Order));
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Parsed %d bonds for component %s"),
           OutComponentBonds.Contains(ComponentName) ? OutComponentBonds[ComponentName].Num() : 0,
           *ComponentName);
}

void APDBViewer::ParsePDB(const FString &Content)
{
    CurrentPDBContent = Content;

    // Preserve SDF-loaded ligands across PDB reload (they would be destroyed by ClearLigandMap)
    TMap<FString, FLigandInfo*> SavedSDFLigands;
    TArray<UStaticMeshComponent*> SavedAtomMeshes;
    TArray<UStaticMeshComponent*> SavedBondMeshes;
    {
        auto It = LigandMap.CreateIterator();
        while (It)
        {
            if (It->Value && It->Value->bFromSDF)
            {
                // Track meshes to preserve
                SavedAtomMeshes.Append(It->Value->AtomMeshes);
                SavedBondMeshes.Append(It->Value->BondMeshes);
                SavedSDFLigands.Add(It->Key, It->Value);
                It.RemoveCurrent();
            }
            else
            {
                ++It;
            }
        }
    }

    ClearResidueMap();
    ClearLigandMap();

    // Clear global mesh arrays but preserve SDF ligand meshes
    AllAtomMeshes = MoveTemp(SavedAtomMeshes);
    AllBondMeshes = MoveTemp(SavedBondMeshes);

    ChainIDs.Empty();
    ClearTrimCache();  // OPTIMIZATION #15: Clear trim cache for new parse

    TArray<FString> Lines;
    Content.ParseIntoArrayLines(Lines);

    // OPTIMIZATION #1: Reserve capacity based on estimated residue count
    TMap<FString, TMap<FString, FVector>> ResAtoms;
    ResAtoms.Reserve(Lines.Num() / 10);  // Estimate ~10 lines per residue
    TMap<FString, FResidueMetadata> ResMeta;
    ResMeta.Reserve(Lines.Num() / 10);

    for (const auto &L : Lines)
    {
        FString RecordType = L.Mid(0, 6);
        if (L.Len() < 80 || !(RecordType.StartsWith("ATOM") || RecordType.StartsWith("HETATM")))
            continue;

        FString Chain = L.Mid(21, 1).TrimStartAndEnd();
        if (Chain.IsEmpty())
            Chain = TEXT("_");

        ChainIDs.Add(Chain);

        // OPTIMIZATION #9: Use FStringBuilder instead of Printf
        FString Key = TStringBuilder<128>()
            .Append(L.Mid(17, 3).TrimStartAndEnd())
            .Append(TEXT("_"))
            .Append(L.Mid(22, 4).TrimStartAndEnd())
            .Append(TEXT("_"))
            .Append(Chain)
            .ToString();

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

    // Restore SDF ligands that were saved before the clear
    for (auto &P : SavedSDFLigands)
    {
        LigandMap.Add(P.Key, P.Value);
        // Re-add the SDF ligand's chain so it appears in the tree
        if (P.Value)
        {
            TArray<FString> Parts;
            P.Key.ParseIntoArray(Parts, TEXT("_"));
            if (Parts.Num() >= 3)
            {
                ChainIDs.Add(Parts[Parts.Num() - 1]);
            }
        }
    }
    if (SavedSDFLigands.Num() > 0)
    {
        bLigandChainCacheDirty = true;
    }

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

    // Preserve SDF-loaded ligands across mmCIF reload (they would be destroyed by ClearLigandMap)
    TMap<FString, FLigandInfo*> SavedSDFLigands;
    TArray<UStaticMeshComponent*> SavedAtomMeshes;
    TArray<UStaticMeshComponent*> SavedBondMeshes;
    {
        auto It = LigandMap.CreateIterator();
        while (It)
        {
            if (It->Value && It->Value->bFromSDF)
            {
                // Track meshes to preserve
                SavedAtomMeshes.Append(It->Value->AtomMeshes);
                SavedBondMeshes.Append(It->Value->BondMeshes);
                SavedSDFLigands.Add(It->Key, It->Value);
                It.RemoveCurrent();
            }
            else
            {
                ++It;
            }
        }
    }

    ClearResidueMap();
    ClearLigandMap();

    // Clear global mesh arrays but preserve SDF ligand meshes
    AllAtomMeshes = MoveTemp(SavedAtomMeshes);
    AllBondMeshes = MoveTemp(SavedBondMeshes);

    ChainIDs.Empty();
    ClearTrimCache();  // OPTIMIZATION #15: Clear trim cache for new parse

    TArray<FString> Lines;
    Content.ParseIntoArrayLines(Lines);

    TArray<FString> Hdrs;
    // OPTIMIZATION #1: Reserve capacity for atom table
    TArray<TArray<FString>> AtomTab;
    AtomTab.Reserve(Lines.Num() / 2);  // Estimate ~2 lines per atom entry
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

    // OPTIMIZATION #1: Reserve capacity based on atom table size
    TMap<FString, TMap<FString, FVector>> ResAtoms;
    ResAtoms.Reserve(AtomTab.Num() / 8);  // Estimate ~8 atoms per residue
    TMap<FString, FResidueMetadata> ResMeta;
    ResMeta.Reserve(AtomTab.Num() / 8);

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
        // OPTIMIZATION #9: Use FStringBuilder instead of Printf
        FString Key = TStringBuilder<128>()
            .Append(R[RI])
            .Append(TEXT("_"))
            .Append(Seq)
            .Append(TEXT("_"))
            .Append(Chain)
            .ToString();

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

    // Restore SDF ligands that were saved before the clear
    for (auto &P : SavedSDFLigands)
    {
        LigandMap.Add(P.Key, P.Value);
        // Re-add the SDF ligand's chain so it appears in the tree
        if (P.Value)
        {
            TArray<FString> Parts;
            P.Key.ParseIntoArray(Parts, TEXT("_"));
            if (Parts.Num() >= 3)
            {
                ChainIDs.Add(Parts[Parts.Num() - 1]);
            }
        }
    }
    if (SavedSDFLigands.Num() > 0)
    {
        bLigandChainCacheDirty = true;
    }

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
    // Try loading from cache first
    FString CachedContent;
    if (LoadFromCache(StructureID, TEXT("cif"), CachedContent))
    {
        UE_LOG(LogTemp, Log, TEXT("Loaded bond information from cache"));
        ParseStructureBondsFromCIF(CachedContent);
        return;
    }

    // Not in cache, fetch from web
    // OPTIMIZATION #9: Use FStringBuilder instead of Printf
    FString URL = TStringBuilder<256>()
        .Append(TEXT("https://files.rcsb.org/download/"))
        .Append(StructureID)
        .Append(TEXT(".cif"))
        .ToString();

    UE_LOG(LogTemp, Log, TEXT("Fetching bond information from mmCIF: %s"), *URL);

    FetchFileAsync(URL, [this, StructureID](bool bOK, const FString &Content)
                   {
        if (bOK)
        {
            UE_LOG(LogTemp, Log, TEXT("Successfully fetched mmCIF bond data"));
            SaveToCache(StructureID, TEXT("cif"), Content);
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
    // OPTIMIZATION #1: Reserve capacity for common residue types
    TMap<FString, TArray<TPair<TPair<FString, FString>, int32>>> ComponentBonds;
    ComponentBonds.Reserve(50);  // Typical proteins have ~20 amino acids + common ligands

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

    // Identify components that need bond data
    TSet<FString> NeededComponents;

    // Check residues
    for (const auto& Pair : ResidueMap)
    {
        if (Pair.Value && !ComponentBonds.Contains(Pair.Value->ResidueName))
        {
            NeededComponents.Add(Pair.Value->ResidueName);
        }
    }

    // Check ligands
    for (const auto& Pair : LigandMap)
    {
        if (Pair.Value)
        {
            FString ResName = Pair.Value->LigandName;
            int32 DashPos;
            if (ResName.FindChar('-', DashPos))
                ResName = ResName.Left(DashPos);

            if (!ComponentBonds.Contains(ResName))
            {
                NeededComponents.Add(ResName);
            }
        }
    }

    // If we have missing components, fetch them
    if (NeededComponents.Num() > 0 && bEnableFileCache)
    {
        UE_LOG(LogTemp, Log, TEXT("Fetching bond data for %d missing components..."), NeededComponents.Num());

        // Create shared pointer to component bonds map that will be populated asynchronously
        TSharedPtr<TMap<FString, TArray<TPair<TPair<FString, FString>, int32>>>> SharedComponentBonds =
            MakeShared<TMap<FString, TArray<TPair<TPair<FString, FString>, int32>>>>(ComponentBonds);

        TSharedPtr<int32> PendingCount = MakeShared<int32>(NeededComponents.Num());

        for (const FString& ComponentName : NeededComponents)
        {
            FetchComponentCIF(ComponentName, [this, SharedComponentBonds, PendingCount, ComponentName](bool bSuccess, const FString& Content)
            {
                if (bSuccess && !Content.IsEmpty())
                {
                    // Parse bonds from this component CIF
                    ParseComponentBonds(Content, ComponentName, *SharedComponentBonds);
                }

                // Decrement pending count
                (*PendingCount)--;

                // If all components have been fetched, apply bonds
                if (*PendingCount == 0)
                {
                    UE_LOG(LogTemp, Log, TEXT("All component CIF files processed. Applying bonds..."));

                    // Apply bonds to all residues and ligands
                    ApplyBondsToResidues(*SharedComponentBonds);

                    // OPTIMIZATION: Rebuild ligand chain cache for faster lookups
                    RebuildLigandChainCache();

                    // Now broadcast events after bonds are applied
                    OnResiduesLoaded.Broadcast();
                    OnLigandsLoaded.Broadcast();

                    // Optionally calculate interactions automatically if enabled
                    if (bAutoCalculateInteractions)
                    {
                        UE_LOG(LogTemp, Log, TEXT("Auto-calculating interactions..."));
                        CalculateAllInteractions(true, true);
                    }
                }
            });
        }
    }
    else
    {
        // No missing components, apply bonds immediately
        ApplyBondsToResidues(ComponentBonds);

        // OPTIMIZATION: Rebuild ligand chain cache for faster lookups
        RebuildLigandChainCache();

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
}

void APDBViewer::ApplyBondsToResidues(const TMap<FString, TArray<TPair<TPair<FString, FString>, int32>>> &ComponentBonds)
{
    // Apply to regular residues (ATOM records)
    // OPTIMIZATION #12: Use const reference (not modifying map entries)
    for (const auto &Pair : ResidueMap)
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

        // OPTIMIZATION #2: Preallocate bond arrays
        ResInfo->BondPairs.Reserve(ResInfo->BondPairs.Num() + BondData->Num());
        ResInfo->BondOrders.Reserve(ResInfo->BondOrders.Num() + BondData->Num());

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
    }

    // Apply to ligands (HETATM records)
    // OPTIMIZATION #12: Use const reference (not modifying map entries)
    for (const auto &Pair : LigandMap)
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

        // OPTIMIZATION #2: Preallocate bond arrays
        LigInfo->BondPairs.Reserve(LigInfo->BondPairs.Num() + BondData->Num());
        LigInfo->BondOrders.Reserve(LigInfo->BondOrders.Num() + BondData->Num());

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
    }

    // ===== CREATE PEPTIDE BONDS BETWEEN CONSECUTIVE RESIDUES =====
    // Group residues by chain
    TMap<FString, TArray<FResidueInfo*>> ResiduesByChain;
    for (auto& Pair : ResidueMap)
    {
        FResidueInfo* ResInfo = Pair.Value;
        if (ResInfo)
        {
            ResiduesByChain.FindOrAdd(ResInfo->Chain).Add(ResInfo);
        }
    }

    // For each chain, sort residues by sequence number and create peptide bonds
    for (auto& ChainPair : ResiduesByChain)
    {
        TArray<FResidueInfo*>& Residues = ChainPair.Value;

        // Sort residues by sequence number - OPTIMIZED: Use cached value
        Residues.Sort([](const FResidueInfo& A, const FResidueInfo& B)
        {
            return A.CachedSequenceNumber < B.CachedSequenceNumber;
        });

        // Create peptide bonds between consecutive residues
        for (int32 i = 0; i < Residues.Num() - 1; ++i)
        {
            FResidueInfo* CurrentRes = Residues[i];
            FResidueInfo* NextRes = Residues[i + 1];

            if (!CurrentRes || !NextRes)
                continue;

            // Check if residues are actually consecutive - OPTIMIZED: Use cached values
            int32 CurrentSeq = CurrentRes->CachedSequenceNumber;
            int32 NextSeq = NextRes->CachedSequenceNumber;

            if (NextSeq != CurrentSeq + 1)
                continue; // Skip if not consecutive

            // Find C atom (carbonyl carbon) in current residue
            int32 CIndex = FindAtomIndexByName(CurrentRes->AtomNames, TEXT("C"));

            // Find N atom in next residue
            int32 NIndex = FindAtomIndexByName(NextRes->AtomNames, TEXT("N"));

            // Create peptide bond if both atoms found
            if (CIndex != -1 && NIndex != -1)
            {
                FVector CPos = CurrentRes->AtomPositions[CIndex];
                FVector NPos = NextRes->AtomPositions[NIndex];

                // Scale positions for drawing
                FVector ScaledCPos = CPos * PDB::SCALE;
                FVector ScaledNPos = NPos * PDB::SCALE;

                // Draw peptide bond and store mesh in current residue
                FString CElement = CurrentRes->AtomElements[CIndex];
                FString NElement = NextRes->AtomElements[NIndex];

                DrawBond(ScaledCPos, ScaledNPos, 1, CElement, NElement,
                         GetRootComponent(), CurrentRes->BondMeshes);

                UE_LOG(LogTemp, Log, TEXT("Created peptide bond: %s %s C to %s %s N (distance: %.2f A)"),
                       *CurrentRes->ResidueName, *CurrentRes->ResidueSeq,
                       *NextRes->ResidueName, *NextRes->ResidueSeq,
                       FVector::Dist(CPos, NPos));
            }
        }
    }

    // ===== GENERATE HYDROGENS AFTER ALL BONDS ARE CREATED =====
    if (bAutoGenerateHydrogens)
    {
        UE_LOG(LogTemp, Log, TEXT("Generating hydrogens for all residues (after peptide bonds)"));

        // Generate hydrogens for residues
        for (auto& Pair : ResidueMap)
        {
            FResidueInfo* ResInfo = Pair.Value;
            if (ResInfo && ResInfo->BondPairs.Num() > 0)
            {
                GenerateHydrogensForResidue(ResInfo);
            }
        }

        // Generate hydrogens for ligands
        for (auto& Pair : LigandMap)
        {
            FLigandInfo* LigInfo = Pair.Value;
            if (!LigInfo || LigInfo->BondPairs.Num() == 0)
                continue;

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

            for (const auto& HPair : Hydrogens)
            {
                int32 ParentIdx = HPair.Value;

                // Store UNSCALED hydrogen position
                int32 HIdx = LigInfo->AtomPositions.Add(HPair.Key);
                LigInfo->AtomElements.Add(TEXT("H"));
                // OPTIMIZATION #9: Use Appendf for integer formatting
                LigInfo->AtomNames.Add(TStringBuilder<16>().Appendf(TEXT("H%d"), HIdx).ToString());

                // Apply scaling only when drawing
                FVector ScaledHPos = HPair.Key * PDB::SCALE;
                DrawSphere(ScaledHPos, FLinearColor::White, GetRootComponent(), LigInfo->AtomMeshes);
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

void APDBViewer::ParseSDF(const FString &Content, const FString& TargetChain)
{
    // Don't overwrite CurrentPDBContent — that stores the PDB/mmCIF source for save/reload
    // Don't clear ligand map - we want to ADD ligands, not replace them
    // ClearLigandMap();

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
            // OPTIMIZATION #9: Use Appendf for integer formatting
            MoleculeName = TStringBuilder<32>().Appendf(TEXT("MOL%d"), MoleculeIndex + 1).ToString();

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
        TArray<FString> AtomNames;
        TMap<FString, int32> ElementCounters; // Per-element counter for generating atom names
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

            // Generate synthetic atom name (e.g. "C1", "C2", "N1") to match PDB convention
            int32& Count = ElementCounters.FindOrAdd(Element, 0);
            Count++;
            AtomNames.Add(TStringBuilder<16>().Append(Element).Appendf(TEXT("%d"), Count).ToString());
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

        // Create proper ligand key in format "NAME_SEQ_CHAIN"
        FString AssignedChain;

        // Use TargetChain if provided, otherwise auto-select
        if (!TargetChain.IsEmpty())
        {
            AssignedChain = TargetChain;
            // Ensure the target chain is registered
            ChainIDs.Add(AssignedChain);
            UE_LOG(LogTemp, Warning, TEXT("ParseSDF: Using specified target chain '%s'"), *AssignedChain);
        }
        else if (ChainIDs.Num() > 0)
        {
            // Auto-assign to first available chain
            TArray<FString> SortedChains = ChainIDs.Array();
            SortedChains.Sort();
            AssignedChain = SortedChains[0];  // Use first chain alphabetically (usually "A")
            UE_LOG(LogTemp, Warning, TEXT("ParseSDF: Auto-assigning to chain '%s' (from %d available chains)"),
                   *AssignedChain, ChainIDs.Num());
        }
        else
        {
            // No chains exist, create a default "SDF" chain
            AssignedChain = TEXT("SDF");
            ChainIDs.Add(AssignedChain);
            UE_LOG(LogTemp, Warning, TEXT("ParseSDF: No chains exist, created default chain '%s'"), *AssignedChain);
        }

        // Create key: "MoleculeName_Index_Chain"
        FString Key = FString::Printf(TEXT("%s_%d_%s"), *MoleculeName, MoleculeIndex + 1, *AssignedChain);

        UE_LOG(LogTemp, Warning, TEXT("ParseSDF: Creating ligand with key: '%s' (Chain: '%s')"), *Key, *AssignedChain);

        auto *Info = new FLigandInfo();
        Info->LigandName = MoleculeName;
        Info->bIsVisible = false; // Make SDF ligands HIDDEN by default - user toggles visibility as needed
        Info->bIsWater = false;  // FIX: SDF ligands are NOT water molecules
        Info->bFromSDF = true;   // Mark as SDF-sourced so it survives PDB reloads
        Info->AtomPositions = AtomPositions;
        Info->AtomElements = AtomElements;
        Info->AtomNames = AtomNames;

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
            DrawSphere(ScaledPos, Color, GetRootComponent(), Info->AtomMeshes);
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

        SetMeshArrayVisibility(Info->AtomMeshes, true); // Show SDF ligands
        SetMeshArrayVisibility(Info->BondMeshes, true);

        // Initialize MD simulation state
        Info->Velocities.SetNum(Info->AtomPositions.Num());
        Info->Forces.SetNum(Info->AtomPositions.Num());
        Info->Masses.SetNum(Info->AtomPositions.Num());
        Info->Charges.SetNum(Info->AtomPositions.Num());
        Info->AtomTypes.SetNum(Info->AtomPositions.Num());

        // Initialize masses from elements
        for (int32 i = 0; i < Info->AtomElements.Num(); ++i)
        {
            Info->Masses[i] = FMDForceCalculator::GetAtomMass(Info->AtomElements[i]);
            Info->Charges[i] = 0.0f; // TODO: Proper charge assignment
            Info->AtomTypes[i] = 0; // TODO: Atom type assignment
        }

        // Initialize velocities and forces to zero
        for (int32 i = 0; i < Info->Velocities.Num(); ++i)
        {
            Info->Velocities[i] = FVector::ZeroVector;
            Info->Forces[i] = FVector::ZeroVector;
        }

        // Add to ligand map with proper chain-based key
        LigandMap.Add(Key, Info);

        UE_LOG(LogTemp, Warning, TEXT("Added ligand to map: %s"), *Key);

        // Mark ligand chain cache as dirty so it gets rebuilt
        bLigandChainCacheDirty = true;

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

    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT("ParseSDF COMPLETE: Added %d molecules"), MoleculeIndex);
    UE_LOG(LogTemp, Warning, TEXT("LigandMap now contains %d total entries"), LigandMap.Num());
    UE_LOG(LogTemp, Warning, TEXT("========================================"));

    // OPTIMIZATION: Rebuild ligand chain cache for faster lookups
    RebuildLigandChainCache();

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
            // OPTIMIZATION #9: Use FStringBuilder instead of Printf
            LigInfo->LigandName = TStringBuilder<128>()
                .Append(M->ResidueName)
                .Append(TEXT("-"))
                .Append(M->ResidueSeq)
                .Append(TEXT("-"))
                .Append(M->Chain)
                .ToString();

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
                DrawSphere(ScaledPos, GetElementColor(Element), GetRootComponent(), LigInfo->AtomMeshes);
            }

            for (auto *Mesh : LigInfo->AtomMeshes)
            {
                if (IsValid(Mesh))
                    Mesh->SetVisibility(LigInfo->bIsVisible);
            }

            // Initialize MD simulation state
            LigInfo->Velocities.SetNum(LigInfo->AtomPositions.Num());
            LigInfo->Forces.SetNum(LigInfo->AtomPositions.Num());
            LigInfo->Masses.SetNum(LigInfo->AtomPositions.Num());
            LigInfo->Charges.SetNum(LigInfo->AtomPositions.Num());
            LigInfo->AtomTypes.SetNum(LigInfo->AtomPositions.Num());

            // Initialize masses from elements
            for (int32 i = 0; i < LigInfo->AtomElements.Num(); ++i)
            {
                LigInfo->Masses[i] = FMDForceCalculator::GetAtomMass(LigInfo->AtomElements[i]);
                LigInfo->Charges[i] = 0.0f; // TODO: Proper charge assignment
                LigInfo->AtomTypes[i] = 0; // TODO: Atom type assignment
            }

            // Initialize velocities and forces to zero
            for (int32 i = 0; i < LigInfo->Velocities.Num(); ++i)
            {
                LigInfo->Velocities[i] = FVector::ZeroVector;
                LigInfo->Forces[i] = FVector::ZeroVector;
            }

            LigandMap.Add(P.Key, LigInfo);
            // Don't fetch individual ligand CIFs anymore - we'll get bonds from the main CIF
        }
        else
        {
            auto *Info = new FResidueInfo();
            Info->ResidueName = M->ResidueName;
            Info->ResidueSeq = M->ResidueSeq;
            Info->CachedSequenceNumber = FCString::Atoi(*M->ResidueSeq);  // OPTIMIZED: Cache for sorting
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

            // Initialize MD simulation state
            Info->Velocities.SetNum(Info->AtomPositions.Num());
            Info->Forces.SetNum(Info->AtomPositions.Num());
            Info->Masses.SetNum(Info->AtomPositions.Num());
            Info->Charges.SetNum(Info->AtomPositions.Num());
            Info->AtomTypes.SetNum(Info->AtomPositions.Num());

            // Initialize masses from elements
            for (int32 i = 0; i < Info->AtomElements.Num(); ++i)
            {
                Info->Masses[i] = FMDForceCalculator::GetAtomMass(Info->AtomElements[i]);
                Info->Charges[i] = 0.0f; // TODO: Proper charge assignment
                Info->AtomTypes[i] = 0; // TODO: Atom type assignment
            }

            // Initialize velocities and forces to zero
            for (int32 i = 0; i < Info->Velocities.Num(); ++i)
            {
                Info->Velocities[i] = FVector::ZeroVector;
                Info->Forces[i] = FVector::ZeroVector;
            }

            // Don't draw bonds yet - wait for CIF bond data
            ResidueMap.Add(P.Key, Info);
        }
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

    // OPTIMIZATION #10: Pre-extract element names to avoid O(n^2) repeated extractions
    TArray<FString> Elements;
    Elements.Reserve(Atoms.Num());
    for (const auto& Atom : Atoms)
    {
        FString Element = Atom.Key.TrimStartAndEnd().Left(1);
        if (Atom.Key.Len() > 1 && FChar::IsLower(Atom.Key[1]))
            Element = Atom.Key.Left(2);
        Elements.Add(Element);
    }

    for (int32 i = 0; i < Atoms.Num(); ++i)
    {
        for (int32 j = i + 1; j < Atoms.Num(); ++j)
        {
            float Distance = FVector::Dist(Atoms[i].Value, Atoms[j].Value);

            if (Distance < BondThreshold)
            {
                const FString& Element1 = Elements[i];
                const FString& Element2 = Elements[j];

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

    // OPTIMIZATION: Build position->index map to avoid O(n^2) search
    TMap<FVector, int32> PositionToIndex;
    for (int32 i = 0; i < Info->AtomPositions.Num(); ++i)
    {
        PositionToIndex.Add(Info->AtomPositions[i], i);
    }

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
                // OPTIMIZED: Use position->index map for O(1) lookup instead of O(n) search
                const int32* Idx1Ptr = PositionToIndex.Find(*P1);
                const int32* Idx2Ptr = PositionToIndex.Find(*P2);

                int32 Idx1 = Idx1Ptr ? *Idx1Ptr : -1;
                int32 Idx2 = Idx2Ptr ? *Idx2Ptr : -1;

                // Fallback to tolerance-based search if exact match not found
                if (Idx1 < 0 || Idx2 < 0)
                {
                    for (const auto& PosIdxPair : PositionToIndex)
                    {
                        if (Idx1 < 0 && PosIdxPair.Key.Equals(*P1, 0.1f))
                            Idx1 = PosIdxPair.Value;
                        if (Idx2 < 0 && PosIdxPair.Key.Equals(*P2, 0.1f))
                            Idx2 = PosIdxPair.Value;
                        if (Idx1 >= 0 && Idx2 >= 0)
                            break;
                    }
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

    // OPTIMIZATION: Rebuild ligand chain cache for faster lookups
    RebuildLigandChainCache();

    // IMPORTANT: Broadcast after bonds are loaded so hydrogens can be generated
    OnLigandsLoaded.Broadcast();
}

FString APDBViewer::NormalizeAtomID(const FString &In) const
{
    // OPTIMIZATION #12: Cache normalized atom IDs to avoid repeated normalization
    static TMap<FString, FString> NormalizationCache;

    // Check cache first
    const FString* Cached = NormalizationCache.Find(In);
    if (Cached)
    {
        return *Cached;
    }

    // Cache miss - normalize and store result
    FString Out;
    for (const TCHAR C : In)
        if (FChar::IsAlnum(C))
            Out.AppendChar(FChar::ToUpper(C));

    NormalizationCache.Add(In, Out);
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

void APDBViewer::FetchLigandBondsForHETATM(const FString &Key, const FString &Name, const TMap<FString, FVector> &Pos, FLigandInfo *LigInfo)
{
    // This function is deprecated - bonds now come from the main CIF file
    UE_LOG(LogTemp, Warning, TEXT("FetchLigandBondsForHETATM is deprecated"));
}
