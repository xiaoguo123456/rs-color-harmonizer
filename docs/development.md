# Development Conventions

- C++20, clang-format, clang-tidy and unit tests for processing kernels
- QML files use one component per file and explicit property names
- No raw GDAL pointers outside RAII wrappers
- All long-running work runs off the UI thread
- Every algorithm has synthetic tests and at least one real-scene regression case
- Keep third-party licenses and data-source terms in `THIRD_PARTY_NOTICES.md`
