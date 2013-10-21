vxwx-rlogind
============

vxwx-rlogind is an rlogin daemon for VxWorks. The program allows for remote access to the system console, eliminating the need for a serial connection. Because it is designed to provide access to the system console, multiple simultaneous connections attach to the same system console, rather than each connection having an independent session. The serial port also attaches to the system console.


Program Startup
---------------

The program compiles to a kernel module, which should be loaded by the NI runtime early in its startup procedure (using EarlyStartupLibraries in ni-rt.ini on NI platforms). If the munch.tcl script is run as part of the build process, a `_ctors` array with a pointer to the entry point will be generated, causing the program to start after its module has been loaded.


Program Shutdown
----------------

Stopping the program is not currently supported. After the shell starts, it becomes impractical to redirect its input and output to its original state. Therefore, stopping the program after the shell has been started would render the console shell unusable until the system is restarted.

Shell commands
--------------

### `rlogind_entry()` ###

This is the program's entry point. This function can be called from the shell if it wasn't already called after loading the module.

### `rlogind_clientShow()` ###

This function prints a formatted list of clients currently connected to the server.

### `rlogind_disconnect(int index)` ###

This function will disconnect a client given an index from the list generated by `rlogind_clientShow()`.

UDP Broadcast Advertisement
---------------------------

Following its startup, the program will send six UDP packets to the broadcast address (255.255.255.255) on port 35120. All six packets will contain the same randomly generated four byte value. This acts as a notification to clients that the server is available, allowing them to connect to the server automatically upon its startup without the need for polling.

Tweakable Parameters
--------------------

The rlogind.h file contains some parameters that can be modified at compile time. They are listed below.

### `RLOGIND_PORT` ###

This specifies the port on which the rlogin daemon listens for connections.

### `RLOGIND_CONN_MAX` ###

This specifies the maximum of simultaneous connections allowed.

### `RLOGIND_BUFSIZE` ###

This specifies the size in bytes of each buffer of recieved data.

### `RLOGIND_USESERIAL` ###

1: relay data to serial port  
0: don't relay data to serial port

### `RLOGIND_DEBUG` ###

If this symbol is defined, debugging messages are printed at runtime.

### `RLOGIND_AUTORLOGIN_PORT` ###

The UDP port to which UDP broadcast advertisements will be sent. If this symbol is defined as zero, UDP broadcast advertisements will not be sent.

Getting Started
---------------
Follow these steps to get vxwx-rlogind running on your FIRST FRC cRio controller.

### Compile the Software ###

This software has been tested using both the FIRST FRC WindRiver Workbench (GCC 3.4.4 for powerpc-wrs-vxworks) toolchain and the new [GCC 4.8.0 toolchain for powerpc-wrs-vxworks](http://firstforge.wpi.edu/sf/projects/c--11_toochain). Makefiles for the latter are included in the project. The project can be built using WindRiver Workbench by creating a "Downloadable Kernel Module" project, adding the \*.c/\*.h/\*.cpp files, and selecting "Build Project" from the "Project" menu.

### Upload the Binary ###

Building the software will generate a binary with the extension ".out" . This binary should uploaded by FTP to the path "/c/ni-rt/system/vxwx-rlogind.out" on the cRio controller.

### Edit ni-rt.ini ###

An entry must also be added into the "/c/ni-rt.ini" configuration file on the cRio controller for the module to be loaded at startup. The ni-rt.ini file should be downloaded, edited, and the modified version uploaded. The file should be modified as follows:

Locate the "STARTUP" section.
```
[STARTUP]
EarlyStartupLibraries="nirtdm.out;nimdnsResponder.out;tsengine.out;nisvcloc.out;NetConsole.out;"
MainExe=/c/ni-rt/system/lvrt.out
DisplayStartupLibProgress=TRUE
```

Add vxwx-rlogind.out to the beginning of EarlyStartupLibraries value.
```
EarlyStartupLibraries="vxwx-rlogind.out;nirtdm.out;nimdnsResponder.out;tsengine.out;nisvcloc.out;NetConsole.out;"
```

### Restart the System ###

The rlogin daemon should begin running at the system's next restart. You can now connect to it using any rlogin client, such as [PuTTY](http://www.chiark.greenend.org.uk/~sgtatham/putty/download.html). When prompted, any username may be entered.

