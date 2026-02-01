// PDBViewer.cpp – UE 5.6 compatible version with TreeView, SDF support, and Ligand Atom Lighting
// Modified to include point light components on each ligand atom

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
#if WITH_EDITOR
#include "Interfaces/IMainFrameModule.h"
#endif
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

// ===== OPTIMIZATION: String Parsing Helpers =====
FString APDBViewer::GetChainFromLigandKey(const FString& Key)
{
    FString Name, Seq, Chain;
    ParseLigandKey(Key, Name, Seq, Chain);
    return Chain;
}

void APDBViewer::ParseLigandKey(const FString& Key, FString& OutName, FString& OutSeq, FString& OutChain)
{
    int32 FirstUnderscore = INDEX_NONE;
    int32 SecondUnderscore = INDEX_NONE;

    OutName = OutSeq = OutChain = TEXT("");

    if (Key.FindChar('_', FirstUnderscore))
    {
        OutName = Key.Left(FirstUnderscore);
        SecondUnderscore = Key.Find(TEXT("_"), ESearchCase::IgnoreCase, ESearchDir::FromStart, FirstUnderscore + 1);

        if (SecondUnderscore != INDEX_NONE)
        {
            OutSeq = Key.Mid(FirstUnderscore + 1, SecondUnderscore - FirstUnderscore - 1);
            OutChain = Key.RightChop(SecondUnderscore + 1);
        }
        else
        {
            OutSeq = Key.RightChop(FirstUnderscore + 1);
        }
    }
}

void APDBViewer::RebuildLigandChainCache()
{
    // OPTIMIZATION #9: Lazy evaluation - only rebuild if dirty
    if (!bLigandChainCacheDirty)
        return;

    LigandsByChain.Empty();

    for (const auto& Pair : LigandMap)
    {
        if (Pair.Value)
        {
            FString ChainID = GetChainFromLigandKey(Pair.Key);
            if (!ChainID.IsEmpty())
            {
                LigandsByChain.FindOrAdd(ChainID).Add(Pair.Value);
            }
        }
    }

    bLigandChainCacheDirty = false;

    UE_LOG(LogTemp, Log, TEXT("Rebuilt ligand chain cache: %d chains, %d total ligands"),
           LigandsByChain.Num(), LigandMap.Num());
}

// OPTIMIZATION #15: Shared static cache for trimmed strings
static TMap<FString, FString> GTrimCache;

const FString& APDBViewer::GetTrimmedString(const FString& Input)
{
    const FString* Cached = GTrimCache.Find(Input);
    if (Cached)
    {
        return *Cached;
    }

    FString Trimmed = Input.TrimStartAndEnd();
    return GTrimCache.Add(Input, Trimmed);
}

void APDBViewer::ClearTrimCache()
{
    GTrimCache.Empty();
}

// OPTIMIZATION #19: Material instance pooling - reuse materials for same colors
UMaterialInstanceDynamic* APDBViewer::GetOrCreateMaterial(const FLinearColor& Color)
{
    // Check if we already have a material for this color
    UMaterialInstanceDynamic** ExistingMat = MaterialPool.Find(Color);
    if (ExistingMat && *ExistingMat && IsValid(*ExistingMat))
    {
        return *ExistingMat;
    }

    // Create new material instance
    UMaterialInstanceDynamic* NewMat = UMaterialInstanceDynamic::Create(SphereMaterialAsset, this);
    if (NewMat)
    {
        NewMat->SetVectorParameterValue(TEXT("Color"), Color);
        MaterialPool.Add(Color, NewMat);
    }

    return NewMat;
}

// ===== CODE REDUCTION HELPERS =====

void APDBViewer::DestroyMeshComponents(TArray<UStaticMeshComponent*>& Meshes)
{
    for (auto* M : Meshes)
    {
        if (IsValidMesh(M))
            M->DestroyComponent();
    }
    Meshes.Empty();
}

int32 APDBViewer::FindAtomIndexByName(const TArray<FString>& Names, const FString& Name)
{
    return Names.IndexOfByKey(Name);
}

// ===== LAMBDA-BASED CODE REDUCTION HELPERS =====

void APDBViewer::SetMeshArrayVisibility(TArray<UStaticMeshComponent*>& Meshes, bool bVisible, bool bPropagateToChildren)
{
    for (auto* M : Meshes)
    {
        if (M)
            M->SetVisibility(bVisible, bPropagateToChildren);
    }
}

template<typename TInfo>
void APDBViewer::UpdateStructureHydrogenVisibility(TInfo* Info, bool bVisible)
{
    if (!Info)
        return;

    // Lambda to check if atom is hydrogen
    auto IsHydrogen = [&](int32 i) {
        return Info->AtomElements.IsValidIndex(i) &&
               Info->AtomElements[i] == TEXT("H") &&
               Info->AtomMeshes.IsValidIndex(i);
    };

    // Lambda to check if bond involves hydrogen
    auto HasHydrogenBond = [&](int32 i) {
        if (!Info->BondPairs.IsValidIndex(i))
            return false;
        int32 A1 = Info->BondPairs[i].Key;
        int32 A2 = Info->BondPairs[i].Value;
        return (Info->AtomElements.IsValidIndex(A1) && Info->AtomElements[A1] == TEXT("H")) ||
               (Info->AtomElements.IsValidIndex(A2) && Info->AtomElements[A2] == TEXT("H"));
    };

    // Update hydrogen atom visibility
    for (int32 i = 0; i < Info->AtomElements.Num(); ++i)
    {
        if (IsHydrogen(i))
            Info->AtomMeshes[i]->SetVisibility(bVisible && Info->bIsVisible);
    }

    // Update bonds involving hydrogen
    for (int32 i = 0; i < Info->BondPairs.Num(); ++i)
    {
        if (HasHydrogenBond(i) && Info->BondMeshes.IsValidIndex(i))
            Info->BondMeshes[i]->SetVisibility(bVisible && Info->bIsVisible);
    }
}

void APDBViewer::ForEachValidLigandLight(TFunction<void(UPointLightComponent*)> Action)
{
    for (auto& Pair : LigandMap)
    {
        if (Pair.Value && Pair.Value->bIsVisible)
        {
            for (UPointLightComponent* Light : Pair.Value->AtomLights)
            {
                if (Light && IsValid(Light))
                    Action(Light);
            }
        }
    }
}

TArray<UPDBTreeNode*> APDBViewer::GetFilteredNodesForChain(const FString& ChainID, TFunction<bool(const FString&)> KeyFilter, const FString& CategoryPrefix)
{
    TArray<UPDBTreeNode*> Nodes;
    TArray<FString> Keys;
    LigandMap.GetKeys(Keys);

    // Sort by sequence number
    Keys.Sort(GetLigandKeyComparator());

    for (const FString& Key : Keys)
    {
        if (!KeyFilter(Key))
            continue;

        const auto* Info = SafeDereference(LigandMap.Find(Key));
        if (!Info)
            continue;

        FString KeyChain = GetChainFromLigandKey(Key);
        if (KeyChain != ChainID)
            continue;

        UPDBTreeNode* Node = NewObject<UPDBTreeNode>(this);
        Node->Initialize(CategoryPrefix + Info->LigandName, Key, false, ChainID);
        Node->bIsVisible = Info->bIsVisible;
        Nodes.Add(Node);
    }

    return Nodes;
}

TFunction<bool(const FString&, const FString&)> APDBViewer::GetLigandKeyComparator(bool bSortByName)
{
    return [bSortByName](const FString& A, const FString& B) {
        int32 U1, U2;
        if (!A.FindChar('_', U1) || !B.FindChar('_', U2))
            return A < B;

        // If sorting by name, compare residue names first
        if (bSortByName)
        {
            FString NameA = A.Left(U1);
            FString NameB = B.Left(U2);
            if (NameA != NameB)
                return NameA < NameB;
        }

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
    };
}

void APDBViewer::UpdateAllHydrogenVisibility(bool bVisible)
{
    for (const auto& Pair : LigandMap)
        if (Pair.Value)
            UpdateLigandHydrogenVisibility(Pair.Value, bVisible);

    for (const auto& Pair : ResidueMap)
        if (Pair.Value)
            UpdateResidueHydrogenVisibility(Pair.Value, bVisible);
}

template<typename TMap, typename TPreDelete>
void APDBViewer::ClearInfoMap(TMap& InfoMap, TPreDelete PreDeleteFunc)
{
    for (auto& P : InfoMap)
    {
        if (!P.Value)
            continue;

        DestroyMeshComponents(P.Value->AtomMeshes);
        DestroyMeshComponents(P.Value->BondMeshes);

        // Use if constexpr to handle nullable lambdas
        if constexpr (!std::is_same_v<TPreDelete, std::nullptr_t>)
            PreDeleteFunc(P.Value);

        delete P.Value;
    }
    InfoMap.Empty();
}

TArray<int32> APDBViewer::CountBondOrdersByType(const TArray<int32>& BondOrders)
{
    TArray<int32> Counts = {0, 0, 0, 0};  // [Single, Double, Triple, Other]

    for (int32 Order : BondOrders)
    {
        if (Order == 1)
            Counts[0]++;
        else if (Order == 2)
            Counts[1]++;
        else if (Order == 3)
            Counts[2]++;
        else
            Counts[3]++;
    }

    return Counts;
}

template<typename K, typename V>
TArray<K> APDBViewer::ExtractKeysFromPairs(const TArray<TPair<K, V>>& Pairs)
{
    TArray<K> Keys;
    Keys.Reserve(Pairs.Num());
    for (const auto& Pair : Pairs)
        Keys.Add(Pair.Key);
    return Keys;
}

APDBViewer::APDBViewer()
{
    // OPTIMIZATION #17: Enable ticking for LOD system
    PrimaryActorTick.bCanEverTick = true;
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

// ===== OPTIMIZATION #17: LOD SYSTEM IMPLEMENTATION =====

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
            if (!AtomMesh)
                continue;

            const FString& AtomName = (i < ResInfo->AtomNames.Num()) ? ResInfo->AtomNames[i] : TEXT("");
            bool bShouldBeVisible = ShouldAtomBeVisibleAtLOD(AtomName, NewLODLevel);

            AtomMesh->SetVisibility(bShouldBeVisible, false);
        }

        // Update bond visibility based on LOD
        for (int32 i = 0; i < ResInfo->BondMeshes.Num(); i++)
        {
            UStaticMeshComponent* BondMesh = ResInfo->BondMeshes[i];
            if (!BondMesh)
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

    LODLevel = 0;
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

// ===== END LOD SYSTEM IMPLEMENTATION =====

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

void APDBViewer::FetchAndDisplayStructure(const FString &ID)
{
    CurrentStructureID = ID;
    // OPTIMIZATION #9: Use FStringBuilder instead of Printf
    FString URL = TStringBuilder<256>().Append(TEXT("https://files.rcsb.org/download/")).Append(ID).Append(TEXT(".pdb")).ToString();
    FetchFileAsync(URL, [this, ID](bool bOK, const FString &Content)
                   {
        if (bOK) ParsePDB(Content);
        else FetchFileAsync(TStringBuilder<256>().Append(TEXT("https://files.rcsb.org/download/")).Append(ID).Append(TEXT(".cif")).ToString(),
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
    // OPTIMIZATION #9: Use FStringBuilder instead of Printf
    FString URL = TStringBuilder<256>()
        .Append(TEXT("https://files.rcsb.org/download/"))
        .Append(StructureID)
        .Append(TEXT(".cif"))
        .ToString();

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

    // Apply bonds to all residues and ligands (includes peptide bonds)
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

        // OPTIMIZATION #9: Key is just MoleculeName
        FString Key = MoleculeName;

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

        SetMeshArrayVisibility(Info->AtomMeshes, Info->bIsVisible);
        SetMeshArrayVisibility(Info->BondMeshes, Info->bIsVisible);

        // OPTIMIZATION #9: Key is just MoleculeName
        LigandMap.Add(MoleculeName, Info);

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

    // OPTIMIZATION #10: Pre-extract element names to avoid O(n²) repeated extractions
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

    // OPTIMIZATION: Build position->index map to avoid O(n²) search
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

void APDBViewer::DrawSphere(float X, float Y, float Z, const FLinearColor &Col, USceneComponent *Par, TArray<UStaticMeshComponent *> &Out)
{
    if (!SphereMeshAsset || !SphereMaterialAsset || !Par)
        return;

    auto *Sph = NewObject<UStaticMeshComponent>(this);
    Sph->SetStaticMesh(SphereMeshAsset);
    Sph->SetWorldScale3D(FVector(PDB::SPHERE_SIZE));
    Sph->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // OPTIMIZATION #19: Use pooled material instance
    auto *Mat = GetOrCreateMaterial(Col);
    if (Mat)
    {
        Mat->SetScalarParameterValue(TEXT("EmissiveIntensity"), 5.0f);
        Sph->SetMaterial(0, Mat);
    }

    Sph->AttachToComponent(Par, FAttachmentTransformRules::KeepWorldTransform);
    Sph->SetRelativeLocation(Par->GetComponentTransform().InverseTransformPosition(FVector(X, Y, Z)));
    Sph->RegisterComponent();

    Out.Add(Sph);
    AllAtomMeshes.Add(Sph);
}

void APDBViewer::DrawSphere(const FVector& Position, const FLinearColor& Color, USceneComponent* Parent, TArray<UStaticMeshComponent*>& OutArray)
{
    DrawSphere(Position.X, Position.Y, Position.Z, Color, Parent, OutArray);
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
        // OPTIMIZATION #19: Use pooled material instance
        auto *Mat = GetOrCreateMaterial(Color);
        if (Mat)
        {
            Mat->SetScalarParameterValue(TEXT("EmissiveIntensity"), 2.0f);
            Cyl->SetMaterial(0, Mat);
        }
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

FLinearColor APDBViewer::GetElementColor(const FString &E) const
{
    // OPTIMIZATION #7: Static result cache to avoid repeated ToUpper() calls
    static TMap<FString, FLinearColor> ResultCache;

    // Check cache first
    const FLinearColor* Cached = ResultCache.Find(E);
    if (Cached)
    {
        return *Cached;
    }

    // Cache miss - compute and store result
    static const TMap<FString, FLinearColor> Colors = {
        {TEXT("C"), FLinearColor(0.1f, 0.1f, 0.1f)}, {TEXT("O"), FLinearColor::Red}, {TEXT("H"), FLinearColor::White}, {TEXT("D"), FLinearColor::White}, {TEXT("N"), FLinearColor::Blue}, {TEXT("S"), FLinearColor::Yellow}, {TEXT("CL"), FLinearColor(0, 1, 0)}, {TEXT("P"), FLinearColor(1, 0.5f, 0)}, {TEXT("F"), FLinearColor(0, 1, 0)}, {TEXT("BR"), FLinearColor(0.6f, 0.2f, 0.2f)}, {TEXT("I"), FLinearColor(0.4f, 0, 0.8f)}, {TEXT("FE"), FLinearColor(0.8f, 0.4f, 0)}, {TEXT("MG"), FLinearColor(0, 0.8f, 0)}, {TEXT("ZN"), FLinearColor(0.5f, 0.5f, 0.5f)}, {TEXT("CA"), FLinearColor(0.2f, 0.6f, 1)}, {TEXT("NA"), FLinearColor(0, 0, 1)}, {TEXT("K"), FLinearColor(0.5f, 0, 1)}, {TEXT("CU"), FLinearColor(1, 0.5f, 0)}, {TEXT("B"), FLinearColor(1, 0.7f, 0.7f)}};
    const auto *C = Colors.Find(E.ToUpper());
    FLinearColor Result = C ? *C : FLinearColor::Gray;

    // Store in cache for future lookups
    ResultCache.Add(E, Result);
    return Result;
}

void APDBViewer::ClearResidueMap()
{
    ClearInfoMap(ResidueMap, nullptr);
    ChainIDs.Empty();
}

void APDBViewer::ClearLigandMap()
{
    ClearInfoMap(LigandMap, [this](auto* Info) { ClearLigandAtomLights(Info); });
    // OPTIMIZATION #9: Mark chain cache as dirty
    bLigandChainCacheDirty = true;
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

TArray<UPDBTreeNode*> APDBViewer::GetChainNodes()
{
    TArray<UPDBTreeNode*> Nodes;
    TArray<FString> SortedChains = ChainIDs.Array();
    SortedChains.Sort();

    // OPTIMIZED: Reserve capacity to avoid reallocations
    Nodes.Reserve(SortedChains.Num());

    for (const FString& ChainID : SortedChains)
    {
        // OPTIMIZATION #9: Use FStringBuilder instead of Printf
        FString DisplayName = ChainID == TEXT("_")
                                  ? TEXT("Chain (No ID)")
                                  : TStringBuilder<64>().Append(TEXT("Chain ")).Append(ChainID).ToString();

        UPDBTreeNode* Node = NewObject<UPDBTreeNode>(this);
        Node->InitializeWithType(DisplayName, ChainID, EPDBNodeType::Chain, ChainID);
        Nodes.Add(Node);
    }

    return Nodes;
}

TArray<UPDBTreeNode*> APDBViewer::GetResidueNodesForChain(const FString& ChainID)
{
    TArray<UPDBTreeNode*> Nodes;

    // OPTIMIZED: Build array of residues for this chain, then sort by cached sequence number
    TArray<TPair<FString, FResidueInfo*>> ChainResidues;
    for (const auto& Pair : ResidueMap)
    {
        if (Pair.Value && Pair.Value->Chain == ChainID)
        {
            ChainResidues.Add(TPair<FString, FResidueInfo*>(Pair.Key, Pair.Value));
        }
    }

    // OPTIMIZED: Early exit if no residues found
    if (ChainResidues.Num() == 0)
        return Nodes;

    // Sort by cached sequence number - avoids string parsing
    ChainResidues.Sort([](const TPair<FString, FResidueInfo*>& A, const TPair<FString, FResidueInfo*>& B)
    {
        return A.Value->CachedSequenceNumber < B.Value->CachedSequenceNumber;
    });

    // OPTIMIZED: Reserve capacity to avoid reallocations
    Nodes.Reserve(ChainResidues.Num());

    for (const auto& Pair : ChainResidues)
    {
        const FResidueInfo* Info = Pair.Value;
        // OPTIMIZATION #9: Use FStringBuilder instead of Printf
        FString DisplayName = TStringBuilder<64>()
            .Append(Info->ResidueName)
            .Append(TEXT(" "))
            .Append(Info->ResidueSeq)
            .ToString();

        UPDBTreeNode* Node = NewObject<UPDBTreeNode>(this);
        Node->InitializeWithType(DisplayName, Pair.Key, EPDBNodeType::Residue, ChainID);
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
    Keys.Sort(GetLigandKeyComparator());

    for (const FString& Key : Keys)
    {
        // Check if this is a water molecule
        if (!IsWaterKey(Key))
            continue;

        const auto* Info = SafeDereference(LigandMap.Find(Key));
        if (!Info)
            continue;
        
        // Extract chain from key (format: "HOH_101_A") - OPTIMIZED
        FString KeyChain = GetChainFromLigandKey(Key);

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
    Keys.Sort(GetLigandKeyComparator(true));

    for (const FString& Key : Keys)
    {
        // Skip water molecules - they go in the Water category
        if (IsWaterKey(Key))
            continue;

        const auto* Info = SafeDereference(LigandMap.Find(Key));
        if (!Info)
            continue;
        
        // Extract chain from key (format: "ATP_501_A") - OPTIMIZED
        FString KeyChain = GetChainFromLigandKey(Key);
        
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
            // OPTIMIZATION #9: Use FStringBuilder instead of Printf
            FString ResiduesKey = TStringBuilder<64>().Append(TEXT("RESIDUES_")).Append(Node->ChainID).ToString();
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
            // OPTIMIZED: Use chain cache for O(1) lookup instead of full map iteration
            const TArray<FLigandInfo*>* ChainLigands = LigandsByChain.Find(Node->ChainID);
            bool bHasHeteroatoms = (ChainLigands && ChainLigands->Num() > 0);
            
            if (bHasHeteroatoms)
            {
                // OPTIMIZATION #9: Use FStringBuilder instead of Printf
                FString HeteroatomsKey = TStringBuilder<64>().Append(TEXT("HETEROATOMS_")).Append(Node->ChainID).ToString();
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
                // OPTIMIZATION #9: Use FStringBuilder instead of Printf
                FString WaterKey = TStringBuilder<64>().Append(TEXT("WATER_")).Append(Node->ChainID).ToString();
                UPDBTreeNode** WaterCategoryPtr = TreeNodeCache.Find(WaterKey);
                UPDBTreeNode* WaterCategory = nullptr;
                
                if (WaterCategoryPtr && *WaterCategoryPtr)
                {
                    WaterCategory = *WaterCategoryPtr;
                }
                else
                {
                    WaterCategory = NewObject<UPDBTreeNode>(this);
                    // OPTIMIZATION #9: Use Appendf for integer formatting
                    WaterCategory->InitializeWithType(
                        TStringBuilder<64>().Appendf(TEXT("Water (%d)"), Waters.Num()).ToString(),
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
                // OPTIMIZATION #9: Use FStringBuilder instead of Printf
                FString LigandsKey = TStringBuilder<64>().Append(TEXT("LIGANDS_")).Append(Node->ChainID).ToString();
                UPDBTreeNode** LigandsCategoryPtr = TreeNodeCache.Find(LigandsKey);
                UPDBTreeNode* LigandsCategory = nullptr;
                
                if (LigandsCategoryPtr && *LigandsCategoryPtr)
                {
                    LigandsCategory = *LigandsCategoryPtr;
                }
                else
                {
                    LigandsCategory = NewObject<UPDBTreeNode>(this);
                    // OPTIMIZATION #9: Use Appendf for integer formatting
                    LigandsCategory->InitializeWithType(
                        TStringBuilder<64>().Appendf(TEXT("Ligands (%d)"), Ligands.Num()).ToString(),
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
    // OPTIMIZED: Early exit if no residues
    if (ResidueMap.Num() == 0)
        return TArray<FString>();

    // OPTIMIZED: Build array with cached sequence numbers, then sort
    TArray<TPair<FString, int32>> KeySeqPairs;
    for (const auto& Pair : ResidueMap)
    {
        if (Pair.Value)
        {
            KeySeqPairs.Add(TPair<FString, int32>(Pair.Key, Pair.Value->CachedSequenceNumber));
        }
    }

    // Sort by cached sequence number - avoids string parsing
    KeySeqPairs.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
    {
        return A.Value < B.Value;
    });

    // Extract just the keys using helper
    return ExtractKeysFromPairs(KeySeqPairs);
}

TArray<FString> APDBViewer::GetLigandList() const { return GetResidueList(); }

FString APDBViewer::GetResidueDisplayName(const FString &Key) const
{
    const auto *P = ResidueMap.Find(Key);
    // OPTIMIZATION #9: Use FStringBuilder instead of Printf
    return (P && *P)
               ? TStringBuilder<64>().Append((*P)->ResidueName).Append(TEXT(" ")).Append((*P)->ResidueSeq).ToString()
               : Key;
}

FString APDBViewer::GetLigandDisplayName(const FString &Key) const
{
    const auto *P = LigandMap.Find(Key);
    // OPTIMIZATION #9: Printf with just %s is redundant
    return (P && *P)
               ? (*P)->LigandName
               : Key;
}

FLigandInfo *APDBViewer::GetVisibleLigandInfo() const
{
    FLigandInfo *BestInfo = nullptr;
    int32 BestCount = 0;

    // OPTIMIZATION #12: Use const reference (read-only iteration)
    for (const auto &Pair : LigandMap)
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
#if WITH_EDITOR
    if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
        if (auto P = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame").GetParentWindow())
            if (P.IsValid() && P->GetNativeWindow().IsValid())
                Handle = P->GetNativeWindow()->GetOSWindowHandle();
#endif

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
        const auto *Info = SafeDereference(LigandMap.Find(Key));
        if (!Info)
            continue;

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

// ===== OPTIMIZATION: Consolidated Hydrogen Visibility Helpers =====
void APDBViewer::UpdateLigandHydrogenVisibility(FLigandInfo* LigInfo, bool bVisible)
{
    UpdateStructureHydrogenVisibility(LigInfo, bVisible);
}

void APDBViewer::UpdateResidueHydrogenVisibility(FResidueInfo* ResInfo, bool bVisible)
{
    UpdateStructureHydrogenVisibility(ResInfo, bVisible);
}

// OPTIMIZATION #16: Batched visibility update helper
void APDBViewer::SetMeshVisibilityBatched(TArray<UStaticMeshComponent*>& Meshes, bool bVisible)
{
    // Batch visibility updates to avoid redundant state changes
    for (UStaticMeshComponent* Mesh : Meshes)
    {
        if (Mesh && Mesh->IsVisible() != bVisible)
        {
            Mesh->SetVisibility(bVisible);
        }
    }
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

// Update ClearCurrentStructure - remove ClearHydrogens() call:
void APDBViewer::ClearCurrentStructure()
{
    ClearResidueMap();
    ClearLigandMap();

    // Clear tree node cache
    TreeNodeCache.Empty();

    // OPTIMIZATION #19: Clear material pool
    MaterialPool.Empty();

    // OPTIMIZATION #17: Reset LOD system cache
    bStructureCenterCached = false;
    CurrentLODLevel = 0;

    // Clear interaction data and meshes
    DestroyMeshComponents(InteractionMeshes);
    DetectedInteractions.Empty();

    // Clear general atom/bond meshes
    DestroyMeshComponents(AllAtomMeshes);
    DestroyMeshComponents(AllBondMeshes);
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

    // OPTIMIZED: Early exit if no ligands
    if (LigandMap.Num() == 0)
        return;

    // OPTIMIZED: Only update lights for visible ligands
    for (auto& Pair : LigandMap)
    {
        if (Pair.Value && Pair.Value->bIsVisible)
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

    if (LigandMap.Num() == 0)
        return;

    ForEachValidLigandLight([Intensity](UPointLightComponent* Light) {
        Light->SetIntensity(Intensity);
    });

    UE_LOG(LogTemp, Log, TEXT("Set ligand atom light intensity to %.1f"), Intensity);
}

void APDBViewer::SetLigandAtomLightRadius(float Radius)
{
    LigandAtomLightRadius = Radius;

    if (LigandMap.Num() == 0)
        return;

    ForEachValidLigandLight([Radius](UPointLightComponent* Light) {
        Light->SetAttenuationRadius(Radius);
    });

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
