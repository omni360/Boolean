#include "precompile.h"
#include "MPMesh.h"
#include "BaseMesh.h"

namespace CSG
{
	inline OpenMesh::Vec3d convert_double3(GS::double3& vec)
	{
		return OpenMesh::Vec3d(vec.x, vec.y, vec.z);
	}
	

	MPMesh::MPMesh(GS::BaseMesh* pMesh):
        ID(-1), pOrigin(pMesh), bInverse(false)
    {
		request_face_normals();
    }

    MPMesh::~MPMesh(void)
    {
		release_face_normals();
    }

	MPMesh* ConvertToMPMesh(GS::BaseMesh* pMesh)
	{
		MPMesh *res = new MPMesh(pMesh);

        int n = (int)pMesh->VertexCount();
        res->BBox.Clear();
		MPMesh::VertexHandle vhandle;
        for (int i = 0; i < n; i++)
		{
            vhandle = res->add_vertex(convert_double3(pMesh->Vertex(i).pos));
			res->BBox.IncludePoint(res->point(vhandle));
		}

		std::vector<MPMesh::VertexHandle>  face_vhandles(3);
        n = (int)pMesh->PrimitiveCount();
        for (int i = 0; i < n; i++)
        {
            auto &index = pMesh->TriangleInfo(i);
			face_vhandles[0] = res->vertex_handle(index.VertexId[0]);
			face_vhandles[1] = res->vertex_handle(index.VertexId[1]);
			face_vhandles[2] = res->vertex_handle(index.VertexId[2]);
            auto hwd = res->add_face(face_vhandles);
        }

		res->update_normals();

		return res;
		return NULL;
	}

} // namespace CSG 
