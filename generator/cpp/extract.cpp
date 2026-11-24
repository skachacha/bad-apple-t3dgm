#define NOMINMAX
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <stdexcept>
#include <string>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
void check(HRESULT h, const char *action) {
  if (FAILED(h)) {
    char msg[160];
    snprintf(msg, sizeof msg, "%s: HRESULT 0x%08lx", action, (unsigned long)h);
    throw std::runtime_error(msg);
  }
}
int wmain(int argc, wchar_t **argv) {
  try {
    if (argc != 3)
      throw std::runtime_error("badapple_extract input.mp4 output.masks");
    check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM");
    check(MFStartup(MF_VERSION), "MFStartup");
    ComPtr<IMFAttributes> a;
    check(MFCreateAttributes(a.GetAddressOf(), 3), "attributes");
    a->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    a->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);
    ComPtr<IMFSourceReader> r;
    check(MFCreateSourceReaderFromURL(argv[1], a.Get(), r.GetAddressOf()),
          "open source");
    check(r->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE),
          "streams off");
    check(r->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE),
          "video on");
    ComPtr<IMFMediaType> t;
    check(MFCreateMediaType(t.GetAddressOf()), "type");
    t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    t->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    check(r->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr,
                                 t.Get()),
          "RGB32 output");
    t.Reset();
    check(r->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                 t.GetAddressOf()),
          "output type");
    UINT32 w = 0, h = 0;
    check(MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &w, &h), "dimensions");
    UINT32 stride_raw = 0;
    LONG stride = LONG(w * 4);
    if (SUCCEEDED(t->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride_raw)))
      stride = LONG(stride_raw);
    PROPVARIANT d;
    PropVariantInit(&d);
    check(r->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,
                                      MF_PD_DURATION, &d),
          "duration");
    std::cout << "source " << w << "x" << h << " stride=" << stride
              << " duration=" << d.uhVal.QuadPart / 10000000. << "s\n";
    PropVariantClear(&d);
    if (w != 960 || h != 720)
      throw std::runtime_error("Expected a 960x720 video");
    UINT32 rate_n = 0, rate_d = 0;
    check(MFGetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, &rate_n, &rate_d),
          "frame rate");
    if (!rate_d || rate_n != 60 * rate_d)
      throw std::runtime_error("Expected 60 fps video");
    const int ow = 320, oh = 240, fps = 40, frames = 8780;
    int n = 0, decoded = 0;
    std::string last_mask;
    std::ofstream out(std::filesystem::path(argv[2]), std::ios::binary);
    if (!out)
      throw std::runtime_error("Cannot create output");
    while (n < frames) {
      DWORD flags = 0;
      LONGLONG timestamp = 0;
      ComPtr<IMFSample> s;
      check(r->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr,
                          &flags, &timestamp, s.GetAddressOf()),
            "read frame");
      if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        break;
      if (!s)
        continue;
      ++decoded;
      if (timestamp + 10000 < LONGLONG(n) * 10000000 / fps)
        continue;
      ComPtr<IMFMediaBuffer> b;
      check(s->ConvertToContiguousBuffer(b.GetAddressOf()), "frame buffer");
      BYTE *data = nullptr;
      DWORD length = 0;
      check(b->Lock(&data, nullptr, &length), "lock");
      if (uint64_t(std::abs(stride)) * h > length) {
        b->Unlock();
        throw std::runtime_error("Short RGB frame");
      }
      std::string pixels(ow * oh / 8, '\0');
      for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x) {
          unsigned total = 0, count = 0;
          for (UINT32 yy = UINT32(y) * h / oh; yy < UINT32(y + 1) * h / oh;
               ++yy) {
            auto row =
                data + (stride >= 0 ? yy : h - 1 - yy) * std::abs(stride);
            for (UINT32 xx = UINT32(x) * w / ow; xx < UINT32(x + 1) * w / ow;
                 ++xx) {
              auto q = row + xx * 4;
              total += unsigned(q[0]) + unsigned(q[1]) + unsigned(q[2]);
              count += 3;
            }
          }
          if (count && total / count >= 128)
            pixels[(y * ow + x) / 8] = char(uint8_t(pixels[(y * ow + x) / 8]) |
                                            (1 << ((y * ow + x) % 8)));
        }
      b->Unlock();
      out.write(pixels.data(), pixels.size());
      last_mask = pixels;
      ++n;
    }
    while (n == frames - 1 && !last_mask.empty()) {
      out.write(last_mask.data(), last_mask.size());
      ++n;
    }
    std::cout << "extracted " << n << " frames at " << fps
              << "fps, decoded=" << decoded << "\n";
    if (n != frames)
      throw std::runtime_error(
          "Video does not supply the expected 8780 pictures");
    r.Reset();
    t.Reset();
    a.Reset();
    MFShutdown();
    CoUninitialize();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
