#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <wincodec.h>
#include <algorithm>
#include <climits>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using Bytes = std::vector<BYTE>;
namespace fs = std::filesystem;

template<class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
    T** put() { return &p; }
};

static void check(HRESULT hr) {
    if (FAILED(hr)) {
        char text[80];
        std::snprintf(text, sizeof(text), "Windows image operation failed (0x%08lX).", static_cast<unsigned long>(hr));
        throw std::runtime_error(text);
    }
}

struct Image { UINT width, height; Bytes pixels; };
static Image pixels(IWICImagingFactory* factory, IWICBitmapSource* source) {
    Image im{};
    check(source->GetSize(&im.width, &im.height));
    if (!im.width || !im.height || uint64_t(im.width) * im.height * 4 > UINT_MAX)
        throw std::runtime_error("Image is too large for the Windows image codec.");
    im.pixels.resize(size_t(im.width) * im.height * 4);
    Com<IWICFormatConverter> converter;
    check(factory->CreateFormatConverter(converter.put()));
    check(converter->Initialize(source, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    check(converter->CopyPixels(nullptr, im.width * 4, static_cast<UINT>(im.pixels.size()), im.pixels.data()));
    return im;
}

static Image load(IWICImagingFactory* factory, const std::wstring& path) {
    Com<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.put()));
    Com<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, frame.put()));
    unsigned orientation = 1;
    Com<IWICMetadataQueryReader> metadata;
    if (SUCCEEDED(frame->GetMetadataQueryReader(metadata.put()))) {
        for (auto query : {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}"}) {
            PROPVARIANT value{};
            if (SUCCEEDED(metadata->GetMetadataByName(query, &value)) && value.vt == VT_UI2)
                orientation = value.uiVal;
            PropVariantClear(&value);
        }
    }
    const WICBitmapTransformOptions transforms[] = {
        WICBitmapTransformRotate0, WICBitmapTransformRotate0,
        WICBitmapTransformFlipHorizontal, WICBitmapTransformRotate180,
        WICBitmapTransformFlipVertical,
        static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal),
        WICBitmapTransformRotate90,
        static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal),
        WICBitmapTransformRotate270
    };
    if (orientation > 1 && orientation < 9) {
        Com<IWICBitmapFlipRotator> rotator;
        check(factory->CreateBitmapFlipRotator(rotator.put()));
        check(rotator->Initialize(frame.p, transforms[orientation]));
        return pixels(factory, rotator.p);
    }
    return pixels(factory, frame.p);
}

static Image fit(IWICImagingFactory* factory, const Image& source, UINT width, UINT height) {
    if (source.width == width && source.height == height) return source;
    double scale = std::min(double(width) / source.width, double(height) / source.height);
    UINT w = std::max(1u, std::min(width, UINT(source.width * scale + 0.5)));
    UINT h = std::max(1u, std::min(height, UINT(source.height * scale + 0.5)));
    Com<IWICBitmap> bitmap;
    check(factory->CreateBitmapFromMemory(source.width, source.height, GUID_WICPixelFormat32bppBGRA,
        source.width * 4, static_cast<UINT>(source.pixels.size()), const_cast<BYTE*>(source.pixels.data()), bitmap.put()));
    Com<IWICBitmapScaler> scaler;
    check(factory->CreateBitmapScaler(scaler.put()));
    check(scaler->Initialize(bitmap.p, w, h, WICBitmapInterpolationModeFant));
    Image small = pixels(factory, scaler.p);
    Image result{width, height, Bytes(size_t(width) * height * 4, 255)};
    for (UINT y = 0; y < h; ++y)
        std::copy_n(small.pixels.data() + size_t(y) * w * 4, size_t(w) * 4,
            result.pixels.data() + (size_t(y + (height - h) / 2) * width + (width - w) / 2) * 4);
    return result;
}

static uint32_t be32(const BYTE* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
static void add32(Bytes& b, uint32_t n) {
    b.insert(b.end(), {BYTE(n >> 24), BYTE(n >> 16), BYTE(n >> 8), BYTE(n)});
}
static Bytes pngData(IWICImagingFactory* factory, const Image& im) {
    Com<IStream> stream;
    check(CreateStreamOnHGlobal(nullptr, TRUE, stream.put()));
    Com<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
    check(encoder->Initialize(stream.p, WICBitmapEncoderNoCache));
    Com<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(frame.put(), nullptr));
    check(frame->Initialize(nullptr));
    check(frame->SetSize(im.width, im.height));
    auto format = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&format));
    if (!IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA))
        throw std::runtime_error("PNG encoder does not support 32-bit BGRA.");
    check(frame->WritePixels(im.height, im.width * 4, static_cast<UINT>(im.pixels.size()), const_cast<BYTE*>(im.pixels.data())));
    check(frame->Commit());
    check(encoder->Commit());
    STATSTG stat{};
    check(stream->Stat(&stat, STATFLAG_NONAME));
    if (stat.cbSize.QuadPart > UINT_MAX) throw std::runtime_error("Encoded PNG is too large.");
    Bytes encoded(static_cast<size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    check(stream->Seek(zero, STREAM_SEEK_SET, nullptr));
    ULONG read = 0;
    check(stream->Read(encoded.data(), static_cast<ULONG>(encoded.size()), &read));
    if (read != encoded.size()) throw std::runtime_error("Incomplete PNG stream.");
    Bytes data;
    for (size_t pos = 8; pos + 12 <= encoded.size();) {
        size_t size = be32(encoded.data() + pos);
        if (size > encoded.size() - pos - 12) throw std::runtime_error("Invalid PNG chunk length.");
        if (memcmp(encoded.data() + pos + 4, "IHDR", 4) == 0 && (encoded[pos + 16] != 8 || encoded[pos + 17] != 6))
            throw std::runtime_error("PNG encoder did not produce RGBA8.");
        if (memcmp(encoded.data() + pos + 4, "IDAT", 4) == 0)
            data.insert(data.end(), encoded.begin() + pos + 8, encoded.begin() + pos + 8 + size);
        pos += size + 12;
    }
    return data;
}

static void chunk(Bytes& out, const char* type, const Bytes& data) {
    add32(out, static_cast<uint32_t>(data.size()));
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    // PNG requires a CRC over the chunk type and payload.
    uint32_t crc = 0xffffffff;
    for (size_t i = start; i < out.size(); ++i) {
        crc ^= out[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    add32(out, crc ^ 0xffffffff);
}
static void control(Bytes& out, uint32_t& sequence, UINT w, UINT h, BYTE blend, unsigned delayMs) {
    Bytes data;
    for (uint32_t n : {sequence++, w, h, 0u, 0u}) add32(data, n);
    data.insert(data.end(), {BYTE(delayMs >> 8), BYTE(delayMs), 3, 232, 0, blend});
    chunk(out, "fcTL", data);
}
static void frameData(Bytes& out, uint32_t& sequence, const Bytes& png) {
    for (size_t pos = 0; pos < png.size(); pos += 65536) {
        Bytes data;
        add32(data, sequence++);
        data.insert(data.end(), png.begin() + pos, png.begin() + std::min(png.size(), pos + 65536));
        chunk(out, "fdAT", data);
    }
}
static void generate(const std::wstring& coverPath, const std::vector<std::wstring>& hiddenPaths, const std::wstring& outputPath, unsigned delayMs = 100) {
    if (_wcsicmp(fs::path(outputPath).extension().c_str(), L".png") != 0)
        throw std::runtime_error("Output filename must end in .png.");
    Com<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, reinterpret_cast<void**>(factory.put())));
    if (hiddenPaths.empty()) throw std::runtime_error("Add at least one playback image.");
    auto hidden = load(factory.p, hiddenPaths.front());
    auto cover = fit(factory.p, load(factory.p, coverPath), hidden.width, hidden.height);
    Bytes out{137, 80, 78, 71, 13, 10, 26, 10}, header;
    add32(header, hidden.width); add32(header, hidden.height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    chunk(out, "IHDR", header);
    Bytes animation; add32(animation, static_cast<uint32_t>(std::max(size_t(2), hiddenPaths.size()))); add32(animation, 0);
    chunk(out, "acTL", animation);
    chunk(out, "IDAT", pngData(factory.p, cover));
    uint32_t sequence = 0;
    control(out, sequence, hidden.width, hidden.height, 0, delayMs);
    frameData(out, sequence, pngData(factory.p, hidden));
    for (size_t i = 1; i < hiddenPaths.size(); ++i) {
        auto next = fit(factory.p, load(factory.p, hiddenPaths[i]), hidden.width, hidden.height);
        control(out, sequence, hidden.width, hidden.height, 0, delayMs);
        frameData(out, sequence, pngData(factory.p, next));
    }
    if (hiddenPaths.size() == 1) {
        control(out, sequence, 1, 1, 1, delayMs);
        frameData(out, sequence, pngData(factory.p, Image{1, 1, Bytes(4, 0)}));
    }
    chunk(out, "IEND", {});
    if (out.size() > MAXDWORD) throw std::runtime_error("Output file is too large.");
    HANDLE file = CreateFileW(outputPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS)
            throw std::runtime_error("Output already exists. Choose another filename.");
        throw std::runtime_error("Cannot create output file. Check the folder and permissions.");
    }
    DWORD written = 0;
    BOOL ok = WriteFile(file, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
    CloseHandle(file);
    if (!ok || written != out.size()) throw std::runtime_error("Could not write the complete PNG file.");
}

static unsigned parseDelay(const std::wstring& value) {
    if (value.empty() || value.size() > 5 || value.find_first_not_of(L"0123456789") != std::wstring::npos)
        throw std::runtime_error("Frame duration must be an integer from 1 to 65535 ms.");
    unsigned delay = static_cast<unsigned>(std::stoul(value));
    if (delay < 1 || delay > 65535) throw std::runtime_error("Frame duration must be from 1 to 65535 ms.");
    return delay;
}

static HWND inputs[3], statusLabel, durationInput;
static std::vector<std::wstring> playbackPaths;
static HFONT font, heading;
static HINSTANCE instance;
static int dpi = 96;
static int px(int n) { return MulDiv(n, dpi, 96); }
static std::wstring text(HWND control) {
    std::wstring value(GetWindowTextLengthW(control) + 1, L'\0');
    GetWindowTextW(control, value.data(), static_cast<int>(value.size()));
    value.resize(wcslen(value.c_str()));
    return value;
}
static std::wstring normalize(std::wstring path) {
    size_t first = path.find_first_not_of(L" \t\r\n"), last = path.find_last_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    return fs::path(path.substr(first, last - first + 1)).make_preferred().wstring();
}
static void setPath(int index, const std::wstring& path) {
    if (index == 1) {
        playbackPaths.push_back(normalize(path));
        SendMessageW(inputs[1], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(playbackPaths.back().c_str()));
        return;
    }
    SetWindowTextW(inputs[index], normalize(path).c_str());
    if (index == 0 && text(inputs[2]).empty()) {
        fs::path cover(normalize(path));
        SetWindowTextW(inputs[2], (cover.parent_path() / (cover.stem().wstring() + L"_藏图.png")).c_str());
    }
}
static LRESULT CALLBACK dropProc(HWND window, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR index, DWORD_PTR) {
    if (msg == WM_DROPFILES) {
        HDROP drop = reinterpret_cast<HDROP>(wp);
        UINT count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
        if (index == 1 || count == 1) {
          for (UINT i = 0; i < count; ++i) {
            std::wstring path(DragQueryFileW(drop, i, nullptr, 0) + 1, L'\0');
            DragQueryFileW(drop, i, path.data(), static_cast<UINT>(path.size()));
            path.resize(wcslen(path.c_str()));
            DWORD attr = GetFileAttributesW(path.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) setPath(static_cast<int>(index), path);
            else MessageBoxW(GetParent(window), L"请拖入图片文件。", L"拖入图片", MB_OK | MB_ICONERROR);
          }
        } else MessageBoxW(GetParent(window), L"封面请一次拖入一个图片文件。", L"拖入图片", MB_OK | MB_ICONERROR);
        DragFinish(drop);
        return 0;
    }
    return DefSubclassProc(window, msg, wp, lp);
}
static void browse(HWND owner, int index) {
    std::vector<wchar_t> path(32768);
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = owner; dialog.lpstrFile = path.data(); dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrFilter = index == 2 ? L"PNG 图片\0*.png\0\0" : L"图片文件\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp\0所有文件\0*.*\0\0";
    dialog.lpstrDefExt = L"png";
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (index == 2 ? 0 : OFN_FILEMUSTEXIST) | (index == 1 ? OFN_ALLOWMULTISELECT : 0);
    if ((index == 2 ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) {
        const wchar_t* next = path.data() + wcslen(path.data()) + 1;
        if (index == 1 && *next) {
            for (; *next; next += wcslen(next) + 1) setPath(1, (fs::path(path.data()) / next).wstring());
        } else setPath(index, path.data());
    } else if (CommDlgExtendedError()) {
        MessageBoxW(owner, L"无法完成文件选择，请减少一次选择的文件数量后重试。", L"文件选择失败", MB_OK | MB_ICONERROR);
    }
}
static HWND control(HWND parent, const wchar_t* cls, const wchar_t* label, DWORD style, int id, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(wcscmp(cls, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, cls, label, WS_CHILD | WS_VISIBLE | style,
        px(x), px(y), px(w), px(h), parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return c;
}
static LRESULT CALLBACK windowProc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CTLCOLORSTATIC) {
        SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    if (msg == WM_CREATE) {
        auto title = control(window, L"STATIC", L"APNG 藏图工具 · C++", 0, 0, 24, 20, 600, 40);
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(heading), TRUE);
        control(window, L"STATIC", L"拖入封面；播放列表支持批量拖入或添加多张图片。", 0, 0, 24, 70, 710, 28);
        const wchar_t* labels[] = {L"封面图", L"播放图", L"保存到"};
        for (int i = 0; i < 3; ++i) {
            int y = i == 0 ? 116 : (i == 1 ? 166 : 330);
            control(window, L"STATIC", labels[i], 0, 0, 24, y + 5, 64, 24);
            if (i == 1) {
                inputs[i] = control(window, L"LISTBOX", L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                    201, 100, y, 510, 145);
                SendMessageW(inputs[i], LB_SETHORIZONTALEXTENT, px(1800), 0);
            } else {
                inputs[i] = control(window, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 200 + i, 100, y, 510, 32);
                SendMessageW(inputs[i], EM_SETLIMITTEXT, 32767, 0);
            }
            if (i < 2) { SetWindowSubclass(inputs[i], dropProc, i, 0); DragAcceptFiles(inputs[i], TRUE); }
            control(window, L"BUTTON", i == 1 ? L"添加图片" : L"浏览…", WS_TABSTOP | BS_PUSHBUTTON, 100 + i, 625, y, 100, 32);
        }
        control(window, L"BUTTON", L"移除选中", WS_TABSTOP | BS_PUSHBUTTON, 103, 625, 210, 100, 32);
        control(window, L"STATIC", L"每张时长", 0, 0, 24, 385, 76, 24);
        durationInput = control(window, L"EDIT", L"100", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 203, 110, 380, 110, 32);
        SendMessageW(durationInput, EM_SETLIMITTEXT, 5, 0);
        control(window, L"STATIC", L"毫秒（1–65535；1000 毫秒 = 1 秒）", 0, 0, 235, 385, 450, 24);
        control(window, L"BUTTON", L"生成藏图", WS_TABSTOP | BS_DEFPUSHBUTTON, 110, 290, 435, 170, 38);
        statusLabel = control(window, L"STATIC", L"按列表顺序循环播放；画布采用第一张播放图尺寸。", 0, 0, 24, 495, 710, 28);
        return 0;
    }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id >= 100 && id <= 102) browse(window, id - 100);
        if (id == 103) {
            auto selected = SendMessageW(inputs[1], LB_GETCURSEL, 0, 0);
            if (selected != LB_ERR) {
                playbackPaths.erase(playbackPaths.begin() + selected);
                SendMessageW(inputs[1], LB_DELETESTRING, selected, 0);
                if (!playbackPaths.empty()) SendMessageW(inputs[1], LB_SETCURSEL, std::min(size_t(selected), playbackPaths.size() - 1), 0);
            }
        }
        if (id == 110) {
            std::wstring paths[3];
            for (int i : {0, 2}) { paths[i] = normalize(text(inputs[i])); SetWindowTextW(inputs[i], paths[i].c_str()); }
            if (paths[0].empty() || playbackPaths.empty() || paths[2].empty()) {
                MessageBoxW(window, L"请选择封面图、至少一张播放图和保存位置。", L"信息不完整", MB_OK | MB_ICONERROR); return 0;
            }
            unsigned delayMs;
            try { delayMs = parseDelay(text(durationInput)); }
            catch (const std::exception&) {
                MessageBoxW(window, L"每张时长请输入 1–65535 的整数，单位为毫秒。", L"时长无效", MB_OK | MB_ICONERROR);
                SetFocus(durationInput); return 0;
            }
            SetWindowTextW(statusLabel, L"正在生成，请稍候…"); UpdateWindow(window); SetCursor(LoadCursorW(nullptr, IDC_WAIT));
            try {
                generate(paths[0], playbackPaths, paths[2], delayMs);
                SetWindowTextW(statusLabel, L"生成完成。");
                MessageBoxW(window, (L"已保存到：\n" + paths[2]).c_str(), L"生成完成", MB_OK | MB_ICONINFORMATION);
            } catch (const std::exception& e) {
                SetWindowTextW(statusLabel, L"生成失败，请检查图片格式、保存位置或文件是否已存在。");
                MessageBoxA(window, e.what(), "APNG error", MB_OK | MB_ICONERROR);
            }
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        }
        return 0;
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE module, HINSTANCE, PWSTR, int show) {
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 1;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) { CoUninitialize(); return 1; }
    if (argc != 1) {
        int code = 0;
        try {
            int first = 1;
            unsigned delayMs = 100;
            if (argc > 2 && std::wstring(argv[1]) == L"--delay-ms") { delayMs = parseDelay(argv[2]); first = 3; }
            if (argc - first < 3) throw std::runtime_error("Usage: QQ-APNG-Disguise-CPP.exe [--delay-ms 100] cover frame1 [frame2 ...] output.png");
            generate(argv[first], std::vector<std::wstring>(argv + first + 1, argv + argc - 1), argv[argc - 1], delayMs);
        } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); code = 1; }
        LocalFree(argv); CoUninitialize(); return code;
    }
    LocalFree(argv);
    instance = module;
    SetProcessDPIAware();
    HDC screen = GetDC(nullptr); dpi = GetDeviceCaps(screen, LOGPIXELSX); ReleaseDC(nullptr, screen);
    font = CreateFontW(-px(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    heading = CreateFontW(-px(28), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = module; cls.lpfnWndProc = windowProc; cls.lpszClassName = L"QQAPNGCpp";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.hIcon = LoadIconW(module, MAKEINTRESOURCEW(1)); cls.hIconSm = cls.hIcon;
    RegisterClassExW(&cls);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT size{0, 0, px(750), px(540)}; AdjustWindowRect(&size, style, FALSE);
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"APNG 藏图工具 · C++", style, CW_USEDEFAULT, CW_USEDEFAULT,
        size.right - size.left, size.bottom - size.top, nullptr, nullptr, module, nullptr);
    if (!window) { DeleteObject(font); DeleteObject(heading); CoUninitialize(); return 1; }
    ShowWindow(window, show);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    DeleteObject(font); DeleteObject(heading); CoUninitialize();
    return static_cast<int>(message.wParam);
}
