// PDBViewer_TreeView.cpp - TreeView and ListView UI functions

#include "PDBViewer.h"
#include "Components/TreeView.h"
#include "Components/ListView.h"

namespace PDB
{
    constexpr float SCALE = 50.0f;
    constexpr float SPHERE_SIZE = 0.5f;
    constexpr float CYLINDER_SIZE = 0.1f;
    constexpr float BOND_OFFSET = 8.0f;
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
        // Use unique cache key for chain nodes
        FString CacheKey = TStringBuilder<64>().Append(TEXT("CHAIN_")).Append(ChainID).ToString();

        // Check cache first to prevent GC issues
        UPDBTreeNode** CachedNode = TreeNodeCache.Find(CacheKey);
        UPDBTreeNode* Node = nullptr;

        if (CachedNode && IsValid(*CachedNode))
        {
            Node = *CachedNode;
        }
        else
        {
            // OPTIMIZATION #9: Use FStringBuilder instead of Printf
            FString DisplayName = ChainID == TEXT("_")
                                      ? TEXT("Chain (No ID)")
                                      : TStringBuilder<64>().Append(TEXT("Chain ")).Append(ChainID).ToString();

            Node = NewObject<UPDBTreeNode>(this);
            Node->InitializeWithType(DisplayName, ChainID, EPDBNodeType::Chain, ChainID);
            TreeNodeCache.Add(CacheKey, Node);
        }

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

        // Check cache first to prevent GC issues
        UPDBTreeNode** CachedNode = TreeNodeCache.Find(Pair.Key);
        UPDBTreeNode* Node = nullptr;

        if (CachedNode && IsValid(*CachedNode))
        {
            Node = *CachedNode;
            Node->bIsVisible = Info->bIsVisible;
        }
        else
        {
            // OPTIMIZATION #9: Use FStringBuilder instead of Printf
            FString DisplayName = TStringBuilder<64>()
                .Append(Info->ResidueName)
                .Append(TEXT(" "))
                .Append(Info->ResidueSeq)
                .ToString();

            Node = NewObject<UPDBTreeNode>(this);
            Node->InitializeWithType(DisplayName, Pair.Key, EPDBNodeType::Residue, ChainID);
            Node->bIsVisible = Info->bIsVisible;
            TreeNodeCache.Add(Pair.Key, Node);
        }

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

        // Check cache first to prevent GC issues
        UPDBTreeNode** CachedNode = TreeNodeCache.Find(Key);
        UPDBTreeNode* Node = nullptr;

        if (CachedNode && IsValid(*CachedNode))
        {
            Node = *CachedNode;
            Node->bIsVisible = Info->bIsVisible;
        }
        else
        {
            Node = NewObject<UPDBTreeNode>(this);
            Node->InitializeWithType(Info->LigandName, Key, EPDBNodeType::Water, ChainID);
            Node->bIsVisible = Info->bIsVisible;
            TreeNodeCache.Add(Key, Node);
        }

        Nodes.Add(Node);
    }

    return Nodes;
}

TArray<UPDBTreeNode*> APDBViewer::GetLigandNodesForChain(const FString& ChainID)
{
    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT("GetLigandNodesForChain called for chain: '%s'"), *ChainID);
    UE_LOG(LogTemp, Warning, TEXT("Total ligands in LigandMap: %d"), LigandMap.Num());

    TArray<UPDBTreeNode*> Nodes;
    TArray<FString> Keys;
    LigandMap.GetKeys(Keys);

    // Sort by name then sequence number
    Keys.Sort(GetLigandKeyComparator(true));

    int32 MatchCount = 0;
    int32 WaterSkipCount = 0;
    int32 ChainMismatchCount = 0;

    for (const FString& Key : Keys)
    {
        // Skip water molecules - they go in the Water category
        if (IsWaterKey(Key))
        {
            WaterSkipCount++;
            continue;
        }

        const auto* Info = SafeDereference(LigandMap.Find(Key));
        if (!Info)
            continue;

        // Extract chain from key (format: "ATP_501_A" or "MolName_1_A")
        FString KeyChain = GetChainFromLigandKey(Key);

        if (KeyChain != ChainID)
        {
            ChainMismatchCount++;
            // Log first few mismatches for debugging
            if (ChainMismatchCount <= 3)
            {
                UE_LOG(LogTemp, Warning, TEXT("  Chain mismatch: Key='%s', KeyChain='%s', Requested='%s'"),
                       *Key, *KeyChain, *ChainID);
            }
            continue;
        }

        MatchCount++;

        // Check cache first to prevent GC issues
        UPDBTreeNode** CachedNode = TreeNodeCache.Find(Key);
        UPDBTreeNode* Node = nullptr;

        if (CachedNode && IsValid(*CachedNode))
        {
            Node = *CachedNode;
            // Update visibility in case it changed
            Node->bIsVisible = Info->bIsVisible;
        }
        else
        {
            Node = NewObject<UPDBTreeNode>(this);
            Node->InitializeWithType(Info->LigandName, Key, EPDBNodeType::Ligand, ChainID);
            Node->bIsVisible = Info->bIsVisible;
            TreeNodeCache.Add(Key, Node);
        }

        Nodes.Add(Node);
    }

    UE_LOG(LogTemp, Warning, TEXT("GetLigandNodesForChain summary for '%s':"), *ChainID);
    UE_LOG(LogTemp, Warning, TEXT("  - Total keys: %d"), Keys.Num());
    UE_LOG(LogTemp, Warning, TEXT("  - Waters skipped: %d"), WaterSkipCount);
    UE_LOG(LogTemp, Warning, TEXT("  - Chain mismatches: %d"), ChainMismatchCount);
    UE_LOG(LogTemp, Warning, TEXT("  - Matches returned: %d"), MatchCount);
    UE_LOG(LogTemp, Warning, TEXT("========================================"));

    return Nodes;
}

void APDBViewer::PopulateTreeView(UTreeView *TreeView)
{
    if (!TreeView)
    {
        UE_LOG(LogTemp, Error, TEXT("PopulateTreeView called with NULL TreeView!"));
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("====== PopulateTreeView starting ======"));
    UE_LOG(LogTemp, Warning, TEXT("LigandMap contains %d entries"), LigandMap.Num());
    UE_LOG(LogTemp, Warning, TEXT("ChainIDs contains %d chains"), ChainIDs.Num());

    // Log all chains
    for (const FString& Chain : ChainIDs)
    {
        UE_LOG(LogTemp, Warning, TEXT("  Chain in ChainIDs: '%s'"), *Chain);
    }

    // Log first few ligand keys to see their format
    int32 LogCount = 0;
    for (const auto& Pair : LigandMap)
    {
        if (LogCount < 5)
        {
            FString KeyChain = GetChainFromLigandKey(Pair.Key);
            UE_LOG(LogTemp, Warning, TEXT("  LigandMap key: '%s' -> Chain: '%s'"), *Pair.Key, *KeyChain);
            LogCount++;
        }
    }
    if (LigandMap.Num() > 5)
    {
        UE_LOG(LogTemp, Warning, TEXT("  ... and %d more ligands"), LigandMap.Num() - 5);
    }

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

            // FIX: Check for heteroatoms by directly scanning LigandMap
            // This is more reliable than using the LigandsByChain cache
            bool bHasHeteroatoms = false;
            for (const auto& Pair : LigandMap)
            {
                if (Pair.Value)
                {
                    FString KeyChain = GetChainFromLigandKey(Pair.Key);
                    if (KeyChain == Node->ChainID)
                    {
                        bHasHeteroatoms = true;
                        break;
                    }
                }
            }

            UE_LOG(LogTemp, Warning, TEXT("Chain %s: bHasHeteroatoms = %s (LigandMap has %d entries)"),
                   *Node->ChainID, bHasHeteroatoms ? TEXT("true") : TEXT("false"), LigandMap.Num());

            if (bHasHeteroatoms)
            {
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
                FString WaterKey = TStringBuilder<64>().Append(TEXT("WATER_")).Append(Node->ChainID).ToString();
                UPDBTreeNode** WaterCategoryPtr = TreeNodeCache.Find(WaterKey);
                UPDBTreeNode* WaterCategory = nullptr;

                // FIX: Build display name with current count FIRST
                FString WaterDisplayName = TStringBuilder<64>().Appendf(TEXT("Water (%d)"), Waters.Num()).ToString();

                if (WaterCategoryPtr && *WaterCategoryPtr)
                {
                    WaterCategory = *WaterCategoryPtr;
                    // FIX: Update DisplayName to reflect current count
                    WaterCategory->DisplayName = WaterDisplayName;
                }
                else
                {
                    WaterCategory = NewObject<UPDBTreeNode>(this);
                    WaterCategory->InitializeWithType(WaterDisplayName, WaterKey,
                        EPDBNodeType::WaterCategory, Node->ChainID);
                    TreeNodeCache.Add(WaterKey, WaterCategory);
                }
                ChildNodes.Add(WaterCategory);
            }

            // Check if there are any ligands for this chain
            TArray<UPDBTreeNode*> Ligands = GetLigandNodesForChain(Node->ChainID);

            UE_LOG(LogTemp, Warning, TEXT("HeteroatomsCategory for chain %s: Found %d ligands"),
                   *Node->ChainID, Ligands.Num());

            if (Ligands.Num() > 0)
            {
                FString LigandsKey = TStringBuilder<64>().Append(TEXT("LIGANDS_")).Append(Node->ChainID).ToString();
                UPDBTreeNode** LigandsCategoryPtr = TreeNodeCache.Find(LigandsKey);
                UPDBTreeNode* LigandsCategory = nullptr;

                // FIX: Build display name with current count FIRST
                FString LigandsDisplayName = TStringBuilder<64>().Appendf(TEXT("Ligands (%d)"), Ligands.Num()).ToString();

                UE_LOG(LogTemp, Warning, TEXT("Creating/updating LigandsCategory with DisplayName: %s"),
                       *LigandsDisplayName);

                if (LigandsCategoryPtr && *LigandsCategoryPtr)
                {
                    LigandsCategory = *LigandsCategoryPtr;
                    // FIX: Update DisplayName to reflect current count
                    LigandsCategory->DisplayName = LigandsDisplayName;
                }
                else
                {
                    LigandsCategory = NewObject<UPDBTreeNode>(this);
                    LigandsCategory->InitializeWithType(LigandsDisplayName, LigandsKey,
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

            UE_LOG(LogTemp, Warning, TEXT("LigandsCategory expansion for chain %s: returning %d ligand nodes"),
                   *Node->ChainID, Ligands.Num());

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

void APDBViewer::ClearTreeNodeCache()
{
    TreeNodeCache.Empty();
    UE_LOG(LogTemp, Warning, TEXT("Tree node cache cleared"));
}
