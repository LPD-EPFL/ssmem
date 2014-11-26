ssmem
=====

ssmem is a fast and simple object-based memory allocator with epoch-based garbage collection. In other words, the freed memory is not reused by the allocator until it is certain that no other threads might be accessing that memory. ssmem can for example be used in lock-free data structures, where the removal of an element from the structure might happen while other threads holding a reference to the same element.

You can create multiple allocators with ssmem, however, each allocator can handle a single object size. For example, you can create an ssmem allocator to *always* allocate and release 64 bytes memory chunks.

Installation:
-------------

Execute `make` in the ssmem base folder.
This should compile `libssmem.a` and `ssmem_test`.

`ssmem_test` contains a few tests for verifying the correctness and testing the performance of ssmem.
Execute: `ssmem_test -h` for the available options.

If you to customize the installation of ssmem, you can add a custom configuration for a platform in the `Makefile`.

You can also compile with `make VERSION=DEBUG` to generate a debug build of ssmem.

Using ssmem:
------------

To use ssmem you need to include `ssmem.h` and link with `-lssmem` in your source code.
`ssmem.h` contains the interface of ssmem.

In short, you can:
   * `ssmem_alloc_init`, or `ssmem_alloc_init_fs_size` to initialize and allocator
   * `ssmem_term` to terminate ssmem
   * `ssmem_alloc` to allocate memory
   * `ssmem_free` to free memory (when it is safe, within the allocator)
   * `ssmem_release` to free memory (when it is safe, to the system -- with `free`)

Details:
--------

Each thread that uses an allocator holds a version/timestamp number. This timestamp is incremented every time takes some steps with ssmem (i.e., either allocates, or frees memory). ssmem batches multiple frees together (in a free set) and once the set is full, it tries to garbage collect memory. The free set is timestamped with a vector clock (an array of the timestamps from all threads). The garbage collection (reclamation of the freed memory) decides if a free set is safe to release (i.e., whether all threads had made progress since it was timestamped) and if it is, it make the memory of this (and of "older") free sets available to the allocator.