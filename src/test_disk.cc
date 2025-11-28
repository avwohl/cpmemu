/*
 * Quick test for disk image reader
 */

#include "disk_image.h"
#include <cstdio>
#include <cstring>

void dump_hex(const uint8_t* data, int len, int offset = 0) {
  for (int i = 0; i < len; i += 16) {
    printf("%04X: ", offset + i);
    for (int j = 0; j < 16 && i + j < len; j++) {
      printf("%02X ", data[i + j]);
    }
    printf(" ");
    for (int j = 0; j < 16 && i + j < len; j++) {
      uint8_t c = data[i + j];
      printf("%c", (c >= 32 && c < 127) ? c : '.');
    }
    printf("\n");
  }
}

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("Usage: %s <disk_image.imd|.dsk>\n", argv[0]);
    return 1;
  }

  DiskImage* disk = open_disk_image(argv[1], true);
  if (!disk) {
    printf("Failed to open %s\n", argv[1]);
    return 1;
  }

  printf("Opened: %s\n", disk->filename().c_str());

  const DiskGeometry& g = disk->geometry();
  printf("Geometry:\n");
  printf("  Tracks: %d\n", g.tracks);
  printf("  Heads: %d\n", g.heads);
  printf("  Sectors/track: %d\n", g.sectors_per_track);
  printf("  Sector size: %d\n", g.sector_size);
  printf("  Total size: %d bytes\n", g.total_bytes());

  // For IMD, show comment
  IMDDiskImage* imd = dynamic_cast<IMDDiskImage*>(disk);
  if (imd) {
    printf("IMD Comment:\n%s\n", imd->comment().c_str());
  }

  // Read and display first few sectors
  uint8_t buffer[1024];
  printf("\n--- Track 0, Sector 1 ---\n");
  if (disk->read_sector(0, 0, 1, buffer)) {
    dump_hex(buffer, g.sector_size > 128 ? 128 : g.sector_size);
  } else {
    printf("Read failed\n");
  }

  // Read track 2 (directory area)
  printf("\n--- Track 2, Sector 1 (directory) ---\n");
  if (disk->read_sector(2, 0, 1, buffer)) {
    dump_hex(buffer, g.sector_size > 128 ? 128 : g.sector_size);

    // Parse directory entries
    printf("\nDirectory entries:\n");
    for (int i = 0; i < g.sector_size && i < 128; i += 32) {
      uint8_t* entry = buffer + i;
      if (entry[0] != 0xE5 && entry[0] < 32) {
        char name[12];
        memcpy(name, entry + 1, 8);
        name[8] = '.';
        memcpy(name + 9, entry + 9, 3);
        name[11] = 0;
        // Strip high bits
        for (int j = 0; j < 11; j++) name[j] &= 0x7F;
        printf("  User %d: %s\n", entry[0], name);
      }
    }
  }

  delete disk;
  return 0;
}
