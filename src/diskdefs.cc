/*
 * CP/M Disk Definition Parser Implementation
 */

#include "diskdefs.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <cctype>

// DiskDef calculated values

int DiskDef::bsh() const {
  // Block shift: log2(blocksize/128)
  int bs = blocksize / 128;
  int shift = 0;
  while (bs > 1) { bs >>= 1; shift++; }
  return shift;
}

int DiskDef::blm() const {
  // Block mask: blocksize/128 - 1
  return (blocksize / 128) - 1;
}

int DiskDef::exm() const {
  // Extent mask depends on block size and whether DSM > 255
  // For DSM <= 255: each directory entry can address 16K
  // For DSM > 255: each directory entry can address 8K (16-bit block numbers)
  int d = dsm();
  switch (blocksize) {
    case 1024:  return (d > 255) ? 0 : 0;   // Not valid for DSM>255
    case 2048:  return (d > 255) ? 0 : 1;
    case 4096:  return (d > 255) ? 1 : 3;
    case 8192:  return (d > 255) ? 3 : 7;
    case 16384: return (d > 255) ? 7 : 15;
    default:    return 0;
  }
}

int DiskDef::dsm() const {
  // Max block number = total data blocks - 1
  // Data area = (tracks - boottrk) * sectrk * seclen
  int data_bytes = (tracks - boottrk) * sectrk * seclen;
  int total_blocks = data_bytes / blocksize;
  return total_blocks - 1;
}

int DiskDef::drm() const {
  return maxdir - 1;
}

int DiskDef::al0() const {
  // Directory allocation bitmap - how many blocks for directory
  // Each directory entry = 32 bytes, so entries per block = blocksize/32
  int entries_per_block = blocksize / 32;
  int dir_blocks = (maxdir + entries_per_block - 1) / entries_per_block;
  if (dir_blocks > 8) dir_blocks = 8;  // AL0 only has 8 bits

  // Set bits from MSB
  int al = 0;
  for (int i = 0; i < dir_blocks && i < 8; i++) {
    al |= (0x80 >> i);
  }
  return al;
}

int DiskDef::al1() const {
  int entries_per_block = blocksize / 32;
  int dir_blocks = (maxdir + entries_per_block - 1) / entries_per_block;
  if (dir_blocks <= 8) return 0;

  int remaining = dir_blocks - 8;
  if (remaining > 8) remaining = 8;

  int al = 0;
  for (int i = 0; i < remaining; i++) {
    al |= (0x80 >> i);
  }
  return al;
}

int DiskDef::cks() const {
  // Checksum vector size for removable media
  // CKS = (DRM + 1) / 4 for removable, 0 for fixed
  // Assume removable for floppies (small capacity)
  if (capacity_kb() < 1000) {
    return (maxdir + 3) / 4;
  }
  return 0;  // Fixed disk
}

int DiskDef::off() const {
  return boottrk;
}

int DiskDef::capacity_kb() const {
  int data_bytes = (tracks - boottrk) * sectrk * seclen;
  return data_bytes / 1024;
}

int DiskDef::total_bytes() const {
  return tracks * sectrk * seclen;
}

bool DiskDef::is_valid() const {
  if (name.empty()) return false;
  if (seclen < 128 || seclen > 4096) return false;
  if (tracks < 1 || tracks > 65535) return false;
  if (sectrk < 1 || sectrk > 255) return false;
  if (blocksize < 1024 || blocksize > 16384) return false;
  if (maxdir < 16 || maxdir > 8192) return false;
  if (boottrk < 0 || boottrk >= tracks) return false;

  // Check 8MB limit for CP/M 2.2
  if (os == OS_CPM22 && total_bytes() > 8 * 1024 * 1024) {
    return false;
  }

  return true;
}

std::string DiskDef::describe() const {
  char buf[256];
  int cap = capacity_kb();
  const char* cap_unit = "KB";
  int cap_val = cap;
  if (cap >= 1024) {
    cap_val = cap / 1024;
    cap_unit = "MB";
  }

  snprintf(buf, sizeof(buf), "%s: %d%s, %d trk, %d sec/trk, %d bytes/sec, %d dir",
           name.c_str(), cap_val, cap_unit, tracks, sectrk, seclen, maxdir);
  return std::string(buf);
}

std::vector<int> DiskDef::build_xlat() const {
  std::vector<int> xlat(sectrk);

  if (!skewtab.empty()) {
    // Use explicit skew table
    for (int i = 0; i < sectrk && i < (int)skewtab.size(); i++) {
      xlat[i] = skewtab[i];
    }
  } else if (skew > 0) {
    // Generate skew table
    int pos = 0;
    for (int i = 0; i < sectrk; i++) {
      xlat[i] = pos + 1;  // 1-based sector numbers
      pos = (pos + skew) % sectrk;
    }
  } else {
    // No skew - 1:1 mapping
    for (int i = 0; i < sectrk; i++) {
      xlat[i] = i + 1;
    }
  }

  return xlat;
}

// DiskDefs implementation

DiskDefs::DiskDefs() {
}

CPMOSType DiskDefs::parse_os(const std::string& s) {
  std::string lower = s;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  if (lower == "2.2" || lower == "cpm22" || lower == "cpm2.2") return OS_CPM22;
  if (lower == "3" || lower == "3.0" || lower == "cpm3" || lower == "cpm+") return OS_CPM3;
  if (lower == "isx") return OS_ISX;
  if (lower == "p2dos") return OS_P2DOS;
  if (lower == "zsys" || lower == "z-system") return OS_ZSYS;
  return OS_CPM22;  // Default
}

bool DiskDefs::parse_diskdef(const std::string& block) {
  DiskDef def;
  std::istringstream iss(block);
  std::string line;

  while (std::getline(iss, line)) {
    // Skip empty lines and comments
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) continue;
    if (line[start] == '#') continue;

    // Parse key/value
    std::istringstream linestream(line);
    std::string key, value;
    linestream >> key >> value;

    // Convert key to lowercase
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);

    if (key == "diskdef") {
      def.name = value;
    } else if (key == "seclen") {
      def.seclen = std::stoi(value);
    } else if (key == "tracks") {
      def.tracks = std::stoi(value);
    } else if (key == "sectrk") {
      def.sectrk = std::stoi(value);
    } else if (key == "heads") {
      def.heads = std::stoi(value);
    } else if (key == "blocksize") {
      def.blocksize = std::stoi(value);
    } else if (key == "maxdir") {
      def.maxdir = std::stoi(value);
    } else if (key == "boottrk") {
      def.boottrk = std::stoi(value);
    } else if (key == "dirblks") {
      def.dirblks = std::stoi(value);
    } else if (key == "skew") {
      def.skew = std::stoi(value);
    } else if (key == "skewtab") {
      // Parse comma-separated sector list
      std::string rest;
      std::getline(linestream, rest);
      value += rest;
      std::istringstream tabstream(value);
      std::string num;
      while (std::getline(tabstream, num, ',')) {
        // Trim whitespace
        size_t s = num.find_first_not_of(" \t");
        size_t e = num.find_last_not_of(" \t");
        if (s != std::string::npos) {
          def.skewtab.push_back(std::stoi(num.substr(s, e - s + 1)));
        }
      }
    } else if (key == "offset") {
      // Handle offset with optional 'trk' suffix
      if (value.find("trk") != std::string::npos) {
        int trks = std::stoi(value);
        def.offset = trks * def.sectrk * def.seclen;
      } else {
        def.offset = std::stoi(value);
      }
    } else if (key == "logicalextents") {
      def.logicalextents = std::stoi(value);
    } else if (key == "os") {
      def.os = parse_os(value);
    } else if (key == "end") {
      break;
    }
    // Ignore unknown keys (like libdsk:format)
  }

  if (def.name.empty()) return false;

  defs_[def.name] = def;
  return true;
}

bool DiskDefs::load_file(const char* filename) {
  FILE* f = fopen(filename, "r");
  if (!f) return false;

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  std::string content(size, '\0');
  size_t read = fread(&content[0], 1, size, f);
  fclose(f);
  if (read != (size_t)size) {
    content.resize(read);
  }

  return load_string(content.c_str());
}

bool DiskDefs::load_string(const char* content) {
  std::string str(content);
  std::istringstream iss(str);
  std::string line;
  std::string block;
  bool in_diskdef = false;

  while (std::getline(iss, line)) {
    // Check for diskdef start
    size_t pos = line.find("diskdef");
    if (pos != std::string::npos && (pos == 0 || isspace(line[pos-1]))) {
      if (in_diskdef && !block.empty()) {
        parse_diskdef(block);
      }
      block = line + "\n";
      in_diskdef = true;
      continue;
    }

    // Check for end
    if (in_diskdef) {
      block += line + "\n";
      std::string trimmed = line;
      size_t start = trimmed.find_first_not_of(" \t");
      if (start != std::string::npos) {
        trimmed = trimmed.substr(start);
      }
      if (trimmed.substr(0, 3) == "end") {
        parse_diskdef(block);
        block.clear();
        in_diskdef = false;
      }
    }
  }

  // Handle last block if no 'end'
  if (in_diskdef && !block.empty()) {
    parse_diskdef(block);
  }

  return !defs_.empty();
}

void DiskDefs::load_defaults() {
  load_string(DEFAULT_DISKDEFS);
}

const DiskDef* DiskDefs::get(const std::string& name) const {
  auto it = defs_.find(name);
  if (it != defs_.end()) {
    return &it->second;
  }
  return nullptr;
}

std::vector<std::string> DiskDefs::list() const {
  std::vector<std::string> names;
  for (const auto& kv : defs_) {
    names.push_back(kv.first);
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::vector<std::string> DiskDefs::find_by_capacity(int min_kb, int max_kb) const {
  std::vector<std::string> result;
  for (const auto& kv : defs_) {
    int cap = kv.second.capacity_kb();
    if (cap >= min_kb && cap <= max_kb) {
      result.push_back(kv.first);
    }
  }
  return result;
}

std::vector<std::string> DiskDefs::find_by_os(CPMOSType os) const {
  std::vector<std::string> result;
  for (const auto& kv : defs_) {
    if (kv.second.os == os) {
      result.push_back(kv.first);
    }
  }
  return result;
}

void DiskDefs::add(const DiskDef& def) {
  if (!def.name.empty()) {
    defs_[def.name] = def;
  }
}

// Default embedded disk definitions
const char* DEFAULT_DISKDEFS = R"(
# Standard CP/M disk definitions
# Based on cpmtools diskdefs format

# ============================================
# 8" Floppy Disks
# ============================================

# IBM 3740 - Standard 8" SSSD (most common CP/M format)
# 77 tracks, 26 sectors/track, 128 bytes/sector = 250KB
diskdef ibm-3740
  seclen 128
  tracks 77
  sectrk 26
  blocksize 1024
  maxdir 64
  skew 6
  boottrk 2
  os 2.2
end

# 8" DSDD - Double-sided double-density
diskdef ibm-8dsdd
  seclen 512
  tracks 154
  sectrk 8
  blocksize 2048
  maxdir 128
  skew 0
  boottrk 2
  os 2.2
end

# ============================================
# 5.25" Floppy Disks
# ============================================

# Kaypro II - 5.25" SSDD
diskdef kaypro2
  seclen 512
  tracks 40
  sectrk 10
  blocksize 1024
  maxdir 64
  skew 0
  boottrk 1
  os 2.2
end

# Kaypro IV - 5.25" DSDD
diskdef kaypro4
  seclen 512
  tracks 80
  sectrk 10
  blocksize 2048
  maxdir 64
  skew 0
  boottrk 1
  os 2.2
end

# Osborne 1
diskdef osborne1
  seclen 256
  tracks 40
  sectrk 10
  blocksize 1024
  maxdir 64
  skew 0
  boottrk 3
  os 2.2
end

# Apple II CP/M (140K)
diskdef apple-do
  seclen 256
  tracks 35
  sectrk 16
  blocksize 1024
  maxdir 64
  skewtab 0,6,12,3,9,15,14,5,11,2,8,7,13,4,10,1
  boottrk 3
  os 2.2
end

# Generic 5.25" DSDD (360K)
diskdef dsdd-360k
  seclen 512
  tracks 80
  sectrk 9
  blocksize 2048
  maxdir 64
  skew 0
  boottrk 2
  os 2.2
end

# ============================================
# 3.5" Floppy Disks
# ============================================

# 3.5" DD (720K)
diskdef dd-720k
  seclen 512
  tracks 160
  sectrk 9
  blocksize 2048
  maxdir 128
  skew 1
  boottrk 2
  os 2.2
end

# 3.5" HD (1.44M) - CP/M 3 format
diskdef hd-1440k
  seclen 512
  tracks 160
  sectrk 18
  blocksize 4096
  maxdir 256
  skew 1
  boottrk 2
  os 3
end

# ============================================
# Hard Disks and Large Media
# ============================================

# 4MB Hard Disk (P2DOS compatible)
diskdef hd-4mb
  seclen 128
  tracks 1024
  sectrk 32
  blocksize 2048
  maxdir 256
  skew 0
  boottrk 2
  os 2.2
end

# 8MB Hard Disk (maximum for CP/M 2.2)
# This is the largest disk CP/M 2.2 can handle
diskdef hd-8mb
  seclen 512
  tracks 512
  sectrk 32
  blocksize 4096
  maxdir 512
  skew 0
  boottrk 2
  os 2.2
end

# Z80Pack 4MB Hard Disk
diskdef z80pack-hd
  seclen 128
  tracks 255
  sectrk 128
  blocksize 2048
  maxdir 1024
  skew 0
  boottrk 0
  os 2.2
end

# SIMH Altair Hard Disk (similar to MITS)
diskdef simh-hd
  seclen 128
  tracks 254
  sectrk 32
  blocksize 2048
  maxdir 256
  skew 0
  boottrk 6
  os 2.2
end

# North Star Hard Disk 4MB
diskdef northstar-hd4
  seclen 512
  tracks 512
  sectrk 16
  blocksize 4096
  maxdir 256
  skew 0
  boottrk 0
  os 2.2
end

# North Star Hard Disk 8MB
diskdef northstar-hd8
  seclen 512
  tracks 1024
  sectrk 16
  blocksize 8192
  maxdir 256
  skew 0
  boottrk 0
  os 2.2
end

# ============================================
# Emulator/Virtual Formats
# ============================================

# Small test disk (100K)
diskdef test-100k
  seclen 128
  tracks 40
  sectrk 20
  blocksize 1024
  maxdir 32
  skew 0
  boottrk 2
  os 2.2
end

# Medium virtual disk (1MB)
diskdef virtual-1mb
  seclen 512
  tracks 128
  sectrk 16
  blocksize 2048
  maxdir 128
  skew 0
  boottrk 2
  os 2.2
end

# Large virtual disk (2MB)
diskdef virtual-2mb
  seclen 512
  tracks 256
  sectrk 16
  blocksize 2048
  maxdir 256
  skew 0
  boottrk 2
  os 2.2
end

)";
