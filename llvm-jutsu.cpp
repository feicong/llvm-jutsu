#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Constants.h"
#if __has_include("llvm/Plugins/PassPlugin.h")
#include "llvm/Plugins/PassPlugin.h"
#elif __has_include("llvm/Passes/PassPlugin.h")
#include "llvm/Passes/PassPlugin.h"
#else
#error "LLVM pass plugin header not found"
#endif
#include "llvm/Passes/PassBuilder.h"
#include "lodepng.h"
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>

using namespace llvm;

//==============================================================================
// Compile-time configuration (set these via #define before including)
//==============================================================================

#ifndef HAND_SIZE
#define HAND_SIZE 128
#endif

#ifndef HAND_STROKE_WIDTH
#define HAND_STROKE_WIDTH 0.025f
#endif

#ifndef HAND_STROKE_R
#define HAND_STROKE_R 0.0f
#endif
#ifndef HAND_STROKE_G
#define HAND_STROKE_G 0.0f
#endif
#ifndef HAND_STROKE_B
#define HAND_STROKE_B 0.0f
#endif

#ifndef HAND_BASE_HUE
#define HAND_BASE_HUE 0.9167f  // 330/360, pink
#endif

#ifndef HAND_ANGLE_AMP
#define HAND_ANGLE_AMP 0.05f
#endif

#ifndef HAND_POSITION_AMP
#define HAND_POSITION_AMP 0.02f
#endif

#ifndef HAND_ROTATION_MAX
#define HAND_ROTATION_MAX 360.0f
#endif

#ifndef HAND_HUE_VARIATION
#define HAND_HUE_VARIATION 0.5f
#endif

#ifndef HAND_MASTER_SEED
#define HAND_MASTER_SEED 0xDEADBEEFu
#endif

namespace {

constexpr float PI = 3.14159265358979323846f;
constexpr float PHI = 1.618033988749895f;  // golden ratio

//==============================================================================
// Fast hash (non-invertible, for deriving params)
//==============================================================================

uint64_t fast_hash(uint64_t x, uint64_t seed) {
    x ^= seed;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

float hash_to_float(uint64_t h, float lo, float hi) {
    return lo + (float)(h & 0xFFFFFFFF) / 4294967295.0f * (hi - lo);
}

//==============================================================================
// Runtime params derived from hash
//==============================================================================

struct RuntimeParams {
    float finger_t;
    float rotation;
    float hue_shift;
    float wave_amp;
    float wave_freq;
    float wave_phase;
    float stroke_r;
    float stroke_g;
    float stroke_b;
};

RuntimeParams derive_runtime_params(uint32_t value) {
    uint64_t h = fast_hash(value, HAND_MASTER_SEED);
    RuntimeParams p;
    p.finger_t = hash_to_float(h, 0, 2 * PI);
    h = fast_hash(h, 0x12345678);
    p.rotation = hash_to_float(h, 0, HAND_ROTATION_MAX);
    h = fast_hash(h, 0x87654321);
    p.hue_shift = hash_to_float(h, 0, HAND_HUE_VARIATION);
    h = fast_hash(h, 0xABCDEF01);
    p.wave_amp = 0;  // disabled for now
    p.wave_freq = 1;
    p.wave_phase = 0;
    // Derive stroke color (dark colors for contrast)
    h = fast_hash(h, 0xFEDCBA98);
    p.stroke_r = hash_to_float(h, 0, 0.3f);
    h = fast_hash(h, 0x13579BDF);
    p.stroke_g = hash_to_float(h, 0, 0.3f);
    h = fast_hash(h, 0x2468ACE0);
    p.stroke_b = hash_to_float(h, 0, 0.3f);
    return p;
}

//==============================================================================
// Per-finger params
//==============================================================================

struct FingerParams {
    float angle_offset;
    float origin_offset_x;
    float origin_offset_y;
};

FingerParams compute_finger_param(int i, float t) {
    float freq_angle = 1 + i * PHI;
    float freq_x = 2 + i * 0.7f;
    float freq_y = 1.5f + i * PHI * 0.5f;
    float phase_angle = i * PHI * 2;
    float phase_x = i * 1.3f;
    float phase_y = i * PHI;
    float dist_from_center = std::abs(i - 3.5f) / 3.5f;
    float amp_scale = 0.5f + 0.5f * dist_from_center;

    FingerParams fp;
    fp.angle_offset = amp_scale * HAND_ANGLE_AMP * std::sin(t * freq_angle + phase_angle);
    fp.origin_offset_x = amp_scale * HAND_POSITION_AMP * std::sin(t * freq_x + phase_x);
    fp.origin_offset_y = amp_scale * HAND_POSITION_AMP * std::cos(t * freq_y + phase_y);
    return fp;
}

//==============================================================================
// Color conversion
//==============================================================================

void hsv_to_rgb(float h, float s, float v, float &r, float &g, float &b) {
    if (s == 0) { r = g = b = v; return; }
    h = h * 6;
    int i = (int)h;
    float f = h - i;
    float p = v * (1 - s);
    float q = v * (1 - s * f);
    float t = v * (1 - s * (1 - f));
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

//==============================================================================
// SDF primitives
//==============================================================================

float sdf_rounded_rect(float x, float y, float cx, float cy, float hw, float hh, float radius) {
    float px = std::abs(x - cx) - hw + radius;
    float py = std::abs(y - cy) - hh + radius;
    float outside = std::sqrt(std::max(px, 0.0f) * std::max(px, 0.0f) +
                              std::max(py, 0.0f) * std::max(py, 0.0f)) - radius;
    float inside = std::min(std::max(px, py), 0.0f) - radius;
    return (px > 0 || py > 0) ? outside : inside;
}

float sdf_capsule(float x, float y, float bx, float by, float length, float angle, float radius) {
    float dx = x - bx;
    float dy = y - by;
    float cos_a = std::cos(angle);
    float sin_a = std::sin(angle);
    float local_x = dx * cos_a + dy * sin_a;
    float local_y = -dx * sin_a + dy * cos_a;

    float t = (length > 0) ? std::max(0.0f, std::min(1.0f, -local_y / length)) : 0;
    float closest_y = -t * length;
    return std::sqrt(local_x * local_x + (local_y - closest_y) * (local_y - closest_y)) - radius;
}

//==============================================================================
// Hand geometry constants
//==============================================================================

constexpr float PALM_CENTER_X = 0.0f;
constexpr float PALM_CENTER_Y = 0.20f;
constexpr float PALM_HALF_W = 0.22f;
constexpr float PALM_HALF_H = 0.16f;
constexpr float PALM_RADIUS = 0.08f;

constexpr float FINGER_WIDTH = 0.055f;
constexpr float FINGER_LENGTH_UP = 0.25f;
constexpr float FINGER_LENGTH_DOWN = 0.08f;

constexpr float THUMB_ANGLE = -42.0f * PI / 180.0f;
constexpr float FINGER_ANGLES[7] = {
    -7.0f * PI / 180.0f,
    -4.5f * PI / 180.0f,
    -2.0f * PI / 180.0f,
    0.0f,
    2.0f * PI / 180.0f,
    4.5f * PI / 180.0f,
    7.0f * PI / 180.0f
};

constexpr int MIDDLE_FINGER_IDX = 3;

//==============================================================================
// Hand rendering
//==============================================================================

struct HandSDFs {
    // Each element: base_x, base_y, angle, length, is_palm
    // Palm stored specially
    float palm_sdf(float x, float y) const {
        return sdf_rounded_rect(x, y, PALM_CENTER_X, PALM_CENTER_Y,
                                PALM_HALF_W, PALM_HALF_H, PALM_RADIUS);
    }

    struct Finger {
        float base_x, base_y, angle, length;
    };
    Finger fingers[8];
    int num_up;
    int up_indices[8];
    int num_down;
    int down_indices[8];
};

HandSDFs build_hand_sdfs(uint8_t value, const FingerParams* finger_params) {
    HandSDFs h;
    h.num_up = 0;
    h.num_down = 0;

    float all_angles[8];
    all_angles[0] = THUMB_ANGLE;
    for (int i = 0; i < 7; i++) all_angles[i + 1] = FINGER_ANGLES[i];

    for (int i = 0; i < 8; i++) {
        bool up = (value >> (7 - i)) & 1;
        float angle = all_angles[i];
        float length;

        if (i == 0) {
            length = up ? FINGER_LENGTH_UP * 0.65f : FINGER_LENGTH_DOWN * 0.8f;
        } else {
            length = up ? FINGER_LENGTH_UP : FINGER_LENGTH_DOWN;
            float mid_idx = 3.0f;
            length *= 1.0f + 0.12f * (1.0f - std::abs(i - mid_idx) / mid_idx);
        }

        float base_x, base_y;
        if (i == 0) {
            base_x = PALM_CENTER_X - PALM_HALF_W;
            base_y = PALM_CENTER_Y + 0.02f;
        } else {
            int finger_idx = i - 1;
            float t = finger_idx / 6.0f;
            base_x = PALM_CENTER_X + (t - 0.5f) * PALM_HALF_W * 1.8f;

            int dist_from_middle = i - MIDDLE_FINGER_IDX;
            float y_drop;
            if (dist_from_middle < 0) {
                y_drop = std::abs(dist_from_middle) * 0.025f;
            } else {
                y_drop = dist_from_middle * 0.005f;
            }
            if (i == 7) y_drop += 0.04f;
            base_y = PALM_CENTER_Y - PALM_HALF_H + y_drop;
        }

        if (finger_params) {
            angle += finger_params[i].angle_offset;
            base_x += finger_params[i].origin_offset_x;
            base_y += finger_params[i].origin_offset_y;
        }

        h.fingers[i] = {base_x, base_y, angle, length};

        if (up) {
            h.up_indices[h.num_up++] = i;
        } else {
            h.down_indices[h.num_down++] = i;
        }
    }
    return h;
}

float eval_finger_sdf(const HandSDFs::Finger& f, float x, float y) {
    return sdf_capsule(x, y, f.base_x, f.base_y, f.length, f.angle, FINGER_WIDTH / 2);
}

//==============================================================================
// Rendering
//==============================================================================

void render_hand(uint8_t value, const RuntimeParams& rt, std::vector<uint8_t>& rgba_out) {
    FingerParams fps[8];
    for (int i = 0; i < 8; i++) {
        fps[i] = compute_finger_param(i, rt.finger_t);
    }

    HandSDFs hand = build_hand_sdfs(value, fps);

    float pixel_size = 1.0f / HAND_SIZE;
    float edge_width = pixel_size * 1.5f;

    // Get fill color
    float fill_hue = std::fmod(HAND_BASE_HUE + rt.hue_shift, 1.0f);
    float fr, fg, fb;
    hsv_to_rgb(fill_hue, 0.6f, 1.0f, fr, fg, fb);

    // Stroke color from runtime params
    float sr = rt.stroke_r, sg = rt.stroke_g, sb = rt.stroke_b;

    // Rotation
    float rot_rad = rt.rotation * PI / 180.0f;
    float cos_r = std::cos(-rot_rad);
    float sin_r = std::sin(-rot_rad);

    rgba_out.resize(HAND_SIZE * HAND_SIZE * 4);

    for (int py = 0; py < HAND_SIZE; py++) {
        for (int px = 0; px < HAND_SIZE; px++) {
            float x = (px + 0.5f) / HAND_SIZE - 0.5f;
            float y = (py + 0.5f) / HAND_SIZE - 0.5f;

            // Apply rotation
            float rx = x * cos_r - y * sin_r;
            float ry = x * sin_r + y * cos_r;

            // Composite elements back-to-front: up fingers, palm, down fingers
            float total_fill = 0, total_stroke = 0;

            auto composite_element = [&](float d) {
                float fill_cov = std::max(0.0f, std::min(1.0f, 0.5f - d / edge_width));
                float outer_d = d - HAND_STROKE_WIDTH;
                float stroke_outer = std::max(0.0f, std::min(1.0f, 0.5f - outer_d / edge_width));
                float stroke_cov = std::max(0.0f, stroke_outer - fill_cov);
                float elem_cov = std::min(1.0f, fill_cov + stroke_cov);
                total_fill = total_fill * (1 - elem_cov) + fill_cov;
                total_stroke = total_stroke * (1 - elem_cov) + stroke_cov;
            };

            // Up fingers (behind palm)
            for (int i = 0; i < hand.num_up; i++) {
                composite_element(eval_finger_sdf(hand.fingers[hand.up_indices[i]], rx, ry));
            }

            // Palm
            composite_element(hand.palm_sdf(rx, ry));

            // Down fingers (in front of palm)
            for (int i = 0; i < hand.num_down; i++) {
                composite_element(eval_finger_sdf(hand.fingers[hand.down_indices[i]], rx, ry));
            }

            // Final compositing: stroke on top
            float final_fill = total_fill * (1 - total_stroke);
            float final_stroke = total_stroke;
            float alpha = final_fill + final_stroke;

            float r = 0, g = 0, b = 0;
            if (alpha > 0) {
                r = (final_fill * fr + final_stroke * sr) / alpha;
                g = (final_fill * fg + final_stroke * sg) / alpha;
                b = (final_fill * fb + final_stroke * sb) / alpha;
            }

            int idx = (py * HAND_SIZE + px) * 4;
            rgba_out[idx + 0] = (uint8_t)(r * 255);
            rgba_out[idx + 1] = (uint8_t)(g * 255);
            rgba_out[idx + 2] = (uint8_t)(b * 255);
            rgba_out[idx + 3] = (uint8_t)(alpha * 255);
        }
    }
}

//==============================================================================
// Render i32 as 2x2 grid
//==============================================================================

std::vector<uint8_t> render_i32_png(uint32_t value) {
    RuntimeParams global_rt = derive_runtime_params(value);

    int grid_size = HAND_SIZE * 2;
    std::vector<uint8_t> grid_rgba(grid_size * grid_size * 4, 0);

    for (int i = 0; i < 4; i++) {
        uint8_t byte_val = (value >> (i * 8)) & 0xFF;

        // Mix params based on position
        RuntimeParams hand_rt;
        hand_rt.finger_t = global_rt.finger_t + i * PHI;
        hand_rt.rotation = (HAND_ROTATION_MAX > 0)
            ? std::fmod(global_rt.rotation + i * 90 * PHI, HAND_ROTATION_MAX)
            : 0;
        hand_rt.hue_shift = std::fmod(global_rt.hue_shift + i * 0.25f, 1.0f);
        hand_rt.wave_amp = global_rt.wave_amp;
        hand_rt.wave_freq = global_rt.wave_freq;
        hand_rt.wave_phase = global_rt.wave_phase + i * PI / 2;
        // Per-hand stroke color variation
        uint64_t stroke_h = fast_hash(value ^ (i * 0x9E3779B9), 0x5720CE01);
        hand_rt.stroke_r = hash_to_float(stroke_h, 0, 1.0f);
        stroke_h = fast_hash(stroke_h, 0xC010E123);
        hand_rt.stroke_g = hash_to_float(stroke_h, 0, 1.0f);
        stroke_h = fast_hash(stroke_h, 0x456C010E);
        hand_rt.stroke_b = hash_to_float(stroke_h, 0, 1.0f);

        std::vector<uint8_t> hand_rgba;
        render_hand(byte_val, hand_rt, hand_rgba);

        // Copy to grid position
        int gx = (i % 2) * HAND_SIZE;
        int gy = (i / 2) * HAND_SIZE;

        for (int y = 0; y < HAND_SIZE; y++) {
            for (int x = 0; x < HAND_SIZE; x++) {
                int src = (y * HAND_SIZE + x) * 4;
                int dst = ((gy + y) * grid_size + (gx + x)) * 4;
                grid_rgba[dst + 0] = hand_rgba[src + 0];
                grid_rgba[dst + 1] = hand_rgba[src + 1];
                grid_rgba[dst + 2] = hand_rgba[src + 2];
                grid_rgba[dst + 3] = hand_rgba[src + 3];
            }
        }
    }

    std::vector<uint8_t> png;
    lodepng::encode(png, grid_rgba, grid_size, grid_size, LCT_RGBA, 8);
    return png;
}

//==============================================================================
// LLVM Pass
//==============================================================================

struct LLVMJutsuPass : public PassInfoMixin<LLVMJutsuPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
        std::vector<ICmpInst *> ToTransform;

        for (Function &F : M) {
            if (F.isDeclaration()) continue;
            if (F.getName().contains("lodepng") || F.getName().contains("compare_hand"))
                continue;

            for (BasicBlock &BB : F) {
                for (Instruction &I : BB) {
                    if (auto *ICI = dyn_cast<ICmpInst>(&I)) {
                        if (ICI->getPredicate() != ICmpInst::ICMP_EQ &&
                            ICI->getPredicate() != ICmpInst::ICMP_NE)
                            continue;
                        Value *Op0 = ICI->getOperand(0);
                        Value *Op1 = ICI->getOperand(1);
                        bool Op0Const = isa<ConstantInt>(Op0);
                        bool Op1Const = isa<ConstantInt>(Op1);
                        if (Op0Const == Op1Const) continue;
                        Type *Ty = Op0->getType();
                        if (!Ty->isIntegerTy()) continue;
                        unsigned BitWidth = Ty->getIntegerBitWidth();
                        // Only handle i32 for now (2x2 grid)
                        if (BitWidth != 32) continue;
                        ToTransform.push_back(ICI);
                    }
                }
            }
        }

        errs() << "llvm-jutsu: Found " << ToTransform.size() << " i32 comparisons to transform\n";

        for (ICmpInst *ICI : ToTransform) {
            transformComparison(M, ICI);
        }

        return PreservedAnalyses::none();
    }

private:
    void transformComparison(Module &M, ICmpInst *ICI) {
        LLVMContext &Ctx = M.getContext();

        Value *Op0 = ICI->getOperand(0);
        Value *Op1 = ICI->getOperand(1);
        bool IsNE = ICI->getPredicate() == ICmpInst::ICMP_NE;

        Value *VarOp = isa<ConstantInt>(Op0) ? Op1 : Op0;
        ConstantInt *ConstOp = isa<ConstantInt>(Op0)
                                   ? cast<ConstantInt>(Op0)
                                   : cast<ConstantInt>(Op1);

        uint32_t ConstVal = (uint32_t)ConstOp->getZExtValue();

        Type *I8Ty = Type::getInt8Ty(Ctx);
        Type *I32Ty = Type::getInt32Ty(Ctx);
        Type *I8PtrTy = PointerType::get(I8Ty, 0);

        // Generate PNG at compile time
        std::vector<uint8_t> pngData = render_i32_png(ConstVal);

        // Embed PNG as global constant
        ArrayType *PngArrayTy = ArrayType::get(I8Ty, pngData.size());
        std::vector<Constant *> PngBytes;
        for (uint8_t b : pngData)
            PngBytes.push_back(ConstantInt::get(I8Ty, b));
        Constant *PngInit = ConstantArray::get(PngArrayTy, PngBytes);
        GlobalVariable *PngGV = new GlobalVariable(
            M, PngArrayTy, true, GlobalValue::PrivateLinkage, PngInit, "hand_png");

        // Get or declare compare function
        // int compare_hand_png_i32(uint32_t value, const uint8_t* png, uint32_t pngSize)
        FunctionType *CmpFnTy = FunctionType::get(I32Ty, {I32Ty, I8PtrTy, I32Ty}, false);
        FunctionCallee CmpFn = M.getOrInsertFunction("compare_hand_png_i32", CmpFnTy);

        IRBuilder<> Builder(ICI);

        Value *PngPtr = Builder.CreateBitCast(PngGV, I8PtrTy);
        Value *PngSizeVal = ConstantInt::get(I32Ty, pngData.size());

        Value *CmpResult = Builder.CreateCall(CmpFn, {VarOp, PngPtr, PngSizeVal});

        // compare returns 1 for match, 0 for mismatch
        Value *Result = Builder.CreateICmpNE(CmpResult, ConstantInt::get(I32Ty, 0));

        if (IsNE)
            Result = Builder.CreateNot(Result);

        ICI->replaceAllUsesWith(Result);
        ICI->eraseFromParent();
    }
};

PassPluginLibraryInfo getLLVMJutsuPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "llvm-jutsu", LLVM_VERSION_STRING,
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, ModulePassManager &MPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "llvm-jutsu") {
                            MPM.addPass(LLVMJutsuPass());
                            return true;
                        }
                        return false;
                    });
            }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return getLLVMJutsuPassPluginInfo();
}

} // namespace
