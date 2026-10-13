#include "runtime/engine_backdrop.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main() { try {
    auto check = [](bool yes, const char *message) { if (!yes) throw std::runtime_error(message); };
    struct File {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("daytona-engine-backdrop-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~File() { std::error_code ec; std::filesystem::remove(path, ec); }
    } file;
    rt::EngineBackdrop art;
    check(!art.load(file.path.string()) && !art.loaded(), "missing file must fall back");
    std::vector<unsigned char> bytes(16 + rt::EngineBackdrop::Width * rt::EngineBackdrop::Height * 2);
    const unsigned char header[16] = {'D','2','B','G','v','0','0','2',0x56,5,0,0,0x80,1,0,0};
    std::copy(header, header + 16, bytes.begin());
    for (size_t i = 16; i < bytes.size(); i += 2) bytes[i] = 0xe2, bytes[i+1] = 128;
    auto write = [&] { std::ofstream out(file.path, std::ios::binary); out.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size())); };
    write(); check(art.load(file.path.string()) && !art.active(), "valid artwork should load but not activate");
    std::array<uint32_t,4096> pens{}; pens[0x602] = 0xff102030; pens[0x60e] = 0xff90a0b0;
    for (int x : {-435, -93, -1, 0, 248, 495, 496, 588, 930})
        for (int y : {0,383}) check(art.sample(x,y,pens.data()) == 0xff506070, "palette blend and crop coordinates");
    pens.fill(0xff000000); check(art.sample(-1,10,pens.data()) == 0xff000000, "live palette fade");
    bytes[16] = 0xff; write(); check(art.load(file.path.string()), "white lettering entry must be accepted");
    pens[0x60f] = 0xffffffff; check(art.sample(-435,0,pens.data()) == 0xffffffff, "white lettering retains the live pen");
    bytes[16] = 0xe2;
    std::vector<uint16_t> pixels(512*512,0x604); pixels[0] = 0x60e;
    std::vector<uint8_t> flags(512*512,16);
    art.update(435,pixels.data(),flags.data(),1,[](unsigned){return 0u;});
    check(!art.active(), "an unrelated picture with the same corner pens must be rejected");
    art.update(0,nullptr,nullptr,2,[](unsigned){return 0u;});
    art.update(436,nullptr,nullptr,2,[](unsigned){return 0u;});
    art.update(435,nullptr,nullptr,2,[](unsigned i){return i==0x5000?1u:0u;});
    art.update(435,nullptr,nullptr,2,[](unsigned i){return i==0x5004?0x8000u:0u;});
    art.update(435,nullptr,nullptr,2,[](unsigned i){return i==0x6000?1u:0u;});
    check(!art.active(), "native, oversized, scrolling, disabled and windowed states must reject before sampling");
    bytes[0] = 'X';write();check(!art.load(file.path.string()) && !art.loaded(), "wrong magic must clear previous artwork");
    bytes[0] = 'D';bytes[7] = '1';write();check(!art.load(file.path.string()), "old sides-only cache must be rejected");
    bytes[7] = '2';
    bytes[0] = 'D';bytes[8] = 0;write();check(!art.load(file.path.string()), "wrong dimensions");
    bytes[8] = 0x56;bytes[16] = 0xf0;write();check(!art.load(file.path.string()), "reserved palette entries");
    bytes[16] = 0xe2;bytes.push_back(0);write();check(!art.load(file.path.string()), "trailing data");
    bytes.resize(64);write();check(!art.load(file.path.string()), "truncated data");
    std::cout << "engine_backdrop passed\n";
    return 0;
} catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
