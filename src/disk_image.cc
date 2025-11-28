/*
 * Disk Image Handler Implementation
 */

#include "disk_image.h"
#include <cstring>
#include <algorithm>

//=============================================================================
// RawDiskImage Implementation
//=============================================================================

RawDiskImage::RawDiskImage()
  : fp_(nullptr), read_only_(false) {
  geometry_ = DiskGeometries::SSSD_8INCH; // Default
}

RawDiskImage::RawDiskImage(const DiskGeometry& geom)
  : fp_(nullptr), geometry_(geom), read_only_(false) {
}

RawDiskImage::~RawDiskImage() {
  close();
}

void RawDiskImage::set_geometry(const DiskGeometry& geom) {
  geometry_ = geom;
}

bool RawDiskImage::auto_detect_geometry(size_t file_size) {
  // Try common geometries
  struct {
    DiskGeometry geom;
    const char* name;
  } known[] = {
    {DiskGeometries::SSSD_8INCH, "8\" SSSD"},
    {DiskGeometries::DSDD_8INCH, "8\" DSDD"},
    {DiskGeometries::SSSD_5INCH, "5.25\" SSSD"},
    {DiskGeometries::APPLE_II, "Apple II"},
    // SIMH Altair format (8 tracks of 137-byte sectors, 32 sectors each?)
    {{77, 2, 32, 137, 2}, "SIMH Altair"},
    // Generic large format
    {{254, 1, 32, 137, 2}, "SIMH Large"},
    // Altair 1.5MB HDF (Hard Disk Floppy) format
    // System tracks: boot loader + CCP + BDOS (need ~4 tracks for full system)
    {{745, 1, 16, 128, 4}, "Altair HDF 1.5MB"},
  };

  for (const auto& k : known) {
    if ((size_t)k.geom.total_bytes() == file_size) {
      geometry_ = k.geom;
      return true;
    }
  }

  // Try to figure out a reasonable geometry
  // Assume 128-byte sectors first
  if (file_size % 128 == 0) {
    int total_sectors = file_size / 128;
    // Try 26 sectors per track (standard)
    if (total_sectors % 26 == 0) {
      int tracks = total_sectors / 26;
      geometry_ = {tracks, 1, 26, 128, 2};
      return true;
    }
  }

  return false;
}

bool RawDiskImage::open(const char* filename, bool read_only) {
  close();

  filename_ = filename;
  read_only_ = read_only;

  fp_ = fopen(filename, read_only ? "rb" : "r+b");
  if (!fp_ && !read_only) {
    // Try read-only if read-write failed
    fp_ = fopen(filename, "rb");
    if (fp_) read_only_ = true;
  }

  if (!fp_) {
    return false;
  }

  // Get file size and try to auto-detect geometry
  fseek(fp_, 0, SEEK_END);
  size_t size = ftell(fp_);
  fseek(fp_, 0, SEEK_SET);

  if (geometry_.total_bytes() != (int)size) {
    // Geometry doesn't match, try auto-detect
    if (!auto_detect_geometry(size)) {
      fprintf(stderr, "Warning: Could not auto-detect geometry for %zu byte image\n", size);
      // Use file size to compute tracks assuming current sector params
      if (geometry_.sector_size > 0 && geometry_.sectors_per_track > 0) {
        int bytes_per_track = geometry_.sector_size * geometry_.sectors_per_track * geometry_.heads;
        if (bytes_per_track > 0 && size % bytes_per_track == 0) {
          geometry_.tracks = size / bytes_per_track;
        }
      }
    }
  }

  return true;
}

void RawDiskImage::close() {
  if (fp_) {
    fclose(fp_);
    fp_ = nullptr;
  }
}

bool RawDiskImage::is_open() const {
  return fp_ != nullptr;
}

const DiskGeometry& RawDiskImage::geometry() const {
  return geometry_;
}

long RawDiskImage::sector_offset(int track, int head, int sector) const {
  // sector is 1-based in CP/M
  int linear_sector = (track * geometry_.heads + head) * geometry_.sectors_per_track + (sector - 1);
  return (long)linear_sector * geometry_.sector_size;
}

bool RawDiskImage::read_sector(int track, int head, int sector, uint8_t* buffer) {
  if (!fp_) return false;
  if (track < 0 || track >= geometry_.tracks) return false;
  if (head < 0 || head >= geometry_.heads) return false;
  if (sector < 1 || sector > geometry_.sectors_per_track) return false;

  long offset = sector_offset(track, head, sector);
  if (fseek(fp_, offset, SEEK_SET) != 0) return false;

  size_t n = fread(buffer, 1, geometry_.sector_size, fp_);
  if (n < (size_t)geometry_.sector_size) {
    // Fill remainder with 0xE5 (CP/M empty marker)
    memset(buffer + n, 0xE5, geometry_.sector_size - n);
  }

  return true;
}

bool RawDiskImage::write_sector(int track, int head, int sector, const uint8_t* buffer) {
  if (!fp_ || read_only_) return false;
  if (track < 0 || track >= geometry_.tracks) return false;
  if (head < 0 || head >= geometry_.heads) return false;
  if (sector < 1 || sector > geometry_.sectors_per_track) return false;

  long offset = sector_offset(track, head, sector);
  if (fseek(fp_, offset, SEEK_SET) != 0) return false;

  size_t n = fwrite(buffer, 1, geometry_.sector_size, fp_);
  fflush(fp_);

  return n == (size_t)geometry_.sector_size;
}

//=============================================================================
// IMDDiskImage Implementation
//=============================================================================

IMDDiskImage::IMDDiskImage()
  : fp_(nullptr), read_only_(true) {
  // IMD is always read-only in this implementation (writes would need re-encoding)
}

IMDDiskImage::~IMDDiskImage() {
  close();
}

int IMDDiskImage::decode_sector_size(uint8_t code) {
  switch (code) {
    case 0: return 128;
    case 1: return 256;
    case 2: return 512;
    case 3: return 1024;
    case 4: return 2048;
    case 5: return 4096;
    case 6: return 8192;
    default: return 128;
  }
}

bool IMDDiskImage::open(const char* filename, bool read_only) {
  (void)read_only; // IMD is always read-only
  close();

  filename_ = filename;
  read_only_ = true;

  fp_ = fopen(filename, "rb");
  if (!fp_) {
    return false;
  }

  if (!parse_imd()) {
    close();
    return false;
  }

  return true;
}

bool IMDDiskImage::parse_imd() {
  // Read header until 0x1A
  comment_.clear();
  int ch;
  while ((ch = fgetc(fp_)) != EOF && ch != 0x1A) {
    comment_ += (char)ch;
  }

  if (ch != 0x1A) {
    fprintf(stderr, "IMD: Missing EOF marker in header\n");
    return false;
  }

  // Initialize geometry from tracks as we read them
  int max_track = 0;
  int max_head = 0;
  int max_sectors = 0;
  int sector_size = 0;

  // Parse track records
  while ((ch = fgetc(fp_)) != EOF) {
    IMDTrack track;
    track.mode = (uint8_t)ch;

    // Read cylinder
    ch = fgetc(fp_);
    if (ch == EOF) break;
    track.cylinder = (uint8_t)ch;

    // Read head (with flags)
    ch = fgetc(fp_);
    if (ch == EOF) break;
    track.head = (uint8_t)ch;
    bool has_cylinder_map = (track.head & 0x80) != 0;
    bool has_head_map = (track.head & 0x40) != 0;
    int physical_head = track.head & 0x01;

    // Read sector count
    ch = fgetc(fp_);
    if (ch == EOF) break;
    track.sector_count = (uint8_t)ch;

    // Read sector size code
    ch = fgetc(fp_);
    if (ch == EOF) break;
    track.sector_size = (uint8_t)ch;
    int this_sector_size = decode_sector_size(track.sector_size);

    // Read sector numbering map
    track.sector_map.resize(track.sector_count);
    if (fread(track.sector_map.data(), 1, track.sector_count, fp_) != track.sector_count) {
      fprintf(stderr, "IMD: Failed to read sector map\n");
      return false;
    }

    // Read optional cylinder map
    if (has_cylinder_map) {
      track.cylinder_map.resize(track.sector_count);
      if (fread(track.cylinder_map.data(), 1, track.sector_count, fp_) != track.sector_count) {
        fprintf(stderr, "IMD: Failed to read cylinder map\n");
        return false;
      }
    }

    // Read optional head map
    if (has_head_map) {
      track.head_map.resize(track.sector_count);
      if (fread(track.head_map.data(), 1, track.sector_count, fp_) != track.sector_count) {
        fprintf(stderr, "IMD: Failed to read head map\n");
        return false;
      }
    }

    // Read sector data
    track.sector_data.resize(track.sector_count);
    for (int i = 0; i < track.sector_count; i++) {
      int status = fgetc(fp_);
      if (status == EOF) {
        fprintf(stderr, "IMD: Unexpected EOF reading sector data\n");
        return false;
      }

      track.sector_data[i].resize(this_sector_size);

      switch (status) {
        case 0x00: // Data unavailable
          memset(track.sector_data[i].data(), 0xE5, this_sector_size);
          break;

        case 0x01: // Normal data
        case 0x03: // Normal with deleted mark
        case 0x05: // Normal with error
        case 0x07: // Deleted with error
          if (fread(track.sector_data[i].data(), 1, this_sector_size, fp_) != (size_t)this_sector_size) {
            fprintf(stderr, "IMD: Failed to read sector data\n");
            return false;
          }
          break;

        case 0x02: // Compressed (all same byte)
        case 0x04: // Compressed with deleted mark
        case 0x06: // Compressed with error
        case 0x08: // Compressed deleted with error
          {
            int fill_byte = fgetc(fp_);
            if (fill_byte == EOF) {
              fprintf(stderr, "IMD: Failed to read fill byte\n");
              return false;
            }
            memset(track.sector_data[i].data(), fill_byte, this_sector_size);
          }
          break;

        default:
          fprintf(stderr, "IMD: Unknown sector status 0x%02X\n", status);
          return false;
      }
    }

    // Update geometry tracking
    if (track.cylinder > max_track) max_track = track.cylinder;
    if (physical_head > max_head) max_head = physical_head;
    if (track.sector_count > max_sectors) max_sectors = track.sector_count;
    if (sector_size == 0) sector_size = this_sector_size;

    // Store track indexed by (cylinder * 2 + head)
    int key = track.cylinder * 2 + physical_head;
    tracks_[key] = std::move(track);
  }

  // Set geometry based on what we found
  geometry_.tracks = max_track + 1;
  geometry_.heads = max_head + 1;
  geometry_.sectors_per_track = max_sectors;
  geometry_.sector_size = sector_size > 0 ? sector_size : 128;
  geometry_.reserved_tracks = 2; // Assume standard

  return true;
}

void IMDDiskImage::close() {
  if (fp_) {
    fclose(fp_);
    fp_ = nullptr;
  }
  tracks_.clear();
  comment_.clear();
}

bool IMDDiskImage::is_open() const {
  return fp_ != nullptr;
}

const DiskGeometry& IMDDiskImage::geometry() const {
  return geometry_;
}

IMDDiskImage::IMDTrack* IMDDiskImage::find_track(int track, int head) {
  int key = track * 2 + head;
  auto it = tracks_.find(key);
  if (it == tracks_.end()) return nullptr;
  return &it->second;
}

bool IMDDiskImage::read_sector(int track, int head, int sector, uint8_t* buffer) {
  if (!fp_) return false;

  IMDTrack* t = find_track(track, head);
  if (!t) {
    // Track not in image - return empty
    memset(buffer, 0xE5, geometry_.sector_size);
    return true;
  }

  // Find sector in sector map
  int sector_index = -1;
  for (int i = 0; i < t->sector_count; i++) {
    if (t->sector_map[i] == sector) {
      sector_index = i;
      break;
    }
  }

  if (sector_index < 0) {
    // Sector not found - return empty
    memset(buffer, 0xE5, geometry_.sector_size);
    return true;
  }

  // Copy sector data
  int copy_size = std::min((int)t->sector_data[sector_index].size(), geometry_.sector_size);
  memcpy(buffer, t->sector_data[sector_index].data(), copy_size);
  if (copy_size < geometry_.sector_size) {
    memset(buffer + copy_size, 0xE5, geometry_.sector_size - copy_size);
  }

  return true;
}

bool IMDDiskImage::write_sector(int track, int head, int sector, const uint8_t* buffer) {
  (void)track; (void)head; (void)sector; (void)buffer;
  // IMD is read-only in this implementation
  fprintf(stderr, "IMD: Write not supported\n");
  return false;
}

//=============================================================================
// SIMHDiskImage Implementation
//=============================================================================

SIMHDiskImage::SIMHDiskImage()
  : fp_(nullptr), read_only_(false) {
  // SIMH Altair default: 77 tracks, 32 sectors, 137-byte sectors (128 data)
  geometry_ = {77, 1, 32, 128, 6};  // 6 reserved tracks for SIMH
}

SIMHDiskImage::~SIMHDiskImage() {
  close();
}

bool SIMHDiskImage::open(const char* filename, bool read_only) {
  close();

  filename_ = filename;
  read_only_ = read_only;

  fp_ = fopen(filename, read_only ? "rb" : "r+b");
  if (!fp_ && !read_only) {
    fp_ = fopen(filename, "rb");
    if (fp_) read_only_ = true;
  }

  if (!fp_) {
    return false;
  }

  // Get file size and determine geometry
  fseek(fp_, 0, SEEK_END);
  size_t size = ftell(fp_);
  fseek(fp_, 0, SEEK_SET);

  // SIMH format: sectors are 137 bytes each
  // Standard SIMH Altair is 77 tracks * 32 sectors * 137 = 337,568 bytes
  // But cpm2.dsk is 1,113,536 bytes = 8128 sectors * 137
  // That's 254 tracks * 32 sectors

  if (size % SIMH_SECTOR_SIZE == 0) {
    int total_sectors = size / SIMH_SECTOR_SIZE;

    // Try standard 32 sectors per track
    if (total_sectors % 32 == 0) {
      geometry_.tracks = total_sectors / 32;
      geometry_.heads = 1;
      geometry_.sectors_per_track = 32;
      geometry_.sector_size = SIMH_DATA_SIZE;
      geometry_.reserved_tracks = 6;  // Standard SIMH
    }
  }

  return true;
}

void SIMHDiskImage::close() {
  if (fp_) {
    fclose(fp_);
    fp_ = nullptr;
  }
}

bool SIMHDiskImage::is_open() const {
  return fp_ != nullptr;
}

const DiskGeometry& SIMHDiskImage::geometry() const {
  return geometry_;
}

long SIMHDiskImage::sector_offset(int track, int head, int sector) const {
  // SIMH uses linear sector addressing
  // sector is 1-based
  int linear_sector = (track * geometry_.heads + head) * geometry_.sectors_per_track + (sector - 1);
  return (long)linear_sector * SIMH_SECTOR_SIZE;
}

bool SIMHDiskImage::read_sector(int track, int head, int sector, uint8_t* buffer) {
  if (!fp_) return false;
  if (track < 0 || track >= geometry_.tracks) return false;
  if (head < 0 || head >= geometry_.heads) return false;
  if (sector < 1 || sector > geometry_.sectors_per_track) return false;

  long offset = sector_offset(track, head, sector);
  if (fseek(fp_, offset + SIMH_DATA_OFFSET, SEEK_SET) != 0) return false;

  size_t n = fread(buffer, 1, SIMH_DATA_SIZE, fp_);
  if (n < SIMH_DATA_SIZE) {
    // Fill remainder with 0xE5
    memset(buffer + n, 0xE5, SIMH_DATA_SIZE - n);
  }

  return true;
}

bool SIMHDiskImage::write_sector(int track, int head, int sector, const uint8_t* buffer) {
  if (!fp_ || read_only_) return false;
  if (track < 0 || track >= geometry_.tracks) return false;
  if (head < 0 || head >= geometry_.heads) return false;
  if (sector < 1 || sector > geometry_.sectors_per_track) return false;

  // Read the full 137-byte sector first
  uint8_t sector_buf[SIMH_SECTOR_SIZE];
  long offset = sector_offset(track, head, sector);

  if (fseek(fp_, offset, SEEK_SET) != 0) return false;
  size_t n = fread(sector_buf, 1, SIMH_SECTOR_SIZE, fp_);
  if (n < SIMH_SECTOR_SIZE) {
    // Initialize new sector
    memset(sector_buf, 0, SIMH_SECTOR_SIZE);
    sector_buf[0] = track;       // Track number
    sector_buf[1] = sector;      // Sector number
    sector_buf[2] = 0;           // Flags
  }

  // Copy data
  memcpy(sector_buf + SIMH_DATA_OFFSET, buffer, SIMH_DATA_SIZE);

  // Write back
  if (fseek(fp_, offset, SEEK_SET) != 0) return false;
  n = fwrite(sector_buf, 1, SIMH_SECTOR_SIZE, fp_);
  fflush(fp_);

  return n == SIMH_SECTOR_SIZE;
}

//=============================================================================
// Utility Functions
//=============================================================================

// Check if file is SIMH format (137-byte sectors)
static bool is_simh_format(const char* filename) {
  FILE* fp = fopen(filename, "rb");
  if (!fp) return false;

  fseek(fp, 0, SEEK_END);
  size_t size = ftell(fp);
  fclose(fp);

  // Check if size is divisible by 137 (SIMH sector size)
  // and also check it's NOT a standard format
  if (size % 137 == 0 && size > 0) {
    int total_sectors = size / 137;
    // SIMH typically uses 32 sectors per track
    if (total_sectors % 32 == 0 && total_sectors >= 32) {
      return true;
    }
  }

  return false;
}

DiskImage* open_disk_image(const char* filename, bool read_only) {
  // Determine format by extension
  std::string name(filename);
  std::string ext;
  size_t dot = name.rfind('.');
  if (dot != std::string::npos) {
    ext = name.substr(dot);
    // Convert to lowercase
    for (auto& c : ext) c = tolower(c);
  }

  DiskImage* img = nullptr;

  if (ext == ".imd") {
    img = new IMDDiskImage();
  } else if (is_simh_format(filename)) {
    // SIMH Altair format (137-byte sectors)
    img = new SIMHDiskImage();
  } else {
    // Default to raw format
    img = new RawDiskImage();
  }

  if (!img->open(filename, read_only)) {
    delete img;
    return nullptr;
  }

  return img;
}
