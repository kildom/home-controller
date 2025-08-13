
Newest GCC crashes using CheerpX.

Switching to v86, but debian image must be highly optimized after installation, e.g.:
- remove unused kernel libraries,
- installation must be done with big image, shrinking should be done as the last step of optimization.
- v86 Source code can be changed, e.g. to better image download on demand (and maybe cache it in indexdb or window.caches)
- since image reads are predictable, some profiling can be done to allow optimal block downloading and/or pre-loading.
  - free space optimization tools: zerofree, e2image (apply both for best results)
  - create image format with block metadata that contains:
    - repeat count (if the same metadata are repeating for multiple blocks)
    - offset+size on disk, offset+size on image file (blocks can be non-equal)
    - compression method: 0 - none, 1 - block is filled with zeros, 2 - zstd
    - pre-load flag
    - note: this way disk does not to be shrinked a lot:
      - zero sectors will be saved as metadata
      - unneeded files will probably never downloaded and compressed with zstd
