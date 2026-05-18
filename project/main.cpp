// Terminal Bad Apple project — CozyCookie

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

#if defined(_MSC_VER) || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <immintrin.h>
#define USE_SSE2 1
#else
#define USE_SSE2 0
#endif

using namespace std;
using namespace std::chrono;

// ===================== SETTINGS =====================

static constexpr double FALLBACK_FPS = 30.0;

static constexpr int MAX_RENDER_WIDTH = 160;
static constexpr float HEIGHT_RATIO = 0.30f;

static constexpr const char* GRADIENT = " .,:;i1tfLCG08@";
static constexpr int GRADIENT_SIZE = 15;

static constexpr bool ENABLE_MOTION_BLUR = true;
static constexpr bool ENABLE_DEBUG_OVERLAY = true;

static constexpr int BLUR_CUR = 3;
static constexpr int BLUR_PREV = 1;
static constexpr int BLUR_DIV = BLUR_CUR + BLUR_PREV;

static constexpr float CINEMATIC_GAMMA = 1.7f;

// ===================== PLATFORM HELPERS =====================

struct TerminalSize {
    int cols = 120;
    int rows = 40;
};

static TerminalSize getTerminalSize() {
    TerminalSize ts;

#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        ts.cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        ts.rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
#endif

    ts.cols = max(40, ts.cols);
    ts.rows = max(10, ts.rows);
    return ts;
}

static void enableAnsiOnWindows() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;

    DWORD mode = 0;
    if (GetConsoleMode(hOut, &mode)) {
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, mode);
    }
#endif
}

// ===================== LUT BUILDING =====================

static array<char, 256> buildShadeLUT() {
    array<char, 256> lut{};

    for (int i = 0; i < 256; ++i) {
        float x = static_cast<float>(i) / 255.0f;
        x = pow(x, CINEMATIC_GAMMA);

        int idx = static_cast<int>(x * (GRADIENT_SIZE - 1) + 0.5f);
        idx = std::clamp(idx, 0, GRADIENT_SIZE - 1);

        lut[i] = GRADIENT[idx];
    }

    return lut;
}

// ===================== AUDIO =====================

static void launchAudioIfPresent() {
    ifstream test("badapple.mp3");
    if (!test.good()) return;

#ifdef _WIN32
    string cmd = "start \"\" /min ffplay -nodisp -autoexit -loglevel quiet \"badapple.mp3\"";
    system(cmd.c_str());
#else
    string cmd = "ffplay -nodisp -autoexit -loglevel quiet \"badapple.mp3\" >/dev/null 2>&1 &";
    system(cmd.c_str());
#endif
}

// ===================== TERMINAL GUARD =====================

class TerminalGuard {
public:
    TerminalGuard() {
        cout << "\x1b[?1049h\x1b[2J\x1b[H\x1b[?25l";
        cout.flush();
    }

    ~TerminalGuard() {
        cout << "\x1b[?25h\x1b[?1049l";
        cout.flush();
    }
};

// ===================== ffprobe TIMESTAMPS =====================

static vector<double> loadFrameTimestamps(const string& videoPath) {
    vector<double> pts;

#ifdef _WIN32
    string cmd =
        "ffprobe -v error -select_streams v:0 "
        "-show_entries frame=best_effort_timestamp_time "
        "-of csv=p=0 \"" + videoPath + "\"";

    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    string cmd =
        "ffprobe -v error -select_streams v:0 "
        "-show_entries frame=best_effort_timestamp_time "
        "-of csv=p=0 \"" + videoPath + "\"";

    FILE* pipe = popen(cmd.c_str(), "r");
#endif

    if (!pipe) return pts;

    char line[256];
    while (fgets(line, sizeof(line), pipe)) {
        char* end = nullptr;
        double t = strtod(line, &end);
        if (end != line) {
            pts.push_back(t);
        }
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    return pts;
}

// ===================== FFMPEG RAW VIDEO PIPE =====================

static FILE* openVideoPipe(const string& videoPath, int w, int h) {
#ifdef _WIN32
    string cmd =
        "ffmpeg -hide_banner -loglevel error -vsync 0 -i \"" + videoPath + "\" "
        "-an -vf \"scale=" + to_string(w) + ":" + to_string(h) +
        ":flags=fast_bilinear,format=gray\" "
        "-f rawvideo -pix_fmt gray pipe:1";

    return _popen(cmd.c_str(), "rb");
#else
    string cmd =
        "ffmpeg -hide_banner -loglevel error -vsync 0 -i \"" + videoPath + "\" "
        "-an -vf \"scale=" + to_string(w) + ":" + to_string(h) +
        ":flags=fast_bilinear,format=gray\" "
        "-f rawvideo -pix_fmt gray pipe:1";

    return popen(cmd.c_str(), "r");
#endif
}

static void closePipe(FILE* p) {
    if (!p) return;
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
}

// ===================== SIMD / FAST BLUR =====================

static void copyFrame(const unsigned char* src, unsigned char* dst, size_t n) {
    memcpy(dst, src, n);
}

static void blendFrames(
    const unsigned char* current,
    const unsigned char* previous,
    unsigned char* out,
    size_t n
) {
#if USE_SSE2
    size_t i = 0;
    const __m128i zero = _mm_setzero_si128();

    for (; i + 16 <= n; i += 16) {
        __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(current + i));
        __m128i p = _mm_loadu_si128(reinterpret_cast<const __m128i*>(previous + i));

        __m128i cLo = _mm_unpacklo_epi8(c, zero);
        __m128i cHi = _mm_unpackhi_epi8(c, zero);
        __m128i pLo = _mm_unpacklo_epi8(p, zero);
        __m128i pHi = _mm_unpackhi_epi8(p, zero);

        __m128i curLo = _mm_mullo_epi16(cLo, _mm_set1_epi16(BLUR_CUR));
        __m128i curHi = _mm_mullo_epi16(cHi, _mm_set1_epi16(BLUR_CUR));
        __m128i prevLo = _mm_mullo_epi16(pLo, _mm_set1_epi16(BLUR_PREV));
        __m128i prevHi = _mm_mullo_epi16(pHi, _mm_set1_epi16(BLUR_PREV));

        __m128i sumLo = _mm_add_epi16(curLo, prevLo);
        __m128i sumHi = _mm_add_epi16(curHi, prevHi);

        sumLo = _mm_srli_epi16(sumLo, 2);
        sumHi = _mm_srli_epi16(sumHi, 2);

        __m128i packed = _mm_packus_epi16(sumLo, sumHi);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i), packed);
    }

    for (; i < n; ++i) {
        out[i] = static_cast<unsigned char>(
            ((current[i] * BLUR_CUR) + (previous[i] * BLUR_PREV)) / BLUR_DIV
        );
    }
#else
    for (size_t i = 0; i < n; ++i) {
        out[i] = static_cast<unsigned char>(
            ((current[i] * BLUR_CUR) + (previous[i] * BLUR_PREV)) / BLUR_DIV
        );
    }
#endif
}

// ===================== RENDER =====================

static void renderFrame(
    const vector<unsigned char>& frame,
    int width,
    int videoHeight,
    const array<char, 256>& shadeLUT,
    size_t frameIndex,
    double targetTimeSec,
    double elapsedSec,
    double fpsNow,
    double driftMs
) {
    string out;
    out.reserve(static_cast<size_t>((width + 1) * videoHeight + width + 64));

    out += "\x1b[H";

    for (int y = 0; y < videoHeight; ++y) {
        const unsigned char* row = frame.data() + static_cast<size_t>(y) * width;

        for (int x = 0; x < width; ++x) {
            out.push_back(shadeLUT[row[x]]);
        }

        out.push_back('\n');
    }

    if (ENABLE_DEBUG_OVERLAY) {
        char buf[256];
        snprintf(
            buf,
            sizeof(buf),
            " frame:%zu  t:%7.3fs  target:%7.3fs  drift:%+7.2fms  fps:%6.2f ",
            frameIndex,
            elapsedSec,
            targetTimeSec,
            driftMs,
            fpsNow
        );

        string line(buf);
        if (static_cast<int>(line.size()) < width) {
            line.append(static_cast<size_t>(width - line.size()), ' ');
        } else if (static_cast<int>(line.size()) > width) {
            line.resize(static_cast<size_t>(width));
        }

        out += "\x1b[7m";
        out += line;
        out += "\x1b[0m";
    }

    cout.write(out.data(), static_cast<streamsize>(out.size()));
    cout.flush();
}

// ===================== MAIN =====================

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    enableAnsiOnWindows();

    const string videoPath = "badapple.mp4";
    const string audioPath = "badapple.mp3";

    TerminalSize term = getTerminalSize();

    int width = min(MAX_RENDER_WIDTH, term.cols);
    int requestedHeight = static_cast<int>(width * HEIGHT_RATIO);

    int overlayRows = ENABLE_DEBUG_OVERLAY ? 1 : 0;
    int maxHeightByTerminal = max(1, term.rows - overlayRows);
    int height = min(requestedHeight, maxHeightByTerminal);
    int videoHeight = max(1, height);

    if (ENABLE_DEBUG_OVERLAY) {
        videoHeight = max(1, min(height, term.rows - 1));
    }

    array<char, 256> shadeLUT = buildShadeLUT();

    vector<double> framePts = loadFrameTimestamps(videoPath);
    double ptsBase = framePts.empty() ? 0.0 : framePts.front();

    FILE* pipe = openVideoPipe(videoPath, width, videoHeight);
    if (!pipe) {
        cerr << "Could not start ffmpeg or open badapple.mp4\n";
        return 1;
    }

    launchAudioIfPresent();

    TerminalGuard guard;

    const size_t frameSize = static_cast<size_t>(width) * static_cast<size_t>(videoHeight);
    vector<unsigned char> rawFrame(frameSize);
    vector<unsigned char> blurFrame(frameSize);
    vector<unsigned char> prevFrame(frameSize);
    bool havePrev = false;

    auto programStart = steady_clock::now();
    int frameIndex = 0;

    while (true) {
        size_t got = fread(rawFrame.data(), 1, frameSize, pipe);
        if (got != frameSize) break;

        vector<unsigned char>* source = &rawFrame;

        if (ENABLE_MOTION_BLUR) {
            if (havePrev) {
                blendFrames(rawFrame.data(), prevFrame.data(), blurFrame.data(), frameSize);
                source = &blurFrame;
            } else {
                copyFrame(rawFrame.data(), blurFrame.data(), frameSize);
                source = &blurFrame;
            }
        }

        double fallbackTarget = frameIndex * (1.0 / FALLBACK_FPS);
        double targetTimeSec = fallbackTarget;

        if (!framePts.empty() && frameIndex < static_cast<int>(framePts.size())) {
            targetTimeSec = max(0.0, framePts[frameIndex] - ptsBase);
        }

        auto expected = programStart + duration<double>(targetTimeSec);
        auto now = steady_clock::now();

        if (now < expected) {
            this_thread::sleep_until(expected);
        }

        auto afterSleep = steady_clock::now();
        double elapsedSec = duration<double>(afterSleep - programStart).count();
        double driftMs = (elapsedSec - targetTimeSec) * 1000.0;
        double fpsNow = (elapsedSec > 0.0) ? (static_cast<double>(frameIndex + 1) / elapsedSec) : 0.0;

        renderFrame(
            *source,
            width,
            videoHeight,
            shadeLUT,
            static_cast<size_t>(frameIndex),
            targetTimeSec,
            elapsedSec,
            fpsNow,
            driftMs
        );

        if (ENABLE_MOTION_BLUR) {
            prevFrame = rawFrame;
            havePrev = true;
        }

        ++frameIndex;
    }

    closePipe(pipe);
    return 0;
}
