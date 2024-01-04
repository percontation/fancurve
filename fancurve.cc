#if 0
set -eux
c++ -std=c++20 -framework IOKit -framework ApplicationServices "$0" -o "${0%.*}"
exec sudo "${0%.*}" "$@"
#endif

#include <map>
#include <cstdint>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <source_location>
#include <algorithm>

#include <IOKit/IOKitLib.h>
#include <CoreGraphics/CoreGraphics.h>
#include <mach/mach_error.h>
#include <unistd.h>

using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::fprintf;

extern "C" {

//
// Data types defined by AppleSMC.kext
//

enum {
  kSMCSuccess     = 0,
  kSMCError       = 1,
  kSMCKeyNotFound = 0x84
};

enum {
  kSMCUserClientOpen  = 0,
  kSMCUserClientClose = 1,
  kSMCHandleYPCEvent  = 2,
  kSMCReadKey         = 5,
  kSMCWriteKey        = 6,
  kSMCGetKeyCount     = 7,
  kSMCGetKeyFromIndex = 8,
  kSMCGetKeyInfo      = 9
};

typedef struct {
  unsigned char  major;
  unsigned char  minor;
  unsigned char  build;
  unsigned char  reserved;
  unsigned short release;
} SMCVersion;

typedef struct {
  uint16_t version;
  uint16_t length;
  uint32_t cpuPLimit;
  uint32_t gpuPLimit;
  uint32_t memPLimit;
} SMCPLimitData;

typedef struct {
  IOByteCount dataSize;
  uint32_t    dataType;
  uint8_t     dataAttributes;
} SMCKeyInfoData;

typedef struct {
  uint32_t       key;
  SMCVersion     vers;
  SMCPLimitData  pLimitData;
  SMCKeyInfoData keyInfo;
  uint8_t        result;
  uint8_t        status;
  uint8_t        data8;
  uint32_t       data32;
  uint8_t        bytes[32];
} SMCParamStruct;

}

namespace {

constexpr SMCParamStruct SMCParamStructZero = {
  .key = 0,
  .vers = {.major = 0, .minor = 0, .build = 0, .reserved = 0, .release = 0},
  .pLimitData = {.version = 0, .length = 0, .cpuPLimit = 0, .gpuPLimit = 0, .memPLimit = 0},
  .keyInfo = {.dataSize = 0, .dataType = 0, .dataAttributes = 0},
  .result = 0,
  .status = 0,
  .data8 = 0,
  .data32 = 0,
  .bytes = {0}
};

// Known types.
enum class smc_type : uint32_t {
  flt  = 'flt ',
  fp1f = 'fp1f',
  fp2e = 'fp2e',
  fp3d = 'fp3d',
  fp4c = 'fp4c',
  fp5b = 'fp5b',
  fp6a = 'fp6a',
  fp79 = 'fp79',
  fp88 = 'fp88',
  fpa6 = 'fpa6',
  fpc4 = 'fpc4',
  fpe2 = 'fpe2',
  sp1e = 'sp1e',
  sp2d = 'sp2d',
  sp3c = 'sp3c',
  sp4b = 'sp4b',
  sp5a = 'sp5a',
  sp69 = 'sp69',
  sp78 = 'sp78',
  sp87 = 'sp87',
  sp96 = 'sp96',
  spa5 = 'spa5',
  spb4 = 'spb4',
  spf0 = 'spf0',

  ui8  = 'ui8 ',
  ui16 = 'ui16',
  ui32 = 'ui32',
  ui64 = 'ui64',
  si8  = 'si8 ',
  si16 = 'si16',
  si32 = 'si32',
  si64 = 'si64',

  pwm  = '{pwm',
  fds  = '{fds',
  jst  = '{jst',
  ch8  = 'ch8*',
  flag = 'flag',
  ioft = 'ioft',
  hex_ = 'hex_',
};

bool is_float(smc_type t) {
  return (t == smc_type::flt
    || ((uint32_t)t & 0xFFFF0000) == 'fp\x00\x00'
    || ((uint32_t)t & 0xFFFF0000) == 'sp\x00\x00'
  );
}

template<class T> T as_int(const uint8_t *dat) = delete;
template<> std::uint8_t as_int<std::uint8_t>(const uint8_t *dat) { return dat[0]; }
template<> std::uint16_t as_int<std::uint16_t>(const uint8_t *dat) {
  return ((std::uint16_t)dat[0] << 8) | dat[1];
}
template<> std::uint32_t as_int<std::uint32_t>(const uint8_t *dat) {
  return ((std::uint32_t)dat[0] << 24)
    | ((std::uint32_t)dat[1] << 16)
    | ((std::uint32_t)dat[2] << 8)
    | ((std::uint32_t)dat[3] << 0);
}
template<> std::uint64_t as_int<std::uint64_t>(const uint8_t *dat) {
  return ((std::uint64_t)dat[0] << 56)
    | ((std::uint64_t)dat[1] << 48)
    | ((std::uint64_t)dat[2] << 40)
    | ((std::uint64_t)dat[3] << 32)
    | ((std::uint64_t)dat[4] << 24)
    | ((std::uint64_t)dat[5] << 16)
    | ((std::uint64_t)dat[6] << 8)
    | ((std::uint64_t)dat[7] << 0);
}
template<> std::int8_t as_int<std::int8_t>(const uint8_t *dat) { return std::int8_t(as_int<std::uint8_t>(dat)); }
template<> std::int16_t as_int<std::int16_t>(const uint8_t *dat) { return std::int16_t(as_int<std::uint16_t>(dat)); }
template<> std::int32_t as_int<std::int32_t>(const uint8_t *dat) { return std::int32_t(as_int<std::uint32_t>(dat)); }
template<> std::int64_t as_int<std::int64_t>(const uint8_t *dat) { return std::int64_t(as_int<std::uint64_t>(dat)); }

template<class T> void to_int(T, uint8_t *dat) = delete;
template<> void to_int(std::uint8_t x, uint8_t *dat) { dat[0] = x; }
template<> void to_int(std::uint16_t x, uint8_t *dat) { dat[0] = x >> 8; dat[1] = x; }
template<> void to_int(std::uint32_t x, uint8_t *dat) { dat[0] = x >> 24; dat[1] = x >> 16; dat[2] = x >> 8; dat[3] = x; }
template<> void to_int(std::uint64_t x, uint8_t *dat) { dat[0] = x >> 56; dat[1] = x >> 48; dat[2] = x >> 40; dat[3] = x >> 32;
                                                        dat[4] = x >> 24; dat[5] = x >> 16; dat[6] = x >> 8; dat[7] = x; }

template<> void to_int(std::int8_t x, uint8_t *dat) { return to_int(std::uint8_t(x), dat); }
template<> void to_int(std::int16_t x, uint8_t *dat) { return to_int(std::uint16_t(x), dat); }
template<> void to_int(std::int32_t x, uint8_t *dat) { return to_int(std::uint32_t(x), dat); }
template<> void to_int(std::int64_t x, uint8_t *dat) { return to_int(std::uint64_t(x), dat); }

double as_num(uint32_t type, const uint8_t *dat) {
  switch((smc_type)type) {
    case smc_type::ui8:  return as_int<std::uint8_t>(dat);
    case smc_type::ui16: return as_int<std::uint16_t>(dat);
    case smc_type::ui32: return as_int<std::uint32_t>(dat);
    case smc_type::ui64: return as_int<std::uint64_t>(dat);
    case smc_type::si8:  return as_int<std::int8_t>(dat);
    case smc_type::si16: return as_int<std::int16_t>(dat);
    case smc_type::si32: return as_int<std::int32_t>(dat);
    case smc_type::si64: return as_int<std::int64_t>(dat);
    case smc_type::flt:  return *(float*)dat;
    case smc_type::fp1f: return as_int<std::uint16_t>(dat) / 32768.0;
    case smc_type::fp2e: return as_int<std::uint16_t>(dat) / 16384.0;
    case smc_type::fp3d: return as_int<std::uint16_t>(dat) / 8192.0;
    case smc_type::fp4c: return as_int<std::uint16_t>(dat) / 4096.0;
    case smc_type::fp5b: return as_int<std::uint16_t>(dat) / 2048.0;
    case smc_type::fp6a: return as_int<std::uint16_t>(dat) / 1024.0;
    case smc_type::fp79: return as_int<std::uint16_t>(dat) / 512.0;
    case smc_type::fp88: return as_int<std::uint16_t>(dat) / 256.0;
    case smc_type::fpa6: return as_int<std::uint16_t>(dat) / 64.0;
    case smc_type::fpc4: return as_int<std::uint16_t>(dat) / 16.0;
    case smc_type::fpe2: return as_int<std::uint16_t>(dat) / 4.0;
    case smc_type::sp1e: return as_int<std::int16_t>(dat) / 16384.0;
    case smc_type::sp2d: return as_int<std::int16_t>(dat) / 8192.0;
    case smc_type::sp3c: return as_int<std::int16_t>(dat) / 4096.0;
    case smc_type::sp4b: return as_int<std::int16_t>(dat) / 2048.0;
    case smc_type::sp5a: return as_int<std::int16_t>(dat) / 1024.0;
    case smc_type::sp69: return as_int<std::int16_t>(dat) / 512.0;
    case smc_type::sp78: return as_int<std::int16_t>(dat) / 256.0;
    case smc_type::sp87: return as_int<std::int16_t>(dat) / 128.0;
    case smc_type::sp96: return as_int<std::int16_t>(dat) / 64.0;
    case smc_type::spa5: return as_int<std::int16_t>(dat) / 32.0;
    case smc_type::spb4: return as_int<std::int16_t>(dat) / 16.0;
    case smc_type::spf0: return as_int<std::int16_t>(dat) / 1.0;
    default: return NAN;
  }
}

int as_int(uint32_t type, const uint8_t *dat) {
  switch((smc_type)type) {
    case smc_type::ui8:  return as_int<std::uint8_t>(dat);
    case smc_type::ui16: return as_int<std::uint16_t>(dat);
    case smc_type::ui32: return as_int<std::uint32_t>(dat);
    case smc_type::ui64: return as_int<std::uint64_t>(dat);
    case smc_type::si8:  return as_int<std::int8_t>(dat);
    case smc_type::si16: return as_int<std::int16_t>(dat);
    case smc_type::si32: return as_int<std::int32_t>(dat);
    case smc_type::si64: return as_int<std::int64_t>(dat);
    case smc_type::flt:  return *(float*)dat;
    case smc_type::fp1f: return as_int<std::uint16_t>(dat) / 32768.0;
    case smc_type::fp2e: return as_int<std::uint16_t>(dat) / 16384.0;
    case smc_type::fp3d: return as_int<std::uint16_t>(dat) / 8192.0;
    case smc_type::fp4c: return as_int<std::uint16_t>(dat) / 4096.0;
    case smc_type::fp5b: return as_int<std::uint16_t>(dat) / 2048.0;
    case smc_type::fp6a: return as_int<std::uint16_t>(dat) / 1024.0;
    case smc_type::fp79: return as_int<std::uint16_t>(dat) / 512.0;
    case smc_type::fp88: return as_int<std::uint16_t>(dat) / 256.0;
    case smc_type::fpa6: return as_int<std::uint16_t>(dat) / 64.0;
    case smc_type::fpc4: return as_int<std::uint16_t>(dat) / 16.0;
    case smc_type::fpe2: return as_int<std::uint16_t>(dat) / 4.0;
    case smc_type::sp1e: return as_int<std::int16_t>(dat) / 16384.0;
    case smc_type::sp2d: return as_int<std::int16_t>(dat) / 8192.0;
    case smc_type::sp3c: return as_int<std::int16_t>(dat) / 4096.0;
    case smc_type::sp4b: return as_int<std::int16_t>(dat) / 2048.0;
    case smc_type::sp5a: return as_int<std::int16_t>(dat) / 1024.0;
    case smc_type::sp69: return as_int<std::int16_t>(dat) / 512.0;
    case smc_type::sp78: return as_int<std::int16_t>(dat) / 256.0;
    case smc_type::sp87: return as_int<std::int16_t>(dat) / 128.0;
    case smc_type::sp96: return as_int<std::int16_t>(dat) / 64.0;
    case smc_type::spa5: return as_int<std::int16_t>(dat) / 32.0;
    case smc_type::spb4: return as_int<std::int16_t>(dat) / 16.0;
    case smc_type::spf0: return as_int<std::int16_t>(dat) / 1.0;
    default: return -1;
  }
}

} //namespace

#if 0
class Motion {
  io_connect_t conn;
public:
  struct xyzparam {
    unsigned short x;
    unsigned short y;
    unsigned short z;
    char pad[34];
  } last;

  IOReturn connect() {
    if(conn)
      return true;

    io_service_t srv = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("SMCMotionSensor"));
    if(srv == IO_OBJECT_NULL)
      return kIOReturnNotFound;

    IOReturn res = IOServiceOpen(srv, mach_task_self(), 0, &conn);
    IOObjectRelease(srv);

    if(res != kIOReturnSuccess) {
      conn = MACH_PORT_NULL;
      return res;
    }

    return res;
  }

  void disconnect() {
    if(conn == MACH_PORT_NULL)
      return;
    IOServiceClose(conn);
    conn = MACH_PORT_NULL;
  }

  bool connected() const {
    return MACH_PORT_VALID(conn);
  }

  Motion() : conn(MACH_PORT_NULL) {}

  ~Motion() {
    if(connected())
      disconnect();
  }

  bool operator ()() {
    xyzparam in = {0, 0, 0, {0}}; // Need to memset('\0x1')???
    size_t size = sizeof(xyzparam);
    IOReturn res = IOConnectCallStructMethod(conn, 5, &in, sizeof(in), &last, &size);
    if(res == kIOReturnSuccess)
      return true;
    std::source_location loc{};
    fprintf(stderr, "%s:%u:%u:`%s`: %s\n", loc.file_name(), loc.line(), loc.column(), loc.function_name(), mach_error_string(res));
    return false;
  }
};
#endif

class SMC {
public:
  struct Key {
    std::uint32_t val;

    Key(const char name[4]) : val(0) {
      val |= (unsigned char)name[0] << 24;
      val |= (unsigned char)name[1] << 16;
      val |= (unsigned char)name[2] << 8;
      val |= (unsigned char)name[3] << 0;
    }
    Key(std::uint32_t v) : val(v) {}
    Key(int v) : val(v) {}
    operator std::uint32_t() const { return val; }

    char operator [](int i) const {
      switch(i) {
        case 0: return val >> 24;
        case 1: return val >> 16;
        case 2: return val >> 8;
        case 3: return val >> 0;
        default: return 0;
      }
    }
  };

private:
  io_connect_t conn;
  struct info_cache_entry {
    SMCKeyInfoData data;
    enum {
      no_entry = 0, // entry doesn't exist yet
      info, // data is valid
      keynotfound, // caching a kSMCKeyNotFound
    } status;
  };
  std::map<Key, info_cache_entry> info_cache;

public:

  IOReturn connect() {
    if(conn)
      return true;

    io_service_t srv = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSMC"));
    if(srv == IO_OBJECT_NULL)
      return kIOReturnNotFound;

    // Note: some other people use 0 instead of 1.
    IOReturn res = IOServiceOpen(srv, mach_task_self(), 1, &conn);
    IOObjectRelease(srv);

    if(res != kIOReturnSuccess) {
      conn = MACH_PORT_NULL;
      return res;
    }

    res = IOConnectCallMethod(conn, kSMCUserClientOpen, NULL, 0, NULL, 0, NULL, NULL, NULL, NULL);
    if(res != kIOReturnSuccess) {
      IOServiceClose(conn);
      conn = MACH_PORT_NULL;
      return res;
    }

    return res;
  }

  void disconnect() {
    if(conn == MACH_PORT_NULL)
      return;

    IOConnectCallMethod(conn, kSMCUserClientClose, NULL, 0, NULL, 0, NULL, NULL, NULL, NULL);
    IOServiceClose(conn);
    conn = MACH_PORT_NULL;
  }

  bool connected() const {
    return MACH_PORT_VALID(conn);
  }

  SMC() : conn(MACH_PORT_NULL) {}

  ~SMC() {
    if(connected())
      disconnect();
  }

private:
  bool ypc(SMCParamStruct *in, SMCParamStruct *out, const std::source_location loc = std::source_location::current()) {
    out->result = -1;
    size_t size = sizeof(SMCParamStruct);
    IOReturn res = IOConnectCallStructMethod(conn, kSMCHandleYPCEvent, in, sizeof(SMCParamStruct), out, &size);
    if(res == kIOReturnSuccess)
      return true;
    fprintf(stderr, "%s:%u:%u:`%s`: %s\n", loc.file_name(), loc.line(), loc.column(), loc.function_name(), mach_error_string(res));
    return false;
  }
public:

  // Get cached key info. Returns null on failure.
  const SMCKeyInfoData *get_key_info(Key key) {
    info_cache_entry &entry = info_cache[key];
    switch(entry.status) {
      case info_cache_entry::no_entry: break;
      case info_cache_entry::info: return &entry.data;
      default: return nullptr;
    }

    SMCParamStruct in = SMCParamStructZero, out = SMCParamStructZero;
    in.key = key;
    in.data8 = kSMCGetKeyInfo;
    if(!ypc(&in, &out))
      return nullptr;
    switch(out.result) {
      case kSMCSuccess:
        entry.data = out.keyInfo;
        entry.status = info_cache_entry::info;
        return &entry.data;
      case kSMCKeyNotFound:
        entry.status = info_cache_entry::keynotfound;
        return nullptr;
      default:
        return nullptr;
    }
  }

  Key get_key_from_index(int idx) {
    SMCParamStruct in = SMCParamStructZero, out = SMCParamStructZero;
    in.data8 = kSMCGetKeyFromIndex;
    in.data32 = idx;
    return ypc(&in, &out) && out.result == kSMCSuccess ? Key(out.key) : Key(0);
  }

  bool read(Key key, SMCParamStruct *out) {
    const SMCKeyInfoData *info = get_key_info(key);
    if(!info)
      return false;
    SMCParamStruct in = SMCParamStructZero;
    in.data8 = kSMCReadKey;
    in.key = key;
    in.keyInfo = *info;
    // uhhh... out->keyInfo gets cleared by the call,
    // but I wanted callers to have access to the get_key_info results,
    // so I guess I'll just shove it back in for now.
    return ypc(&in, out) && (out->keyInfo = *info, out->result == kSMCSuccess);
  }

  bool write(Key key, const SMCKeyInfoData &info, const uint8_t bytes[32]) {
    SMCParamStruct in = SMCParamStructZero, out;
    in.key = key;
    in.data8 = kSMCWriteKey;
    in.keyInfo = info;
    if(info.dataSize > 32)
      return false; // NYI
    for(int i = 0; i < 32; i++)
      in.bytes[i] = bytes[i];
    return ypc(&in, &out) && out.result == kSMCSuccess;
  }

  double read_num(Key key, double fail = NAN) {
    SMCParamStruct out = SMCParamStructZero;
    if(!read(key, &out))
      return fail;
    return as_num(out.keyInfo.dataType, out.bytes);
  }

  int read_int(Key key, int fail = -1) {
    SMCParamStruct out = SMCParamStructZero;
    if(!read(key, &out))
      return fail;
    return as_int(out.keyInfo.dataType, out.bytes);
  }

private:
  template<class T>
  bool write_(Key key, T val) {
    const SMCKeyInfoData *info = get_key_info(key);
    if(!info)
      return false;
    uint8_t bytes[32] = {0};
    switch(smc_type(info->dataType)) {
      case smc_type::ui8:  if(val < 0) return false; to_int(std::uint8_t(val), bytes); break;
      case smc_type::ui16: if(val < 0) return false; to_int(std::uint16_t(val), bytes); break;
      case smc_type::ui32: if(val < 0) return false; to_int(std::uint32_t(val), bytes); break;
      case smc_type::ui64: if(val < 0) return false; to_int(std::uint64_t(val), bytes); break;
      case smc_type::si8:  to_int(std::int8_t(val), bytes); break;
      case smc_type::si16: to_int(std::int16_t(val), bytes); break;
      case smc_type::si32: to_int(std::int32_t(val), bytes); break;
      case smc_type::si64: to_int(std::int64_t(val), bytes); break;
      case smc_type::flt:  *(float*)bytes = (float)val; break;
      case smc_type::fp1f: if(val < 0) return false; to_int(std::uint16_t(val * 32768.0), bytes); break;
      case smc_type::fp2e: if(val < 0) return false; to_int(std::uint16_t(val * 16384.0), bytes); break;
      case smc_type::fp3d: if(val < 0) return false; to_int(std::uint16_t(val * 8192.0), bytes); break;
      case smc_type::fp4c: if(val < 0) return false; to_int(std::uint16_t(val * 4096.0), bytes); break;
      case smc_type::fp5b: if(val < 0) return false; to_int(std::uint16_t(val * 2048.0), bytes); break;
      case smc_type::fp6a: if(val < 0) return false; to_int(std::uint16_t(val * 1024.0), bytes); break;
      case smc_type::fp79: if(val < 0) return false; to_int(std::uint16_t(val * 512.0), bytes); break;
      case smc_type::fp88: if(val < 0) return false; to_int(std::uint16_t(val * 256.0), bytes); break;
      case smc_type::fpa6: if(val < 0) return false; to_int(std::uint16_t(val * 64.0), bytes); break;
      case smc_type::fpc4: if(val < 0) return false; to_int(std::uint16_t(val * 16.0), bytes); break;
      case smc_type::fpe2: if(val < 0) return false; to_int(std::uint16_t(val * 4.0), bytes); break;
      case smc_type::sp1e: to_int(std::int16_t(val * 16384.0), bytes); break;
      case smc_type::sp2d: to_int(std::int16_t(val * 8192.0), bytes); break;
      case smc_type::sp3c: to_int(std::int16_t(val * 4096.0), bytes); break;
      case smc_type::sp4b: to_int(std::int16_t(val * 2048.0), bytes); break;
      case smc_type::sp5a: to_int(std::int16_t(val * 1024.0), bytes); break;
      case smc_type::sp69: to_int(std::int16_t(val * 512.0), bytes); break;
      case smc_type::sp78: to_int(std::int16_t(val * 256.0), bytes); break;
      case smc_type::sp87: to_int(std::int16_t(val * 128.0), bytes); break;
      case smc_type::sp96: to_int(std::int16_t(val * 64.0), bytes); break;
      case smc_type::spa5: to_int(std::int16_t(val * 32.0), bytes); break;
      case smc_type::spb4: to_int(std::int16_t(val * 16.0), bytes); break;
      case smc_type::spf0: to_int(std::int16_t(val * 1.0), bytes); break;
      default: return false;
    }
    return write(key, *info, bytes);
  }

public:
  bool write_num(Key key, float val) {
    return write_(key, val);
  }

  bool write_int(Key key, int val) {
    return write_(key, val);
  }
};

namespace {

// We use this function as a proxy for being allowed to run the chassis temperatures
// hotter on account of not being in contact with a person.
bool is_docked() {
  uint32_t count = 0;
  return CGGetOnlineDisplayList(0, nullptr, &count) == kCGErrorSuccess && count > 1;
}

struct fan_info {
  float max;
  float min;
  char id;
  SMC::Key Tg() const {
    return SMC::Key('F\x00Tg' | ((int)id << 16));
  }
  SMC::Key Md() const {
    return SMC::Key('F\x00Md' | ((int)id << 16));
  }
};

volatile std::sig_atomic_t gSignalStatus;
void signal_handler(int signal) {
  gSignalStatus = signal;
}

} // namespace

#if 0
int main(int argc, char *argv[]) {
  Motion m;
  auto x = m.connect();
  fprintf(stderr, "%s\n", mach_error_string(x));
  if(!m.connected())
    return -1;
  puts("conn");
  if(!m())
    return -1;
  puts("dat");
  printf("%hd\n", m.last.x);
}
#endif

int main(int argc, char *argv[]) {
  SMC smc;
  smc.connect();
  if(!smc.connected())
    return -1;

  gSignalStatus = 0;
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  bool tty = isatty(fileno(stderr));
  bool templog = tty; // Write out the temps to the terminal.
  bool raise_floor = false; // Keep fans at least 68% high.

  for(int i = 1; i < argc; i++) {
    if(std::strcmp(argv[i], "log") == 0)
      templog = true;
    if(std::strcmp(argv[i], "nolog") == 0)
      templog = false;
    if(std::strcmp(argv[i], "high") == 0)
      raise_floor = true;
  }

  std::vector<SMC::Key> hot;
  std::vector<SMC::Key> warm;
  std::vector<SMC::Key> skin;
  std::vector<SMC::Key> other;
  std::vector<fan_info> fans;

  int keys = smc.read_int('#KEY');
  for(int i = 0; i < keys; ++i) {
    SMC::Key key = smc.get_key_from_index(i);
    if(key[0] == 'T') {
      const SMCKeyInfoData *info = smc.get_key_info(key);
      if(is_float(smc_type(info->dataType))) {
        // Temperature sensor
        if(key[1] == 's') {
          // "skin" sensor, for the case.
          skin.push_back(key);
        }
        else if(key[1] == 'C' && key[3] != 'P') {
          // This includes CPU cores and other on-die sensors
          // that run hotter than the rest of the board.
          hot.push_back(key);
        }
        else if(key[1] == 'G' && key[3] != 'P') {
          // GPU sensors (that aren't proximity).
          hot.push_back(key);
        }
        else if(key[1] == 'T' && (key[2] == 'L' || key[2] == 'R') && key[3] == 'D') {
          // Thunderbolt ports.
          // Maybe this should just be the same as the cold other sensors,
          // but this was what was generally setting off my fans when docked
          // so I want to try letting them get warmer.
          warm.push_back(key);
        }
        else if(key[1] == 'P' && key[2] == 'C' && key[3] == 'D') {
          // PCH
          // Same deal, this is what is generally tripping the fans, and is
          // fine to be hotter. I'd say 80 degC is on the high end of fine,
          // and that's where the "warm" curve maxes out. So, perfect.
          warm.push_back(key);
        }
        else {
          other.push_back(key);
        }
      }
    }
    else if(key[0] == 'F' && key[1] >= '0' && key[1] <= '9' && key[2] == 'T' && key[3] == 'g') {
      fan_info fan;
      fan.id = key[1];
      char t[4];
      t[0] = 'F';
      t[1] = fan.id;
      t[2] = 'M';
      t[3] = 'x';
      fan.max = smc.read_num(SMC::Key(t));
      t[3] = 'n';
      fan.min = smc.read_num(SMC::Key(t));
      
      bool success = false;
      if(fan.max > fan.min) { // sneaky: check that neither is NaN
        t[3] = 'd';
        if(smc.write_int(SMC::Key(t), 1)) {
          t[2] = 'T';
          t[3] = 'g';
          if(smc.write_num(SMC::Key(t), fan.max)) {
            success = true;
          }
        }
      }
      if(success) {
        fans.push_back(fan);
      } else {
        // Return to automatic control.
        t[2] = 'M';
        t[3] = 'd';
        smc.write_int(SMC::Key(t), 0);
      }
    }
  }

  if(hot.size() + other.size() == 0) {
    fprintf(stderr, "No temperature sensors!\n");
    return 1;
  }
  if(fans.size() == 0) {
    fprintf(stderr, "No controllable fans!\n");
    if(geteuid() != 0)
      fprintf(stderr, "You probably need to run me as root.\n");
    return 1;
  }

  float max_lin;
  SMC::Key max_key = 0;
  float max_val;

  auto mu = [&](float low, float high, SMC::Key k){
    float val = smc.read_num(k);
    float lin = (val - low) / (high - low);
    if(lin > max_lin) {
      max_lin = lin;
      max_key = k;
      max_val = val;
    }
  };

  char roll[3] = {99, 99, 99}; // fans will start maxed as a "hello, it's working"
  int counter = 0;
  if(templog && tty)
    fprintf(stderr, "\033[K\n\033[K\n\033[K\n\033[K\n\033[K\n\033[K\n\033[K\n\033[K\n\033[K\n\033[K\n\033[K\033[10A");
  while(gSignalStatus == 0) {
    max_lin = -INFINITY;
    max_key = 0;
    max_val = 0.0;
    // It's possible that "hot" should be changed to MUCH hotter.
    // This is because it's not like turning the fans up does much to
    // change on-die temperatures, when we're already keeping the
    // heatsinks and finstacks cool.
    // Let's try it.
    //for(SMC::Key k : hot) mu(69., 83., k);
    for(SMC::Key k : hot) mu(82., 96., k);
    for(SMC::Key k : warm) mu(65., 79., k);
    for(SMC::Key k : skin) is_docked() ? mu(40., 45., k) : mu(36., 40., k);
    for(SMC::Key k : other) mu(60., 70., k);

    // actually only goes to 99
    int percent = max_lin >= 0.99 ? 99
                : max_lin <= 0.0 ? 0
                : int(99*(max_lin + 0.01));

    if(++counter >= 11) {
      counter = 1;
      if(templog && tty)
        fprintf(stderr, "\033[10A");
      // Need to periodically re-enable manual mode.
      for(fan_info &fan : fans)
        smc.write_int(fan.Md(), 1);
    }
    // Print the target fan percentage value and the "hottest" sensor responsible for it.
    if(templog)
      fprintf(stderr, "%02d%% %6.2f %c%c%c%c\n%s", percent, max_val, max_key[0], max_key[1], max_key[2], max_key[3], (tty?"\033[K":""));

    // Don't use the current target percentage, get median of the last 3 percentages.
    // This is mostly because cpu core temps (TC%dC) are spiky.
    roll[0] = roll[1];
    roll[1] = roll[2];
    roll[2] = percent;
    char sorted[3] = {roll[0], roll[1], roll[2]};
    std::sort(&sorted[0], &sorted[3]);
    percent = sorted[1];

    if(raise_floor && percent < 68)
      percent = 68;
    for(fan_info &fan : fans)
      smc.write_num(fan.Tg(), percent/99.f * (fan.max - fan.min) + fan.min);

    // Sleep 2-7 seconds; updates come slower when temps are cool.
    // FWIW: 3 second update interval was giving (sys+user)/real = 0.1%
    usleep(2'000'000 + int(5'000'000 * (1. - sorted[2]/99.)));
  }

  for(fan_info &fan : fans)
    smc.write_int(fan.Md(), 0);

  return 0;
}
