// Included after the WIC image and PNG chunk helpers in main.cpp.
struct PlaybackFrame { Image image; unsigned ticks, denominator; };

static void appendFrame(std::vector<PlaybackFrame>& frames, const Image& image, unsigned ticks, unsigned denominator, bool merge) {
    if (!ticks) { ticks = 1; denominator = 10; }
    if (merge && !frames.empty()) {
        auto& last = frames.back();
        if (last.denominator == denominator && last.ticks <= UINT_MAX - ticks &&
            last.image.width == image.width && last.image.height == image.height && last.image.pixels == image.pixels) {
            last.ticks += ticks;
            return;
        }
    }
    frames.push_back({image, ticks, denominator});
}
static Image canvasImage(UINT width, UINT height) {
    if (!width || !height || uint64_t(width) * height * 4 > UINT_MAX)
        throw std::runtime_error("Animation canvas is too large.");
    return Image{width, height, Bytes(size_t(width) * height * 4, 0)};
}
static void composite(Image& canvas, const Image& patch, UINT x, UINT y, bool over) {
    if (x > canvas.width || y > canvas.height || patch.width > canvas.width - x || patch.height > canvas.height - y)
        throw std::runtime_error("Animation frame is outside its canvas.");
    for (UINT row = 0; row < patch.height; ++row) for (UINT col = 0; col < patch.width; ++col) {
        const BYTE* s = patch.pixels.data() + (size_t(row) * patch.width + col) * 4;
        BYTE* d = canvas.pixels.data() + (size_t(y + row) * canvas.width + x + col) * 4;
        if (!over || s[3] == 255) std::copy_n(s, 4, d);
        else if (s[3]) {
            unsigned alpha = unsigned(s[3]) * 255 + unsigned(d[3]) * (255 - s[3]);
            for (int c = 0; c < 3; ++c)
                d[c] = BYTE((unsigned(s[c]) * s[3] * 255 + unsigned(d[c]) * d[3] * (255 - s[3]) + alpha / 2) / alpha);
            d[3] = BYTE((alpha + 127) / 255);
        }
    }
}
static void clearRect(Image& canvas, UINT x, UINT y, UINT w, UINT h, const Bytes& color = Bytes(4, 0)) {
    for (UINT row = 0; row < h; ++row) for (UINT col = 0; col < w; ++col)
        std::copy_n(color.data(), 4, canvas.pixels.data() + (size_t(y + row) * canvas.width + x + col) * 4);
}
static unsigned metadataNumber(IWICMetadataQueryReader* reader, const wchar_t* name, unsigned fallback = 0) {
    if (!reader) return fallback;
    PROPVARIANT v{};
    HRESULT hr = reader->GetMetadataByName(name, &v);
    unsigned result = fallback;
    if (SUCCEEDED(hr)) {
        if (v.vt == VT_UI1) result = v.bVal;
        else if (v.vt == VT_UI2) result = v.uiVal;
        else if (v.vt == VT_UI4) result = v.ulVal;
        else if (v.vt == VT_BOOL) result = v.boolVal != VARIANT_FALSE;
    }
    PropVariantClear(&v);
    return result;
}
static std::vector<PlaybackFrame> gifFrames(IWICImagingFactory* factory, IWICBitmapDecoder* decoder, bool merge) {
    Com<IWICMetadataQueryReader> global;
    check(decoder->GetMetadataQueryReader(global.put()));
    auto canvas = canvasImage(metadataNumber(global.p, L"/logscrdesc/Width"), metadataNumber(global.p, L"/logscrdesc/Height"));
    Com<IWICPalette> palette;
    check(factory->CreatePalette(palette.put()));
    WICColor colors[256]{}; UINT actual = 0;
    if (SUCCEEDED(decoder->CopyPalette(palette.p))) check(palette->GetColors(256, colors, &actual));
    UINT count = 0; check(decoder->GetFrameCount(&count));
    std::vector<PlaybackFrame> result;
    for (UINT i = 0; i < count; ++i) {
        Com<IWICBitmapFrameDecode> frame;
        check(decoder->GetFrame(i, frame.put()));
        Com<IWICMetadataQueryReader> meta;
        check(frame->GetMetadataQueryReader(meta.put()));
        auto patch = pixels(factory, frame.p);
        unsigned x = metadataNumber(meta.p, L"/imgdesc/Left"), y = metadataNumber(meta.p, L"/imgdesc/Top");
        unsigned disposal = metadataNumber(meta.p, L"/grctlext/Disposal");
        bool transparent = metadataNumber(meta.p, L"/grctlext/TransparencyFlag") != 0;
        Bytes background(4, 0);
        unsigned index = metadataNumber(global.p, L"/logscrdesc/BackgroundColorIndex");
        if (!transparent && index < actual) {
            auto color = colors[index];
            background = {BYTE(color), BYTE(color >> 8), BYTE(color >> 16), 255};
        }
        if (!i) clearRect(canvas, 0, 0, canvas.width, canvas.height, background);
        Image previous;
        if (disposal == 3) previous = canvas;
        composite(canvas, patch, x, y, true);
        appendFrame(result, canvas, metadataNumber(meta.p, L"/grctlext/Delay", 10), 100, merge);
        if (disposal == 2) clearRect(canvas, x, y, patch.width, patch.height, background);
        else if (disposal == 3) canvas = std::move(previous);
    }
    return result;
}
struct InputChunk { std::string type; Bytes data; };
static std::vector<InputChunk> readPngChunks(const Bytes& input) {
    std::vector<InputChunk> result;
    size_t offset = 8;
    while (offset + 12 <= input.size()) {
        size_t size = be32(input.data() + offset);
        if (size > input.size() - offset - 12) throw std::runtime_error("Truncated PNG chunk.");
        result.push_back({std::string(input.begin() + offset + 4, input.begin() + offset + 8),
            Bytes(input.begin() + offset + 8, input.begin() + offset + 8 + size)});
        offset += size + 12;
        if (result.back().type == "IEND") break;
    }
    return result;
}
static Image decodeMemory(IWICImagingFactory* factory, Bytes& png) {
    if (png.size() > UINT_MAX) throw std::runtime_error("PNG frame is too large.");
    Com<IWICStream> stream; check(factory->CreateStream(stream.put()));
    check(stream->InitializeFromMemory(png.data(), static_cast<DWORD>(png.size())));
    Com<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromStream(stream.p, nullptr, WICDecodeMetadataCacheOnLoad, decoder.put()));
    Com<IWICBitmapFrameDecode> frame; check(decoder->GetFrame(0, frame.put()));
    return pixels(factory, frame.p);
}
static std::vector<PlaybackFrame> apngFrames(IWICImagingFactory* factory, const std::vector<InputChunk>& chunks, bool merge) {
    if (chunks.empty() || chunks.front().type != "IHDR" || chunks.front().data.size() != 13)
        throw std::runtime_error("Invalid PNG header.");
    Bytes header = chunks.front().data;
    auto canvas = canvasImage(be32(header.data()), be32(header.data() + 4));
    std::vector<InputChunk> palette;
    Bytes ctl, compressed;
    unsigned expected = 0, decoded = 0;
    for (auto& c : chunks) if (c.type == "acTL" && c.data.size() == 8) expected = be32(c.data.data());
    std::vector<PlaybackFrame> frames;
    auto finish = [&]() {
        if (ctl.empty()) return;
        if (compressed.empty()) throw std::runtime_error("Missing APNG frame data.");
        UINT w = be32(ctl.data() + 4), h = be32(ctl.data() + 8), x = be32(ctl.data() + 12), y = be32(ctl.data() + 16);
        Bytes ihdr; add32(ihdr, w); add32(ihdr, h); ihdr.insert(ihdr.end(), header.begin() + 8, header.end());
        Bytes png{137, 80, 78, 71, 13, 10, 26, 10}; chunk(png, "IHDR", ihdr);
        for (auto& c : palette) chunk(png, c.type.c_str(), c.data);
        chunk(png, "IDAT", compressed); chunk(png, "IEND", {});
        auto patch = decodeMemory(factory, png);
        unsigned disposal = ctl[24], blend = ctl[25];
        if (disposal > 2 || blend > 1) throw std::runtime_error("Invalid APNG frame operation.");
        Image previous;
        if (disposal == 2) previous = canvas;
        composite(canvas, patch, x, y, blend == 1);
        unsigned ticks = unsigned(ctl[20]) * 256 + ctl[21], denominator = unsigned(ctl[22]) * 256 + ctl[23];
        appendFrame(frames, canvas, ticks, denominator ? denominator : 100, merge);
        if (disposal == 1) clearRect(canvas, x, y, w, h);
        else if (disposal == 2) canvas = std::move(previous);
        ++decoded;
        compressed.clear();
    };
    for (auto& c : chunks) {
        if (c.type == "PLTE" || c.type == "tRNS") palette.push_back(c);
        else if (c.type == "fcTL") {
            finish();
            if (c.data.size() != 26) throw std::runtime_error("Invalid APNG frame control.");
            ctl = c.data;
        } else if (c.type == "IDAT" && !ctl.empty()) compressed.insert(compressed.end(), c.data.begin(), c.data.end());
        else if (c.type == "fdAT") {
            if (ctl.empty() || c.data.size() < 4) throw std::runtime_error("Invalid APNG frame data.");
            compressed.insert(compressed.end(), c.data.begin() + 4, c.data.end());
        }
    }
    finish();
    if (!expected || expected != decoded) throw std::runtime_error("APNG frame count does not match its data.");
    return frames;
}
static std::vector<PlaybackFrame> loadPlayback(IWICImagingFactory* factory, const std::wstring& path, unsigned delayMs, bool preserveTiming) {
    std::ifstream file(fs::path(path), std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open playback image.");
    char signature[8]{}; file.read(signature, 8);
    std::vector<PlaybackFrame> result;
    if (memcmp(signature, "\x89PNG\r\n\x1a\n", 8) == 0) {
        file.seekg(0); Bytes data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto chunks = readPngChunks(data);
        if (std::any_of(chunks.begin(), chunks.end(), [](const auto& c) { return c.type == "acTL"; })) result = apngFrames(factory, chunks, preserveTiming);
    } else if (memcmp(signature, "GIF87a", 6) == 0 || memcmp(signature, "GIF89a", 6) == 0) {
        Com<IWICBitmapDecoder> decoder;
        check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.put()));
        result = gifFrames(factory, decoder.p, preserveTiming);
    }
    if (result.empty()) result.push_back({load(factory, path), delayMs, 1000});
    else if (!preserveTiming) for (auto& f : result) { f.ticks = delayMs; f.denominator = 1000; }
    return result;
}
