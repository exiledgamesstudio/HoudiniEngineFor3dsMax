#include "HEMAX_Input_Geometry.h"

#include "HEMAX_Logger.h"
#include "HEMAX_SessionManager.h"

#include "triobj.h"
#include "polyobj.h"
#include "MeshNormalSpec.h"
#include "MNNormalSpec.h"
#include "VertexNormal.h"
#include "stdmat.h"

HEMAX_Input_Geometry::HEMAX_Input_Geometry(ULONG MaxNode)
    : HEMAX_Input(MaxNode)
{
    BuildInputNode();
}

HEMAX_Input_Geometry::HEMAX_Input_Geometry(HEMAX_InputType Type, int Id, ULONG MaxNode) : HEMAX_Input(Type, Id, MaxNode)
{
    BuildInputNode();
}

HEMAX_Input_Geometry::HEMAX_Input_Geometry(HEMAX_InputType Type, PolyObject* MaxPolyObject, INode* MaxNode) : HEMAX_Input(Type, -1, -1)
{
    if (MaxPolyObject)
    {
	BuildPolyGeometryForInputNode(Node, MaxPolyObject->GetMesh(), "modifier_input", HEMAX_Utilities::GetIdentityTransform(), MaxNode);
    }
}

HEMAX_Input_Geometry::~HEMAX_Input_Geometry()
{
    HEMAX_SessionManager& SessionManager = HEMAX_SessionManager::GetSessionManager();

    if (SessionManager.IsSessionActive())
    {
	HEMAX_NodeId ParentNodeId = Node->Info.parentId;
	Node->Delete();
	HEMAX_SessionManager::GetSessionManager().Session->DeleteNode(ParentNodeId);
    }
}

void
HEMAX_Input_Geometry::RebuildAfterChange()
{
    HEMAX_SessionManager& SessionManager = HEMAX_SessionManager::GetSessionManager();

    HEMAX_NodeId ParentNodeId = Node->Info.parentId;
    Node->Delete();
    HEMAX_SessionManager::GetSessionManager().Session->DeleteNode(ParentNodeId);
    BuildInputNode();
}

void
HEMAX_Input_Geometry::BuildInputNode()
{
    INode* MaxInputNode = GetCOREInterface()->GetINodeByHandle(MaxNodeHandle);

    if (MaxInputNode)
    {
	ObjectState MaxObjectState = MaxInputNode->EvalWorldState(GetCOREInterface()->GetTime());
	Object* MaxObject = MaxObjectState.obj;

	if (MaxObject->CanConvertToType(Class_ID(POLYOBJ_CLASS_ID, 0)))
	{
	    PolyObject* MaxPolyObject = (PolyObject*)MaxObject->ConvertToType(GetCOREInterface()->GetTime(), Class_ID(POLYOBJ_CLASS_ID, 0));
	    INode* InputNode = GetCOREInterface()->GetINodeByHandle(MaxNodeHandle);

	    BuildPolyGeometryForInputNode(Node, MaxPolyObject->GetMesh(), GetInputNodeName(), HEMAX_Utilities::BuildMaxTransformFromINode(InputNode), MaxInputNode);

	    if (MaxObject != MaxPolyObject)
	    {
		delete MaxPolyObject;
	    }
	}
	else
	{
	    std::string Msg = "Cannot build a geometry input node from the supplied object";
	    HEMAX_Logger::Instance().AddEntry(Msg, HEMAX_LOG_LEVEL_WARN);
	}
    }
}

void
HEMAX_Input_Geometry::BuildPolyGeometryForInputNode(HEMAX_Node* Node, MNMesh& MaxMesh, std::string InputNodeName, HEMAX_MaxTransform NodeTransform, INode* MaxNode)
{
    CreateInputNode(InputNodeName + "_" + std::to_string(rand()) + std::to_string(rand()));

    MaxMesh.CollapseDeadStructs();
    MaxMesh.buildNormals();

    // Look up material (if it exists)
    INode* SourceNode = GetCOREInterface()->GetINodeByHandle(MaxNodeHandle);

    MarshalNodeNameDetailAttribute();

    bool SingleMaterial = false;
    const char* SingleMaterialName;
    std::vector<const char*> SingleMaterialNameData;

    std::unordered_map<int, const char*> SubMatNames;
    std::vector<const char*> FaceMaterialNames;

    if (SourceNode)
    {
	Mtl* MeshMat = SourceNode->GetMtl();

	if (MeshMat)
	{
	    // We have a multi-material
	    if (MeshMat->NumSubMtls() > 0)
	    {
		FaceMaterialNames.resize(MaxMesh.FNum());
		for (int m = 0; m < MeshMat->NumSubMtls(); m++)
		{
		    Mtl* SubMat = MeshMat->GetSubMtl(m);

		    if (SubMat)
		    {
			SubMatNames.insert({ m, SubMat->GetName().ToCStr() });
		    }
		}
	    }
	    else
	    {
		SingleMaterial = true;
		SingleMaterialName = MeshMat->GetName().ToCStr();
		SingleMaterialNameData = std::vector<const char*>(MaxMesh.FNum(), SingleMaterialName);
	    }
	}
    }

    // Positions

    int FaceCount = MaxMesh.FNum();
    int VertCount = MaxMesh.VNum();

    float* PointArray = new float[VertCount * 3];
    int* FaceCountArray = new int[FaceCount];
    int* SmoothingGroupArray = new int[FaceCount];
    int* MaterialIDArray = new int[FaceCount];

    float ScaleConversion = HEMAX_Utilities::GetMaxToHoudiniScale();

    for (int i = 0; i < VertCount; i++)
    {
	PointArray[i * 3] = MaxMesh.V(i)->p.x * ScaleConversion;
	PointArray[(i * 3) + 1] = MaxMesh.V(i)->p.z * ScaleConversion;
	PointArray[(i * 3) + 2] = -MaxMesh.V(i)->p.y * ScaleConversion;
    }

    int VertIndexCount = 0;

    for (int i = 0; i < FaceCount; i++)
    {
	FaceCountArray[i] = MaxMesh.F(i)->deg;
	VertIndexCount += MaxMesh.F(i)->deg;
    }

    int* VertIndexArray = new int[VertIndexCount];
    int CurIndex = 0;

    for (int i = 0; i < FaceCount; i++)
    {
	// Smoothing group
	SmoothingGroupArray[i] = MaxMesh.F(i)->smGroup;
	MaterialIDArray[i] = MaxMesh.F(i)->material;

	if (!SingleMaterial)
	{
	    auto MatName = SubMatNames.find(MaxMesh.F(i)->material);

	    if (MatName != SubMatNames.end())
	    {
		FaceMaterialNames[i] = MatName->second;
	    }
	    else if (FaceMaterialNames.size() > 0)
	    {
		FaceMaterialNames[i] = "";
	    }
	}

	for (int v = (MaxMesh.F(i)->deg - 1), c = 0; v >= 0; v--, c++)
	{
	    VertIndexArray[CurIndex + c] = MaxMesh.F(i)->vtx[v];
	}
	CurIndex += MaxMesh.F(i)->deg;
    }

    AddNewPart(HEMAX_PARTTYPE_MESH, FaceCount, VertIndexCount, VertCount);

    HEMAX_AttributeInfo PointAttributeInfo = AddNewPointAttribute(VertCount, 3, HEMAX_POSITION_ATTRIBUTE);
    SendPointAttributeData(PointAttributeInfo, PointArray, VertIndexArray, FaceCountArray, FaceCount, VertIndexCount, VertCount, HEMAX_POSITION_ATTRIBUTE);

    HEMAX_AttributeInfo SmoothingGroupAttributeInfo = AddNewPrimitiveIntAttribute(FaceCount, 1, HEMAX_SMOOTHING_GROUP_ATTRIBUTE);
    SendIntAttributeData(HEMAX_SMOOTHING_GROUP_ATTRIBUTE, SmoothingGroupAttributeInfo, SmoothingGroupArray, FaceCount);

    HEMAX_AttributeInfo MaterialIDAttributeInfo = AddNewPrimitiveIntAttribute(FaceCount, 1, HEMAX_MATERIAL_ID_ATTRIBUTE);
    SendIntAttributeData(HEMAX_MATERIAL_ID_ATTRIBUTE, MaterialIDAttributeInfo, MaterialIDArray, FaceCount);

    if (SingleMaterial && SingleMaterialNameData.size() > 0)
    {
	HEMAX_AttributeInfo MaterialNamesAttrInfo = AddNewPrimitiveStringAttribute(FaceCount, 1, HEMAX_MATERIAL_PATH_ATTRIBUTE);
	SendStringAttributeData(HEMAX_MATERIAL_PATH_ATTRIBUTE, MaterialNamesAttrInfo, SingleMaterialNameData.data(), FaceCount);
    }
    else if (FaceMaterialNames.size() > 0)
    {
	HEMAX_AttributeInfo MaterialNamesAttrInfo = AddNewPrimitiveStringAttribute(FaceCount, 1, HEMAX_MATERIAL_PATH_ATTRIBUTE);
	SendStringAttributeData(HEMAX_MATERIAL_PATH_ATTRIBUTE, MaterialNamesAttrInfo, FaceMaterialNames.data(), FaceCount);
    }

    // Normals
    std::vector<float> NormalArray;

    MNNormalSpec* SpecifiedNormals = MaxMesh.GetSpecifiedNormals();

    if (!SpecifiedNormals || SpecifiedNormals->GetNumNormals() == 0)
    {
	std::unordered_map<int, int> NoSGTable;

	MNVert* Vertices = MaxMesh.v;

	Tab<MaxSDK::VertexNormal*> VertexNormals;
	Tab<Point3> FaceNormals;

	VertexNormals.SetCount(VertIndexCount);
	FaceNormals.SetCount(FaceCount);

	for (int i = 0; i < VertIndexCount; i++)
	{
	    VertexNormals[i] = new MaxSDK::VertexNormal();
	}

	for (int i = 0; i < FaceCount; i++)
	{
	    FaceNormals[i] = MaxMesh.GetFaceNormal(i, true);

	    for (int j = MaxMesh.F(i)->deg - 1; j >= 0; j--)
	    {
		VertexNormals[MaxMesh.F(i)->vtx[j]]->AddNormal(FaceNormals[i], MaxMesh.F(i)->smGroup);

		if (MaxMesh.F(i)->smGroup == 0)
		{
		    NoSGTable.insert({ MaxMesh.F(i)->vtx[j], 0 });
		}
	    }
	}

	for (int i = 0; i < FaceCount; i++)
	{
	    for (int j = MaxMesh.F(i)->deg - 1; j >= 0; j--)
	    {
		int Vert = MaxMesh.F(i)->vtx[j];

		Point3 NormalizedNormal;

		if (MaxMesh.F(i)->smGroup == 0)
		{
		    auto Search = NoSGTable.find({ Vert });
		    int Index = Search->second;

		    MaxSDK::VertexNormal* VNorm = VertexNormals[Vert];

		    while (Index > 0)
		    {
			VNorm = VNorm->next;
			--Index;
		    }

		    NormalizedNormal = Normalize(VNorm->norm);

		    ++(Search->second);
		}
		else
		{
		    NormalizedNormal = Normalize(VertexNormals[Vert]->GetNormal(MaxMesh.F(i)->smGroup));
		}

		NormalArray.push_back(NormalizedNormal.x);
		NormalArray.push_back(NormalizedNormal.z);
		NormalArray.push_back(-NormalizedNormal.y);
	    }
	}

	for (int z = 0; z < VertexNormals.Count(); z++)
	{
	    delete VertexNormals[z];
	}
    }
    else
    {
	int SpecNormalCount = SpecifiedNormals->GetNumNormals();
	int SpecFaceCount = SpecifiedNormals->GetNumFaces();

	for (int f = 0; f < SpecFaceCount; f++)
	{
	    for (int v = SpecifiedNormals->Face(f).GetDegree() - 1; v >= 0; v--)
	    {
		Point3 NormalizedNormal = Normalize(SpecifiedNormals->GetNormal(f, v));

		NormalArray.push_back(NormalizedNormal.x);
		NormalArray.push_back(NormalizedNormal.z);
		NormalArray.push_back(-NormalizedNormal.y);
	    }
	}
    }

    HEMAX_AttributeInfo NormalAttributeInfo = AddNewVertexAttribute(VertIndexCount, 3, HEMAX_NORMAL_ATTRIBUTE);
    SendFloatAttributeData(HEMAX_NORMAL_ATTRIBUTE, NormalAttributeInfo, &NormalArray.front(), VertIndexCount);

    // UVs
    std::vector<float> UVArray;

    for (int texMap = 1; texMap < MAX_MESHMAPS; texMap++)
    {
	MNMap* UVMap = MaxMesh.M(texMap);
	UVArray.clear();

	if (UVMap)
	{
	    UVVert* MapData = UVMap->v;

	    if (MapData)
	    {
		for (int f = 0; f < FaceCount; f++)
		{
		    for (int v = MaxMesh.F(f)->deg - 1; v >= 0; v--)
		    {
			UVArray.push_back(MapData[UVMap->F(f)->tv[v]].x);
			UVArray.push_back(MapData[UVMap->F(f)->tv[v]].y);
			UVArray.push_back(MapData[UVMap->F(f)->tv[v]].z);
		    }
		}

		std::string UVAttrName = (texMap == 1 ? HEMAX_UV_ATTRIBUTE : HEMAX_UV_ATTRIBUTE + std::to_string(texMap));

		HEMAX_AttributeInfo UVAttributeInfo = AddNewVertexAttribute(VertIndexCount, 3, UVAttrName);
		SendFloatAttributeData(UVAttrName, UVAttributeInfo, UVArray.data(), VertIndexCount);
	    }
	}
    }

    // Soft Selection
    float* SoftSelectionArray = nullptr;

    if (MaxMesh.vDataSupport(VDATA_SELECT))
    {
	SoftSelectionArray = new float[VertCount];
	float* Data = MaxMesh.getVSelectionWeights();

	for (int i = 0; i < VertCount; i++)
	{
	    SoftSelectionArray[i] = Data[i];
	}
    }

    float* CdArray = nullptr;
    HEMAX_AttributeOwner CdOwner = HEMAX_ATTRIBUTEOWNER_INVALID;

    MNMap* ColorMap = MaxMesh.M(HEMAX_MAPPING_CHANNEL_COLOR);
    if (ColorMap && ColorMap->numv > 0)
    {
	if (ColorMap->numv == VertCount)
	{
	    CdOwner = HEMAX_ATTRIBUTEOWNER_POINT;
	    CdArray = new float[VertCount * 3];

	    UVVert* MapData = ColorMap->v;

	    for (int c = 0; c < VertCount; c++)
	    {
		CdArray[(c * 3)] = MapData[c].x;
		CdArray[(c * 3) + 1] = MapData[c].y;
		CdArray[(c * 3) + 2] = MapData[c].z;
	    }
	}
        else if (ColorMap->numv <= VertIndexCount)
	{
	    CdOwner = HEMAX_ATTRIBUTEOWNER_VERTEX;
	    CdArray = new float[VertIndexCount * 3];

	    UVVert* MapData = ColorMap->v;

	    int CdVertIndex = 0;

	    for (int f = 0; f < FaceCount; f++)
	    {
		for (int v = MaxMesh.F(f)->deg - 1; v >= 0; v--)
		{
		    CdArray[CdVertIndex] = MapData[ColorMap->F(f)->tv[v]].x;
		    CdArray[CdVertIndex + 1] = MapData[ColorMap->F(f)->tv[v]].y;
		    CdArray[CdVertIndex + 2] = MapData[ColorMap->F(f)->tv[v]].z;

		    CdVertIndex += 3;
		}
	    }
	}
    }

    float* AlphaArray = nullptr;
    HEMAX_AttributeOwner AlphaOwner = HEMAX_ATTRIBUTEOWNER_INVALID;

    MNMap* AlphaMap = MaxMesh.M(HEMAX_MAPPING_CHANNEL_ALPHA);
    if (AlphaMap && AlphaMap->numv > 0)
    {
	if (AlphaMap->numv == VertCount)
	{
	    AlphaOwner = HEMAX_ATTRIBUTEOWNER_POINT;
	    AlphaArray = new float[VertCount];

	    UVVert* MapData = AlphaMap->v;

	    for (int c = 0; c < VertCount; c++)
	    {
		float ConvertedAlpha = (0.2126f * MapData[c].x) + (0.7152f * MapData[c].y) + (0.0722f * MapData[c].z);
		AlphaArray[c] = ConvertedAlpha;
	    }
	}
        else if (AlphaMap->numv <= VertIndexCount)
	{
	    AlphaOwner = HEMAX_ATTRIBUTEOWNER_VERTEX;
	    AlphaArray = new float[VertIndexCount];

	    UVVert* MapData = AlphaMap->v;

	    int AlphaVertIndex = 0;

	    for (int f = 0; f < FaceCount; f++)
	    {
		for (int v = MaxMesh.F(f)->deg - 1; v >= 0; v--)
		{
		    float ConvertedAlpha = (0.2126f * MapData[AlphaMap->F(f)->tv[v]].x) + (0.7152f * MapData[AlphaMap->F(f)->tv[v]].y) + (0.0722f * MapData[AlphaMap->F(f)->tv[v]].z);

		    AlphaArray[AlphaVertIndex] = ConvertedAlpha;

		    AlphaVertIndex++;
		}
	    }
	}
    }

    float* IlluminationArray = nullptr;
    HEMAX_AttributeOwner IlluminationOwner = HEMAX_ATTRIBUTEOWNER_INVALID;

    MNMap* IlluminationMap = MaxMesh.M(HEMAX_MAPPING_CHANNEL_ILLUMINATION);
    if (IlluminationMap && IlluminationMap->numv > 0)
    {
	if (IlluminationMap->numv == VertCount)
	{
	    IlluminationOwner = HEMAX_ATTRIBUTEOWNER_POINT;
	    IlluminationArray = new float[VertCount * 3];

	    UVVert* MapData = IlluminationMap->v;

	    for (int c = 0; c < VertCount; c++)
	    {
		IlluminationArray[(c * 3)] = MapData[c].x;
		IlluminationArray[(c * 3) + 1] = MapData[c].y;
		IlluminationArray[(c * 3) + 2] = MapData[c].z;
	    }
	}
        else if (IlluminationMap->numv <= VertIndexCount)
	{
	    IlluminationOwner = HEMAX_ATTRIBUTEOWNER_VERTEX;
	    IlluminationArray = new float[VertIndexCount * 3];

	    UVVert* MapData = IlluminationMap->v;

	    int CurrentIndex = 0;

	    for (int f = 0; f < FaceCount; f++)
	    {
		for (int v = MaxMesh.F(f)->deg - 1; v >= 0; v--)
		{
		    IlluminationArray[CurrentIndex] = MapData[IlluminationMap->F(f)->tv[v]].x;
		    IlluminationArray[CurrentIndex + 1] = MapData[IlluminationMap->F(f)->tv[v]].y;
		    IlluminationArray[CurrentIndex + 2] = MapData[IlluminationMap->F(f)->tv[v]].z;

		    CurrentIndex += 3;
		}
	    }
	}
    }

    if (IlluminationArray)
    {
	if (IlluminationOwner == HEMAX_ATTRIBUTEOWNER_VERTEX)
	{
	    HEMAX_AttributeInfo IlluminationAttributeInfo = AddNewVertexAttribute(VertIndexCount, 3, HEMAX_ILLUMINATION_ATTRIBUTE);
	    SendFloatAttributeData(HEMAX_ILLUMINATION_ATTRIBUTE, IlluminationAttributeInfo, IlluminationArray, VertIndexCount);
	}
	else if (IlluminationOwner == HEMAX_ATTRIBUTEOWNER_POINT)
	{
	    HEMAX_AttributeInfo IlluminationAttributeInfo = AddNewPointAttribute(VertCount, 3, HEMAX_ILLUMINATION_ATTRIBUTE);
	    SendFloatAttributeData(HEMAX_ILLUMINATION_ATTRIBUTE, IlluminationAttributeInfo, IlluminationArray, VertCount);
	}

	delete[] IlluminationArray;
    }

    if (AlphaArray)
    {
	if (AlphaOwner == HEMAX_ATTRIBUTEOWNER_VERTEX)
	{
	    HEMAX_AttributeInfo AlphaAttributeInfo = AddNewVertexAttribute(VertIndexCount, 1, HEMAX_ALPHA_ATTRIBUTE);
	    SendFloatAttributeData(HEMAX_ALPHA_ATTRIBUTE, AlphaAttributeInfo, AlphaArray, VertIndexCount);
	}
	else if (AlphaOwner == HEMAX_ATTRIBUTEOWNER_POINT)
	{
	    HEMAX_AttributeInfo AlphaAttributeInfo = AddNewPointAttribute(VertCount, 1, HEMAX_ALPHA_ATTRIBUTE);
	    SendFloatAttributeData(HEMAX_ALPHA_ATTRIBUTE, AlphaAttributeInfo, AlphaArray, VertCount);
	}

	delete[] AlphaArray;
    }

    if (CdArray)
    {
	if (CdOwner == HEMAX_ATTRIBUTEOWNER_VERTEX)
	{
	    HEMAX_AttributeInfo CdAttributeInfo = AddNewVertexAttribute(VertIndexCount, 3, HEMAX_COLOR_ATTRIBUTE);
	    SendFloatAttributeData(HEMAX_COLOR_ATTRIBUTE, CdAttributeInfo, CdArray, VertIndexCount);
	}
	else if (CdOwner == HEMAX_ATTRIBUTEOWNER_POINT)
	{
	    HEMAX_AttributeInfo CdAttributeInfo = AddNewPointAttribute(VertCount, 3, HEMAX_COLOR_ATTRIBUTE);
	    SendFloatAttributeData(HEMAX_COLOR_ATTRIBUTE, CdAttributeInfo, CdArray, VertCount);
	}

	delete[] CdArray;
    }

    if (SoftSelectionArray)
    {
	HEMAX_AttributeInfo SoftSelectAttributeInfo = AddNewPointAttribute(VertCount, 1, HEMAX_SOFT_SELECTION_ATTRIBUTE);
	SendFloatAttributeData(HEMAX_SOFT_SELECTION_ATTRIBUTE, SoftSelectAttributeInfo, SoftSelectionArray, VertCount);

	delete[] SoftSelectionArray;
    }

    // Transform Detail Attributes

    if (MaxNode)
    {
	HEMAX_MaxTransform NodeTM = HEMAX_Utilities::BuildMaxTransformFromINode(MaxNode);
	HAPI_Transform HAPITM = HEMAX_Utilities::MaxTransformToHAPITransform(NodeTM);
	EulerTM = HEMAX_Utilities::MaxTransformToHAPITransformEuler(NodeTM);
	Matrix3 RawNodeTM = HEMAX_Utilities::GetINodeTransformationMatrix(MaxNode);

	HEMAX_AttributeInfo TranslateAttrInfo = AddNewDetailFloatAttribute(1, 3, HEMAX_TRANSLATE_ATTR);
	HEMAX_AttributeInfo RotateAttrInfo = AddNewDetailFloatAttribute(1, 3, HEMAX_ROTATE_ATTR);
	HEMAX_AttributeInfo ScaleAttrInfo = AddNewDetailFloatAttribute(1, 3, HEMAX_SCALE_ATTR);
	HEMAX_AttributeInfo QuatAttrInfo = AddNewDetailFloatAttribute(1, 4, HEMAX_QUATERNION_ATTR);
	HEMAX_AttributeInfo WorldSpaceAttrInfo = AddNewDetailFloatAttribute(1, 12, HEMAX_MAX_RAW_TM_WORLD);
	HEMAX_AttributeInfo LocalSpaceAttrInfo = AddNewDetailFloatAttribute(1, 12, HEMAX_MAX_RAW_TM_LOCAL);

	SendFloatAttributeData(HEMAX_TRANSLATE_ATTR, TranslateAttrInfo, EulerTM.position, 1);
	SendFloatAttributeData(HEMAX_ROTATE_ATTR, RotateAttrInfo, EulerTM.rotationEuler, 1);
	SendFloatAttributeData(HEMAX_SCALE_ATTR, ScaleAttrInfo, EulerTM.scale, 1);
	SendFloatAttributeData(HEMAX_QUATERNION_ATTR, QuatAttrInfo, HAPITM.rotationQuaternion, 1);

	std::vector<float> WorldSpaceTM;
	HEMAX_Utilities::Matrix3ToFlatArray(RawNodeTM, WorldSpaceTM);

	SendFloatAttributeData(HEMAX_MAX_RAW_TM_WORLD, WorldSpaceAttrInfo, &WorldSpaceTM.front(), 1);

	Matrix3 NodeLocalTM = HEMAX_Utilities::GetINodeLocalTransformationMatrix(MaxNode);
	std::vector<float> LocalSpaceTM;
	HEMAX_Utilities::Matrix3ToFlatArray(NodeLocalTM, LocalSpaceTM);

	SendFloatAttributeData(HEMAX_MAX_RAW_TM_LOCAL, LocalSpaceAttrInfo, &LocalSpaceTM.front(), 1);
    }

    Node->SetParentTransform(NodeTransform);
    FinalizeInputGeometry();

    delete[] VertIndexArray;
    delete[] MaterialIDArray;
    delete[] SmoothingGroupArray;
    delete[] FaceCountArray;
    delete[] PointArray;
}
