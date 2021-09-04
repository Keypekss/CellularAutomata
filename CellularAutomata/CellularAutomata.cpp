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

struct Particle {
	uint8_t id;
	float life_time;
	Vector2 velocity;
	Color color;
	bool has_been_updated_this_frame;
};

// material ids
#define mat_id_empty (uint8_t)0
#define mat_id_sand  (uint8_t)1
#define mat_id_water (uint8_t)2

// material colors
// Colors
#define mat_col_empty { 0.0, 0.0, 0.0, 0.0}
#define mat_col_sand  { 0.58, 0.39, 0.19, 1.0 }
#define mat_col_water { 0.07, 0.39, 0.66, 0.78 }

// width and height of texture buffer (equals to screen size)
constexpr int textureWidth = 800;
constexpr int textureHeight = 600;

enum class material_selection
{
	mat_sel_sand = 0,
	mat_sel_water	
};

// selected material (by default, it's sand)
material_selection selectedMaterial = material_selection::mat_sel_sand;

// world particle data
std::vector<Particle> worldData; 

// color data
std::vector<Color> colorData;

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

	// input handling
	LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;
	void OnMouseDown(WPARAM btnState, int x, int y) override;
	void OnMouseUp(WPARAM btnState, int x, int y) override;
	void OnMouseMove(WPARAM btnState, int x, int y) override;

	Particle particle_empty();
	Particle particle_sand();
	Particle particle_water();

	// particle updates
	void update_particle_sim(const GameTimer& gt);
	void update_sand(uint8_t x, uint8_t y, const GameTimer& gt);
	void update_water(uint8_t x, uint8_t y, const GameTimer& gt);
	void update_default(uint8_t w, uint8_t h);

	// Utility functions
	void write_data(uint8_t idx, Particle);
	Vector2 get_mouse_position();
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

	return true;
}

void CellularAutomata::OnResize()
{
	D3DApp::OnResize();
}

void CellularAutomata::Update(const GameTimer& gt)
{
	frameCounter = (frameCounter + 1) % UINT_MAX;
}

void CellularAutomata::Draw(const GameTimer& gt)
{
	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(mDirectCmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Set the viewport and scissor rect.  This needs to be reset whenever the command list is reset.
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

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

LRESULT CellularAutomata::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	D3DApp::MsgProc(hwnd, msg, wParam, lParam);

	switch (msg) {
	case WM_KEYDOWN:
		switch (wParam) {
		case 0x43: // button 'c'
			worldData.clear();
			worldData.reserve(textureWidth * textureHeight);

			colorData.clear();
			colorData.reserve(textureWidth * textureHeight);
			break;
		default:
			break;
		}		

	default:
		break;
	}

	return LRESULT();
}

void CellularAutomata::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	if (btnState == VK_LBUTTON)
	{
		Vector2 mp = get_mouse_position();
		float mp_x = std::clamp(mp.x, 0.f, (float)textureWidth - 1.f);
		float mp_y = std::clamp(mp.y, 0.f, (float)textureHeight - 1.f);
		int max_idx = (textureWidth * textureHeight) - 1;
		unsigned int r_amt = random_val(1, 10000);
		const float R = selectionRadius;

		// Spawn in a circle around the mouse
		for (unsigned int i = 0; i < r_amt; ++i)
		{
			float ran = (float)random_val(0, 100) / 100.f;
			float r = R * sqrt(ran);
			float theta = (float)random_val(0, 100) / 100.f * 2.f * MathHelper::Pi;
			float rx = cos((float)theta) * r;
			float ry = sin((float)theta) * r;
			int mpx = (int)std::clamp(mp_x + (float)rx, 0.f, (float)textureWidth - 1.f);
			int mpy = (int)std::clamp(mp_y + (float)ry, 0.f, (float)textureHeight - 1.f);
			int idx = mpy * (int)textureWidth + mpx;
			idx = std::clamp(idx, 0, max_idx);

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
		Vector2 mp = get_mouse_position();
		float mp_x = std::clamp(mp.x, 0.f, (float)textureWidth - 1.f);
		float mp_y = std::clamp(mp.y, 0.f, (float)textureHeight - 1.f);
		unsigned int max_idx = (textureWidth * textureHeight) - 1;
		const float R = selectionRadius;
	
		// Erase in a circle pattern
		for (int i = -R; i < R; ++i)
		{
			for (int j = -R; j < R; ++j)
			{
				int rx = ((int)mp_x + j);
				int ry = ((int)mp_y + i);
				Vector2 r = Vector2{ static_cast<float>(rx), static_cast<float>(ry) };
	
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
	if ((btnState & MK_LBUTTON) != 0)
	{

	}
	else if ((btnState & MK_RBUTTON) != 0)
	{

	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
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
	float r = (float)(random_val(0, 10)) / 10.f;
	p.color.x = (0.8f, 1.f, r);
	p.color.y = (0.5f, 0.6f, r);
	p.color.z = (0.2f, 0.25f, r);
	p.color.w = 255;
	return p;
}

Particle CellularAutomata::particle_water() {
	Particle p = { 0 };
	p.id = mat_id_water;
	float r = (float)(random_val(0, 1)) / 2.f;
	p.color.x = (0.1f, 0.15f, r);
	p.color.y = (0.3f, 0.35f, r);
	p.color.z = (0.7f, 0.8f, r);
	p.color.w = 255;
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

void CellularAutomata::update_default(uint8_t w, uint8_t h) {
	uint8_t read_idx = compute_idx(w, h);
	write_data(read_idx, get_particle_at(w, h));
}

void CellularAutomata::update_sand(uint8_t x, uint8_t y, const GameTimer& gt) {
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

void CellularAutomata::update_water(uint8_t x, uint8_t y, const GameTimer& gt) {
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
		p->color.x = (0.1f, 0.15f, r);
		p->color.y = (0.3f, 0.35f, r);
		p->color.z = (0.7f, 0.8f, r);
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

void CellularAutomata::write_data(uint8_t idx, Particle p) {
	// Write into particle data for id value
	worldData.at(idx) = p;
	colorData.at(idx) = p.color;
}

Vector2 CellularAutomata::get_mouse_position() {
	Vector2 mousePos;
	
	mousePos.x = mLastMousePos.x;
	mousePos.y = mLastMousePos.y;

	return mousePos;
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