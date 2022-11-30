****************
Compartment Demo
****************

This is a simple demo aimed at demonstrating the principles of
compartmentalization in Morello. It provides a limited framework that allows
multiple compartments to run in the same address space, supported by a
privileged component, the compartment manager.

One use of architectural capabilities, as defined in CHERI / Morello, is to
enable the creation of software constructs called "compartments". Each
compartment contains a software component, and is isolated from other
compartments. Unlike conventional software compartmentalization techniques that
rely on the process boundary to achieve isolation, multiple compartments may be
hosted in the same address space. Isolation is achieved by carefully
constraining the set of capabilities available to each compartment, in such a
way that a compartment can only access its own memory, and memory that is shared
with it explicitly. The appeal of CHERI compartmentalization is twofold. For
software that already relies heavily on multiple processes to achieve isolation
between components (for instance browsers), compartments offer potentially
significant performance improvements, by cutting down on the context switching
overhead between components. On the other hand, software that is currently
monolithic may be made more robust by leveraging the relatively low performance
impact of compartments and their flexibility, which notably allows memory to be
transparently shared between compartments at no extra cost, in order to isolate
specific components (for instance an image decoding library). Much more on
compartmentalization can be found in `this technical report`_ by the University
of Cambridge (see especially chapter 3).

.. _this technical report: https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-887.pdf

For the sake of clarity and to avoid any misunderstanding, a brief summary of
what this demo is and isn't follows.

This demo is:

* A modest attempt at demonstrating how the Morello architecture can provide a
  form of compartmentalization.

* A simple framework allowing to load compartments into a process and run them,
  with a certain degree of isolation between them.

* A small example with two compartments communicating with each other.

This demo **is not**:

* A reference implementation of the compartmentalization framework. Many
  compartment models exist, more will appear, and the one used in this demo is
  certainly not production-ready.

* A security demonstrator. Compartments are isolated only to a certain extent,
  and it is straightforward for them to escape their boundaries (see also the
  `Security-related limitations`_ section).

* A practical framework to conduct large-scale experiments. There are multiple
  reasons to this, see the `Functional limitations`_ section.

Overview
========

The demo is split into two parts:

* The main executable (``compartment-demo``), which contains the compartment
  manager (CM). Its only purpose is to create and call compartments.
* The compartment executables (``client_*``, ``server``). Each executable is
  loaded as a self-contained compartment by the CM into the process's address
  space.

Roughly speaking, this is what happens when the demo is started by executing
``compartment-demo``:

#. The main executable asks the CM to load and initialize each compartment in
   turn.
#. The main executable transfers control to one of the compartments (the
   client, "CC") with the help of the CM. Once the switchover is complete, CC
   runs in its own subset of the address space, and cannot access memory
   outside of it.
#. CC calls another compartment, the server ("CS"). This is done by calling a
   special function through a capability provided by the CM. Once the
   switchover is complete, CC's state is fully preserved and CS is running.
#. CS performs the appropriate action, which may include accessing a capability
   provided by CC. Once it is done, it hands over control to the CM (again
   through a special capability), which in turns resumes the execution of CC.
#. CC performs a few more actions, like printing some data provided by CS. When
   it is done, it returns (through the CM), handing control back to the main
   executable.
#. The main executable prints that the demo has completed, and exits.

The client and server compartments in this demo are using a toy protocol,
which only aims at illustrating compartment-related concepts. The server has two
features of interest to the client: it is able to generate keys, and has a
public-private key pair. Correspondingly, the server can fulfill two requests
when called (via a compartment call): generate a new public-private key pair for
the client, storing it via a capability provided by the client, or provide a
capability to its public key. Three client implementations are provided:

* ``client_generate_keys``: allocates a key pair on its stack, requests the
  server to write a fresh key pair there by passing an appropriate capability,
  then prints the new key pair.
* ``client_get_server_key``: requests a capability to the server's public key,
  and then prints it.
* ``client_get_server_key_rogue``: requests a capability to the server's public key,
  but tries to read its private key by reading the next key in memory. Since the
  access is out of bounds, a capability bounds fault occurs and the demo
  crashes.

Testing
=======

To build the compartment demo, simply build the ``compartment-demo`` target::

  m compartment-demo

The binaries are generated in ``/data/nativetest64/compartment-demo``. Then
either regenerate the data image using ``m userdataimage-nodeps``, or copy the
binaries to the running target using ``adb push``.

To run the demo with the default client implementation
(``client_generate_keys``), just start the demo with no argument::

  $ /data/nativetest64/compartment-demo/compartment-demo

To try the other client implementations, specify which compartment binary to
load for the clients, for instance::

  $ cd /data/nativetest64/compartment-demo
  $ ./compartment-demo compartments/client_get_server_key

Technical details
=================

Code structure
--------------

::

  src/
  ├── compartment-manager                 * Implementation of the compartment manager
  │   ├── compartment_manager.h             │ Privileged API to the CM (used by the main executable)
  │   ├── compartment_manager.cpp           │ CM implementation (C++ part)
  │   ├── compartment_manager_asm.h         │ Internal CM API
  │   ├── compartment_manager_asm.S         │ CM implementation (assembly part)
  │   ├── compartment_config.h              │ Static configuration used for all compartments
  │   ├── compartment_interface.cpp         │ CM side of the compartment interface
  │   └── main.cpp                          │ Main executable implementation
  ├── compartments                        * Implementation of the compartments
  │   ├── compartment_globals.h             │ Declaration of the special global variables (set by the CM)
  │   ├── compartment_globals.cpp           │ Definition of those globals
  │   ├── compartment_helpers.h             │ Helpers for implementing compartments
  │   ├── compartment_helpers.cpp           │ Helpers implementation
  │   ├── compartment_mmap.cpp              │ mmap() and munmap() interposers
  │   ├── compartment_interface.cpp         │ Compartment side of the compartment interface
  │   ├── client.cpp                        │ Client compartment implementation
  │   ├── server.cpp                        │ Server compartment immplementation
  │   └── protocol.h                        │ Shared API between the client and server
  ├── compartment_interface.h             │ API between compartments and/or the CM
  ├── compartment_interface_impl.h        │ Shared implementation (see both versions of compartment_interface.cpp)
  └── utils                               * Utilities
      ├── align.h                           │ Alignment helpers
      ├── asm_helpers.h                     │ Assembly helpers
      ├── elf_util.h                        │ ELF utility for loading static executables at runtime
      └── elf_util.cpp                      │ ELF utility implementation

Compartment representation
--------------------------

This demo uses a very simple mechanism to represent compartments. Each
compartment is a statically linked, self-contained binary. Symbols are not
stripped, allowing the CM to look up special symbols. A specific base address is
provided to the linker, so that multiple compartments can be loaded in the same
address space without clashing with each other or with the main executable.
Since a compartment is a static executable, loading it in the process's address
space is done exactly like the kernel's ELF loader would, i.e. by mapping the
appropriate segments.

A compartment exposes two callable symbols:

* The standard ELF entry point (``_start``). This is used to initialize the
  compartment by running all the libc init code up to ``main()``, and is called
  only once. In this model, ``main()`` is only used as an initialization routine
  for the compartment's own data, and must return to the CM instead of returning
  to libc.
* The compartment's actual entry point (``__compartment_entry``). This is the
  routine that gets called when a compartment is called from the main executable
  or another compartment through ``CompartmentCall()``. The routine must also
  return to the CM explicitly.

In order to allow the CM to provide information and capabilities to the
compartment, "magic" global variables are allocated in the binary. They are
directly used by the compartment, but initialized by the CM. These are the
special global variables (see also ``compartment_interface.h``):

* Two executable capabilities (function pointers) that provide the compartment
  with well-defined entry points to the CM. One capability is used to call
  another compartment, and the other is used to return to the caller (another
  compartment or the main executable).
* Two 64-bit pointers, defining the address range the compartment can map memory
  in (see ``compartment_mmap.cpp`` for details).

Compartment manager
-------------------

The compartment manager (CM) is a fairly small piece of code that is privileged
with respect to the compartments, as it is able to access the entire address
space and manipulate the state of compartments. In particular, the CM has access
to two key pieces of memory, which compartments cannot and must not be able to
access:

* The compartment descriptors (``cm_compartments`` global variable). Each
  compartment is assigned a descriptor, in which the minimal context required to
  call it is stored (mainly the entry point capability, DDC and CSP).

* The stack. The compartment manager uses the main executable's stack to store
  the caller's context when switching to a compartment (see `Compartment
  calls`_ section).

Compartment loading and initialization
--------------------------------------

Each compartment is loaded and initialized by the CM in the ``CompartmentAdd()``
function. Here are the main steps:

#. Open and parse the compartment's ELF binary.
#. Reserve the compartment's address range (specified by the binary's base address
   and a statically defined size) by ``mmap()``'ing it, then map the binary as
   specified in the ELF headers.
#. Map the stack at the top of the compartment's range, and initialize its
   contents (like the kernel would: argv, envp, etc.).
#. From the CM's privileged capabilities, build appropriate capabilities for the
   compartment and store them in the compartment's descriptor. The compartment's
   root data capability (DDC) and root executable capability (PCC, entry point)
   have their bounds set to the compartment's range.
#. Look up the special symbols (see `Compartment representation`_) and
   initialize them.
#. Let the compartment initialize itself by effectively performing a compartment
   call targeting its ELF entry point.

Compartment calls
-----------------

A running compartment is able to call another compartment, thanks to the
privileged executable capability provided by the CM. Up to six arguments can be
passed in registers (including capabilities), and a value can be returned in a
(capability) register. This is what the call stack looks like when a compartment
C1 calls a compartment C2, passing ``arg`` as argument (with the component
running each function in square brackets)::

  <some function in C1> [C1]
  └── CompartmentCall(id_c2, arg) [C1]
      └── CompartmentSwitch(id_c2, arg) [CM]
          └── <C2 entry point>(arg) [C2]

``CompartmentSwitch()`` performs the compartment switch itself. This is a
privileged operation, therefore it is part of the CM. Its implementation is
fairly simple: after checking that the target compartment ID (C2's) is valid, it
saves C1's minimal context on the CM's stack, and then installs C2's context
from its descriptor (looked up in ``cm_compartments``). Finally, it transfers
control to C2. To avoid any leak of information (especially capabilities),
registers are sanitized before branching to C2.

``CompartmentCall()`` is essentially a helper that loads the executable
capability provided by the CM in one of the compartment's global variable and
branches to it. It also takes care of saving and restoring callee-saved
registers (to minimize the memory footprint on the CM's stack, callee-saved
registers are not preserved by ``CompartmentSwitch()``).

When C2 is done, it returns to its caller (here C1) by calling the other executable
capability provided by the CM (see the ``CompartmentReturn()`` helper). When it
does so, all the stack frames since C2 started running are effectively
discarded, and execution resumes after the branch to C2 in
``CompartmentSwitch()``. C1's context (saved on the CM's stack) is then
restored, and control is returned to C1.

Note: Executive/Restricted banking and compartment switching
  To help with the management of compartments, the Morello architecture makes it
  possible to switch between two "banks" for certain registers, in particular
  DDC and CSP. Which bank is currently active for those registers is controlled
  by the Executive permission in PCC: if the permission is set, then the
  Executive bank is active, otherwise the Restricted bank is active. When
  running in Executive (i.e. the Executive permission in PCC is set), the
  Restricted bank is still accessible via separate system registers (RDDC,
  RCSP). Conversely, the Executive bank cannot be accessed while in Restricted.

  In this demo, the CM (as part of the main executable) is running in Executive,
  while the compartments are running in Restricted. This allows the CM to switch
  all the main "ambient" registers (PCC, DDC, CSP) in one atomic operation, i.e.
  branching to the compartment's entry point. Similarly, when a compartment
  calls or returns to the CM, the Executive DDC and CSP become active, restoring
  access to the whole address space. There are architectural subtleties around
  Executive-Restricted transitions, see ``compartment_manager_asm.S`` for
  details.

Writing custom compartments
===========================

Aside from the client/server implementation, all of the code in the demo is
fairly generic and can be used to implement new compartments. To do so, you can
use ``server.cpp`` as a template. A clean compartment implementation
``custom.cpp`` needs to define two functions:

* ``main(argc, argv)``: this is the standard ``main()`` function. It is called
  once - when the compartment is initialized. It **must** return to the CM using
  ``CompartmentReturn()``.
* ``COMPARTMENT_ENTRY_POINT(args...)``: this is the entry point called when a
  compartment call to this compartment is made. It can take up to six arguments
  fitting in a capability register, and return one capability value. It **must**
  return to the CM using ``CompartmentReturn(ret_value)``.

The new compartment can then be built by adding a new ``cc_test`` module to the
demo's ``Android.bp``, using the ``compartment_server`` module as a template.
Note that the base address needs to be set correctly, as all compartments loaded
at the same time must have disjoint memory ranges (the size of each
compartment's range is configured by ``kCompartmentMemoryRangeLength`` in
``compartment_config.h``).

Finally, you may want to modify ``main.cpp`` to load the compartment as desired,
and potentially call it.

Limitations
===========

As mentioned in the introduction, this implementation of compartments is very
simplistic, and comes with many limitations. This section provides a
non-exhaustive list of these limitations.

Functional limitations
----------------------

* Compartments are essentially static. They are represented using a statically
  allocated ID, and cannot be removed once loaded. Their memory range is fixed
  and cannot be extended.

* Because compartments issue syscalls directly and the kernel has no
  awareness of compartments, ``mmap()`` must be intercepted to make sure that
  all memory mappings are within the compartment's range. This is done by
  making an ``mmap()`` call with ``MAP_FIXED``, at an address computed in
  ``compartment_mmap.cpp``. A range is never reused once mapped, even after
  ``munmap()``, so a compartment may easily run out of memory.

* There is a strong assumption that the main executable is not mapped in lower
  addresses, since they are being used for the compartments.

* Signals are not handled in any particular way. In particular, an invalid
  access in a compartment will crash the entire process. Since the kernel always
  delivers signals in Executive, the CM could in principle handle such faults
  and terminate the faulty compartment accordingly. However, it is currently
  unclear how asynchronous signals (sent by other processes) like SIGUSR1 should
  be handled.

* Multithreading is not supported at all. Supporting compartments with multiple
  threads requires a significantly more complex model, probably with
  asynchronous communication channels between compartments.

* The demo is entirely built in the hybrid-cap ABI, including the compartments.
  This creates significant limitations on the interactions between compartments,
  as propagating capabilities requires all pointers to be annotated with
  ``__capability``. Compartments built in the pure-cap ABI are much more
  practical and flexible. It should be possible to adapt this demo to the
  pure-cap ABI with a reasonable amount of changes.

* The error handling is overall very basic (mostly ``exit(1)``) and could easily
  be improved.

Security-related limitations
----------------------------

* As mentioned previously, compartments are able to issue syscalls without any
  restriction.  As a result, they can very easily escape their sandbox by using
  syscalls such as ``read()`` or ``write()``. This can only be solved in Morello
  by making the kernel aware of the concept of compartments and preventing the
  running compartment from accessing any memory outside its own range via
  syscalls. The architecture facilitates this by providing the system register
  ``CID_EL0``: in ``CompartmentSwitch()``, the CM could install a capability
  with the CompartmentId permission in ``CID_EL0`` identifying the next running
  compartment, thereby informing the kernel in an unforgeable manner.

* SPECTRE-like (side-channel) attacks between compartments are not prevented.
  Morello does not include mitigations against such attacks. A future
  architecture may include mitigations that rely on using ``CID_EL0`` as
  described above to identify compartment contexts.

* Executable capabilities provided by the CM to compartments can be modified,
  and in particular their address can be changed, allowing compartments to jump
  to arbitrary places in the CM. This should be addressed by using sealed
  capabilities.
