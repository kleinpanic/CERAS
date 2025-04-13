# TODO

This document outlines potential improvements and optimizations for the Screen Recorder project. These items are ideas and enhancements that could be implemented to improve performance, resource management, production robustness, and debugging.

## Performance & Resource Optimizations

- **Thread & Buffer Management**
  - [ ] Implement pooling or reuse of AVFrame objects in the encoder.
  - [ ] Explore adding a ring buffer for incoming video and audio frames so encoding or file I/O does not block capture.
  - [ ] Investigate asynchronous I/O for file writes to avoid disk bottlenecks during long recordings.

- **Hardware Acceleration**
  - [ ] Research and integrate support for hardware-accelerated encoding using platforms such as NVENC, Intel QuickSync, or VA-API when available.

- **Screen Capture Enhancements**
  - [ ] Explore enabling XShm (shared memory) for X11 screen capture to improve capture speed.
  - [ ] Profile the capture loop and encoding pipeline to identify and optimize any bottlenecks.

## Enhanced Debugging & Command-line Options

- **Global Debug Logging**
  - [ ] Introduce a global debug flag (e.g. `g_debug`) that, when enabled via the `--debug` command-line flag, prints detailed debug messages throughout the code.
  - [ ] Add additional debug log statements in critical parts of the program (e.g., in audio capture, screen capture, encoder, and thread management) to facilitate troubleshooting.

- **Command-line Options**
  - [ ] Implement command-line parsing with options:
    - `--help`: Prints a detailed usage message.
    - `--version`: Displays the version (from a new `version.h` file) and exits.
    - `--debug`: Enables extra debug logging.
  - [ ] Improve help message and documentation output.

## Documentation & Build System

- **Makefile Improvements**
  - [ ] Upgrade the Makefile to use wildcards for source file detection.
  - [ ] Ensure the Makefile creates an output directory for object files automatically.
  - [ ] Add comments and a clean target to simplify building and cleaning the project.

- **README Documentation**
  - [ ] Write a professional README.md that provides:
    - An overview of how the code operates internally.
    - A list of dependencies needed (GTK+3, FFmpeg libraries, ALSA, X11, XRandR, etc.).
    - Build and installation instructions.
    - Usage instructions for command-line options.
    - Future work and potential improvements.

## Additional Production Considerations

- **Robust Error Handling**
  - [ ] Review and enhance error checking and logging in each module.
  - [ ] Consider implementing automatic cleanup or recovery mechanisms in case of errors during a long recording session.

- **Modularization & Refactoring**
  - [ ] Refactor the code for clearer separation of concerns, so that each module (audio, video, GUI, encoding) is easier to optimize individually.
  - [ ] Consider extracting common routines (e.g., debug logging, resource pooling) into separate modules or libraries.

- **Testing and Benchmarking**
  - [ ] Develop test cases to measure performance during long recording sessions.
  - [ ] Benchmark resource usage (CPU, memory, disk I/O) to identify further optimization opportunities.


