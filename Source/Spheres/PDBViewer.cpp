// PDBViewer.cpp - Core functionality (constructor, BeginPlay, helper functions)
// Implementation split across multiple files:
//   - PDBViewer_Parsing.cpp    - File parsing (PDB, mmCIF, SDF)
//   - PDBViewer_Rendering.cpp  - Mesh rendering (DrawSphere, DrawBond)
//   - PDBViewer_TreeView.cpp   - TreeView/ListView UI
//   - PDBViewer_Visibility.cpp - Visibility toggle functions
//   - PDBViewer_Hydrogen.cpp   - Hydrogen management
//   - PDBViewer_Lighting.cpp   - Ligand atom lighting
//   - PDBViewer_LOD.cpp        - Level of Detail system
//   - PDBViewer_FileIO.cpp     - File save/load dialogs
//   - PDBViewer_Debug.cpp      - Debug/logging functions
//   - PDBViewer_Interactions.cpp - Molecular interaction detection

#include "PDBViewer.h"
#include "PDBCameraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"

namespace PDB
{
    constexpr float SCALE = 50.0f;
    constexpr float SPHERE_SIZE = 0.5f;
    constexpr float CYLINDER_SIZE = 0.1f;
    constexpr float BOND_OFFSET = 8.0f;
}

// Static backbone atoms set for efficient LOD checks
const TSet<FString> APDBViewer::BackboneAtoms = {TEXT("CA"), TEXT("C"), TEXT("N"), TEXT("O")};

// ===== OPTIMIZATION: String Parsing Helpers =====
FString APDBViewer::GetChainFromLigandKey(const FString& Key)
{
    FString Name, Seq, Chain;
    ParseLigandKey(Key, Name, Seq, Chain);
    return Chain;
}

void APDBViewer::ParseLigandKey(const FString& Key, FString& OutName, FString& OutSeq, FString& OutChain)
{
    OutName = OutSeq = OutChain = TEXT("");

    // Key format: "Name_Seq_Chain" where Name may contain underscores but Seq and Chain never do.
    // Find the last two underscores to split correctly.
    int32 LastUnderscore = INDEX_NONE;
    int32 SecondLastUnderscore = INDEX_NONE;
    for (int32 i = Key.Len() - 1; i >= 0; --i)
    {
        if (Key[i] == '_')
        {
            if (LastUnderscore == INDEX_NONE)
                LastUnderscore = i;
            else
            {
                SecondLastUnderscore = i;
                break;
            }
        }
    }

    if (LastUnderscore == INDEX_NONE)
    {
        OutName = Key;
    }
    else if (SecondLastUnderscore == INDEX_NONE)
    {
        OutName = Key.Left(LastUnderscore);
        OutSeq = Key.Mid(LastUnderscore + 1);
    }
    else
    {
        OutName = Key.Left(SecondLastUnderscore);
        OutSeq = Key.Mid(SecondLastUnderscore + 1, LastUnderscore - SecondLastUnderscore - 1);
        OutChain = Key.Mid(LastUnderscore + 1);
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
        if (IsValid(M))
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

// Explicit template instantiations
template void APDBViewer::UpdateStructureHydrogenVisibility<FLigandInfo>(FLigandInfo*, bool);
template void APDBViewer::UpdateStructureHydrogenVisibility<FResidueInfo>(FResidueInfo*, bool);

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

        // Use cache key with prefix to handle different category views
        FString CacheKey = CategoryPrefix.IsEmpty() ? Key : (CategoryPrefix + Key);

        // Check cache first to prevent GC issues
        UPDBTreeNode** CachedNode = TreeNodeCache.Find(CacheKey);
        UPDBTreeNode* Node = nullptr;

        if (CachedNode && IsValid(*CachedNode))
        {
            Node = *CachedNode;
            Node->bIsVisible = Info->bIsVisible;
        }
        else
        {
            Node = NewObject<UPDBTreeNode>(this);
            Node->Initialize(CategoryPrefix + Info->LigandName, Key, false, ChainID);
            Node->bIsVisible = Info->bIsVisible;
            TreeNodeCache.Add(CacheKey, Node);
        }

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

// Explicit template instantiation for the ClearInfoMap function
template void APDBViewer::ClearInfoMap<TMap<FString, FResidueInfo*>, std::nullptr_t>(TMap<FString, FResidueInfo*>&, std::nullptr_t);

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

// Explicit template instantiation
template TArray<FString> APDBViewer::ExtractKeysFromPairs<FString, int32>(const TArray<TPair<FString, int32>>&);

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

    // Clear general atom/bond mesh arrays (meshes already destroyed by ClearResidueMap/ClearLigandMap)
    AllAtomMeshes.Empty();
    AllBondMeshes.Empty();
}

int32 APDBViewer::GetAtomCharge(const FString& Element, const FString& AtomName, const FString& ResidueName) const
{
    // Simplified charge estimation
    if (Element == TEXT("N"))
    {
        if (ResidueName == TEXT("LYS") && AtomName == TEXT("NZ"))
            return +1;
        if (ResidueName == TEXT("ARG") && (AtomName == TEXT("NH1") || AtomName == TEXT("NH2")))
            return +1;
    }
    if (Element == TEXT("O"))
    {
        if ((ResidueName == TEXT("ASP") || ResidueName == TEXT("GLU")) &&
            (AtomName == TEXT("OD1") || AtomName == TEXT("OD2") || AtomName == TEXT("OE1") || AtomName == TEXT("OE2")))
            return -1;
    }
    return 0;
}
