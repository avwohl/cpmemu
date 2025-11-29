#include "diskdefs.h"
#include <cstdio>

int main() {
  DiskDefs defs;
  defs.load_defaults();

  printf("Loaded %zu disk definitions:\n\n", defs.count());

  for (const auto& name : defs.list()) {
    const DiskDef* d = defs.get(name);
    if (d && d->is_valid()) {
      printf("%-16s %6dKB  %3d trk  %2d sec/trk  %4d B/sec  blk=%5d  dir=%4d  boot=%d\n",
             d->name.c_str(), d->capacity_kb(), d->tracks, d->sectrk, d->seclen,
             d->blocksize, d->maxdir, d->boottrk);
      printf("                DPB: SPT=%d BSH=%d BLM=%d EXM=%d DSM=%d DRM=%d AL0=%02X AL1=%02X CKS=%d OFF=%d\n",
             d->sectrk, d->bsh(), d->blm(), d->exm(), d->dsm(), d->drm(),
             d->al0(), d->al1(), d->cks(), d->off());
    }
  }

  // Test loading external file
  printf("\n--- Loading external diskdefs file ---\n");
  DiskDefs ext;
  if (ext.load_file("../diskdefs")) {
    printf("Loaded %zu definitions from ../diskdefs\n", ext.count());
    const DiskDef* hd8 = ext.get("hd-8mb");
    if (hd8) {
      printf("hd-8mb: %s\n", hd8->describe().c_str());
      printf("  Total bytes: %d (%dMB)\n", hd8->total_bytes(), hd8->total_bytes() / (1024*1024));
    }
  }

  return 0;
}
