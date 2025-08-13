
Newest GCC crashes using CheerpX.

Switching to v86, but debian image must be highly optimized after installation, e.g.:
- remove unused kernel libraries,
- installation must be done with big image, shrinking should be done as the last step of optimization.
- v86 Source code can be changed, e.g. to better image download on demand (and maybe cache it in indexdb or window.caches)
- since image reads are predictable, some profiling can be done to allow optimal block downloading and/or pre-loading.
