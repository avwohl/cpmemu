/*
 * CP/M Disk Definition Parser
 *
 * Parses cpmtools-style diskdefs files for disk geometry definitions.
 * Format based on cpmtools (https://github.com/lipro-cpm4l/cpmtools)
 */

#ifndef DISKDEFS_H
#define DISKDEFS_H

#include <cstdint>
#include <string>
#include <vector>
#include <map>

// CP/M OS type for compatibility
enum CPMOSType {
  OS_CPM22 = 0,    // CP/M 2.2
  OS_CPM3 = 1,     // CP/M 3.0 (CP/M Plus)
  OS_ISX = 2,      // ISX
  OS_P2DOS = 3,    // P2DOS (extended)
  OS_ZSYS = 4      // Z-System
};

// Disk definition structure - matches cpmtools diskdef format
struct DiskDef {
  std::string name;           // Definition name (e.g., "ibm-3740")

  // Geometry
  int seclen = 128;           // Sector length in bytes
  int tracks = 77;            // Total tracks (cylinders * heads)
  int sectrk = 26;            // Sectors per track
  int heads = 1;              // Number of heads (sides) - derived if not explicit

  // CP/M parameters
  int blocksize = 1024;       // Allocation block size (1024, 2048, 4096, 8192, 16384)
  int maxdir = 64;            // Maximum directory entries
  int boottrk = 2;            // Reserved boot/system tracks
  int dirblks = 0;            // Directory blocks (0 = auto-calculate from AL0/AL1)

  // Skew
  int skew = 0;               // Sector skew/interleave
  std::vector<int> skewtab;   // Explicit sector translation table

  // Advanced
  int offset = 0;             // Image offset in bytes
  int logicalextents = 0;     // Logical extents (rare)
  CPMOSType os = OS_CPM22;    // OS compatibility

  // Calculated DPB values (Disk Parameter Block)
  int bsh() const;            // Block shift factor (log2(blocksize/128))
  int blm() const;            // Block mask (blocksize/128 - 1)
  int exm() const;            // Extent mask
  int dsm() const;            // Max block number (total blocks - 1)
  int drm() const;            // Max directory entry (maxdir - 1)
  int al0() const;            // Directory allocation bitmap byte 0
  int al1() const;            // Directory allocation bitmap byte 1
  int cks() const;            // Checksum vector size
  int off() const;            // Reserved tracks offset (= boottrk)

  // Derived values
  int capacity_kb() const;    // Usable capacity in KB
  int total_bytes() const;    // Total image size in bytes
  bool is_valid() const;      // Check if definition is valid
  std::string describe() const; // Human-readable description

  // Build sector translation table
  std::vector<int> build_xlat() const;
};

// Disk definitions collection
class DiskDefs {
public:
  DiskDefs();

  // Load definitions from file
  bool load_file(const char* filename);

  // Load from string (embedded definitions)
  bool load_string(const char* content);

  // Load built-in defaults
  void load_defaults();

  // Get definition by name
  const DiskDef* get(const std::string& name) const;

  // List all definitions
  std::vector<std::string> list() const;

  // Find definitions matching criteria
  std::vector<std::string> find_by_capacity(int min_kb, int max_kb) const;
  std::vector<std::string> find_by_os(CPMOSType os) const;

  // Add a definition
  void add(const DiskDef& def);

  // Get count
  size_t count() const { return defs_.size(); }

private:
  std::map<std::string, DiskDef> defs_;

  // Parse a single diskdef block
  bool parse_diskdef(const std::string& block);

  // Parse OS type string
  static CPMOSType parse_os(const std::string& s);
};

// Default embedded diskdefs (common formats)
extern const char* DEFAULT_DISKDEFS;

#endif // DISKDEFS_H
