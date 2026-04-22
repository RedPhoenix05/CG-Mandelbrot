## Mandelbrot Infinite Zoom Renderer (DX12) — Implementation Plan

### 1. Project Setup

- Create a C++ project (Visual Studio)
- Add **DirectX 12** dependencies (Windows SDK / Agility SDK)
- Set up a window using Win32
- Verify build + run with an empty window

---

### 2. Initialize DX12 Core

- Create:
    - Device (`ID3D12Device`)
    - Command queue
    - Swap chain
    - Descriptor heaps (RTV)
    - Command allocator + command list
- Create render target views (RTVs) for back buffers
- Implement basic frame synchronization (fence)

---

### 3. Render a Fullscreen Pass

- Create a root signature (minimal: no resources or a small constant buffer)
- Compile simple HLSL shaders:
    - Vertex shader → fullscreen triangle
    - Pixel shader → solid color or UV gradient
- Create pipeline state object (PSO)
- Render to screen each frame

---

### 4. Implement Mandelbrot Shader

- Replace pixel shader with Mandelbrot logic:
    - Map screen UV → complex plane
    - Iterate fractal function
    - Output based on iteration count
- Add constant buffer for:
    - `center` (float2)
    - `scale` (zoom level)
    - `maxIterations`

---

### 5. Add Animation (Zoom)

- Update per-frame constants:
    - Gradually reduce `scale`
    - Optionally move `center` over time
- Pass updated values to GPU each frame

---

### 6. Improve Visual Quality

- Implement smooth coloring (continuous escape time)
- Add color palette mapping
- Tune iteration count vs performance

---

### 7. Handle Precision Limits

- Start with `float`
- Upgrade to `double` in shader if supported
- If artifacts appear at deep zoom:
    - Plan for perturbation method (future step)

---

### 8. Offscreen Rendering & Frame Capture

- Render to a texture instead of directly to swap chain
- Copy texture → CPU-readable buffer
- Save frames as images (e.g., PNG)

---

### 9. Video Generation

- Export frame image sequence
- Encode into video using FFmpeg (or maybe just Blender, we'll see)

---

### 10. Optimization Pass

- Adjust `maxIterations` dynamically with zoom depth
- Minimize shader branching
- Ensure GPU/CPU synchronization is efficient

---

### 11. Optional Enhancements

- Smooth camera path (not just straight zoom)
- Supersampling / anti-aliasing
- More advanced coloring techniques
- Interactive controls (pause, zoom target)