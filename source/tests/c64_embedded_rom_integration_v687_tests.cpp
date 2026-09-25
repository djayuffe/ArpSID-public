#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/c64_embedded_rom_loader.h"
#include <cstdio>
#include <vector>
using namespace ArpSID::C64;
static std::vector<uint8_t> buildRsid(uint16_t loadAddr, const std::vector<uint8_t>& initCode){
  std::vector<uint8_t> v(0x7Cu,0u);
  v[0]='R';v[1]='S';v[2]='I';v[3]='D';v[4]=0;v[5]=2;v[6]=0;v[7]=0x7Cu;
  v[10]=loadAddr>>8;v[11]=loadAddr&0xFF;v[14]=0;v[15]=1;v[16]=0;v[17]=1;v[0x76]=0;v[0x77]=0x04;
  v.push_back(loadAddr&0xFF);v.push_back(loadAddr>>8);v.insert(v.end(),initCode.begin(),initCode.end());
  return v;
}
int main(){
  int fail=0;
  // 1. Bare platform
  C64Platform plat;
  bool stock = c64LoadEmbeddedStockRoms(plat);
  printf("platform: complete=%d verifiedStock=%d  kernalId=%d basicId=%d charId=%d\n",
    plat.hasCompleteExternalRomSet(), plat.hasVerifiedStockRomSet(),
    (int)plat.kernalRomIdentity(),(int)plat.basicRomIdentity(),(int)plat.characterRomIdentity());
  if (kEmbeddedC64RomsAvailable) {
    if(!stock){printf("FAIL: embedded ROMs not verified stock\n");fail=1;}
  } else {
    if(stock){printf("FAIL: reported verified stock without bundled ROMs\n");fail=1;}
    if(plat.hasVerifiedStockRomSet()){printf("FAIL: verifiedStock true without bundled ROMs\n");fail=1;}
  }

  // 2. RSID runtime WITHOUT roms (baseline blockers)
  C64Runtime noRom; noRom.reset(true);
  auto rsid=buildRsid(0x0800u,{0x60u});
  noRom.loadPsid(rsid.data(),rsid.size()); noRom.runInit(1,4096); noRom.enablePhi2Machine(true);
  uint32_t mNo=noRom.physicalExactnessBlockerMask();

  // 3. RSID runtime WITH embedded roms (as kernel now does)
  C64Runtime withRom; withRom.reset(true);
  c64LoadEmbeddedStockRoms(withRom.platform());
  withRom.loadPsid(rsid.data(),rsid.size()); withRom.runInit(1,4096); withRom.enablePhi2Machine(true);
  uint32_t mYes=withRom.physicalExactnessBlockerMask();

  const uint32_t MISS=(uint32_t)C64PhysicalExactnessBlocker::MissingRealRoms;
  const uint32_t UNV =(uint32_t)C64PhysicalExactnessBlocker::RomIdentityUnverified;
  printf("blockerMask no-rom=0x%X  with-rom=0x%X  (MissingRealRoms=0x%X RomIdentityUnverified=0x%X)\n",mNo,mYes,MISS,UNV);
  printf("with-rom: MissingRealRoms set? %d  RomIdentityUnverified set? %d\n",(mYes&MISS)!=0,(mYes&UNV)!=0);
  if (kEmbeddedC64RomsAvailable) {
    if(mYes & MISS){printf("FAIL: MissingRealRoms still set with embedded ROMs\n");fail=1;}
    if(mYes & UNV){printf("FAIL: RomIdentityUnverified still set with embedded ROMs\n");fail=1;}
  } else {
    if(!(mYes & MISS)){printf("FAIL: MissingRealRoms cleared without bundled ROMs\n");fail=1;}
  }
  if(!(mNo & MISS)){printf("WARN: baseline expected MissingRealRoms set\n");}
  printf("%s\n", fail?"ROMTEST FAIL":"ROMTEST PASS");
  return fail;
}
