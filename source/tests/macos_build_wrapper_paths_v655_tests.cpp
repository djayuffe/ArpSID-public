#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string build = readFile(root + "/build.sh");
    const std::string a = readFile(root + "/scripts/macos/macos_full_build_install_clear_au_logic_cache.sh");
    const std::string b = readFile(root + "/scripts/macos/full_build_install_clear_au_logic_cache.sh");
    const std::string refresh = readFile(root + "/scripts/macos/refresh_logic_auv3.sh");
    const std::string signLogic = readFile(root + "/scripts/macos/sign_logic_bundle.sh");
    const std::string cmake = readFile(root + "/CMakeLists.txt");

    require(build.rfind("#!/usr/bin/env bash", 0) == 0, "build.sh has correct shebang");
    require(build.find("scripts/macos/install_auv2_component.sh") != std::string::npos,
            "build.sh delegates AUv2 install to canonical installer");
    require(a.find("scripts/macos_full_build_install_clear_au_logic_cache.sh") != std::string::npos,
            "macos/macos_full alias points to canonical script");
    require(b.find("scripts/macos_full_build_install_clear_au_logic_cache.sh") != std::string::npos,
            "macos/full alias points to canonical script");
    require(build.find("_pass59") == std::string::npos,
            "build.sh contains no old pass-numbered macOS entrypoint");
    require(refresh.find("CFBundleExecutable") != std::string::npos,
            "Logic AUv3 refresh reads the installed executable name from Info.plist");
    require(refresh.find("stop_container_app") != std::string::npos,
            "Logic AUv3 refresh has a container-app cleanup helper");
    require(refresh.find("killall -9 \"$APP_EXECUTABLE\"") != std::string::npos,
            "Logic AUv3 refresh kills the actual installed executable");
    require(refresh.find("killall -9 \"ArpSID AUv3\"") != std::string::npos,
            "Logic AUv3 refresh covers the AUv3 wrapper executable fallback");
    require(refresh.find("tell application id") != std::string::npos,
            "Logic AUv3 refresh asks the launched app to quit by bundle id first");
    require(cmake.find("_arpsid_user_target_codesign_args") != std::string::npos,
            "macOS user install keeps a target-specific codesign arg list for entitlements");
    require(cmake.find("add_library(arpsid_auv3 MODULE") != std::string::npos &&
            cmake.find("BUNDLE TRUE") != std::string::npos &&
            cmake.find("add_executable(arpsid_auv3 MACOSX_BUNDLE") == std::string::npos &&
            cmake.find("source/au3/ArpSIDAUv3ExtensionMain.mm") == std::string::npos,
            "AUv3 appex uses the known-good factory-principal Mach-O bundle module shape");
    require(cmake.find("XCODE_ATTRIBUTE_PRODUCT_TYPE \"com.apple.product-type.app-extension\"") == std::string::npos,
            "AUv3 appex does not force app-extension executable product metadata");
    require(cmake.find("<key>NSExtensionPrincipalClass</key><string>ArpSIDAUAudioUnitFactory</string>") != std::string::npos &&
            cmake.find("<key>NSExtensionPrincipalClass</key><string>ArpSIDAUExtensionViewController</string>") == std::string::npos,
            "AUv3 plist uses the known-good AUAudioUnit factory principal class");
    require(cmake.find("add_custom_target(arpsid_auv3_wrapper_embed") != std::string::npos &&
            cmake.find("DEPENDS arpsid_auv3_wrapper arpsid_auv3") != std::string::npos,
            "AUv3 wrapper has an explicit embed target that depends on the current appex");
    require(cmake.find("add_custom_target(arpsid_logic ALL\n        DEPENDS arpsid_auv3_wrapper_embed") != std::string::npos,
            "Logic wrapper assembly depends on the refreshed AUv3 embed target");
    require(cmake.find("set(_arpsid_logic_post_assemble_sign_cmds)") != std::string::npos &&
            cmake.find("${_arpsid_logic_post_assemble_sign_cmds}") != std::string::npos,
            "Logic wrapper assembly signs after mutating the copied app plist");
    require(cmake.find("--entitlements \"${ARPSID_HOST_AUDIO_CAPTURE_ENTITLEMENTS}\"") != std::string::npos,
            "macOS user install preserves standalone/wrapper audio-capture entitlements");
    require(cmake.find("--entitlements \"${ARPSID_AUV3_EXTENSION_ENTITLEMENTS}\"") != std::string::npos,
            "macOS user install signs raw AUv3 appex with extension entitlements");
    require(cmake.find("ArpSID-AUv3-raw/arpsid_auv3.appex/Contents/MacOS/arpsid_auv3") != std::string::npos,
            "macOS user install signs the raw AUv3 executable before the appex bundle");
    require(cmake.find("\"${ARPSID_LOGIC_APP_INSTALL_DIR}/ArpSID.app\"\n"
                       "                    \"${ARPSID_CODESIGN_IDENTITY}\"\n"
                       "                    \"${ARPSID_AUV3_HOST_ENTITLEMENTS}\"\n"
                       "                    \"${ARPSID_AUV3_EXTENSION_ENTITLEMENTS}\"") != std::string::npos,
            "Logic-visible AUv3 install re-signs the installed app with host and extension entitlements");
    require(cmake.find("COMMAND /usr/bin/codesign ${_arpsid_user_codesign_args} --deep \"${ARPSID_LOGIC_APP_INSTALL_DIR}/ArpSID.app\"") == std::string::npos,
            "Logic-visible AUv3 install must not generic deep-sign and strip entitlements");
    require(signLogic.find("--entitlements \"$EXT_ENTITLEMENTS\"") != std::string::npos &&
            signLogic.find("\"$APPEX_BIN\"") != std::string::npos,
            "Logic AUv3 signer embeds extension entitlements on the nested appex executable");
    std::cout << "MacOSBuildWrapperPathsV655Tests PASS\n";
    return 0;
}
