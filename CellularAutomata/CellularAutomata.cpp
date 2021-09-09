#include <initguid.h>
#include "d3dApp.h"
#include "MathHelper.h"
#include "UploadBuffer.h"
#include "FrameResource.h"
#include <SimpleMath.h>
#include <algorithm>

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;
using namespace DirectX::SimpleMath;

// material ids
#define mat_id_empty (uint8_t)0
#define mat_id_sand  (uint8_t)1
#define mat_id_water (uint8_t)2

// material colors
// Colors
#define mat_col_empty { 0, 0, 0, 0}
#define mat_col_sand  { 150, 100, 50, 255 }
#define mat_col_water { 20, 100, 170, 200 }

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
	mat_sel_water	
};

// selected material (by default, it's sand)
material_selection selectedMaterial = material_selection::mat_sel_sand;

// world particle data
std::vector<Particle> worldData{ textureWidth * textureHeight };

// color data
std::vector<Color32> colorData{ textureWidth * textureHeight, Color32(0, 0, 0, 0) };

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

	Particle particle_empty();
	Particle particle_sand();
	Particle particle_water();

	// particle updates
	void update_particle_sim(const GameTimer& gt);
	void update_sand(uint32_t x, uint32_t y, const GameTimer& gt);
	void update_water(uint32_t x, uint32_t y, const GameTimer& gt);
	void update_default(uint32_t w, uint32_t h);

	// Utility functions
	void write_data(uint32_t idx, Particle);
	int random_val(int lower, int upper);
	int compute_idx(int x, int y);
	bool in_bounds(int x, int y);
	bool is_empty(int x, int y);
	Particle get_particle_at(int x, int y);
	bool completely_surrounded(int x, int y);
	bool is_in_water(int x, int y, int* lx, int* ly);
	void putpixel(int x, int y);
	void drawCircle(int xc, int yc, int x, int y);
	void circleBres(int xc, int yc, int r);
	float vectorDistance(Vector2 vec1, Vector2 vec2);
	void UploadToTexture();

	// texture related
	ComPtr<ID3D12Resource> mTexture = nullptr;
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
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildPSOs();
	BuildBuffers();

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

	update_particle_sim(gt);
}

void CellularAutomata::Draw(const GameTimer& gt)
{
	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(mDirectCmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

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

	// Wait until frame commands are complete.  This waiting is inefficient and is
	// done for simplicity.  Later we will show how to organize our rendering code
	// so we do not have to wait per frame.
	FlushCommandQueue();
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
		unsigned int r_amt = random_val(1, 10000);
		const float R = selectionRadius;

		// Spawn in a circle around the mouse
		for (unsigned int i = 0; i < r_amt; ++i)
		{
			float ran = (float)random_val(0, 100) / 100.f;
			float r = R * sqrt(ran);
			float theta = (float)random_val(0, 100) / 100.f * 2.f * MathHelper::Pi;
			unsigned int rx = static_cast<unsigned int>(cos(theta) * r);
			unsigned int ry = static_cast<unsigned int>(sin(theta) * r);
			unsigned int mpx = std::clamp(mp_x + rx, 0u, textureWidth - 1);
			unsigned int mpy = std::clamp(mp_y + ry, 0u, textureHeight - 1);
			unsigned int idx = mpy * textureWidth + mpx;
			idx = std::clamp(idx, 0u, max_idx);

			if (is_empty(mpx, mpy))
			{
				Particle p = { 0 };
				switch (selectedMaterial) {
				case material_selection::mat_sel_sand: p = particle_sand(); break;
				case material_selection::mat_sel_water: p = particle_water(); break;
				}
				p.velocity = Vector2{ static_cast<float>(random_val(-1, 1)), static_cast<float>(random_val(-2, 5)) };
				write_data(idx, p);
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
				if (in_bounds(rx, ry) && vectorDistance(mp, r) <= R) {
					write_data(compute_idx(rx, ry), particle_empty());
				}
			}
		}
	}

	SetCapture(mhMainWnd);
}

void CellularAutomata::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void CellularAutomata::OnMouseMove(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void CellularAutomata::OnKeyUp(WPARAM button)
{
	worldData.clear();
	worldData.reserve(textureWidth * textureHeight);

	colorData.clear();
	colorData.reserve(textureWidth * textureHeight);
}

Particle CellularAutomata::particle_empty() {
	Particle p = { 0 };
	p.id = mat_id_empty;
	p.color = mat_col_empty;
	return p;
}

Particle CellularAutomata::particle_sand() {
	Particle p = { 0 };
	p.id = mat_id_sand;
	// Random sand color
	p.color.r = 204;
	p.color.g = 127;
	p.color.b = 51;
	p.color.a = 255;
	return p;
}

Particle CellularAutomata::particle_water() {
	Particle p = { 0 };
	p.id = mat_id_water;
	p.color.r = 25;
	p.color.g = 76;
	p.color.b = 178;
	p.color.a = 255;
	return p;
}

void CellularAutomata::update_particle_sim(const GameTimer& gt)
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
			unsigned int read_idx = compute_idx(x, y);

			// Get material of particle at point
			uint8_t mat_id = get_particle_at(x, y).id;

			// Update particle's lifetime (I guess just use frames)? Or should I have sublife?
			worldData.at(read_idx).life_time += 1.f * dt;

			switch (mat_id) {

			case mat_id_sand:  update_sand(x, y, gt);  break;
			case mat_id_water: update_water(x, y, gt); break;
				// Do nothing for empty or default case
			default:
			case mat_id_empty:
			{
				// update_default( w, h, i ); 
			} break;
			}
		}
	}

	// Can remove this loop later on by keeping update structure and setting that for the particle as it moves, 
	// then at the end of frame just memsetting the entire structure to 0.
	for (unsigned int y = textureHeight - 1; y > 0; --y) {
		for (unsigned int x = ran ? 0 : textureWidth - 1; ran ? x < textureWidth : x > 0; ran ? ++x : --x) {
			// Set particle's update to false for next frame
			worldData.at(compute_idx(x, y)).has_been_updated_this_frame = false;
		}
	}
}

void CellularAutomata::update_default(uint32_t w, uint32_t h) {
	uint8_t read_idx = compute_idx(w, h);
	write_data(read_idx, get_particle_at(w, h));
}

void CellularAutomata::update_sand(uint32_t x, uint32_t y, const GameTimer& gt) {
	float dt = gt.DeltaTime();

	// For water, same as sand, but we'll check immediate left and right as well
	unsigned int read_idx = compute_idx(x, y);
	Particle* p = &worldData.at(read_idx);
	unsigned int write_idx = read_idx;
	unsigned int fall_rate = 4;

	p->velocity.y = std::clamp(p->velocity.y + (gravity * dt), -10.f, 10.f);

	// Just check if you can move directly beneath you. If not, then reset your velocity. God, this is going to blow.
	if (in_bounds(x, y + 1) && !is_empty(x, y + 1) && get_particle_at(x, y + 1).id != mat_id_water) {
		p->velocity.y /= 2.f;
	}

	int vi_x = x + (int)p->velocity.x;
	int vi_y = y + (int)p->velocity.y;

	// Check to see if you can swap first with other element below you
	unsigned int b_idx = compute_idx(x, y + 1);
	unsigned int br_idx = compute_idx(x + 1, y + 1);
	unsigned int bl_idx = compute_idx(x - 1, y + 1);

	int lx{}, ly{};

	Particle tmp_a = worldData.at(read_idx);

	// Physics (using velocity)
	if (in_bounds(vi_x, vi_y) && ((is_empty(vi_x, vi_y) ||
		(((worldData.at(compute_idx(vi_x, vi_y)).id == mat_id_water) &&
			!worldData.at(compute_idx(vi_x, vi_y)).has_been_updated_this_frame &&
			(worldData.at(compute_idx(vi_x, vi_y)).velocity.Length() - tmp_a.velocity.Length()) > 10.f))))) {

		Particle tmp_b = worldData.at(compute_idx(vi_x, vi_y));

		// Try to throw water out
		if (tmp_b.id == mat_id_water) {

			int rx = random_val(-2, 2);
			tmp_b.velocity = Vector2{ static_cast<float>(rx), -4.f };

			write_data(compute_idx(vi_x, vi_y), tmp_a);

			for (int i = -10; i < 0; ++i) {
				for (int j = -10; j < 10; ++j) {
					if (is_empty(vi_x + j, vi_y + i)) {
						write_data(compute_idx(vi_x + j, vi_y + i), tmp_b);
						break;
					}
				}
			}

			// Couldn't write there, so, uh, destroy it.
			write_data(read_idx, particle_empty());
		}
		else if (is_empty(vi_x, vi_y)) {
			write_data(compute_idx(vi_x, vi_y), tmp_a);
			write_data(read_idx, tmp_b);
		}
	}
	// Simple falling, changing the velocity here ruins everything. I need to redo this entire simulation.
	else if (in_bounds(x, y + 1) && ((is_empty(x, y + 1) || (worldData.at(b_idx).id == mat_id_water)))) {
		p->velocity.y += (gravity * dt);
		Particle tmp_b = get_particle_at(x, y + 1);
		write_data(b_idx, *p);
		write_data(read_idx, tmp_b);
	}
	else if (in_bounds(x - 1, y + 1) && ((is_empty(x - 1, y + 1) || worldData.at(bl_idx).id == mat_id_water))) {
		p->velocity.x = random_val(0, 1) == 0 ? -1.f : 1.f;
		p->velocity.y += (gravity * dt);
		Particle tmp_b = get_particle_at(x - 1, y + 1);
		write_data(bl_idx, *p);
		write_data(read_idx, tmp_b);
	}
	else if (in_bounds(x + 1, y + 1) && ((is_empty(x + 1, y + 1) || worldData.at(br_idx).id == mat_id_water))) {
		p->velocity.x = random_val(0, 1) == 0 ? -1.f : 1.f;
		p->velocity.y += (gravity * dt);
		Particle tmp_b = get_particle_at(x + 1, y + 1);
		write_data(br_idx, *p);
		write_data(read_idx, tmp_b);
	}
	else if (random_val(0, 10) == 0) {
		Particle tmp_b = get_particle_at(lx, ly);
		write_data(compute_idx(lx, ly), *p);
		write_data(read_idx, tmp_b);
	}
}

void CellularAutomata::update_water(uint32_t x, uint32_t y, const GameTimer& gt) {
	float dt = gt.DeltaTime();

	unsigned int read_idx = compute_idx(x, y);
	Particle* p = &worldData.at(read_idx);
	unsigned int write_idx = read_idx;
	int fall_rate = 2;
	int spread_rate = 5;

	p->velocity.y = std::clamp(p->velocity.y + (gravity * dt), -10.f, 10.f);

	p->has_been_updated_this_frame = true;

	// Just check if you can move directly beneath you. If not, then reset your velocity. God, this is going to blow.
	// if ( in_bounds( x, y + 1 ) && !is_empty( x, y + 1 ) && get_particle_at( x, y + 1 ).id != mat_id_water ) {
	if (in_bounds(x, y + 1) && !is_empty(x, y + 1)) {
		p->velocity.y /= 2.f;
	}

	// Change color depending on pressure? Pressure would dictate how "deep" the water is, I suppose.
	if (random_val(0, (int)(p->life_time * 100.f)) % 20 == 0) {
		float r = (float)(random_val(0, 1)) / 2.f;
		p->color.r = 25;
		p->color.g = 76;
		p->color.b = 178;
	}

	int ran = random_val(0, 1);
	int r = ran ? spread_rate : -spread_rate;
	int l = -r;
	int u = fall_rate;
	int v_idx = compute_idx(x + (int)p->velocity.x, y + (int)p->velocity.y);
	int b_idx = compute_idx(x, y + u);
	int bl_idx = compute_idx(x + l, y + u);
	int br_idx = compute_idx(x + r, y + u);
	int l_idx = compute_idx(x + l, y);
	int r_idx = compute_idx(x + r, y);
	int vx = (int)p->velocity.x, vy = (int)p->velocity.y;
	int lx{}, ly{};

	if (in_bounds(x + vx, y + vy) && (is_empty(x + vx, y + vy))) {
		write_data(v_idx, *p);
		write_data(read_idx, particle_empty());
	}
	else if (is_empty(x, y + u)) {
		write_data(b_idx, *p);
		write_data(read_idx, particle_empty());
	}
	else if (is_empty(x + r, y + u)) {
		write_data(br_idx, *p);
		write_data(read_idx, particle_empty());
	}
	else if (is_empty(x + l, y + u)) {
		write_data(bl_idx, *p);
		write_data(read_idx, particle_empty());
	}
	// Simple falling, changing the velocity here ruins everything. I need to redo this entire simulation.
	else if (in_bounds(x, y + u) && (is_empty(x, y + u))) {
		p->velocity.y += (gravity * dt);
		Particle tmp_b = get_particle_at(x, y + u);
		write_data(b_idx, *p);
		write_data(read_idx, tmp_b);
	}
	else if (in_bounds(x + l, y + u) && (is_empty(x + l, y + u))) {
		p->velocity.x = random_val(0, 1) == 0 ? -1.f : 1.f;
		p->velocity.y += (gravity * dt);
		Particle tmp_b = get_particle_at(x + l, y + u);
		write_data(bl_idx, *p);
		write_data(read_idx, tmp_b);
	}
	else if (in_bounds(x + r, y + u) && (is_empty(x + r, y + u) )) {
		p->velocity.x = random_val(0, 1) == 0 ? -1.f : 1.f;
		p->velocity.y += (gravity * dt);
		Particle tmp_b = get_particle_at(x + r, y + u);
		write_data(br_idx, *p);
		write_data(read_idx, tmp_b);
	}
	else if (random_val(0, 10) == 0) {
		Particle tmp_b = get_particle_at(lx, ly);
		write_data(compute_idx(lx, ly), *p);
		write_data(read_idx, tmp_b);
	}
	else {
		Particle tmp = *p;
		bool found = false;

		// Don't try to spread if something is directly above you?
		if (completely_surrounded(x, y)) {
			write_data(read_idx, tmp);
			return;
		}
		else {
			for (unsigned int i = 0; i < fall_rate; ++i) {
				for (int j = spread_rate; j > 0; --j)
				{
					if (in_bounds(x - j, y + i) && (is_empty(x - j, y + i))) {
						Particle tmp = get_particle_at(x - j, y + i);
						write_data(compute_idx(x - j, y + i), *p);
						write_data(read_idx, tmp);
						found = true;
						break;
					}
					if (in_bounds(x + j, y + i) && (is_empty(x + j, y + i))) {
						Particle tmp = get_particle_at(x + j, y + i);
						write_data(compute_idx(x + j, y + i), *p);
						write_data(read_idx, tmp);
						found = true;
						break;
					}
				}
			}

			if (!found) {
				write_data(read_idx, tmp);
			}
		}
	}
}

void CellularAutomata::write_data(uint32_t idx, Particle p) {
	// Write into particle data for id value
	worldData.at(idx) = p;
	colorData.at(idx) = p.color;

// 	for (const auto& p : worldData) {
// 		std::wstring text = L"\n Data written to : " + std::to_wstring(p.id);
// 		OutputDebugString(text.c_str());
// 	}	
}

int CellularAutomata::random_val(int lower, int upper) {
	if (upper < lower) {
		int tmp = lower;
		lower = upper;
		upper = tmp;
	}
	return (rand() % (upper - lower + 1) + lower);
}

int CellularAutomata::compute_idx(int x, int y) {
	return (y * textureWidth + x);
}

bool CellularAutomata::in_bounds(int x, int y) {
	if (x < 0 || x >(textureWidth - 1) || y < 0 || y >(textureHeight - 1)) return false;
	return true;
}

bool CellularAutomata::is_empty(int x, int y) {
	return (in_bounds(x, y) && worldData.at(compute_idx(x, y)).id == mat_id_empty);
}

Particle CellularAutomata::get_particle_at(int x, int y) {
	return worldData.at(compute_idx(x, y));

}

bool CellularAutomata::completely_surrounded(int x, int y) {
	// Top
	if (in_bounds(x, y - 1) && !is_empty(x, y - 1)) {
		return false;
	}
	// Bottom
	if (in_bounds(x, y + 1) && !is_empty(x, y + 1)) {
		return false;
	}
	// Left
	if (in_bounds(x - 1, y) && !is_empty(x - 1, y)) {
		return false;
	}
	// Right
	if (in_bounds(x + 1, y) && !is_empty(x + 1, y)) {
		return false;
	}
	// Top Left
	if (in_bounds(x - 1, y - 1) && !is_empty(x - 1, y - 1)) {
		return false;
	}
	// Top Right
	if (in_bounds(x + 1, y - 1) && !is_empty(x + 1, y - 1)) {
		return false;
	}
	// Bottom Left
	if (in_bounds(x - 1, y + 1) && !is_empty(x - 1, y + 1)) {
		return false;
	}
	// Bottom Right
	if (in_bounds(x + 1, y + 1) && !is_empty(x + 1, y + 1)) {
		return false;
	}

	return true;
}

bool CellularAutomata::is_in_water(int x, int y, int* lx, int* ly) {
	if (in_bounds(x, y) && (get_particle_at(x, y).id == mat_id_water)) {
		*lx = x; *ly = y;
		return true;
	}
	if (in_bounds(x, y - 1) && (get_particle_at(x, y - 1).id == mat_id_water)) {
		*lx = x; *ly = y - 1;
		return true;
	}
	if (in_bounds(x, y + 1) && (get_particle_at(x, y + 1).id == mat_id_water)) {
		*lx = x; *ly = y + 1;
		return true;
	}
	if (in_bounds(x - 1, y) && (get_particle_at(x - 1, y).id == mat_id_water)) {
		*lx = x - 1; *ly = y;
		return true;
	}
	if (in_bounds(x - 1, y - 1) && (get_particle_at(x - 1, y - 1).id == mat_id_water)) {
		*lx = x - 1; *ly = y - 1;
		return true;
	}
	if (in_bounds(x - 1, y + 1) && (get_particle_at(x - 1, y + 1).id == mat_id_water)) {
		*lx = x - 1; *ly = y + 1;
		return true;
	}
	if (in_bounds(x + 1, y) && (get_particle_at(x + 1, y).id == mat_id_water)) {
		*lx = x + 1; *ly = y;
		return true;
	}
	if (in_bounds(x + 1, y - 1) && (get_particle_at(x + 1, y - 1).id == mat_id_water)) {
		*lx = x + 1; *ly = y - 1;
		return true;
	}
	if (in_bounds(x + 1, y + 1) && (get_particle_at(x + 1, y + 1).id == mat_id_water)) {
		*lx = x + 1; *ly = y + 1;
		return true;
	}
	return false;
}

void CellularAutomata::putpixel(int x, int y) {

}

void CellularAutomata::drawCircle(int xc, int yc, int x, int y) {

}

void CellularAutomata::circleBres(int xc, int yc, int r) {

}

float CellularAutomata::vectorDistance(Vector2 vec1, Vector2 vec2) {
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
		IID_PPV_ARGS(&mTexture)));

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mTexture.Get(), 0, 1);

	// Create the GPU upload buffer.
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&textureUploadHeap)));

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = colorData.data();
	textureData.RowPitch = textureWidth * (sizeof(Color32));
	textureData.SlicePitch = textureData.RowPitch * textureHeight;

	UpdateSubresources(mCommandList.Get(), mTexture.Get(), textureUploadHeap.Get(), 0, 0, 1, &textureData);
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

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
	md3dDevice->CreateShaderResourceView(mTexture.Get(), &srvDesc, mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
}
