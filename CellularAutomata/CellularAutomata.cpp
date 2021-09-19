#include <initguid.h>
#include "d3dApp.h"
#include "MathHelper.h"
#include <SimpleMath.h>
#include <algorithm>
#include <random>

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;
using namespace DirectX::SimpleMath;

// material ids
#define mat_id_empty  (uint8_t)0
#define mat_id_sand   (uint8_t)1
#define mat_id_water  (uint8_t)2
#define mat_id_stone  (uint8_t)3
#define mat_id_fire   (uint8_t)4
#define mat_id_smoke  (uint8_t)5
#define mat_id_steam  (uint8_t)6

// material colors
// Colors
#define mat_col_empty  { 0, 0, 0, 0}
#define mat_col_sand   { 150, 100, 50, 255 }
#define mat_col_water  { 20, 100, 170, 200 }
#define mat_col_stone  { 128, 128, 128, 200 }
#define mat_col_fire   { 150, 20, 0, 255 }
#define mat_col_smoke  { 50, 50, 50, 255 }
#define mat_col_steam  { 220, 220, 250, 255 }

struct Color32 {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;

	Color32(uint8_t x, uint8_t y, uint8_t z, uint8_t w)
		: r(x), g(y), b(z), a(w)
	{
	}
};

struct Particle {
	uint8_t id = mat_id_empty;
	float life_time;
	Vector2 velocity;
	Color32 color = mat_col_empty;
	bool has_been_updated_this_frame;
};

// width and height of texture buffer (equals to screen size)
constexpr unsigned int textureWidth = 800;
constexpr unsigned int textureHeight = 600;

enum class material_selection
{
	mat_sel_sand = 0,
	mat_sel_water,
	mat_sel_stone,
	mat_sel_fire,
	mat_sel_smoke,
	mat_sel_steam
};

// selected material (by default, it's sand)
material_selection selectedMaterial = material_selection::mat_sel_sand;

// world particle data
std::vector<Particle> WorldData{ textureWidth * textureHeight };

// color data
std::vector<Color32> ColorData{ textureWidth * textureHeight, Color32(0, 0, 0, 0) };

// gravity settings
float gravity = 10.0f;

// selection radius
float selectionRadius = 10.0f;

// frame counter
unsigned int frameCounter = 0;

class CellularAutomata : public D3DApp
{
public:
	CellularAutomata(HINSTANCE hInstance);
	~CellularAutomata();

	bool Initialize() override;	

private:
	void OnResize() override;
	void Update(const GameTimer& gt) override;
	void Draw(const GameTimer& gt) override;

	void BuildRootSignature();
	void BuildBuffers();
	void BuildShadersAndInputLayout();
	void BuildPSOs();

	// input handling
	void OnMouseDown(WPARAM btnState, int x, int y) override;
	void OnMouseUp(WPARAM btnState, int x, int y) override;
	void OnMouseMove(WPARAM btnState, int x, int y) override;
	void OnKeyUp(WPARAM button) override;

	// particle definitions
	inline Particle ParticleEmpty();
	inline Particle ParticleSand();
	inline Particle ParticleWater();
	inline Particle ParticleStone();
	inline Particle ParticleFire();
	inline Particle ParticleSmoke();
	inline Particle ParticleSteam();

	// particle updates
	void UpdateParticleSim(const GameTimer& gt);
	void UpdateSand(uint32_t x, uint32_t y, const GameTimer& gt);
	void UpdateWater(uint32_t x, uint32_t y, const GameTimer& gt);
	void UpdateFire(uint32_t x, uint32_t y, const GameTimer& gt);
	void UpdateSmoke(uint32_t x, uint32_t y, const GameTimer& gt);
	void UpdateSteam(uint32_t x, uint32_t y, const GameTimer& gt);

	// Utility functions
	void ShowControls();
	void ClearScreen();
	void SelectMaterial(WPARAM button);
	void WriteData(uint32_t idx, Particle);
	inline int RandomVal(int lower, int upper);
	inline int ComputeID(int x, int y);
	bool InBounds(int x, int y);
	bool IsEmpty(int x, int y);
	Particle GetParticleAt(int x, int y);
	bool CompletelySurrounded(int x, int y);
	bool IsInWater(int x, int y, int* lx, int* ly);
	inline float VectorDistance(Vector2 vec1, Vector2 vec2);
	void UploadToTexture();

	// texture related
	ComPtr<ID3D12Resource> mTexture[SwapChainBufferCount]{};
	ComPtr<ID3D12Resource> mTextureBufferUploader = nullptr;
	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;
	ComPtr<ID3D12Resource> textureUploadHeap = nullptr;

	ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
	ComPtr<ID3DBlob> IndexBufferCPU = nullptr;

	ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
	ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;

	ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
	ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr <ID3DBlob> mVertexShader = nullptr;
	ComPtr <ID3DBlob> mPixelShader = nullptr;
	ComPtr<ID3D12PipelineState> mPSO = nullptr;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;	
	D3D12_VERTEX_BUFFER_VIEW mVertexBufferView;
	D3D12_INDEX_BUFFER_VIEW mIndexBufferView;

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
		CellularAutomata theApp(hInstance);
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

CellularAutomata::CellularAutomata(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

CellularAutomata::~CellularAutomata()
{
}

bool CellularAutomata::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc[mFrameIndex].Get(), nullptr));

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildPSOs();
	BuildBuffers();
	ShowControls();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();

	return true;
}

void CellularAutomata::OnResize()
{
	D3DApp::OnResize();
}

void CellularAutomata::Update(const GameTimer& gt)
{
	frameCounter = (frameCounter + 1) % UINT_MAX;

	UpdateParticleSim(gt);
}

void CellularAutomata::Draw(const GameTimer& gt)
{
	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(mDirectCmdListAlloc[mFrameIndex]->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc[mFrameIndex].Get(), mPSO.Get()));

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Set the viewport and scissor rect.  This needs to be reset whenever the command list is reset.
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	// set root signature
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	// upload color data to the texture
	UploadToTexture();

	// draw color buffer
	mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
	mCommandList->IASetIndexBuffer(&mIndexBufferView);
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);	

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	mCommandList->SetGraphicsRootDescriptorTable(0, tex);
	mCommandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	MoveToNextFrame();
}

void CellularAutomata::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,  // number of descriptors
		0); // register t0

	CD3DX12_ROOT_PARAMETER slotRootParameter[1];
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER); // addressW

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameter, 1, &pointClamp,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());

	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void CellularAutomata::BuildBuffers()
{
	struct Vertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 TexC;
	};

	Vertex vertices[] = {
		{ { -1.0f, -1.0f , 0.0f}, { 0.0f, 1.0f } },
		{ {  1.0f, -1.0f , 0.0f}, { 1.0f, 1.0f } },
		{ { -1.0f,  1.0f , 0.0f}, { 0.0f, 0.0f } },
		{ {  1.0f,  1.0f , 0.0f}, { 1.0f, 0.0f } }
	};		

	// set indices
	uint16_t indices[] =	{
		0, 2, 1,
		1, 2, 3
	};

	// sizes of buffers in terms of bytes
	const UINT vbByteSize = sizeof(vertices);
	const UINT ibByteSize = sizeof(indices);

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &VertexBufferCPU));
	CopyMemory(VertexBufferCPU->GetBufferPointer(), &vertices, vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &IndexBufferCPU));
	CopyMemory(IndexBufferCPU->GetBufferPointer(), &indices, ibByteSize);

	// send buffers to the gpu
	VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), &vertices, vbByteSize, VertexBufferUploader);

	IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), &indices, ibByteSize, IndexBufferUploader);

	// set vertex buffer view
	mVertexBufferView.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress();
	mVertexBufferView.StrideInBytes = sizeof(Vertex);
	mVertexBufferView.SizeInBytes = vbByteSize;

	// set index buffer view
	mIndexBufferView.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress();
	mIndexBufferView.Format = DXGI_FORMAT_R16_UINT;
	mIndexBufferView.SizeInBytes = ibByteSize;
}

void CellularAutomata::BuildShadersAndInputLayout()
{
	mVertexShader = d3dUtil::CompileShader(L"Shaders\\shader.hlsl", nullptr, "VS", "vs_5_0");
	mPixelShader = d3dUtil::CompileShader(L"Shaders\\shader.hlsl", nullptr, "PS", "ps_5_0");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void CellularAutomata::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;

	// PSO for opaque objects.
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	psoDesc.pRootSignature = mRootSignature.Get();
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(mVertexShader.Get());
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(mPixelShader.Get());
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	psoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}

void CellularAutomata::OnMouseDown(WPARAM btnState, int x, int y) 
{
	
	if (btnState == VK_LBUTTON)
	{		
		unsigned int mp_x = std::clamp(static_cast<unsigned int>(x), 0u, textureWidth - 1);
		unsigned int mp_y = std::clamp(static_cast<unsigned int>(y), 0u, textureHeight - 1);
		unsigned int max_idx = (textureWidth * textureHeight) - 1;
		unsigned int r_amt = RandomVal(1, 10000);
		const float R = selectionRadius;

		// Spawn in a circle around the mouse
		for (unsigned int i = 0; i < r_amt; ++i)
		{
			float ran = (float)RandomVal(0, 100) / 100.f;
			float r = R * sqrt(ran);
			float theta = (float)RandomVal(0, 100) / 100.f * 2.f * MathHelper::Pi;
			unsigned int rx = static_cast<unsigned int>(cos(theta) * r);
			unsigned int ry = static_cast<unsigned int>(sin(theta) * r);
			unsigned int mpx = std::clamp(mp_x + rx, 0u, textureWidth - 1);
			unsigned int mpy = std::clamp(mp_y + ry, 0u, textureHeight - 1);
			unsigned int idx = mpy * textureWidth + mpx;
			idx = std::clamp(idx, 0u, max_idx);

			if (IsEmpty(mpx, mpy))
			{
				Particle p = { 0 };
				switch (selectedMaterial) {
				case material_selection::mat_sel_sand: p = ParticleSand(); break;
				case material_selection::mat_sel_water: p = ParticleWater(); break;
				case material_selection::mat_sel_stone: p = ParticleStone(); break;
				case material_selection::mat_sel_fire: p = ParticleFire(); break;
				case material_selection::mat_sel_smoke: p = ParticleSmoke(); break;
				case material_selection::mat_sel_steam: p = ParticleSteam(); break;					
				}
				p.velocity = Vector2{ static_cast<float>(RandomVal(-1, 1)), static_cast<float>(RandomVal(-2, 5)) };
				WriteData(idx, p);
			}
		}
	}

	// Solid Erase
	if (btnState == VK_RBUTTON)
	{		
		unsigned int mp_x = std::clamp(static_cast<unsigned int>(x), 0u, textureWidth - 1);
		unsigned int mp_y = std::clamp(static_cast<unsigned int>(y), 0u, textureHeight - 1);
		unsigned int max_idx = (textureWidth * textureHeight) - 1;
		const float R = selectionRadius;
	
		// Erase in a circle pattern
		for (int i = -R; i < R; ++i)
		{
			for (int j = -R; j < R; ++j)
			{
				unsigned int rx = mp_x + j;
				unsigned int ry = mp_y + i;
				Vector2 r = Vector2{ static_cast<float>(rx), static_cast<float>(ry) };
				Vector2 mp = Vector2{ static_cast<float>(x), static_cast<float>(y) };
				if (InBounds(rx, ry) && VectorDistance(mp, r) <= R) {
					WriteData(ComputeID(rx, ry), ParticleEmpty());
				}
			}
		}
	}
}

void CellularAutomata::OnMouseUp(WPARAM btnState, int x, int y)
{
	
}

void CellularAutomata::OnMouseMove(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void CellularAutomata::OnKeyUp(WPARAM button)
{
	switch (button)
	{
		case VK_ESCAPE:
			PostQuitMessage(0);
			break;
		case 0x43: // 'C' button
			ClearScreen();
			break;
		default:
			break;
	}
	SelectMaterial(button);
}

inline Particle CellularAutomata::ParticleEmpty()
{
	Particle p = { 0 };
	p.id = mat_id_empty;
	p.color = mat_col_empty;
	return p;
}

inline Particle CellularAutomata::ParticleSand()
{
	Particle p = { 0 };
	p.id = mat_id_sand;
	// Random sand color
	p.color.r = 204;
	p.color.g = 127;
	p.color.b = 51;
	p.color.a = 255;
	return p;
}

inline Particle CellularAutomata::ParticleWater()
{
	Particle p = { 0 };
	p.id = mat_id_water;
	p.color.r = 25;
	p.color.g = 76;
	p.color.b = 178;
	p.color.a = 255;
	return p;
}

inline Particle CellularAutomata::ParticleStone()
{
	Particle p = { 0 };
	p.id = mat_id_stone;
	float r = (float)(RandomVal(0, 1)) / 2.f;
	p.color.r = 128;
	p.color.g = 128;
	p.color.b = 128;
	p.color.a = 255;
	return p;
}

inline Particle CellularAutomata::ParticleFire()
{
	Particle p = { 0 };
	p.id = mat_id_fire;
	p.color = mat_col_fire;
	return p;
}

inline Particle CellularAutomata::ParticleSmoke()
{
	Particle p = { 0 };
	p.id = mat_id_smoke;
	p.color = mat_col_smoke;
	return p;
}

inline Particle CellularAutomata::ParticleSteam()
{
	Particle p = { 0 };
	p.id = mat_id_steam;
	p.color = mat_col_steam;
	return p;
}

void CellularAutomata::UpdateParticleSim(const GameTimer& gt)
{
	// Update frame counter ( loop back to 0 if we roll past unsigned int max )
	bool frame_counter_even = ((frameCounter % 2) == 0);
	unsigned int ran = frame_counter_even ? 0 : 1;

	const float dt = gt.DeltaTime();

	// Rip through read data and update write buffer
	// Note(John): We update "bottom up", since all the data is edited "in place". Double buffering all data would fix this 
	// 	issue, however it requires double all of the data.
	for (unsigned int y = textureHeight - 1; y > 0; --y)
	{
		for (unsigned int x = ran ? 0 : textureWidth - 1; ran ? x < textureWidth : x > 0; ran ? ++x : --x)
		{
			// Current particle idx
			unsigned int read_idx = ComputeID(x, y);

			// Get material of particle at point
			uint8_t mat_id = GetParticleAt(x, y).id;

			// Update particle's lifetime (I guess just use frames)? Or should I have sublife?
			WorldData.at(read_idx).life_time += 1.f * dt;

			switch (mat_id) {

			case mat_id_sand:  UpdateSand(x, y, gt);  break;
			case mat_id_water: UpdateWater(x, y, gt); break;
			case mat_id_smoke: UpdateSmoke(x, y, gt); break;
			case mat_id_steam: UpdateSteam(x, y, gt); break;
			case mat_id_fire:  UpdateFire(x, y, gt);  break;
				// Do nothing for empty or default case
			default:
			case mat_id_empty:
			{
			} break;
			}
		}
	}

	// Can remove this loop later on by keeping update structure and setting that for the particle as it moves, 
	// then at the end of frame just memsetting the entire structure to 0.
	for (unsigned int y = textureHeight - 1; y > 0; --y) {
		for (unsigned int x = ran ? 0 : textureWidth - 1; ran ? x < textureWidth : x > 0; ran ? ++x : --x) {
			// Set particle's update to false for next frame
			WorldData.at(ComputeID(x, y)).has_been_updated_this_frame = false;
		}
	}
}

void CellularAutomata::UpdateFire(uint32_t x, uint32_t y, const GameTimer& gt)
{
	float dt = gt.DeltaTime();

	// For water, same as sand, but we'll check immediate left and right as well
	int read_idx = ComputeID(x, y);
	Particle* p = &WorldData.at(read_idx);
	uint32_t write_idx = read_idx;
	uint32_t fall_rate = 4;

	if (p->has_been_updated_this_frame) {
		return;
	}

	p->has_been_updated_this_frame = true;

	if (p->life_time > 0.2f) {
		if (RandomVal(0, 100) == 0) {
			WriteData(read_idx, ParticleEmpty());
			return;
		}
	}

	float st = sin(gt.TotalTime());
	// float grav_mul = random_val( 0, 10 ) == 0 ? 2.f : 1.f;
	p->velocity.y = std::clamp(p->velocity.y - ((gravity * dt)) * 0.2f, -5.0f, 0.f);
	// p->velocity.x = std::clamp( st, -1.f, 1.f );
	p->velocity.x = std::clamp(p->velocity.x + (float)RandomVal(-100, 100) / 200.f, -0.5f, 0.5f);

	// Change color based on life_time

	if (RandomVal(0, (int)(p->life_time * 100.f)) % 200 == 0) {
		int ran = RandomVal(0, 3);
		switch (ran) {
		case 0: p->color = { 255, 80, 20, 255 }; break;
		case 1: p->color = { 250, 150, 10, 255 }; break;
		case 2: p->color = { 200, 150, 0, 255 }; break;
		case 3: p->color = { 100, 50, 2, 255 }; break;
		}
	}

	if (p->life_time < 0.02f) {
		p->color.r = 200;
	}
	else {
		p->color.r = 255;
	}

	// In water, so create steam and DIE
	// Should also kill the water...
	int lx, ly;
	if (IsInWater(x, y, &lx, &ly)) {
		if (RandomVal(0, 1) == 0) {
			int ry = RandomVal(-5, -1);
			int rx = RandomVal(-5, 5);
			for (int i = ry; i > -5; --i) {
				for (int j = rx; j < 5; ++j) {
					Particle p = ParticleSteam();
					if (InBounds(x + j, y + i) && IsEmpty(x + j, y + i)) {
						Particle p = ParticleSteam();
						WriteData(ComputeID(x + j, y + i), p);
					}
				}
			}
			Particle p = ParticleSteam();
			WriteData(read_idx, ParticleEmpty());
			WriteData(read_idx, p);
			WriteData(ComputeID(lx, ly), ParticleEmpty());
			return;
		}
	}

	// Just check if you can move directly beneath you. If not, then reset your velocity. God, this is going to blow.
	if (InBounds(x, y + 1) && !IsEmpty(x, y + 1) && (GetParticleAt(x, y + 1).id != mat_id_water || GetParticleAt(x, y + 1).id != mat_id_smoke)) {
		p->velocity.y /= 2.f;
	}

	if (RandomVal(0, 10) == 0) {
		// p->velocity.x = std::clamp( p->velocity.x + (float)random_val( -1, 1 ) / 2.f, -1.f, 1.f );
	}
	// p->velocity.x = std::clamp( p->velocity.x, -0.5f, 0.5f );

	// Kill fire underneath
	if (InBounds(x, y + 3) && GetParticleAt(x, y + 3).id == mat_id_fire && RandomVal(0, 100) == 0) {
		WriteData(ComputeID(x, y + 3), *p);
		WriteData(read_idx, ParticleEmpty());
		return;
	}

	// Chance to kick itself up ( to simulate flames )
	if (InBounds(x, y + 1) && GetParticleAt(x, y + 1).id == mat_id_fire &&
		InBounds(x, y - 1) && GetParticleAt(x, y - 1).id == mat_id_empty) {
		if (RandomVal(0, 10) == 0 * p->life_time < 10.f && p->life_time > 1.f) {
			int r = RandomVal(0, 1);
			int rh = RandomVal(-10, -1);
			int spread = 3;
			for (int i = rh; i < 0; ++i) {
				for (int j = r ? -spread : spread; r ? j < spread : j > -spread; r ? ++j : --j) {
					int rx = j, ry = i;
					if (InBounds(x + rx, y + ry) && IsEmpty(x + rx, y + ry)) {
						WriteData(ComputeID(x + rx, y + ry), *p);
						WriteData(read_idx, ParticleEmpty());
						break;
					}
				}
			}
		}
		return;
	}

	int vi_x = x + (int)p->velocity.x;
	int vi_y = y + (int)p->velocity.y;

	// Check to see if you can swap first with other element below you
	uint32_t b_idx = ComputeID(x, y + 1);
	uint32_t br_idx = ComputeID(x + 1, y + 1);
	uint32_t bl_idx = ComputeID(x - 1, y + 1);

	const int wood_chance = 100;
	const int gun_powder_chance = 1;
	const int oil_chance = 5;

	// Chance to spawn smoke above
	for (uint32_t i = 0; i < RandomVal(1, 10); ++i) {
		if (RandomVal(0, 500) == 0) {
			if (InBounds(x, y - 1) && IsEmpty(x, y - 1)) {
				WriteData(ComputeID(x, y - 1), ParticleSmoke());
			}
			else if (InBounds(x + 1, y - 1) && IsEmpty(x + 1, y - 1)) {
				WriteData(ComputeID(x + 1, y - 1), ParticleSmoke());
			}
			else if (InBounds(x - 1, y - 1) && IsEmpty(x - 1, y - 1)) {
				WriteData(ComputeID(x - 1, y - 1), ParticleSmoke());
			}
		}
	}		

	if (InBounds(vi_x, vi_y) && (IsEmpty(vi_x, vi_y) ||
		GetParticleAt(vi_x, vi_y).id == mat_id_fire ||
		GetParticleAt(vi_x, vi_y).id == mat_id_smoke))
	{
		// p->velocity.y -= (gravity * dt );
		Particle tmp_b = WorldData.at(ComputeID(vi_x, vi_y));
		WriteData(ComputeID(vi_x, vi_y), *p);
		WriteData(read_idx, tmp_b);
	}

	// Simple falling, changing the velocity here ruins everything. I need to redo this entire simulation.
	else if (InBounds(x, y + 1) && ((IsEmpty(x, y + 1) || (WorldData.at(b_idx).id == mat_id_water)))) {
		// p->velocity.y -= (gravity * dt );
		// p->velocity.x = random_val( 0, 1 ) == 0 ? -1.f : 1.f;
		Particle tmp_b = WorldData.at(b_idx);
		WriteData(b_idx, *p);
		WriteData(read_idx, tmp_b);
	}
	else if (InBounds(x - 1, y + 1) && ((IsEmpty(x - 1, y + 1) || WorldData.at(bl_idx).id == mat_id_water))) {
		// p->velocity.x = random_val( 0, 1 ) == 0 ? -1.f : 1.f;
		// p->velocity.y -= (gravity * dt );
		Particle tmp_b = WorldData.at(bl_idx);
		WriteData(bl_idx, *p);
		WriteData(read_idx, tmp_b);
	}
	else if (InBounds(x + 1, y + 1) && ((IsEmpty(x + 1, y + 1) || WorldData.at(br_idx).id == mat_id_water))) {
		// p->velocity.x = random_val( 0, 1 ) == 0 ? -1.f : 1.f;
		// p->velocity.y -= (gravity * dt );
		Particle tmp_b = WorldData.at(br_idx);
		WriteData(br_idx, *p);
		WriteData(read_idx, tmp_b);
	}
	else if (InBounds(x - 1, y - 1) && (WorldData.at(ComputeID(x - 1, y - 1)).id == mat_id_water)) {
		uint32_t idx = ComputeID(x - 1, y - 1);
		// p->velocity.x = random_val( 0, 1 ) == 0 ? -1.f : 1.f;
		Particle tmp_b = WorldData.at(idx);
		WriteData(idx, *p);
		WriteData(read_idx, tmp_b);
	}
	else if (InBounds(x + 1, y - 1) && (WorldData.at(ComputeID(x + 1, y - 1)).id == mat_id_water)) {
		uint32_t idx = ComputeID(x + 1, y - 1);
		// p->velocity.x = random_val( 0, 1 ) == 0 ? -1.f : 1.f;
		Particle tmp_b = WorldData.at(idx);
		WriteData(idx, *p);
		WriteData(read_idx, tmp_b);
	}
	else if (InBounds(x, y - 1) && (WorldData.at(ComputeID(x, y - 1)).id == mat_id_water)) {
		uint32_t idx = ComputeID(x, y - 1);
		// p->velocity.x = random_val( 0, 1 ) == 0 ? -1.f : 1.f;
		Particle tmp_b = WorldData.at(idx);
		WriteData(idx, *p);
		WriteData(read_idx, tmp_b);
	}
	else {
		// p->velocity.x = random_val( 0, 1 ) == 0 ? -1.f : 1.f;
		WriteData(read_idx, *p);
	}
}

void CellularAutomata::UpdateSmoke(uint32_t x, uint32_t y, const GameTimer& gt)
{
	float dt = gt.DeltaTime();

	// For water, same as sand, but we'll check immediate left and right as well
	uint32_t read_idx = ComputeID(x, y);
	Particle* p = &WorldData.at(read_idx);
	uint32_t write_idx = read_idx;
	uint32_t fall_rate = 4;

	if (p->life_time > 10.f) {
		WriteData(read_idx, ParticleEmpty());
		return;
	}

	if (p->has_been_updated_this_frame) {
		return;
	}

	p->has_been_updated_this_frame = true;

	// Smoke rises over time. This might cause issues, actually...
	p->velocity.y = std::clamp(p->velocity.y - (gravity * dt), -2.f, 10.f);
	p->velocity.x = std::clamp(p->velocity.x + (float)RandomVal(-100, 100) / 100.f, -1.f, 1.f);

	// Just check if you can move directly beneath you. If not, then reset your velocity. God, this is going to blow.
	if (InBounds(x, y - 1) && !IsEmpty(x, y - 1) && GetParticleAt(x, y - 1).id != mat_id_water) {
		p->velocity.y /= 2.f;
	}

	int vi_x = x + (int)p->velocity.x;
	int vi_y = y + (int)p->velocity.y;

	// if ( in_bounds( vi_x, vi_y ) && ( (is_empty( vi_x, vi_y ) || get_particle_at( vi_x, vi_y ).id == mat_id_water || get_particle_at( vi_x, vi_y ).id == mat_id_fire ) ) ) {
	if (InBounds(vi_x, vi_y) && GetParticleAt(vi_x, vi_y).id != mat_id_smoke) {

		Particle tmp_b = WorldData.at(ComputeID(vi_x, vi_y));

		// Try to throw water out
		if (tmp_b.id == mat_id_water) {

			tmp_b.has_been_updated_this_frame = true;

			int rx = RandomVal(-2, 2);
			tmp_b.velocity = { static_cast<float>(rx), -3.0f };

			WriteData(ComputeID(vi_x, vi_y), *p);
			WriteData(read_idx, tmp_b);

		}
		else if (IsEmpty(vi_x, vi_y)) {
			WriteData(ComputeID(vi_x, vi_y), *p);
			WriteData(read_idx, tmp_b);
		}
	}
	// Simple falling, changing the velocity here ruins everything. I need to redo this entire simulation.
	else if (InBounds(x, y - 1) && GetParticleAt(x, y - 1).id != mat_id_smoke &&
		GetParticleAt(x, y - 1).id != mat_id_stone) {
		p->velocity.y -= (gravity * dt);
		Particle tmp_b = GetParticleAt(x, y - 1);
		WriteData(ComputeID(x, y - 1), *p);
		WriteData(read_idx, tmp_b);
	}
	else if (InBounds(x - 1, y - 1) && GetParticleAt(x - 1, y - 1).id != mat_id_smoke &&
		GetParticleAt(x - 1, y - 1).id != mat_id_stone) {
		p->velocity.x = RandomVal(0, 1) == 0 ? -1.2f : 1.2f;
		p->velocity.y -= (gravity * dt);
		Particle tmp_b = GetParticleAt(x - 1, y - 1);
		WriteData(ComputeID(x - 1, y - 1), *p);
		WriteData(read_idx, tmp_b);
	}
	else if (InBounds(x + 1, y - 1) && GetParticleAt(x + 1, y - 1).id != mat_id_smoke &&
		GetParticleAt(x + 1, y - 1).id != mat_id_stone) {
		p->velocity.x = RandomVal(0, 1) == 0 ? -1.2f : 1.2f;
		p->velocity.y -= (gravity * dt);
		Particle tmp_b = GetParticleAt(x + 1, y - 1);
		WriteData(ComputeID(x + 1, y - 1), *p);
		WriteData(read_idx, tmp_b);
	}
	// Can move if in liquid
	else if (InBounds(x + 1, y) && GetParticleAt(x + 1, y).id != mat_id_smoke &&
		GetParticleAt(x + 1, y).id != mat_id_stone) {
		uint32_t idx = ComputeID(x + 1, y);
		Particle tmp_b = WorldData.at(idx);
		WriteData(idx, *p);
		WriteData(read_idx, tmp_b);
	}
	else if (InBounds(x - 1, y) && GetParticleAt(x - 1, y).id != mat_id_smoke &&
		GetParticleAt(x - 1, y).id != mat_id_stone) {
		uint32_t idx = ComputeID(x - 1, y);
		Particle tmp_b = WorldData.at(idx);
		WriteData(idx, *p);
		WriteData(read_idx, tmp_b);
	}
	else {
		WriteData(read_idx, *p);
	}
}

void CellularAutomata::UpdateSteam(uint32_t x, uint32_t y, const GameTimer& gt)
{
	float dt = gt.DeltaTime();

	// For water, same as sand, but we'll check immediate left and right as well
	uint32_t read_idx = ComputeID(x, y);
	Particle* p = &WorldData.at(read_idx);
	uint32_t write_idx = read_idx;
	uint32_t fall_rate = 4;

	if (p->life_time > 10.f) {
		WriteData(read_idx, ParticleEmpty());
		return;
	}

	if (p->has_been_updated_this_frame) {
		return;
	}

	p->has_been_updated_this_frame = true;

	// Smoke rises over time. This might cause issues, actually...
	p->velocity.y = std::clamp(p->velocity.y - (gravity * dt), -2.f, 10.f);
	p->velocity.x = std::clamp(p->velocity.x + (float)RandomVal(-100, 100) / 100.f, -1.f, 1.f);

	// Just check if you can move directly beneath you. If not, then reset your velocity. God, this is going to blow.
	if (InBounds(x, y - 1) && !IsEmpty(x, y - 1) && GetParticleAt(x, y - 1).id != mat_id_water) {
		p->velocity.y /= 2.f;
	}

	int vi_x = x + (int)p->velocity.x;
	int vi_y = y + (int)p->velocity.y;

	if (InBounds(vi_x, vi_y) && ((IsEmpty(vi_x, vi_y) || GetParticleAt(vi_x, vi_y).id == mat_id_water || GetParticleAt(vi_x, vi_y).id == mat_id_fire))) {

		Particle tmp_b = WorldData.at(ComputeID(vi_x, vi_y));

		// Try to throw water out
		if (tmp_b.id == mat_id_water) {

			tmp_b.has_been_updated_this_frame = true;

			int rx = RandomVal(-2, 2);
			tmp_b.velocity = { static_cast<float>(rx), -3.f };

			WriteData(ComputeID(vi_x, vi_y), *p);
			WriteData(read_idx, tmp_b);

		}
		else if (IsEmpty(vi_x, vi_y)) {
			WriteData(ComputeID(vi_x, vi_y), *p);
			WriteData(read_idx, tmp_b);
		}
	}
	// Simple falling, changing the velocity here ruins everything. I need to redo this entire simulation.
	else if (InBounds(x, y - 1) && ((IsEmpty(x, y - 1) || (GetParticleAt(x, y - 1).id == mat_id_water) || GetParticleAt(x, y - 1).id == mat_id_fire))) {
		p->velocity.y -= (gravity * dt);
		Particle tmp_b = GetParticleAt(x, y - 1);
		WriteData(ComputeID(x, y - 1), *p);
		WriteData(read_idx, tmp_b);
	}
	else if (InBounds(x - 1, y - 1) && ((IsEmpty(x - 1, y - 1) || GetParticleAt(x - 1, y - 1).id == mat_id_water) || GetParticleAt(x - 1, y - 1).id == mat_id_fire)) {
		p->velocity.x = RandomVal(0, 1) == 0 ? -1.2f : 1.2f;
		p->velocity.y -= (gravity * dt);
		Particle tmp_b = GetParticleAt(x - 1, y - 1);
		WriteData(ComputeID(x - 1, y - 1), *p);
		WriteData(read_idx, tmp_b);
	}
	else if (InBounds(x + 1, y - 1) && ((IsEmpty(x + 1, y - 1) || GetParticleAt(x + 1, y - 1).id == mat_id_water) || GetParticleAt(x + 1, y - 1).id == mat_id_fire)) {
		p->velocity.x = RandomVal(0, 1) == 0 ? -1.2f : 1.2f;
		p->velocity.y -= (gravity * dt);
		Particle tmp_b = GetParticleAt(x + 1, y - 1);
		WriteData(ComputeID(x + 1, y - 1), *p);
		WriteData(read_idx, tmp_b);
	}
	// Can move if in liquid
	else if (InBounds(x + 1, y) && (GetParticleAt(x + 1, y).id == mat_id_water)) {
		uint32_t idx = ComputeID(x + 1, y);
		Particle tmp_b = WorldData.at(idx);
		WriteData(idx, *p);
		WriteData(read_idx, tmp_b);
	}
	else if (InBounds(x - 1, y) && (WorldData.at(ComputeID(x - 1, y)).id == mat_id_water)) {
		uint32_t idx = ComputeID(x - 1, y);
		Particle tmp_b = WorldData.at(idx);
		WriteData(idx, *p);
		WriteData(read_idx, tmp_b);
	}
	else {
		WriteData(read_idx, *p);
	}
}

void CellularAutomata::ShowControls()
{
	std::wstring controls = L"Controls:\n"
		"Press Left Mouse Button to put particles \n"
		"Press Right Mouse Button to delete particles\n"
		"Press 1 to select particle 'sand'\n"
		"Press 2 to select particle 'water'\n"
		"Press 3 to select particle 'stone'\n"
		"Press 4 to select particle 'fire'\n"
		"Press 5 to select particle 'smoke'\n"
		"Press 6 to select particle 'steam'\n"
		"Press C to clear screen\n";
	MessageBox(nullptr, controls.c_str(), L"Controls", MB_OK);
}

void CellularAutomata::ClearScreen()
{
	std::vector<Particle> tempData{ textureWidth * textureHeight }; // construct a new scene with default data
	WorldData.assign(tempData.begin(), tempData.end()); // overwrite existing data

	std::vector<Color32> tempColor{ textureWidth * textureHeight, Color32(0, 0, 0, 0) };
	ColorData.assign(tempColor.begin(), tempColor.end());
}

void CellularAutomata::SelectMaterial(WPARAM button)
{
	switch (button) {
	case 0x31: // button '1' pressed
		selectedMaterial = material_selection::mat_sel_sand;
		break;
	case 0x32: // button '2' pressed
		selectedMaterial = material_selection::mat_sel_water;
		break;
	case 0x33: // button '3' pressed
		selectedMaterial = material_selection::mat_sel_stone;
		break;
	case 0x34: // button '4' pressed
		selectedMaterial = material_selection::mat_sel_fire;
		break;
	case 0x35: // button '5' pressed
		selectedMaterial = material_selection::mat_sel_smoke;
		break;
	case 0x36: // button '6' pressed
		selectedMaterial = material_selection::mat_sel_steam;
		break;
	}
}

void CellularAutomata::UpdateSand(uint32_t x, uint32_t y, const GameTimer& gt) {
	float dt = gt.DeltaTime();

	// For water, same as sand, but we'll check immediate left and right as well
	unsigned int read_idx = ComputeID(x, y);
	Particle* p = &WorldData.at(read_idx);
	unsigned int write_idx = read_idx;
	unsigned int fall_rate = 4;

	p->velocity.y = std::clamp(p->velocity.y + (gravity * dt), -10.f, 10.f);

	// Just check if you can move directly beneath you. If not, then reset your velocity. God, this is going to blow.
	if (InBounds(x, y + 1) && !IsEmpty(x, y + 1) && GetParticleAt(x, y + 1).id != mat_id_water) {
		p->velocity.y /= 2.f;
	}

	int vi_x = x + (int)p->velocity.x;
	int vi_y = y + (int)p->velocity.y;

	// Check to see if you can swap first with other element below you
	unsigned int b_idx = ComputeID(x, y + 1);
	unsigned int br_idx = ComputeID(x + 1, y + 1);
	unsigned int bl_idx = ComputeID(x - 1, y + 1);

	int lx{}, ly{};

	Particle tmp_a = WorldData.at(read_idx);

	// Physics (using velocity)
	if (InBounds(vi_x, vi_y) && ((IsEmpty(vi_x, vi_y) ||
		(((WorldData.at(ComputeID(vi_x, vi_y)).id == mat_id_water) &&
			!WorldData.at(ComputeID(vi_x, vi_y)).has_been_updated_this_frame &&
			(WorldData.at(ComputeID(vi_x, vi_y)).velocity.Length() - tmp_a.velocity.Length()) > 10.f))))) {

		Particle tmp_b = WorldData.at(ComputeID(vi_x, vi_y));

		// Try to throw water out
		if (tmp_b.id == mat_id_water) {

			int rx = RandomVal(-2, 2);
			tmp_b.velocity = Vector2{ static_cast<float>(rx), -4.f };

			WriteData(ComputeID(vi_x, vi_y), tmp_a);

			for (int i = -10; i < 0; ++i) {
				for (int j = -10; j < 10; ++j) {
					if (IsEmpty(vi_x + j, vi_y + i)) {
						WriteData(ComputeID(vi_x + j, vi_y + i), tmp_b);
						break;
					}
				}
			}

			// Couldn't write there, so, uh, destroy it.
			WriteData(read_idx, ParticleEmpty());
		}
		else if (IsEmpty(vi_x, vi_y)) {
			WriteData(ComputeID(vi_x, vi_y), tmp_a);
			WriteData(read_idx, tmp_b);
		}
	}
	// Simple falling, changing the velocity here ruins everything. I need to redo this entire simulation.
	else if (InBounds(x, y + 1) && ((IsEmpty(x, y + 1) || (WorldData.at(b_idx).id == mat_id_water)))) {
		p->velocity.y += (gravity * dt);
		Particle tmp_b = GetParticleAt(x, y + 1);
		WriteData(b_idx, *p);
		WriteData(read_idx, tmp_b);
	}
	else if (InBounds(x - 1, y + 1) && ((IsEmpty(x - 1, y + 1) || WorldData.at(bl_idx).id == mat_id_water))) {
		p->velocity.x = RandomVal(0, 1) == 0 ? -1.f : 1.f;
		p->velocity.y += (gravity * dt);
		Particle tmp_b = GetParticleAt(x - 1, y + 1);
		WriteData(bl_idx, *p);
		WriteData(read_idx, tmp_b);
	}
	else if (InBounds(x + 1, y + 1) && ((IsEmpty(x + 1, y + 1) || WorldData.at(br_idx).id == mat_id_water))) {
		p->velocity.x = RandomVal(0, 1) == 0 ? -1.f : 1.f;
		p->velocity.y += (gravity * dt);
		Particle tmp_b = GetParticleAt(x + 1, y + 1);
		WriteData(br_idx, *p);
		WriteData(read_idx, tmp_b);
	}
	else if (RandomVal(0, 10) == 0) {
		Particle tmp_b = GetParticleAt(lx, ly);
		WriteData(ComputeID(lx, ly), *p);
		WriteData(read_idx, tmp_b);
	}
}

void CellularAutomata::UpdateWater(uint32_t x, uint32_t y, const GameTimer& gt) {
	float dt = gt.DeltaTime();

	unsigned int read_idx = ComputeID(x, y);
	Particle* p = &WorldData.at(read_idx);
	unsigned int write_idx = read_idx;
	int fall_rate = 2;
	int spread_rate = 5;

	p->velocity.y = std::clamp(p->velocity.y + (gravity * dt), -10.f, 10.f);

	p->has_been_updated_this_frame = true;

	// Just check if you can move directly beneath you. If not, then reset your velocity. God, this is going to blow.
	// if ( in_bounds( x, y + 1 ) && !is_empty( x, y + 1 ) && get_particle_at( x, y + 1 ).id != mat_id_water ) {
	if (InBounds(x, y + 1) && !IsEmpty(x, y + 1)) {
		p->velocity.y /= 2.f;
	}

	// Change color depending on pressure? Pressure would dictate how "deep" the water is, I suppose.
	if (RandomVal(0, (int)(p->life_time * 100.f)) % 20 == 0) {
		float r = (float)(RandomVal(0, 1)) / 2.f;
		p->color.r = 25;
		p->color.g = 76;
		p->color.b = 178;
	}

	int ran = RandomVal(0, 1);
	int r = ran ? spread_rate : -spread_rate;
	int l = -r;
	int u = fall_rate;
	int v_idx = ComputeID(x + (int)p->velocity.x, y + (int)p->velocity.y);
	int b_idx = ComputeID(x, y + u);
	int bl_idx = ComputeID(x + l, y + u);
	int br_idx = ComputeID(x + r, y + u);
	int l_idx = ComputeID(x + l, y);
	int r_idx = ComputeID(x + r, y);
	int vx = (int)p->velocity.x, vy = (int)p->velocity.y;
	int lx{}, ly{};

	if (InBounds(x + vx, y + vy) && (IsEmpty(x + vx, y + vy))) {
		WriteData(v_idx, *p);
		WriteData(read_idx, ParticleEmpty());
	}
	else if (IsEmpty(x, y + u)) {
		WriteData(b_idx, *p);
		WriteData(read_idx, ParticleEmpty());
	}
	else if (IsEmpty(x + r, y + u)) {
		WriteData(br_idx, *p);
		WriteData(read_idx, ParticleEmpty());
	}
	else if (IsEmpty(x + l, y + u)) {
		WriteData(bl_idx, *p);
		WriteData(read_idx, ParticleEmpty());
	}
	// Simple falling, changing the velocity here ruins everything. I need to redo this entire simulation.
	else if (InBounds(x, y + u) && (IsEmpty(x, y + u))) {
		p->velocity.y += (gravity * dt);
		Particle tmp_b = GetParticleAt(x, y + u);
		WriteData(b_idx, *p);
		WriteData(read_idx, tmp_b);
	}
	else if (InBounds(x + l, y + u) && (IsEmpty(x + l, y + u))) {
		p->velocity.x = RandomVal(0, 1) == 0 ? -1.f : 1.f;
		p->velocity.y += (gravity * dt);
		Particle tmp_b = GetParticleAt(x + l, y + u);
		WriteData(bl_idx, *p);
		WriteData(read_idx, tmp_b);
	}
	else if (InBounds(x + r, y + u) && (IsEmpty(x + r, y + u) )) {
		p->velocity.x = RandomVal(0, 1) == 0 ? -1.f : 1.f;
		p->velocity.y += (gravity * dt);
		Particle tmp_b = GetParticleAt(x + r, y + u);
		WriteData(br_idx, *p);
		WriteData(read_idx, tmp_b);
	}
	else if (RandomVal(0, 10) == 0) {
		Particle tmp_b = GetParticleAt(lx, ly);
		WriteData(ComputeID(lx, ly), *p);
		WriteData(read_idx, tmp_b);
	}
	else {
		Particle tmp = *p;
		bool found = false;

		// Don't try to spread if something is directly above you?
		if (CompletelySurrounded(x, y)) {
			WriteData(read_idx, tmp);
			return;
		}
		else {
			for (unsigned int i = 0; i < fall_rate; ++i) {
				for (int j = spread_rate; j > 0; --j)
				{
					if (InBounds(x - j, y + i) && (IsEmpty(x - j, y + i))) {
						Particle tmp = GetParticleAt(x - j, y + i);
						WriteData(ComputeID(x - j, y + i), *p);
						WriteData(read_idx, tmp);
						found = true;
						break;
					}
					if (InBounds(x + j, y + i) && (IsEmpty(x + j, y + i))) {
						Particle tmp = GetParticleAt(x + j, y + i);
						WriteData(ComputeID(x + j, y + i), *p);
						WriteData(read_idx, tmp);
						found = true;
						break;
					}
				}
			}

			if (!found) {
				WriteData(read_idx, tmp);
			}
		}
	}
}

void CellularAutomata::WriteData(uint32_t idx, Particle p) {
	// Write into particle data for id value
	WorldData.at(idx) = p;
	ColorData.at(idx) = p.color;
}

inline int CellularAutomata::RandomVal(int lower, int upper) {
	if (upper < lower) {
		int tmp = lower;
		lower = upper;
		upper = tmp;
	}

	return (std::rand() % (upper - lower + 1) + lower);
}

inline int CellularAutomata::ComputeID(int x, int y) {
	return (y * textureWidth + x);
}

bool CellularAutomata::InBounds(int x, int y) {
	if (x < 0 || x >(textureWidth - 1) || y < 0 || y >(textureHeight - 1)) return false;
	return true;
}

bool CellularAutomata::IsEmpty(int x, int y) {
	return (InBounds(x, y) && WorldData.at(ComputeID(x, y)).id == mat_id_empty);
}

Particle CellularAutomata::GetParticleAt(int x, int y) {
	return WorldData.at(ComputeID(x, y));
}

bool CellularAutomata::CompletelySurrounded(int x, int y) {
	// Top
	if (InBounds(x, y - 1) && !IsEmpty(x, y - 1)) {
		return false;
	}
	// Bottom
	if (InBounds(x, y + 1) && !IsEmpty(x, y + 1)) {
		return false;
	}
	// Left
	if (InBounds(x - 1, y) && !IsEmpty(x - 1, y)) {
		return false;
	}
	// Right
	if (InBounds(x + 1, y) && !IsEmpty(x + 1, y)) {
		return false;
	}
	// Top Left
	if (InBounds(x - 1, y - 1) && !IsEmpty(x - 1, y - 1)) {
		return false;
	}
	// Top Right
	if (InBounds(x + 1, y - 1) && !IsEmpty(x + 1, y - 1)) {
		return false;
	}
	// Bottom Left
	if (InBounds(x - 1, y + 1) && !IsEmpty(x - 1, y + 1)) {
		return false;
	}
	// Bottom Right
	if (InBounds(x + 1, y + 1) && !IsEmpty(x + 1, y + 1)) {
		return false;
	}

	return true;
}

bool CellularAutomata::IsInWater(int x, int y, int* lx, int* ly) {
	if (InBounds(x, y) && (GetParticleAt(x, y).id == mat_id_water)) {
		*lx = x; *ly = y;
		return true;
	}
	if (InBounds(x, y - 1) && (GetParticleAt(x, y - 1).id == mat_id_water)) {
		*lx = x; *ly = y - 1;
		return true;
	}
	if (InBounds(x, y + 1) && (GetParticleAt(x, y + 1).id == mat_id_water)) {
		*lx = x; *ly = y + 1;
		return true;
	}
	if (InBounds(x - 1, y) && (GetParticleAt(x - 1, y).id == mat_id_water)) {
		*lx = x - 1; *ly = y;
		return true;
	}
	if (InBounds(x - 1, y - 1) && (GetParticleAt(x - 1, y - 1).id == mat_id_water)) {
		*lx = x - 1; *ly = y - 1;
		return true;
	}
	if (InBounds(x - 1, y + 1) && (GetParticleAt(x - 1, y + 1).id == mat_id_water)) {
		*lx = x - 1; *ly = y + 1;
		return true;
	}
	if (InBounds(x + 1, y) && (GetParticleAt(x + 1, y).id == mat_id_water)) {
		*lx = x + 1; *ly = y;
		return true;
	}
	if (InBounds(x + 1, y - 1) && (GetParticleAt(x + 1, y - 1).id == mat_id_water)) {
		*lx = x + 1; *ly = y - 1;
		return true;
	}
	if (InBounds(x + 1, y + 1) && (GetParticleAt(x + 1, y + 1).id == mat_id_water)) {
		*lx = x + 1; *ly = y + 1;
		return true;
	}
	return false;
}

inline float CellularAutomata::VectorDistance(Vector2 vec1, Vector2 vec2) {
	float dx = (vec1.x - vec2.x);
	float dy = (vec1.y - vec2.y);
	return (std::sqrt(dx * dx + dy * dy));
}

void CellularAutomata::UploadToTexture()
{
	// Describe and create a Texture2D.
	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.Width = textureWidth;
	textureDesc.Height = textureHeight;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&mTexture[mFrameIndex])));

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mTexture[mFrameIndex].Get(), 0, 1);

	// Create the GPU upload buffer.
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&textureUploadHeap)));

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = ColorData.data();
	textureData.RowPitch = textureWidth * (sizeof(Color32));
	textureData.SlicePitch = textureData.RowPitch * textureHeight;

	UpdateSubresources(mCommandList.Get(), mTexture[mFrameIndex].Get(), textureUploadHeap.Get(), 0, 0, 1, &textureData);
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mTexture[mFrameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	// Describe and create a SRV for the texture.
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = textureDesc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	md3dDevice->CreateShaderResourceView(mTexture[mFrameIndex].Get(), &srvDesc, mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
}