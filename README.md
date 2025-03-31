The repo is the implementation of my MInf Project (Part 1) at the University of Edinburgh. The thesis of my project is "Making Message-Based Transport Easy to Use". For the detailed discussion of the implementation, please refer to Chapter 4 of my thesis.

Our implementation introduced a connection-oriented abstraction to Homa, yet not changing the connection-less protocol semantics of the Homa transport protocol, preserving the format and efficiency benefits of the protocol. 

- Thanks to HomaModule developers for the baseline module: (https://github.com/PlatformLab/HomaModule/commit/6f58bef).
- The specific commit used in this project is `6f58bef`.
- Note that this module was developed and tested on Linux 6.10.6 with gcc-14. Please ensure that the environment is correctly set before compiling this module. 
- To build the module, type `make all`; then type `sudo insmod homa.ko` to install
  it, and `sudo rmmod homa` to remove an installed module.

For more information about the native HomaModule, please refer to the official repo: (https://github.com/PlatformLab/HomaModule). Note that the official repo is ahead of the repo used in this project, so the implementation can be slightly different.
