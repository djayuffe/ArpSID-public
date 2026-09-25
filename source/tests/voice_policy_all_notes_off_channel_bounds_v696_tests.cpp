#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::abort(); }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
int main() {
    const std::string src = readFile(std::string(ARPSID_SOURCE_DIR) + "/include/arpsid/core/sid_runtime_voice_policy.h");
    require(src.find("const int boundedHeldCount = std::clamp(heldCount_, 0, kMaxHeldNotes)") != std::string::npos,
            "allNotesOffChannel clamps heldCount_ before compacting");
    require(src.find("read < boundedHeldCount") != std::string::npos,
            "allNotesOffChannel reads only bounded held count");
    require(src.find("HeldNote compacted[kMaxHeldNotes]") != std::string::npos,
            "allNotesOffChannel compacts through fixed-size scratch storage");
    require(src.find("compacted[write++] = h") != std::string::npos,
            "allNotesOffChannel writes retained notes only to bounded scratch storage");
    require(src.find("heldCount_ = std::clamp(write, 0, kMaxHeldNotes)") != std::string::npos,
            "allNotesOffChannel stores bounded held count after compaction");
    const auto fnPos = src.find("void allNotesOffChannel(int channel) noexcept");
    const auto nextPos = src.find("void panic", fnPos);
    const std::string fn = (fnPos != std::string::npos && nextPos != std::string::npos) ? src.substr(fnPos, nextPos - fnPos) : std::string{};
    require(fn.find("for (int read = 0; read < heldCount_; ++read)") == std::string::npos,
            "old unbounded read loop removed from allNotesOffChannel");
    require(fn.find("if (write != read) held_[write] = h") == std::string::npos,
            "old data-dependent direct held_ write removed from allNotesOffChannel");
    std::cout << "VoicePolicyAllNotesOffChannelBoundsV696Tests PASS\n";
    return 0;
}
