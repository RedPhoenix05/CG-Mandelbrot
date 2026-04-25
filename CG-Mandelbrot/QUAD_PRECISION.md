# Quad-Precision (Double-Double) Mode

## Overview
The Mandelbrot renderer now supports **quad-precision arithmetic** using double-double (FP128) emulation. This allows for significantly deeper zooms than standard double precision.

## Precision Modes

| Mode | Type | Effective Precision | Max Zoom Depth | Performance |
|------|------|---------------------|----------------|-------------|
| **FP32** | float | 32-bit | ~10^-7 | Fast |
| **FP64** | double | 64-bit | ~10^-15 | Medium |
| **FP128** | double-double | ~106-bit | ~10^-30 | Slow (5-10x slower) |

## Enabling Quad-Precision

In `mandelbrot.ini`, set:
```ini
enableQuadPrecision=1
```

Note: Your GPU must support double-precision operations (FP64). If not supported, the renderer will fall back to float32.

## Configuration for Deep Zooms

For extreme zooms, you'll want to adjust these settings in `mandelbrot.ini`:

```ini
# Allow much smaller scales
minScale=0.00000000000000000000000001

# Increase iterations as you zoom deeper
maxIterations=4096
minIterations=128
iterationsPerZoomOctave=64.0

# Slower zoom for smoother animation
zoomFactorPerSecond=0.95
```

## How It Works

Double-double arithmetic represents each number as a pair of doubles:
- **hi**: Contains the high-order bits (main value)
- **lo**: Contains the low-order bits (error correction)

This technique achieves ~106 bits of effective precision (compared to 53 bits for standard double).

### Implementation Details

1. **Shader Side** (`mandelbrot.hlsl`):
   - `dd_real` struct holds hi/lo components
   - Arithmetic operations (`dd_add`, `dd_mul`, `dd_sqr`) maintain precision
   - Uses Dekker's algorithm for exact floating-point arithmetic

2. **CPU Side** (`main.cpp`):
   - Maintains high-precision center coordinates (`centerX_qp`, `centerY_qp`)
   - Converts to quad-precision format for GPU constant buffer
   - Handles panning with full precision

## Performance Impact

Quad-precision mode is **5-10x slower** than double-precision due to:
- Multiple operations per arithmetic operation
- Increased register pressure
- More complex shader code

At typical zoom levels, you may see:
- FP64: 60 FPS at 1080p
- FP128: 8-15 FPS at 1080p

## When to Use Each Mode

- **FP32**: Initial exploration, scales > 10^-6
- **FP64**: Deep zooms, scales between 10^-6 and 10^-14
- **FP128**: Extreme zooms, scales < 10^-14

The renderer will automatically use the highest precision mode enabled and supported by your GPU.

## Limitations

- Maximum practical zoom is around **10^-30** before numerical instabilities appear
- For deeper zooms, perturbation theory would be required (not yet implemented)
- Performance degrades significantly, especially at high resolutions

## GPU Compatibility

Check the debug output at startup:
- `"Using quad-precision (FP128) shader mode."` - FP128 active
- `"Using double-precision (FP64) shader mode."` - FP64 active
- `"FP64 shader ops unsupported..."` - Falling back to FP32

Most modern GPUs support FP64, but with reduced performance compared to FP32.
