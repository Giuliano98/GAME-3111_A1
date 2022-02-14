/** @file Week4-5-ShapePractice.cpp
 *  @brief Shape Practice.
 *
 *  Place all of the scene geometry in one big vertex and index buffer. 
 * Then use the DrawIndexedInstanced method to draw one object at a time ((as the
 * world matrix needs to be changed between objects)
 *
 *   Controls:
 *   Hold down '1' key to view scene in wireframe mode.
 *   Hold the left mouse button down and move the mouse to rotate.
 *   Hold the right mouse button down and move the mouse to zoom in and out.
 *
 *  @author Hooman Salamat
 */

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

class ShapesApp : public D3DApp
{
public:
	ShapesApp(HINSTANCE hInstance);
	ShapesApp(const ShapesApp& rhs) = delete;
	ShapesApp& operator=(const ShapesApp& rhs) = delete;
	~ShapesApp();

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void BuildDescriptorHeaps();
	void BuildConstantBufferViews();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

	PassConstants mMainPassCB;

	UINT mPassCbvOffset = 0;

	bool mIsWireframe = false;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f * XM_PI;
	float mPhi = 0.2f * XM_PI;
	float mRadius = 15.0f;

	POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		ShapesApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

ShapesApp::ShapesApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

ShapesApp::~ShapesApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool ShapesApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescriptorHeaps();
	BuildConstantBufferViews();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();

	return true;
}

void ShapesApp::OnResize()
{
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void ShapesApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);
}

void ShapesApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	if (mIsWireframe)
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
	}
	else
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));
	}

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	int passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
	auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	passCbvHandle.Offset(passCbvIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += dx;
		mPhi += dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.2 unit in the scene.
		float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void ShapesApp::OnKeyboardInput(const GameTimer& gt)
{
	if (GetAsyncKeyState('1') & 0x8000)
		mIsWireframe = true;
	else
		mIsWireframe = false;
}

void ShapesApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void ShapesApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void ShapesApp::BuildDescriptorHeaps()
{
	UINT objCount = (UINT)mOpaqueRitems.size();

	// Need a CBV descriptor for each object for each frame resource,
	// +1 for the perPass CBV for each frame resource.
	UINT numDescriptors = (objCount + 1) * gNumFrameResources;

	// Save an offset to the start of the pass CBVs.  These are the last 3 descriptors.
	mPassCbvOffset = objCount * gNumFrameResources;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = numDescriptors;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,
		IID_PPV_ARGS(&mCbvHeap)));
}

void ShapesApp::BuildConstantBufferViews()
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	UINT objCount = (UINT)mOpaqueRitems.size();

	// Need a CBV descriptor for each object for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();
		for (UINT i = 0; i < objCount; ++i)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();

			// Offset to the ith object constant buffer in the buffer.
			cbAddress += i * objCBByteSize;

			// Offset to the object cbv in the descriptor heap.
			int heapIndex = frameIndex * objCount + i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
			handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress;
			cbvDesc.SizeInBytes = objCBByteSize;

			md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// Last three descriptors are the pass CBVs for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto passCB = mFrameResources[frameIndex]->PassCB->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();

		// Offset to the pass cbv in the descriptor heap.
		int heapIndex = mPassCbvOffset + frameIndex;
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = cbAddress;
		cbvDesc.SizeInBytes = passCBByteSize;

		md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
	}
}

void ShapesApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE cbvTable0;
	cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE cbvTable1;
	cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	// Create root CBVs.
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
	slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void ShapesApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\VS.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\PS.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void ShapesApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 1);

	//TODO: Step1
	GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1.0f, 1.0f, 1.0f);
	GeometryGenerator::MeshData triPrism = geoGen.CreateTriangularPrism(1.0f, 1.0f);
	GeometryGenerator::MeshData pentaPrism = geoGen.CreatePentaPrism(2.0f, 1.0f);
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.0f, 1.0f);
	GeometryGenerator::MeshData cone = geoGen.CreateCone(3.0f, 2.0f, 16);
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(2.5f, 0.6f);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(2.5f, 1.0f, 1.0f, 20, 20);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(40.0f, 35.0f, 60, 40);

	/*
	Step1
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);*/

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	//TODO: Step2 
	UINT wedgeVertexOffset = (UINT)box.Vertices.size();
	UINT triPrismVertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();
	UINT pentaPrismVertexOffset = triPrismVertexOffset + (UINT)triPrism.Vertices.size();
	UINT pyramidVertexOffset = pentaPrismVertexOffset + (UINT)pentaPrism.Vertices.size();
	UINT coneVertexOffset = pyramidVertexOffset + (UINT)pyramid.Vertices.size();
	UINT diamondVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();
	UINT cylinderVertexOffset = diamondVertexOffset + (UINT)diamond.Vertices.size();
	UINT gridVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();

	/*
	Step2
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();*/

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	//TODO: Step3
	UINT wedgeIndexOffset = (UINT)box.Indices32.size();
	UINT triPrismIndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size();
	UINT pentaPrismIndexOffset = triPrismIndexOffset + (UINT)triPrism.Indices32.size();
	UINT pyramidIndexOffset = pentaPrismIndexOffset + (UINT)pentaPrism.Indices32.size();
	UINT coneIndexOffset = pyramidIndexOffset + (UINT)pyramid.Indices32.size();
	UINT diamondIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
	UINT cylinderIndexOffset = diamondIndexOffset + (UINT)diamond.Indices32.size();
	UINT gridIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();

	/*
	Step3
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();*/

	// Define the SubmeshGeometry that cover different 
	// regions of the vertex/index buffers.

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	//TODO: Step4
	SubmeshGeometry wedgeSubmesh;
	wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size();
	wedgeSubmesh.StartIndexLocation = wedgeIndexOffset;
	wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;

	SubmeshGeometry triPrismSubmesh;
	triPrismSubmesh.IndexCount = (UINT)triPrism.Indices32.size();
	triPrismSubmesh.StartIndexLocation = triPrismIndexOffset;
	triPrismSubmesh.BaseVertexLocation = triPrismVertexOffset;

	SubmeshGeometry pentaPrismSubmesh;
	pentaPrismSubmesh.IndexCount = (UINT)pentaPrism.Indices32.size();
	pentaPrismSubmesh.StartIndexLocation = pentaPrismIndexOffset;
	pentaPrismSubmesh.BaseVertexLocation = pentaPrismVertexOffset;

	SubmeshGeometry pyramidSubmesh;
	pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
	pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
	pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

	SubmeshGeometry coneSubmesh;
	coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
	coneSubmesh.StartIndexLocation = coneIndexOffset;
	coneSubmesh.BaseVertexLocation = coneVertexOffset;

	SubmeshGeometry diamondSubmesh;
	diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();
	diamondSubmesh.StartIndexLocation = diamondIndexOffset;
	diamondSubmesh.BaseVertexLocation = diamondVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	//step4
	//SubmeshGeometry gridSubmesh;
	//gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	//gridSubmesh.StartIndexLocation = gridIndexOffset;
	//gridSubmesh.BaseVertexLocation = gridVertexOffset;

	//SubmeshGeometry sphereSubmesh;
	//sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	//sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	//sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	//SubmeshGeometry cylinderSubmesh;
	//cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	//cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	//cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	//TODO: Step5 
	auto totalVertexCount = 
		box.Vertices.size() + 
		wedge.Vertices.size() +
		triPrism.Vertices.size() +
		pentaPrism.Vertices.size() +
		pyramid.Vertices.size() +
		cone.Vertices.size() +
		diamond.Vertices.size() +
		cylinder.Vertices.size() +
		grid.Vertices.size();

	    //step5
		//box.Vertices.size() +
		//grid.Vertices.size() +
		//sphere.Vertices.size() +
		//cylinder.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkOrange);
	}

	//TODO: Step6  
	for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = wedge.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);
	}

	for (size_t i = 0; i < triPrism.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = triPrism.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::AliceBlue);
	}

	for (size_t i = 0; i < pentaPrism.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pentaPrism.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Black);
	}

	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Brown);
	}

	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Coral);
	}

	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkViolet);
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Navy);
	}

	//step6
	//for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	//{
	//	vertices[k].Pos = grid.Vertices[i].Position;
	//	vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);
	//}

	//for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	//{
	//	vertices[k].Pos = sphere.Vertices[i].Position;
	//	vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson);
	//}

	//for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	//{
	//	vertices[k].Pos = cylinder.Vertices[i].Position;
	//	vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);
	//}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	//TODO: Step7 
	indices.insert(indices.end(), std::begin(wedge.GetIndices16()), std::end(wedge.GetIndices16()));
	indices.insert(indices.end(), std::begin(triPrism.GetIndices16()), std::end(triPrism.GetIndices16()));
	indices.insert(indices.end(), std::begin(pentaPrism.GetIndices16()), std::end(pentaPrism.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));
	indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));
	indices.insert(indices.end(), std::begin(diamond.GetIndices16()), std::end(diamond.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));

	//step7
	//indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	//indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	//indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	//TODO: Step8 
	geo->DrawArgs["wedge"] = wedgeSubmesh;
	geo->DrawArgs["triPrism"] = triPrismSubmesh;
	geo->DrawArgs["pentaPrism"] = pentaPrismSubmesh;
	geo->DrawArgs["pyramid"] = pyramidSubmesh;
	geo->DrawArgs["cone"] = coneSubmesh;
	geo->DrawArgs["diamond"] = diamondSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;

	//step8
	//geo->DrawArgs["grid"] = gridSubmesh;
	//geo->DrawArgs["sphere"] = sphereSubmesh;
	//geo->DrawArgs["cylinder"] = cylinderSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void ShapesApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));


	//
	// PSO for opaque wireframe objects.
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
	opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));
}

void ShapesApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size()));
	}
}

void ShapesApp::BuildRenderItems()
{
	// Castle Data =====================
	float castleWidth = 15.0f;
	float castleDepth = 20.0f;

	float castleWidth2 = castleWidth/2;
	float castleDepth2 = castleDepth/2;
	// =================================


	// Walls World Data ====================================================
	// <<  O = S * R * T  >>
	int numWalls = 3;
	float heightWall = 5.0f;
	float heightWall2 = heightWall / 2;
	float depthWall = 1.5f;
	XMMATRIX scaleWall = XMMatrixScaling(castleDepth, heightWall, depthWall);
	
	XMMATRIX leftWallWorld  = scaleWall * XMMatrixRotationY(90 * PI / 180) * XMMatrixTranslation(-castleWidth2, heightWall2, 0.0f);
	XMMATRIX rightWallWorld = scaleWall * XMMatrixRotationY(90 * PI / 180) * XMMatrixTranslation(+castleWidth2, heightWall2, 0.0f);
	XMMATRIX backWallWorld  = XMMatrixScaling(castleWidth, heightWall, depthWall) * XMMatrixTranslation(0.0f, heightWall / 2.0f, castleDepth2);

	std::vector<XMMATRIX> vectorWallsWorld;
	vectorWallsWorld.push_back(leftWallWorld);
	vectorWallsWorld.push_back(rightWallWorld);
	vectorWallsWorld.push_back(backWallWorld);


	int numShortWals = 2;
	XMMATRIX scaleShortWall = XMMatrixScaling(castleWidth / 4, heightWall, depthWall);

	XMMATRIX leftShortWall  = scaleShortWall * XMMatrixTranslation(-castleWidth * 5 / 16, +heightWall2, -castleDepth2);
	XMMATRIX rightShortWall = scaleShortWall * XMMatrixTranslation(+castleWidth * 5 / 16, +heightWall2, -castleDepth2);

	std::vector<XMMATRIX> vectorShortWallsWorld;
	vectorShortWallsWorld.push_back(leftShortWall);
	vectorShortWallsWorld.push_back(rightShortWall);
	// =======================================================================

	// Wall Spikes Data ==================================================
	XMMATRIX scaleWallSpikes      = XMMatrixScaling(depthWall, 2.5f, castleDepth / 7);
	XMMATRIX tranfLeftWallSpikes  = XMMatrixTranslation(-castleWidth2, heightWall, 0.0f);
	XMMATRIX tranfRightWallSpikes = XMMatrixTranslation(+castleWidth2, heightWall, 0.0f);
	// ===================================================================


	// Door World Data ================================
	int numWedgeDoor = 2;
	XMMATRIX scaleWedgeDoor = XMMatrixScaling(depthWall, castleWidth / 6, heightWall);


	XMMATRIX leftDoorRota  = XMMatrixRotationRollPitchYaw(-90.0f * PI / 180, -90.0f * PI / 180, 0.0f);
	XMMATRIX rightDoorRota = XMMatrixRotationRollPitchYaw(-90.0f * PI / 180, 90.0f * PI / 180, 0.0f);

	std::vector<XMMATRIX> vectorDoorRota;
	vectorDoorRota.push_back(leftDoorRota);
	vectorDoorRota.push_back(rightDoorRota);

	XMMATRIX leftDoorTransf  = XMMatrixTranslation(-castleWidth * 3 / 16, heightWall, -(castleDepth2 + depthWall / 2));
	XMMATRIX rightDoorTransf = XMMatrixTranslation(+castleWidth * 3 / 16, heightWall, -(castleDepth2 - depthWall / 2));

	std::vector<XMMATRIX> vectorDoorTransf;
	vectorDoorTransf.push_back(leftDoorTransf);
	vectorDoorTransf.push_back(rightDoorTransf);
	// ================================================


	// Towers World Data ==================================================================
	int numTowers = 4;
	float towerBaseHeight = heightWall * 1.3f;
	float towerBaseHeight2 = towerBaseHeight / 2;
	XMMATRIX baseCylWorld = XMMatrixScaling(1.0f, towerBaseHeight, 1.0f);

	XMMATRIX leftBottomCylWorld  = baseCylWorld * XMMatrixTranslation(-castleWidth2, towerBaseHeight2, -castleDepth2);
	XMMATRIX leftTopCylWorld     = baseCylWorld * XMMatrixTranslation(-castleWidth2, towerBaseHeight2, +castleDepth2);
	XMMATRIX rightBottomCylWorld = baseCylWorld * XMMatrixTranslation(+castleWidth2, towerBaseHeight2, -castleDepth2);
	XMMATRIX rightTopCylWorld    = baseCylWorld * XMMatrixTranslation(+castleWidth2, towerBaseHeight2, +castleDepth2);

	std::vector<XMMATRIX> vectorCylsWorld;
	vectorCylsWorld.push_back(leftBottomCylWorld);
	vectorCylsWorld.push_back(leftTopCylWorld);
	vectorCylsWorld.push_back(rightBottomCylWorld);
	vectorCylsWorld.push_back(rightTopCylWorld);


	XMMATRIX leftBottomConeWorld  = XMMatrixTranslation(-castleWidth2, towerBaseHeight, -castleDepth2);
	XMMATRIX leftTopConeWorld     = XMMatrixTranslation(-castleWidth2, towerBaseHeight, +castleDepth2);
	XMMATRIX rightBottomConeWorld = XMMatrixTranslation(+castleWidth2, towerBaseHeight, -castleDepth2);
	XMMATRIX rightTopConeWorld    = XMMatrixTranslation(+castleWidth2, towerBaseHeight, +castleDepth2);

	std::vector<XMMATRIX> vectorConesWorld;
	vectorConesWorld.push_back(leftBottomConeWorld);
	vectorConesWorld.push_back(leftTopConeWorld);
	vectorConesWorld.push_back(rightBottomConeWorld);
	vectorConesWorld.push_back(rightTopConeWorld);
	// =====================================================================================


	

	UINT objCBIndex = 0; // Collect all objCBIndex

	// Grid
	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->ObjCBIndex = objCBIndex++;
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));
	// ------------------

	// Walls <<Left, right, back>>
	for (int i = 0; i < numWalls; ++i)
	{
		auto wallRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&wallRitem->World, vectorWallsWorld[i]);
		wallRitem->ObjCBIndex = objCBIndex++;
		wallRitem->Geo = mGeometries["shapeGeo"].get();
		wallRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wallRitem->IndexCount = wallRitem->Geo->DrawArgs["box"].IndexCount;
		wallRitem->StartIndexLocation = wallRitem->Geo->DrawArgs["box"].StartIndexLocation;
		wallRitem->BaseVertexLocation = wallRitem->Geo->DrawArgs["box"].BaseVertexLocation;
		mAllRitems.push_back(std::move(wallRitem));
	}
	// --------------------------------------

	// Short Walls <<FrontLeft, FrontRight>>
	for (int i = 0; i < numShortWals; ++i)
	{
		auto shortWallRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&shortWallRitem->World, vectorShortWallsWorld[i]);
		shortWallRitem->ObjCBIndex = objCBIndex++;
		shortWallRitem->Geo = mGeometries["shapeGeo"].get();
		shortWallRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		shortWallRitem->IndexCount = shortWallRitem->Geo->DrawArgs["box"].IndexCount;
		shortWallRitem->StartIndexLocation = shortWallRitem->Geo->DrawArgs["box"].StartIndexLocation;
		shortWallRitem->BaseVertexLocation = shortWallRitem->Geo->DrawArgs["box"].BaseVertexLocation;
		mAllRitems.push_back(std::move(shortWallRitem));
	}
	// -------------------------------------

	// Door
	for (int i = 0; i < numWedgeDoor; ++i)
	{
		auto wedgeDoorRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&wedgeDoorRitem->World, scaleWedgeDoor * vectorDoorRota[i] * vectorDoorTransf[i]);
		wedgeDoorRitem->ObjCBIndex = objCBIndex++;
		wedgeDoorRitem->Geo = mGeometries["shapeGeo"].get();
		wedgeDoorRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wedgeDoorRitem->IndexCount = wedgeDoorRitem->Geo->DrawArgs["wedge"].IndexCount;
		wedgeDoorRitem->StartIndexLocation = wedgeDoorRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
		wedgeDoorRitem->BaseVertexLocation = wedgeDoorRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mAllRitems.push_back(std::move(wedgeDoorRitem));
	}
	// ---------------------------

	// Door's Top
	auto triPrismRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&triPrismRitem->World, XMMatrixScaling(1.5f, depthWall, 4.0f) *
												XMMatrixRotationRollPitchYaw (0.0f * PI / 180, 90.0f * PI / 180, 90.0f * PI / 180) *
												XMMatrixTranslation(0.0f, +heightWall + 0.75f, -castleDepth2));
	triPrismRitem->ObjCBIndex = objCBIndex++;
	triPrismRitem->Geo = mGeometries["shapeGeo"].get();
	triPrismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triPrismRitem->IndexCount = triPrismRitem->Geo->DrawArgs["triPrism"].IndexCount;
	triPrismRitem->StartIndexLocation = triPrismRitem->Geo->DrawArgs["triPrism"].StartIndexLocation;
	triPrismRitem->BaseVertexLocation = triPrismRitem->Geo->DrawArgs["triPrism"].BaseVertexLocation;
	mAllRitems.push_back(std::move(triPrismRitem));
	// ---------------------------

	// Towers
	for (int i = 0; i < numTowers; ++i)
	{
		auto cylRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&cylRitem->World, vectorCylsWorld[i]);
		cylRitem->ObjCBIndex = objCBIndex++;
		cylRitem->Geo = mGeometries["shapeGeo"].get();
		cylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		cylRitem->IndexCount = cylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		cylRitem->StartIndexLocation = cylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		cylRitem->BaseVertexLocation = cylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
		mAllRitems.push_back(std::move(cylRitem));

		auto coneRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&coneRitem->World, vectorConesWorld[i]);
		coneRitem->ObjCBIndex = objCBIndex++;
		coneRitem->Geo = mGeometries["shapeGeo"].get();
		coneRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		coneRitem->IndexCount = coneRitem->Geo->DrawArgs["cone"].IndexCount;
		coneRitem->StartIndexLocation = coneRitem->Geo->DrawArgs["cone"].StartIndexLocation;
		coneRitem->BaseVertexLocation = coneRitem->Geo->DrawArgs["cone"].BaseVertexLocation;
		mAllRitems.push_back(std::move(coneRitem));
	}
	// -------------------------------

	// Castle Center Base
	auto pentaPrismRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pentaPrismRitem->World, XMMatrixScaling(2.5f, 0.5f, 2.5f) *
												XMMatrixRotationRollPitchYaw(0.0f, 0.0f, 0.0f) *
												XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	pentaPrismRitem->ObjCBIndex = objCBIndex++;
	pentaPrismRitem->Geo = mGeometries["shapeGeo"].get();
	pentaPrismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pentaPrismRitem->IndexCount = pentaPrismRitem->Geo->DrawArgs["pentaPrism"].IndexCount;
	pentaPrismRitem->StartIndexLocation = pentaPrismRitem->Geo->DrawArgs["pentaPrism"].StartIndexLocation;
	pentaPrismRitem->BaseVertexLocation = pentaPrismRitem->Geo->DrawArgs["pentaPrism"].BaseVertexLocation;
	mAllRitems.push_back(std::move(pentaPrismRitem));
	// ------------------------------

	// Castle Center Diamond
	auto diamondRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamondRitem->World, XMMatrixScaling(1.0f, 1.5f, 1.0f) *
												XMMatrixRotationRollPitchYaw(0.0f, 0.0f, 0.0f) *
												XMMatrixTranslation(0.0f, 3.5f, 0.0f));
	diamondRitem->ObjCBIndex = objCBIndex++;
	diamondRitem->Geo = mGeometries["shapeGeo"].get();
	diamondRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamondRitem->IndexCount = diamondRitem->Geo->DrawArgs["diamond"].IndexCount;
	diamondRitem->StartIndexLocation = diamondRitem->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamondRitem->BaseVertexLocation = diamondRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(diamondRitem));
	// ------------------------------

	// spikes
	auto pyramidRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramidRitem->World, scaleWallSpikes *
		XMMatrixRotationRollPitchYaw(0.0f, 0.0f, 0.0f) *
		tranfLeftWallSpikes);
	pyramidRitem->ObjCBIndex = objCBIndex++;
	pyramidRitem->Geo = mGeometries["shapeGeo"].get();
	pyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(pyramidRitem));
	for (int i = 0; i < 2; ++i)
	{
		// left top spikes
		auto leftTopSpikeRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&leftTopSpikeRitem->World, scaleWallSpikes*
			XMMatrixRotationRollPitchYaw(0.0f, 0.0f, 0.0f)*
			XMMatrixTranslation(-castleWidth2, heightWall, (i + 1) * (castleDepth / 7)));
		leftTopSpikeRitem->ObjCBIndex = objCBIndex++;
		leftTopSpikeRitem->Geo = mGeometries["shapeGeo"].get();
		leftTopSpikeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftTopSpikeRitem->IndexCount = leftTopSpikeRitem->Geo->DrawArgs["pyramid"].IndexCount;
		leftTopSpikeRitem->StartIndexLocation = leftTopSpikeRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
		leftTopSpikeRitem->BaseVertexLocation = leftTopSpikeRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
		mAllRitems.push_back(std::move(leftTopSpikeRitem));

		// left bottom spikes
		auto leftBottomSpikeRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&leftBottomSpikeRitem->World, scaleWallSpikes*
			XMMatrixRotationRollPitchYaw(0.0f, 0.0f, 0.0f)*
			XMMatrixTranslation(-castleWidth2, heightWall, -(i + 1) * (castleDepth / 7)));
		leftBottomSpikeRitem->ObjCBIndex = objCBIndex++;
		leftBottomSpikeRitem->Geo = mGeometries["shapeGeo"].get();
		leftBottomSpikeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftBottomSpikeRitem->IndexCount = leftBottomSpikeRitem->Geo->DrawArgs["pyramid"].IndexCount;
		leftBottomSpikeRitem->StartIndexLocation = leftBottomSpikeRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
		leftBottomSpikeRitem->BaseVertexLocation = leftBottomSpikeRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
		mAllRitems.push_back(std::move(leftBottomSpikeRitem));

		// right top spikes
		auto rightTopSpikeRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&rightTopSpikeRitem->World, scaleWallSpikes *
			XMMatrixRotationRollPitchYaw(0.0f, 0.0f, 0.0f) *
			XMMatrixTranslation(castleWidth2, heightWall, (i + 1) * (castleDepth / 7)));
		rightTopSpikeRitem->ObjCBIndex = objCBIndex++;
		rightTopSpikeRitem->Geo = mGeometries["shapeGeo"].get();
		rightTopSpikeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightTopSpikeRitem->IndexCount = rightTopSpikeRitem->Geo->DrawArgs["pyramid"].IndexCount;
		rightTopSpikeRitem->StartIndexLocation = rightTopSpikeRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
		rightTopSpikeRitem->BaseVertexLocation = rightTopSpikeRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
		mAllRitems.push_back(std::move(rightTopSpikeRitem));

		// right bottom spikes
		auto rightBottomRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&rightBottomRitem->World, scaleWallSpikes*
			XMMatrixRotationRollPitchYaw(0.0f, 0.0f, 0.0f)*
			XMMatrixTranslation(castleWidth2, heightWall, -(i + 1) * (castleDepth / 7)));
		rightBottomRitem->ObjCBIndex = objCBIndex++;
		rightBottomRitem->Geo = mGeometries["shapeGeo"].get();
		rightBottomRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightBottomRitem->IndexCount = rightBottomRitem->Geo->DrawArgs["pyramid"].IndexCount;
		rightBottomRitem->StartIndexLocation = rightBottomRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
		rightBottomRitem->BaseVertexLocation = rightBottomRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
		mAllRitems.push_back(std::move(rightBottomRitem));
	}
	auto rightpyramidRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rightpyramidRitem->World, scaleWallSpikes*
		XMMatrixRotationRollPitchYaw(0.0f, 0.0f, 0.0f)*
		tranfRightWallSpikes);
	rightpyramidRitem->ObjCBIndex = objCBIndex++;
	rightpyramidRitem->Geo = mGeometries["shapeGeo"].get();
	rightpyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rightpyramidRitem->IndexCount = rightpyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
	rightpyramidRitem->StartIndexLocation = rightpyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	rightpyramidRitem->BaseVertexLocation = rightpyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rightpyramidRitem));

	// --------------------------------------------------------------------

	//TODO: Step9 
	//auto wedgeRitem = std::make_unique<RenderItem>();
	////wedgeRitem->World = MathHelper::Identity4x4();
	//XMStoreFloat4x4(&wedgeRitem->World, XMMatrixScaling(2.0f, 1.5f, 1.0f) * XMMatrixTranslation(1.0f, 2.0f, 0.0f));
	//wedgeRitem->ObjCBIndex = 1;
	//wedgeRitem->Geo = mGeometries["shapeGeo"].get();
	//wedgeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//wedgeRitem->IndexCount = wedgeRitem->Geo->DrawArgs["wedge"].IndexCount;
	//wedgeRitem->StartIndexLocation = wedgeRitem->Geo->DrawArgs["wedge"].StartIndexLocation;
	//wedgeRitem->BaseVertexLocation = wedgeRitem->Geo->DrawArgs["wedge"].BaseVertexLocation;
	//mAllRitems.push_back(std::move(wedgeRitem));

	//auto triPrismRitem = std::make_unique<RenderItem>();
	////wedgeRitem->World = MathHelper::Identity4x4();
	//XMStoreFloat4x4(&triPrismRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(-4.0f, 2.0f, 1.0f));
	//triPrismRitem->ObjCBIndex = 2;
	//triPrismRitem->Geo = mGeometries["shapeGeo"].get();
	//triPrismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//triPrismRitem->IndexCount = triPrismRitem->Geo->DrawArgs["triPrism"].IndexCount;
	//triPrismRitem->StartIndexLocation = triPrismRitem->Geo->DrawArgs["triPrism"].StartIndexLocation;
	//triPrismRitem->BaseVertexLocation = triPrismRitem->Geo->DrawArgs["triPrism"].BaseVertexLocation;
	//mAllRitems.push_back(std::move(triPrismRitem));

	//auto pentaPrismRitem = std::make_unique<RenderItem>();
	////wedgeRitem->World = MathHelper::Identity4x4();
	//XMStoreFloat4x4(&pentaPrismRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(-2.0f, -3.0f, -3.0f));
	//pentaPrismRitem->ObjCBIndex = 3;
	//pentaPrismRitem->Geo = mGeometries["shapeGeo"].get();
	//pentaPrismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//pentaPrismRitem->IndexCount = pentaPrismRitem->Geo->DrawArgs["pentaPrism"].IndexCount;
	//pentaPrismRitem->StartIndexLocation = pentaPrismRitem->Geo->DrawArgs["pentaPrism"].StartIndexLocation;
	//pentaPrismRitem->BaseVertexLocation = pentaPrismRitem->Geo->DrawArgs["pentaPrism"].BaseVertexLocation;
	//mAllRitems.push_back(std::move(pentaPrismRitem));

	//auto pyramidRitem = std::make_unique<RenderItem>();
	////wedgeRitem->World = MathHelper::Identity4x4();
	//XMStoreFloat4x4(&pyramidRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(3.0f, 3.0f, 2.0f));
	//pyramidRitem->ObjCBIndex = 4;
	//pyramidRitem->Geo = mGeometries["shapeGeo"].get();
	//pyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
	//pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	//pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	//mAllRitems.push_back(std::move(pyramidRitem));

	//auto coneRitem = std::make_unique<RenderItem>();
	////wedgeRitem->World = MathHelper::Identity4x4();
	//XMStoreFloat4x4(&coneRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(1.0f, 5.0f, -2.0f));
	//coneRitem->ObjCBIndex = 5;
	//coneRitem->Geo = mGeometries["shapeGeo"].get();
	//coneRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//coneRitem->IndexCount = coneRitem->Geo->DrawArgs["cone"].IndexCount;
	//coneRitem->StartIndexLocation = coneRitem->Geo->DrawArgs["cone"].StartIndexLocation;
	//coneRitem->BaseVertexLocation = coneRitem->Geo->DrawArgs["cone"].BaseVertexLocation;
	//mAllRitems.push_back(std::move(coneRitem));

	//auto diamondRitem = std::make_unique<RenderItem>();
	////wedgeRitem->World = MathHelper::Identity4x4();
	//XMStoreFloat4x4(&diamondRitem->World, XMMatrixScaling(1.0f, 3.0f, 1.0f) * XMMatrixTranslation(0.0f, 2.0f, -3.0f) * XMMatrixRotationX(90));
	//diamondRitem->ObjCBIndex = 6;
	//diamondRitem->Geo = mGeometries["shapeGeo"].get();
	//diamondRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//diamondRitem->IndexCount = diamondRitem->Geo->DrawArgs["diamond"].IndexCount;
	//diamondRitem->StartIndexLocation = diamondRitem->Geo->DrawArgs["diamond"].StartIndexLocation;
	//diamondRitem->BaseVertexLocation = diamondRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	//mAllRitems.push_back(std::move(diamondRitem));

	/*
	Step9
	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->ObjCBIndex = 1;
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));

	UINT objCBIndex = 2;
	for (int i = 0; i < 5; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

		XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}*/

	// All the render items are opaque.
	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		// Offset to the CBV in the descriptor heap for this object and for this frame resource.
		UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mOpaqueRitems.size() + ri->ObjCBIndex;
		auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
		cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);

		cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}


