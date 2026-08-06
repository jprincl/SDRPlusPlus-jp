#pragma once
#include <cstdint>
#include <string>

namespace sstv {

    // SSTV mode identifiers
    enum class Mode {
        AUTO        = -1,
        UNKNOWN     = -2,
        MARTIN_M1   = 0,
        MARTIN_M2   = 1,
        SCOTTIE_S1  = 2,
        SCOTTIE_S2  = 3,
        SCOTTIE_DX  = 4,
        ROBOT_36    = 5,
        ROBOT_72    = 6,
        PD50        = 7,
        PD90        = 8,
        PD120       = 9,
        PD180       = 10,
        PD160       = 11,
        PD240       = 12,
        PD290       = 13,
    };

    // What a single segment of a line means for the decoder
    enum class SegmentKind {
        IGNORE,      // Sync, separator, porch - skip
        Y_GREEN,     // Plain pixel scan into Green channel (Martin, Scottie)
        Y_BLUE,
        Y_RED,
        PD_Y1,       // PD: Y of pixel row N (luminance)
        PD_R,        // PD: R-Y of pixel row N (and N+1)
        PD_B,        // PD: B-Y of pixel row N (and N+1)
        PD_Y2,       // PD: Y of pixel row N+1
        R72_Y,       // Robot 72: Y for current image line
        R72_R,       // Robot 72: R-Y for current image line (half-res chroma)
        R72_B,       // Robot 72: B-Y for current image line
        R36_Y_ODD,   // Robot 36: Y of odd line (with R-Y chroma after)
        R36_RY,      // Robot 36: R-Y for the previous Y (half-res, shared with even line)
        R36_Y_EVEN,  // Robot 36: Y of even line (with B-Y chroma after)
        R36_BY,      // Robot 36: B-Y for the previous Y
    };

    struct LineSegment {
        SegmentKind kind;
        float       duration;       // seconds
        uint32_t    pixelCount;     // 0 for IGNORE
    };

    // Color encoding family
    enum class ColorModel {
        MARTIN_GBR,   // G/B/R sub-scans per line
        SCOTTIE_GBR,  // G/B/R sub-scans (sync in middle of line)
        PD_YUV,       // Y1/R-Y/B-Y/Y2 over 2 image lines per cycle
        R72_YUV,      // Y/R-Y/B-Y per line, chroma at half res
        R36_YUV,      // Robot 36: 2 image lines per cycle, alternating R-Y/B-Y chroma
    };

    // VIS frequencies / timing
    constexpr float SSTV_BLACK_HZ        = 1500.0f;
    constexpr float SSTV_WHITE_HZ        = 2300.0f;
    constexpr float SSTV_SYNC_HZ         = 1200.0f;
    constexpr float SSTV_PORCH_HZ        = 1500.0f;
    constexpr float SSTV_VIS_LEADER_HZ   = 1900.0f;
    constexpr float SSTV_VIS_START_HZ    = 1200.0f;
    constexpr float SSTV_VIS_BIT0_HZ     = 1300.0f;
    constexpr float SSTV_VIS_BIT1_HZ     = 1100.0f;
    constexpr float VIS_LEADER_DURATION  = 0.300f;
    constexpr float VIS_BREAK_DURATION   = 0.010f;
    constexpr float VIS_BIT_DURATION     = 0.030f;

    struct ModeParams {
        Mode             mode;
        const char*      shortName;
        const char*      longName;
        uint8_t          visCode;
        ColorModel       colorModel;
        uint32_t         width;
        uint32_t         height;
        uint32_t         linesPerCycle;
        float            cycleDuration;
        const LineSegment* segments;
        uint32_t         segmentCount;
        float            startupOffsetDuration;
        // Sync detection
        float            syncOffsetInCycle;   // midpoint of (first) sync inside one cycle (sec)
        float            syncDuration;        // sync pulse duration (sec)
        // Number of sync pulses emitted per cycle. 1 for most modes; Robot 36
        // emits 2 (one per half-cycle line). The slant calibrator divides the
        // detected sync count by this to recover the per-cycle index.
        int              syncsPerCycle;
    };

    // ==================== MARTIN ====================
    // Layout: sync(4.862) porch(0.572) G(146.432) sep(0.572) B(146.432) sep(0.572) R(146.432) sep(0.572)

    constexpr LineSegment MARTIN_M1_SEGMENTS[] = {
        { SegmentKind::IGNORE,  0.004862f, 0 },
        { SegmentKind::IGNORE,  0.000572f, 0 },
        { SegmentKind::Y_GREEN, 0.146432f, 320 },
        { SegmentKind::IGNORE,  0.000572f, 0 },
        { SegmentKind::Y_BLUE,  0.146432f, 320 },
        { SegmentKind::IGNORE,  0.000572f, 0 },
        { SegmentKind::Y_RED,   0.146432f, 320 },
        { SegmentKind::IGNORE,  0.000572f, 0 },
    };
    constexpr LineSegment MARTIN_M2_SEGMENTS[] = {
        { SegmentKind::IGNORE,  0.004862f, 0 },
        { SegmentKind::IGNORE,  0.000572f, 0 },
        { SegmentKind::Y_GREEN, 0.073216f, 320 },
        { SegmentKind::IGNORE,  0.000572f, 0 },
        { SegmentKind::Y_BLUE,  0.073216f, 320 },
        { SegmentKind::IGNORE,  0.000572f, 0 },
        { SegmentKind::Y_RED,   0.073216f, 320 },
        { SegmentKind::IGNORE,  0.000572f, 0 },
    };

    constexpr ModeParams MARTIN_M1_PARAMS = {
        Mode::MARTIN_M1, "M1", "Martin M1", 0x2C, ColorModel::MARTIN_GBR,
        320, 256, 1, 0.446446f,
        MARTIN_M1_SEGMENTS, sizeof(MARTIN_M1_SEGMENTS) / sizeof(LineSegment),
        0.0f, 0.002431f, 0.004862f, 1
    };
    constexpr ModeParams MARTIN_M2_PARAMS = {
        Mode::MARTIN_M2, "M2", "Martin M2", 0x28, ColorModel::MARTIN_GBR,
        320, 256, 1, 0.226798f,
        MARTIN_M2_SEGMENTS, sizeof(MARTIN_M2_SEGMENTS) / sizeof(LineSegment),
        0.0f, 0.002431f, 0.004862f, 1
    };

    // ==================== SCOTTIE ====================
    // Layout: sep(=blank) + G + sep + B + porch(=fp) + sync + porch(=bp) + R
    //   S1:  blank=1.5, fp=0.5(?), sync=9, bp=1.5, scan=138.24ms => cycle ~428ms
    //   S2:  blank=1.5, fp=0.5,    sync=9, bp=1.5, scan=88.064  => cycle ~278ms
    //   SDX: blank=1.0, fp=0,      sync=9, bp=0,   scan~346.789 => cycle ~1050ms
    //
    // Note: per QSSTV the sequence starts at G (no leading blank in segment table);
    // however we keep our existing "sep before G" since it works in practice and
    // the slant correction compensates any small offset.

    constexpr LineSegment SCOTTIE_S1_SEGMENTS[] = {
        { SegmentKind::IGNORE,  0.001500f, 0 },
        { SegmentKind::Y_GREEN, 0.138240f, 320 },
        { SegmentKind::IGNORE,  0.001500f, 0 },
        { SegmentKind::Y_BLUE,  0.138240f, 320 },
        { SegmentKind::IGNORE,  0.000500f, 0 },
        { SegmentKind::IGNORE,  0.009000f, 0 },
        { SegmentKind::IGNORE,  0.001500f, 0 },
        { SegmentKind::Y_RED,   0.138240f, 320 },
    };
    constexpr LineSegment SCOTTIE_S2_SEGMENTS[] = {
        { SegmentKind::IGNORE,  0.001500f, 0 },
        { SegmentKind::Y_GREEN, 0.088064f, 320 },
        { SegmentKind::IGNORE,  0.001500f, 0 },
        { SegmentKind::Y_BLUE,  0.088064f, 320 },
        { SegmentKind::IGNORE,  0.000500f, 0 },
        { SegmentKind::IGNORE,  0.009000f, 0 },
        { SegmentKind::IGNORE,  0.001500f, 0 },
        { SegmentKind::Y_RED,   0.088064f, 320 },
    };
    // Scottie DX: blank=1ms between scans, no porch, just sync(9ms). 3 scans of ~345ms
    //   Cycle 268.8938/256 = 1050.37 ms
    //   3v = 1050.37 - 1 - 9 - 1 = 1039.37 => v = 346.456 ms
    constexpr LineSegment SCOTTIE_DX_SEGMENTS[] = {
        { SegmentKind::IGNORE,  0.001000f, 0 },
        { SegmentKind::Y_GREEN, 0.345600f, 320 },
        { SegmentKind::IGNORE,  0.001000f, 0 },
        { SegmentKind::Y_BLUE,  0.345600f, 320 },
        { SegmentKind::IGNORE,  0.001000f, 0 },
        { SegmentKind::IGNORE,  0.009000f, 0 },
        { SegmentKind::IGNORE,  0.001000f, 0 },
        { SegmentKind::Y_RED,   0.345600f, 320 },
    };

    constexpr ModeParams SCOTTIE_S1_PARAMS = {
        Mode::SCOTTIE_S1, "S1", "Scottie S1", 0x3C, ColorModel::SCOTTIE_GBR,
        320, 256, 1, 0.428220f,
        SCOTTIE_S1_SEGMENTS, sizeof(SCOTTIE_S1_SEGMENTS) / sizeof(LineSegment),
        0.009000f, 0.284480f, 0.009000f, 1
    };
    constexpr ModeParams SCOTTIE_S2_PARAMS = {
        Mode::SCOTTIE_S2, "S2", "Scottie S2", 0x38, ColorModel::SCOTTIE_GBR,
        320, 256, 1, 0.278192f,
        SCOTTIE_S2_SEGMENTS, sizeof(SCOTTIE_S2_SEGMENTS) / sizeof(LineSegment),
        0.009000f, 0.184128f, 0.009000f, 1
    };
    // Scottie DX: sync midpoint = 1 + 345.6 + 1 + 345.6 + 1 + 4.5 = 698.7 ms
    constexpr ModeParams SCOTTIE_DX_PARAMS = {
        Mode::SCOTTIE_DX, "SDX", "Scottie DX", 0x4C, ColorModel::SCOTTIE_GBR,
        320, 256, 1, 1.049800f,
        SCOTTIE_DX_SEGMENTS, sizeof(SCOTTIE_DX_SEGMENTS) / sizeof(LineSegment),
        0.009000f, 0.698700f, 0.009000f, 1
    };

    // ==================== ROBOT 72 (full YCrCb per line) ====================
    // Per line: sync(9) + bp(3.5) + Y(137.56) + blank(6) + R-Y(68.78) + blank(6) + B-Y(68.78) + fp(0.4)
    // = 300.02 ms/line, 240 lines, 320x240
    constexpr LineSegment ROBOT_72_SEGMENTS[] = {
        { SegmentKind::IGNORE,  0.009000f, 0 },     // Sync
        { SegmentKind::IGNORE,  0.003500f, 0 },     // BP
        { SegmentKind::R72_Y,   0.137560f, 320 },   // Y (2v)
        { SegmentKind::IGNORE,  0.006000f, 0 },     // Blank
        { SegmentKind::R72_R,   0.068780f, 320 },   // R-Y (v)
        { SegmentKind::IGNORE,  0.006000f, 0 },     // Blank
        { SegmentKind::R72_B,   0.068780f, 320 },   // B-Y (v)
        { SegmentKind::IGNORE,  0.000400f, 0 },     // FP
    };
    constexpr ModeParams ROBOT_72_PARAMS = {
        Mode::ROBOT_72, "R72", "Robot 72 Color", 0x0C, ColorModel::R72_YUV,
        320, 240, 1, 0.300020f,
        ROBOT_72_SEGMENTS, sizeof(ROBOT_72_SEGMENTS) / sizeof(LineSegment),
        0.0f, 0.004500f, 0.009000f, 1      // sync at start, midpoint = 4.5 ms
    };

    // ==================== ROBOT 36 (interlaced Y / alternating chroma) ====================
    // Per cycle of 2 lines:
    //   Line N (odd):  sync(9) + bp(2.5) + Y(87.4) + blank2/3(4.67) + blank1/3(2.33) + R-Y(43.7) + fp(0.4)
    //   Line N+1 (even): sync(9) + bp(2.5) + Y(87.4) + blank2/3(4.67) + blank1/3(2.33) + B-Y(43.7) + fp(0.4)
    // = 150 ms each, 240 lines / 2 = 120 cycles, total ~36 s
    // Output: 320x240 image. Each cycle produces 2 image lines:
    //   line N+0: Y_odd combined with R-Y (and interpolated B-Y from neighbor)
    //   line N+1: Y_even combined with B-Y (and interpolated R-Y from neighbor)
    // Simpler approximation: use same chroma sample for both rows (we copy R-Y and B-Y across).
    constexpr LineSegment ROBOT_36_SEGMENTS[] = {
        // Line N (odd, has R-Y)
        { SegmentKind::IGNORE,    0.009000f, 0 },     // Sync
        { SegmentKind::IGNORE,    0.002500f, 0 },     // BP
        { SegmentKind::R36_Y_ODD, 0.087400f, 320 },   // Y odd
        { SegmentKind::IGNORE,    0.004667f, 0 },     // blank 2/3 (MB1500 marker)
        { SegmentKind::IGNORE,    0.002333f, 0 },     // blank 1/3
        { SegmentKind::R36_RY,    0.043700f, 320 },   // R-Y
        { SegmentKind::IGNORE,    0.000400f, 0 },     // FP
        // Line N+1 (even, has B-Y)
        { SegmentKind::IGNORE,    0.009000f, 0 },     // Sync
        { SegmentKind::IGNORE,    0.002500f, 0 },     // BP
        { SegmentKind::R36_Y_EVEN,0.087400f, 320 },   // Y even
        { SegmentKind::IGNORE,    0.004667f, 0 },     // blank 2/3 (MB2300 marker)
        { SegmentKind::IGNORE,    0.002333f, 0 },     // blank 1/3
        { SegmentKind::R36_BY,    0.043700f, 320 },   // B-Y
        { SegmentKind::IGNORE,    0.000400f, 0 },     // FP
    };
    constexpr ModeParams ROBOT_36_PARAMS = {
        Mode::ROBOT_36, "R36", "Robot 36 Color", 0x08, ColorModel::R36_YUV,
        320, 240, 2, 0.300000f,
        ROBOT_36_SEGMENTS, sizeof(ROBOT_36_SEGMENTS) / sizeof(LineSegment),
        0.0f, 0.004500f, 0.009000f, 2      // first sync at midpoint 4.5 ms, 2 syncs/cycle
    };

    // ==================== PD modes ====================
    // PD50: 320x256, 49.69 s, VIS 0xDD
    //   cycle = 49.6877/128 = 388.18 ms
    //   per cycle: sync(20) + bp(2.08) + 4 * scan(91.526) = 388.18 ms
    //   pixel pitch = 91.526/320 = 286 us
    constexpr LineSegment PD50_SEGMENTS[] = {
        { SegmentKind::IGNORE,  0.020000f, 0   },
        { SegmentKind::IGNORE,  0.002080f, 0   },
        { SegmentKind::PD_Y1,   0.091520f, 320 },
        { SegmentKind::PD_R,    0.091520f, 320 },
        { SegmentKind::PD_B,    0.091520f, 320 },
        { SegmentKind::PD_Y2,   0.091520f, 320 },
    };
    constexpr ModeParams PD50_PARAMS = {
        Mode::PD50, "PD50", "PD50", 0x5D, ColorModel::PD_YUV,
        320, 256, 2, 0.388160f,
        PD50_SEGMENTS, sizeof(PD50_SEGMENTS) / sizeof(LineSegment),
        0.0f, 0.010000f, 0.020000f, 1
    };

    // PD90: 320x256, 89.995 s, VIS 0x63
    //   cycle = 89.995/128 = 703.09 ms
    //   per cycle: sync(20) + bp(2.08) + 4 * scan(170.25) = 703.08 ms
    constexpr LineSegment PD90_SEGMENTS[] = {
        { SegmentKind::IGNORE,  0.020000f, 0   },
        { SegmentKind::IGNORE,  0.002080f, 0   },
        { SegmentKind::PD_Y1,   0.170240f, 320 },
        { SegmentKind::PD_R,    0.170240f, 320 },
        { SegmentKind::PD_B,    0.170240f, 320 },
        { SegmentKind::PD_Y2,   0.170240f, 320 },
    };
    constexpr ModeParams PD90_PARAMS = {
        Mode::PD90, "PD90", "PD90", 0x63, ColorModel::PD_YUV,
        320, 256, 2, 0.703040f,
        PD90_SEGMENTS, sizeof(PD90_SEGMENTS) / sizeof(LineSegment),
        0.0f, 0.010000f, 0.020000f, 1
    };

    // PD120: 640x496, 126.11 s, VIS 0x5F  (already verified)
    constexpr LineSegment PD120_SEGMENTS[] = {
        { SegmentKind::IGNORE,  0.020000f, 0   },
        { SegmentKind::IGNORE,  0.002080f, 0   },
        { SegmentKind::PD_Y1,   0.121520f, 640 },
        { SegmentKind::PD_R,    0.121520f, 640 },
        { SegmentKind::PD_B,    0.121520f, 640 },
        { SegmentKind::PD_Y2,   0.121520f, 640 },
    };
    constexpr ModeParams PD120_PARAMS = {
        Mode::PD120, "PD120", "PD120", 0x5F, ColorModel::PD_YUV,
        640, 496, 2, 0.508160f,
        PD120_SEGMENTS, sizeof(PD120_SEGMENTS) / sizeof(LineSegment),
        0.0f, 0.010000f, 0.020000f, 1
    };

    // PD180: 640x496, 187.06 s, VIS 0x60
    //   cycle = 187.0645/248 = 754.29 ms
    //   per cycle: sync(20) + bp(2.0) + 4 * scan(183.07) = 754.28 ms
    constexpr LineSegment PD180_SEGMENTS[] = {
        { SegmentKind::IGNORE,  0.020000f, 0   },
        { SegmentKind::IGNORE,  0.002000f, 0   },
        { SegmentKind::PD_Y1,   0.183070f, 640 },
        { SegmentKind::PD_R,    0.183070f, 640 },
        { SegmentKind::PD_B,    0.183070f, 640 },
        { SegmentKind::PD_Y2,   0.183070f, 640 },
    };
    constexpr ModeParams PD180_PARAMS = {
        Mode::PD180, "PD180", "PD180", 0x60, ColorModel::PD_YUV,
        640, 496, 2, 0.754280f,
        PD180_SEGMENTS, sizeof(PD180_SEGMENTS) / sizeof(LineSegment),
        0.0f, 0.010000f, 0.020000f, 1
    };

    // PD160: 512x400, 160.89 s, VIS 0x62 (7-bit)
    //   cycle = 160.8942/200 = 804.47 ms
    //   sync(20) + bp(2.0) + 4 * scan(195.62) = 804.47 ms
    constexpr LineSegment PD160_SEGMENTS[] = {
        { SegmentKind::IGNORE,  0.020000f, 0   },
        { SegmentKind::IGNORE,  0.002000f, 0   },
        { SegmentKind::PD_Y1,   0.195620f, 512 },
        { SegmentKind::PD_R,    0.195620f, 512 },
        { SegmentKind::PD_B,    0.195620f, 512 },
        { SegmentKind::PD_Y2,   0.195620f, 512 },
    };
    constexpr ModeParams PD160_PARAMS = {
        Mode::PD160, "PD160", "PD160", 0x62, ColorModel::PD_YUV,
        512, 400, 2, 0.804480f,
        PD160_SEGMENTS, sizeof(PD160_SEGMENTS) / sizeof(LineSegment),
        0.0f, 0.010000f, 0.020000f, 1
    };

    // PD240: 640x496, 248.02 s, VIS 0x61 (7-bit)
    //   cycle = 248.017/248 = 1000.07 ms
    //   sync(20) + fp(2.0) + bp(2.0) + 4 * scan(244.02) = 1000.07 ms
    //   (PD240 uniquely has fp=2ms before the sync porch)
    constexpr LineSegment PD240_SEGMENTS[] = {
        { SegmentKind::IGNORE,  0.020000f, 0   },
        { SegmentKind::IGNORE,  0.004000f, 0   },  // fp + bp combined (2+2 ms)
        { SegmentKind::PD_Y1,   0.244017f, 640 },
        { SegmentKind::PD_R,    0.244017f, 640 },
        { SegmentKind::PD_B,    0.244017f, 640 },
        { SegmentKind::PD_Y2,   0.244017f, 640 },
    };
    constexpr ModeParams PD240_PARAMS = {
        Mode::PD240, "PD240", "PD240", 0x61, ColorModel::PD_YUV,
        640, 496, 2, 1.000068f,
        PD240_SEGMENTS, sizeof(PD240_SEGMENTS) / sizeof(LineSegment),
        0.0f, 0.010000f, 0.020000f, 1
    };

    // PD290: 800x616, 288.70 s, VIS 0x5E (7-bit)
    //   cycle = 288.702/308 = 937.34 ms
    //   sync(20) + bp(2.0) + 4 * scan(228.84) = 937.34 ms
    constexpr LineSegment PD290_SEGMENTS[] = {
        { SegmentKind::IGNORE,  0.020000f, 0   },
        { SegmentKind::IGNORE,  0.002000f, 0   },
        { SegmentKind::PD_Y1,   0.228836f, 800 },
        { SegmentKind::PD_R,    0.228836f, 800 },
        { SegmentKind::PD_B,    0.228836f, 800 },
        { SegmentKind::PD_Y2,   0.228836f, 800 },
    };
    constexpr ModeParams PD290_PARAMS = {
        Mode::PD290, "PD290", "PD290", 0x5E, ColorModel::PD_YUV,
        800, 616, 2, 0.937344f,
        PD290_SEGMENTS, sizeof(PD290_SEGMENTS) / sizeof(LineSegment),
        0.0f, 0.010000f, 0.020000f, 1
    };

    inline const ModeParams* getModeParams(Mode m) {
        switch (m) {
            case Mode::MARTIN_M1:  return &MARTIN_M1_PARAMS;
            case Mode::MARTIN_M2:  return &MARTIN_M2_PARAMS;
            case Mode::SCOTTIE_S1: return &SCOTTIE_S1_PARAMS;
            case Mode::SCOTTIE_S2: return &SCOTTIE_S2_PARAMS;
            case Mode::SCOTTIE_DX: return &SCOTTIE_DX_PARAMS;
            case Mode::ROBOT_36:   return &ROBOT_36_PARAMS;
            case Mode::ROBOT_72:   return &ROBOT_72_PARAMS;
            case Mode::PD50:       return &PD50_PARAMS;
            case Mode::PD90:       return &PD90_PARAMS;
            case Mode::PD120:      return &PD120_PARAMS;
            case Mode::PD180:      return &PD180_PARAMS;
            case Mode::PD160:      return &PD160_PARAMS;
            case Mode::PD240:      return &PD240_PARAMS;
            case Mode::PD290:      return &PD290_PARAMS;
            default:               return nullptr;
        }
    }

    inline Mode lookupVIS(uint8_t vis7) {
        switch (vis7) {
            // All values are 7-bit VIS codes (parity bit stripped by the decoder).
            case 0x2C: return Mode::MARTIN_M1;
            case 0x28: return Mode::MARTIN_M2;
            case 0x3C: return Mode::SCOTTIE_S1;
            case 0x38: return Mode::SCOTTIE_S2;
            case 0x4C: return Mode::SCOTTIE_DX;
            case 0x08: return Mode::ROBOT_36;   // 0x88 with parity stripped
            case 0x0C: return Mode::ROBOT_72;
            case 0x5D: return Mode::PD50;       // 0xDD with parity stripped
            case 0x63: return Mode::PD90;
            case 0x5F: return Mode::PD120;
            case 0x60: return Mode::PD180;
            case 0x62: return Mode::PD160;   // 0xE2 with parity stripped
            case 0x61: return Mode::PD240;   // 0xE1 with parity stripped
            case 0x5E: return Mode::PD290;   // 0xDE with parity stripped
            default:   return Mode::UNKNOWN;
        }
    }

    inline const char* getModeName(Mode m) {
        const ModeParams* p = getModeParams(m);
        return p ? p->longName : "Unknown";
    }

} // namespace sstv
