#include "precompile.h"
#include "MPMesh.h"
#define CSG_EXPORTS
#include "Bool.h"
#include <ctime>
#include <list>
#include "COctree.h"
#include "BinaryTree.h"
#include "isect.h"
#include "IsectTriangle.h"
#include <queue>
#include "CSGExprNode.h"
#include "configure.h"


#ifdef _DEBUG
#include <vld.h>
int countd1, countd2, countd3, countd4, countd5;
#endif

#pragma warning(disable: 4800 4996)


namespace CSG
{
	extern ISectZone* ZONE;

	HANDLE _output;
	clock_t t0;

    static void GetLeafNodes(OctreeNode* pNode, std::list<OctreeNode*>& leaves, int NodeType);

	void StdOutput(char* str)
	{
		std::string ch(str);
		WriteConsole(_output, str, ch.size(), 0, 0);
		WriteConsole(_output, "\n", 1, 0, 0);
	}

	void DebugInfo(char* str, clock_t& t0)
	{
		char ch[32];
		sprintf(ch, "%s: %d\0", str, clock()-t0);
		t0 = clock();
		StdOutput(ch);
	}


	static void RelationTest(OctreeNode* pNode, Octree* pOctree, std::map<unsigned, Relation>& famap)
	{
		for (auto &pair: pNode->DiffMeshIndex) pair.Rela = famap[pair.ID];

		std::map<unsigned, Relation> relmap;
		if (pNode->Child)
		{
			for (int i = 0; i < 8; i++)
			{
				for (auto &pair: pNode->Child[i].DiffMeshIndex)
				{
					if (relmap.find(pair.ID) != relmap.end()) continue;
					else relmap[pair.ID] = PolyhedralInclusionTest(pNode->Child[i].BoundingBox.Center(), pOctree, pair.ID);
				}
			}

			for (unsigned i = 0; i < 8; i++)
				RelationTest(&pNode->Child[i], pOctree, relmap);
		}
	}

	static void RelationTest(Octree* pOctree)
	{
		std::map<unsigned, Relation> relmap;
		RelationTest(pOctree->Root, pOctree, relmap);
	}

	void ISectTest(Octree* pOctree)
	{
		assert(pOctree);
		std::list<OctreeNode*> leaves;
		GetLeafNodes(pOctree->Root, leaves, NODE_COMPOUND);

		std::map<GS::IndexPair, std::set<GS::IndexPair>> antiOverlapMap;
		for (auto leaf: leaves)
		{
			auto itr = leaf->TriangleTable.begin();
			auto iEnd = leaf->TriangleTable.end();
			decltype(iEnd) itr2;
			unsigned i, j, ni, nj;
			MPMesh *meshi, *meshj;
			MPMesh::FaceHandle tri1, tri2;
			Vec3d *v0,*v1,*v2, nv,*u0,*u1,*u2,nu,start,end;
			MPMesh::FVIter fvItr;
			int isISect;
			int startT(0), endT(0);
			ISectTriangle **si=nullptr, **sj=nullptr;
			VertexPos startiT, startjT, endiT, endjT;
			ISVertexItr vP1, vP2;

			int meshId[2];
			for (; itr != iEnd; ++itr)
			{
				itr2 = itr;
				++itr2;
				for (; itr2 != iEnd; ++itr2)
				{
					ni = itr->second.size();
					nj = itr2->second.size();

					meshi = pOctree->pMesh[itr->first];
					meshj = pOctree->pMesh[itr2->first];

					if (meshi->ID > meshj->ID) {meshId[0] = meshj->ID; meshId[1] = meshi->ID;}
					else {meshId[0] = meshi->ID; meshId[1] = meshj->ID;}

					GS::IndexPair iPair;
					GS::MakeIndex(meshId, iPair);
					auto &antiOverlapSet = antiOverlapMap[iPair];

					for (i = 0; i < ni; i++)
					{
						for (j = 0; j < nj; j++)
						{
							tri1 = itr->second[i];
							tri2 = itr2->second[j];
							if (meshi->ID > meshj->ID) {meshId[0] = tri2.idx(); meshId[1] = tri1.idx();}
							else {meshId[0] = tri1.idx(); meshId[1] = tri2.idx();}
							GS::MakeIndex(meshId, iPair);
							if (antiOverlapSet.find(iPair) != antiOverlapSet.end()) continue;
							else antiOverlapSet.insert(iPair);

							// intersection test main body
							GetCorners(meshi, tri1, v0, v1, v2);
							GetCorners(meshj, tri2, u0, u1, u2);

							nv = meshi->normal(tri1);
							nu = meshj->normal(tri2);
							
							startT = INNER; endT = INNER; // return to Zero.

							//auto &isecTris = (*si)->isecTris[meshj->ID];
							//auto &icoplTris = (*si)->coplanarTris;

							isISect = TriTriIntersectTest(*v0, *v1, *v2, nv,
								*u0, *u1, *u2, nu, startT, endT, start, end);

							if (isISect < 0) continue;

							si = &meshi->property(meshi->SurfacePropHandle, tri1);
							sj = &meshj->property(meshj->SurfacePropHandle, tri2);

							if (!*si) *si = new ISectTriangle(meshi, tri1);
							if (!*sj) *sj = new ISectTriangle(meshj, tri2);

							if (isISect == 0)
							{
								(*si)->coplanarTris.emplace_back(meshj, tri2);
								(*sj)->coplanarTris.emplace_back(meshi, tri1);
								continue;
							}

							startiT = VertexPos(startT & 0xffff);
							startjT = VertexPos(startT >> 16);

							endiT = VertexPos(endT & 0xffff);
							endjT = VertexPos(endT >> 16);

							if (IsEqual(start, end))
							{
								// 点相交
								Vec3d point = (start+end)/2;

								// 最后一个参数表示，可能存在两个以上的插入点
								vP1 = InsertPoint(*si, startiT, point);
								InsertPoint(*si, endiT, vP1);
								InsertPoint(*sj, startjT, vP1);
								InsertPoint(*sj, endjT, vP1);
							}
							else
							{
								// 线相交
								if (OpenMesh::dot(OpenMesh::cross(nv, nu), end-start) > 0)
								{
									vP1 = InsertPoint(*si, startiT, start);
									vP2 = InsertPoint(*si, endiT, end);
									InsertSegment(*si, vP1, vP2, *sj);
									vP1 = InsertPoint(*sj, startjT, vP1);
									vP2 = InsertPoint(*sj, endjT, vP2);
									InsertSegment(*sj, vP2, vP1, *si);
								}
								else
								{
									vP1 = InsertPoint(*si, startiT, start);
									vP2 = InsertPoint(*si, endiT, end);
									InsertSegment(*si, vP2, vP1, *sj);
									vP1 = InsertPoint(*sj, startjT, vP1);
									vP2 = InsertPoint(*sj, endjT, vP2);
									InsertSegment(*sj, vP1, vP2, *si);
								}
							}
						}
					}
				}
			}
		}
	}

	void FloodColoring(Octree* pOctree, CSGTree* pPosCSG);

	GS::BaseMesh* result;

	static GS::BaseMesh* BooleanOperation2(GS::CSGExprNode* input, HANDLE stdoutput)
	{
		_output= stdoutput;
        MPMesh** arrMesh = NULL;
		result = new GS::BaseMesh;
        int nMesh = -1;
		StdOutput("Start:");
        t0 = clock();
        CSGTree* pCSGTree = ConvertCSGTree(input, &arrMesh, &nMesh);
		CSGTree* pPosCSG = ConvertToPositiveTree(pCSGTree);
		delete pCSGTree;
        DebugInfo("Convert", t0);
        Octree* pOctree = BuildOctree(arrMesh, nMesh);
        DebugInfo("BuildTree", t0);
		InitZone();
		ISectTest(pOctree);
        DebugInfo("ISectTest", t0);
		FloodColoring(pOctree, pPosCSG);
        DebugInfo("FloodColoring", t0);

		delete pOctree;
		delete pPosCSG;
		delete input;

		ReleaseZone();
		for (int i = 0; i < nMesh; i++)
			delete arrMesh[i];
		delete [] arrMesh;

		return result;
	}


    extern "C" CSG_API GS::BaseMesh* BooleanOperation(GS::CSGExprNode* input, HANDLE stdoutput)
    {
		return BooleanOperation2(input, stdoutput);
    }

    extern "C" CSG_API GS::BaseMesh* BooleanOperation_MultiThread(GS::CSGExprNode* input)
    {
        return NULL;
    }

    static void GetLeafNodes(OctreeNode* pNode, std::list<OctreeNode*>& leaves, int NodeType)
    {
        if (pNode == NULL) return;

		if (IsLeaf(pNode) || pNode->Type == NODE_SIMPLE)
        {
            if (!pNode->TriangleCount) return; 
            if (NodeType == pNode->Type)
                leaves.push_back(pNode);
            return;
        }

        for (int i = 0; i < 8 ; i++)
            GetLeafNodes(&pNode->Child[i], leaves, NodeType);
    }

	struct FacePair
	{
		MPMesh::FaceHandle seed, current;
		const MPMesh::FaceHandle& operator[](int index) const
		{
			switch (index)
			{
			case 0:	return seed;
			case 1:	return current;
			default:
				assert(0);
				return current;
			}
		}

		MPMesh::FaceHandle& operator[](int index)
		{
			switch (index)
			{
			case 0:	return seed;
			case 1:	return current;
			default:
				assert(0);
				return current;
			}
		}
	};

	struct SeedInfo
	{
		std::queue<FacePair> queue;
		Relation *relation;

		SeedInfo():relation(nullptr){}
		~SeedInfo(){SAFE_RELEASE_ARRAY(relation);}
	};

	bool CompareRelationSpace(ISectTriangle* triSeed, ISectTriangle* triCur)
	{
		assert(triSeed);
		if (!triCur)
		{
			if (!triSeed->relationTestId.size()) return true;
			else return false;
		}

		// 首先相交集必须是子集，然后relationTest必须相同
		auto end = triCur->segs.end();
		for (auto triId: triSeed->relationTestId)
		{
			if (triCur->segs.find(triId) == end) return false;
		}

		end = triSeed->segs.end();
		for (auto &pair: triCur->segs)
		{
			if (triSeed->segs.find(pair.first) == end) return false;
		}

		return true;
	}

	static void AddTriangle(MPMesh* pMesh, MPMesh::FaceHandle face)
    {
		//countd1 ++;
		GS::double3 v[3];
		Vec3d *v0, *v1, *v2;
		GetCorners(pMesh, face, v0, v1, v2);
		v[0] = Vec3dToDouble3(*v0);
		v[1] = Vec3dToDouble3(*v1);
		v[2] = Vec3dToDouble3(*v2);
		result->AddTriangle(v);
    }

	void FloodColoring(Octree* pOctree, CSGTree* pPosCSG)
	{
		MPMesh *pMesh;
		std::queue<MPMesh::FaceHandle> faceQueue;
		std::queue<SeedInfo> seedQueueList;
		MPMesh::FaceHandle curFace, relatedFace;
		MPMesh::FaceFaceIter ffItr;
		Vec3d *v0, *v1, *v2;
		Relation *curRelationTable, curRelation;
		CSGTree* curTree;

		SeedInfo** meshSeedInfo = new SeedInfo*[pOctree->nMesh];
		memset(meshSeedInfo, 0, sizeof(SeedInfo*)*pOctree->nMesh);
		FacePair fPair;
		
		for (unsigned i0 = 0; i0 < pOctree->nMesh; i0++)
		{
			pMesh = pOctree->pMesh[i0];
			curFace = *pMesh->faces_begin();

			meshSeedInfo[i0] = new SeedInfo;
			meshSeedInfo[i0]->relation = new Relation[pOctree->nMesh];
			memset(meshSeedInfo[i0]->relation, 0, sizeof(Relation)*pOctree->nMesh);
			meshSeedInfo[i0]->queue.emplace();
			meshSeedInfo[i0]->queue.back().seed = curFace;
			meshSeedInfo[i0]->queue.back().current = curFace;

			auto surface = pMesh->property(pMesh->SurfacePropHandle, curFace);
			if (surface)
			{
				for (auto &iItr: surface->segs)
					meshSeedInfo[i0]->relation[iItr.first] = REL_NOT_AVAILABLE;
				for (auto &jItr: surface->coplanarTris)
					meshSeedInfo[i0]->relation[jItr.pMesh->ID] = REL_NOT_AVAILABLE;
			}

			GetCorners(pMesh, curFace, v0, v1, v2);
			Vec3d bc = (*v0+*v1+*v2)/3.0;
			for (unsigned i = 0; i < pOctree->nMesh; i++)
			{
				if (i == i0 || meshSeedInfo[i0]->relation[i] == REL_NOT_AVAILABLE) continue;
				meshSeedInfo[i0]->relation[i] = 
					PolyhedralInclusionTest(bc, pOctree, i, pOctree->pMesh[i]->bInverse);
			}

			seedQueueList.push(*meshSeedInfo[i0]);
			meshSeedInfo[i0]->relation = nullptr;

			while (!seedQueueList.empty())
			{
				auto &seeds = seedQueueList.front();
				while (!seeds.queue.empty())
				{
					while (!seeds.queue.empty())
					{
						if (pMesh->property(pMesh->MarkPropHandle, seeds.queue.front()[1]) != 2)
							break;
						seeds.queue.pop();
					}
					if (seeds.queue.empty()) break;

					curFace = seeds.queue.front()[1];
					relatedFace = seeds.queue.front()[0];
					faceQueue.push(curFace);
					seeds.queue.pop();
					GetRelationTable(pMesh, curFace, relatedFace, seeds.relation, pOctree->nMesh, curRelationTable);
					seedQueueList.emplace();
					seedQueueList.back().relation = curRelationTable;
					curTree = copy(pPosCSG);
					curRelation = ParsingCSGTree(pMesh, curRelationTable, pOctree->nMesh, curTree);

					if (!pMesh->property(pMesh->SurfacePropHandle, curFace)) // 简单模式
					{
						assert(curRelation == REL_INSIDE || curRelation == REL_SAME);
						while (!faceQueue.empty())
						{			
							//countd3 ++;
							while (1)
							{
								if (pMesh->property(pMesh->MarkPropHandle, faceQueue.front()) != 2)
									break;
								faceQueue.pop();
							}

							curFace = faceQueue.front();
							faceQueue.pop();
							if (curRelation == REL_SAME) AddTriangle(pMesh, curFace);
							pMesh->property(pMesh->MarkPropHandle, curFace) = 2; // processed

							// add neighbor
							ffItr = pMesh->ff_iter(curFace);
							int *markPtr;
							for (int i = 0; i < 3; i++, ffItr++)
							{
								markPtr = &(pMesh->property(pMesh->MarkPropHandle, *ffItr));
								if (*markPtr == 0)
								{
									if (!pMesh->property(pMesh->SurfacePropHandle, *ffItr))
										faceQueue.push(*ffItr);
									else
									{
										FacePair fh;
										fh[0] = curFace;
										fh[1] = *ffItr;
										seedQueueList.back().queue.push(fh);
									}
									*markPtr = 1; // queued
								}
							}
						}
					}
					else // 复合模式
					{
						while (!faceQueue.empty())
						{					
							//countd4 ++;
							while (1)
							{
								if (pMesh->property(pMesh->MarkPropHandle, faceQueue.front()) != 2)
									break;
								faceQueue.pop();
							}

							curFace = faceQueue.front();
							assert(pMesh->property(pMesh->SurfacePropHandle, curFace));
							faceQueue.pop();
							switch (curRelation)
							{
							case REL_NOT_AVAILABLE:
								ParsingFace(pMesh, curFace, curTree, result);
								break;
							case REL_SAME:
								AddTriangle(pMesh, curFace);
								break;
							case REL_INSIDE:
								break;
							default:
								assert(0);
								break;
							}
							pMesh->property(pMesh->MarkPropHandle, curFace) = 2; // processed

							// add neighbor
							ffItr = pMesh->ff_iter(curFace);
							int *markPtr;
							auto &seedSurface = pMesh->property(pMesh->SurfacePropHandle, curFace);
							for (int i = 0; i < 3; i++, ffItr++)
							{
								markPtr = &(pMesh->property(pMesh->MarkPropHandle, *ffItr));
								if (*markPtr == 0)
								{
									auto &legSurface = pMesh->property(pMesh->SurfacePropHandle, *ffItr);
									if (surface && CompareRelationSpace(seedSurface, legSurface))
										faceQueue.push(*ffItr);
									else
									{
										FacePair fh;
										fh[0] = curFace;
										fh[1] = *ffItr;
										seedQueueList.back().queue.push(fh);
									}
									*markPtr = 1; // queued
								}
							}
						}
					}
				}
				seedQueueList.pop();
			}
		}
	}
}

