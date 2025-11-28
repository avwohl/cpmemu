/*
 * Disk Image Handler for CP/M Emulator
 *
 * Supports:
 * - ImageDisk (.IMD) format with full metadata
 * - SIMH Altair format (137-byte sectors with metadata)
 * - Raw sector images (.dsk)
 */

#ifndef DISK_IMAGE_H
#define DISK_IMAGE_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <map>

// Disk geometry parameters
struct DiskGeometry {
  int tracks;           // Number of tracks (cylinders)
  int heads;            // Number of heads (sides)
  int sectors_per_track; // Sectors per track
  int sector_size;      // Bytes per sector
  int reserved_tracks;  // System tracks (for boot/CCP/BDOS)

  int total_sectors() const {
    return tracks * heads * sectors_per_track;
  }

  int total_bytes() const {
    return total_sectors() * sector_size;
  }
};

// Standard disk geometries
namespace DiskGeometries {
  // Standard 8" SSSD (Single-Sided Single-Density)
  const DiskGeometry SSSD_8INCH = {77, 1, 26, 128, 2};

  // Standard 8" DSDD (Double-Sided Double-Density)
  const DiskGeometry DSDD_8INCH = {77, 2, 26, 256, 2};

  // 5.25" SSSD
  const DiskGeometry SSSD_5INCH = {40, 1, 18, 128, 2};

  // Apple II CP/M (140K)
  const DiskGeometry APPLE_II = {35, 1, 16, 256, 3};
}

// Abstract base class for disk images
class DiskImage {
public:
  virtual ~DiskImage() = default;

  // Open a disk image file
  virtual bool open(const char* filename, bool read_only = false) = 0;

  // Close the disk image
  virtual void close() = 0;

  // Check if image is open
  virtual bool is_open() const = 0;

  // Get disk geometry
  virtual const DiskGeometry& geometry() const = 0;

  // Read a sector
  // track: 0-based track number
  // head: 0-based head (side) number
  // sector: 1-based sector number (CP/M convention)
  // buffer: output buffer (must be sector_size bytes)
  // Returns: true on success
  virtual bool read_sector(int track, int head, int sector, uint8_t* buffer) = 0;

  // Write a sector
  virtual bool write_sector(int track, int head, int sector, const uint8_t* buffer) = 0;

  // Get filename
  virtual const std::string& filename() const = 0;

  // Check if read-only
  virtual bool is_read_only() const = 0;
};

// Raw disk image (.dsk format)
// Simple sequential sector dump
class RawDiskImage : public DiskImage {
public:
  RawDiskImage();
  explicit RawDiskImage(const DiskGeometry& geom);
  ~RawDiskImage() override;

  bool open(const char* filename, bool read_only = false) override;
  void close() override;
  bool is_open() const override;
  const DiskGeometry& geometry() const override;
  bool read_sector(int track, int head, int sector, uint8_t* buffer) override;
  bool write_sector(int track, int head, int sector, const uint8_t* buffer) override;
  const std::string& filename() const override { return filename_; }
  bool is_read_only() const override { return read_only_; }

  // Set geometry (must be called before open if not using constructor)
  void set_geometry(const DiskGeometry& geom);

  // Auto-detect geometry from file size
  bool auto_detect_geometry(size_t file_size);

private:
  FILE* fp_;
  std::string filename_;
  DiskGeometry geometry_;
  bool read_only_;

  // Calculate file offset for a sector
  long sector_offset(int track, int head, int sector) const;
};

// IMD (ImageDisk) format
class IMDDiskImage : public DiskImage {
public:
  IMDDiskImage();
  ~IMDDiskImage() override;

  bool open(const char* filename, bool read_only = false) override;
  void close() override;
  bool is_open() const override;
  const DiskGeometry& geometry() const override;
  bool read_sector(int track, int head, int sector, uint8_t* buffer) override;
  bool write_sector(int track, int head, int sector, const uint8_t* buffer) override;
  const std::string& filename() const override { return filename_; }
  bool is_read_only() const override { return read_only_; }

  // Get IMD header comment
  const std::string& comment() const { return comment_; }

private:
  // IMD track record
  struct IMDTrack {
    uint8_t mode;         // Transfer mode
    uint8_t cylinder;     // Physical cylinder
    uint8_t head;         // Physical head + flags
    uint8_t sector_count;
    uint8_t sector_size;  // Size code (0=128, 1=256, etc.)
    std::vector<uint8_t> sector_map;      // Sector numbering
    std::vector<uint8_t> cylinder_map;    // Optional cylinder IDs
    std::vector<uint8_t> head_map;        // Optional head IDs
    std::vector<std::vector<uint8_t>> sector_data; // Decompressed sector data
  };

  FILE* fp_;
  std::string filename_;
  std::string comment_;
  DiskGeometry geometry_;
  bool read_only_;

  // Track data indexed by (cylinder * 2 + head)
  std::map<int, IMDTrack> tracks_;

  // Parse IMD file
  bool parse_imd();

  // Decode sector size
  static int decode_sector_size(uint8_t code);

  // Find track record
  IMDTrack* find_track(int track, int head);
};

// SIMH Altair disk format (137-byte sectors)
// Format: 3-byte header + 128-byte data + 4-byte trailer + 2 bytes = 137
class SIMHDiskImage : public DiskImage {
public:
  SIMHDiskImage();
  ~SIMHDiskImage() override;

  bool open(const char* filename, bool read_only = false) override;
  void close() override;
  bool is_open() const override;
  const DiskGeometry& geometry() const override;
  bool read_sector(int track, int head, int sector, uint8_t* buffer) override;
  bool write_sector(int track, int head, int sector, const uint8_t* buffer) override;
  const std::string& filename() const override { return filename_; }
  bool is_read_only() const override { return read_only_; }

private:
  FILE* fp_;
  std::string filename_;
  DiskGeometry geometry_;
  bool read_only_;

  // SIMH sector format constants
  static const int SIMH_SECTOR_SIZE = 137;
  static const int SIMH_DATA_OFFSET = 3;   // Data starts at byte 3
  static const int SIMH_DATA_SIZE = 128;   // Actual data size

  // Calculate file offset for a sector
  long sector_offset(int track, int head, int sector) const;
};

// Utility: Open a disk image by extension
DiskImage* open_disk_image(const char* filename, bool read_only = false);

#endif // DISK_IMAGE_H
